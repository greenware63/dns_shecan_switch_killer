#include <winsock2.h>
#include <ws2tcpip.h>

#include "firewallmanager.h"

#include <windows.h>
#include <iphlpapi.h>
#include <netfw.h>
#include <objbase.h>

#include <QtConcurrent>
#include <QSettings>
#include <QVariantMap>
#include <QRandomGenerator>
#include <algorithm>
#include <array>
#include <mutex>
#include <vector>

namespace {
    const QString kRuleAllowLanV4      = "Shecan_KS_AllowLAN_v4";
    const QString kRuleAllowLanV6      = "Shecan_KS_AllowLAN_v6";
    const QString kRuleAllowDnsUdp     = "Shecan_KS_AllowShecanDNS_UDP";
    const QString kRuleAllowDnsTcp     = "Shecan_KS_AllowShecanDNS_TCP";
    const QString kRuleAllowShecanApp1 = "Shecan_KS_AllowShecanApp_NextDNS";
    const QString kRuleAllowShecanApp2 = "Shecan_KS_AllowShecanApp_Desktop";
    const QString kRuleAllowShecanApp3 = "Shecan_KS_AllowShecanApp_Game";
    const QString kLegacyRule          = "Shecan_KS_BlockAll";

    const QString kLanV4 = "192.168.0.0/16,10.0.0.0/8,172.16.0.0/12,127.0.0.0/8,169.254.0.0/16";
    const QString kShecanDnsCsv = "178.22.122.100,185.51.200.2";
    const QString kShecanNextDnsExe = "C:\\Program Files\\shecan-nextdns\\shecan-nextdns.exe";
    const QString kShecanDesktopExe = "C:\\Program Files\\shecan\\shecan-desktop\\shecan-desktop.exe";
    const QString kShecanGameExe    = "C:\\Program Files\\shecan\\shecan-desktop\\shecan-game.exe";
    const QStringList kAllAppRuleNames = {
        kLegacyRule,
        kRuleAllowLanV4,
        kRuleAllowLanV6,
        kRuleAllowDnsUdp,
        kRuleAllowDnsTcp,
        kRuleAllowShecanApp1,
        kRuleAllowShecanApp2,
        kRuleAllowShecanApp3
    };
    const QByteArray kTestDomain = "www.google.com";

    const DWORD kNetshTimeoutMs = 10000;
    const int kCheckInterval = 2000;
    const NET_FW_PROFILE_TYPE2 kProfiles[] = {
        NET_FW_PROFILE2_DOMAIN,
        NET_FW_PROFILE2_PRIVATE,
        NET_FW_PROFILE2_PUBLIC
    };

    bool runCommandHidden(const QString& cmd, DWORD timeoutMs) {
        STARTUPINFOW si{};
        PROCESS_INFORMATION pi{};
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;

        std::wstring cmdLine = cmd.toStdWString();
        if (!CreateProcessW(nullptr, cmdLine.data(), nullptr, nullptr, FALSE,
                            CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
            return false;

        const DWORD wait = WaitForSingleObject(pi.hProcess, timeoutMs);
        if (wait == WAIT_TIMEOUT) {
            TerminateProcess(pi.hProcess, 1);
            WaitForSingleObject(pi.hProcess, 1000);
        }

        DWORD exitCode = 1;
        GetExitCodeProcess(pi.hProcess, &exitCode);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return wait == WAIT_OBJECT_0 && exitCode == 0;
    }

    bool beginCom(bool* mustUninitialize) {
        const HRESULT result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        *mustUninitialize = SUCCEEDED(result);
        return SUCCEEDED(result) || result == RPC_E_CHANGED_MODE;
    }

    INetFwPolicy2* createFirewallPolicy() {
        CLSID clsid{};
        if (FAILED(CLSIDFromProgID(L"HNetCfg.FwPolicy2", &clsid)))
            return nullptr;

        INetFwPolicy2* policy = nullptr;
        if (FAILED(CoCreateInstance(clsid, nullptr, CLSCTX_INPROC_SERVER,
                                    __uuidof(INetFwPolicy2),
                                    reinterpret_cast<void**>(&policy))))
            return nullptr;
        return policy;
    }

    bool removeFirewallRules(const QStringList& names) {
        bool mustUninitialize = false;
        if (!beginCom(&mustUninitialize))
            return false;

        INetFwPolicy2* policy = createFirewallPolicy();
        INetFwRules* rules = nullptr;
        bool ok = policy && SUCCEEDED(policy->get_Rules(&rules));
        if (rules) {
            for (const QString& name : names) {
                const std::wstring wideName = name.toStdWString();
                BSTR ruleName = SysAllocString(wideName.c_str());
                if (!ruleName) {
                    ok = false;
                    break;
                }

                for (int attempt = 0; attempt < 100; ++attempt) {
                    INetFwRule* rule = nullptr;
                    const HRESULT itemResult = rules->Item(ruleName, &rule);
                    if (itemResult == E_INVALIDARG
                        || itemResult == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND)
                        || itemResult == HRESULT_FROM_WIN32(ERROR_NOT_FOUND))
                        break;
                    if (FAILED(itemResult)) {
                        ok = false;
                        break;
                    }

                    if (rule)
                        rule->Release();
                    if (FAILED(rules->Remove(ruleName))) {
                        ok = false;
                        break;
                    }
                    if (attempt == 99)
                        ok = false;
                }
                SysFreeString(ruleName);
                if (!ok)
                    break;
            }
            rules->Release();
        }
        if (policy)
            policy->Release();

        if (mustUninitialize)
            CoUninitialize();
        return ok;
    }

    bool setProfilePolicy(const QVariantList& profiles, bool blocked) {
        bool mustUninitialize = false;
        if (!beginCom(&mustUninitialize))
            return false;

        INetFwPolicy2* policy = createFirewallPolicy();
        bool ok = policy != nullptr;
        if (policy) {
            for (const QVariant& item : profiles) {
                const QVariantMap profile = item.toMap();
                const auto profileType = static_cast<NET_FW_PROFILE_TYPE2>(
                    profile.value("profile").toLongLong());
                HRESULT result = policy->put_DefaultOutboundAction(
                    profileType, blocked ? NET_FW_ACTION_BLOCK
                                         : static_cast<NET_FW_ACTION>(profile.value("outbound").toInt()));
                if (SUCCEEDED(result) && blocked)
                    result = policy->put_FirewallEnabled(profileType, VARIANT_TRUE);
                else if (SUCCEEDED(result) && !blocked)
                    result = policy->put_FirewallEnabled(
                        profileType, profile.value("enabled").toBool()
                            ? VARIANT_TRUE : VARIANT_FALSE);
                if (FAILED(result))
                    ok = false;
            }
            policy->Release();
        }

        if (mustUninitialize)
            CoUninitialize();
        return ok;
    }

    bool queryDnsServer(const QString& ipStr, int timeoutMs) {
        static std::once_flag winsockInitFlag;
        static bool winsockReady = false;
        std::call_once(winsockInitFlag, []() {
            WSADATA data{};
            winsockReady = WSAStartup(MAKEWORD(2, 2), &data) == 0;
        });
        if (!winsockReady)
            return false;

        const QByteArray ipBytes = ipStr.toLatin1();
        sockaddr_storage serverAddress{};
        int serverAddressLength = 0;
        if (InetPtonA(AF_INET, ipBytes.constData(),
                      &reinterpret_cast<sockaddr_in*>(&serverAddress)->sin_addr) == 1) {
            auto* address = reinterpret_cast<sockaddr_in*>(&serverAddress);
            address->sin_family = AF_INET;
            address->sin_port = htons(53);
            serverAddressLength = sizeof(sockaddr_in);
        } else if (InetPtonA(AF_INET6, ipBytes.constData(),
                             &reinterpret_cast<sockaddr_in6*>(&serverAddress)->sin6_addr) == 1) {
            auto* address = reinterpret_cast<sockaddr_in6*>(&serverAddress);
            address->sin6_family = AF_INET6;
            address->sin6_port = htons(53);
            serverAddressLength = sizeof(sockaddr_in6);
        } else {
            return false;
        }

        SOCKET socketHandle = socket(serverAddress.ss_family, SOCK_DGRAM, IPPROTO_UDP);
        if (socketHandle == INVALID_SOCKET)
            return false;

        const unsigned short id = static_cast<unsigned short>(
            QRandomGenerator::global()->generate());
        std::array<unsigned char, 512> request{};
        request[0] = static_cast<unsigned char>(id >> 8);
        request[1] = static_cast<unsigned char>(id);
        request[2] = 0x01;
        request[5] = 0x01;

        int offset = 12;
        const QList<QByteArray> labels = kTestDomain.split('.');
        for (const QByteArray& label : labels) {
            if (label.isEmpty() || label.size() > 63) {
                closesocket(socketHandle);
                return false;
            }
            request[offset++] = static_cast<unsigned char>(label.size());
            std::copy(label.cbegin(), label.cend(), request.begin() + offset);
            offset += label.size();
        }
        request[offset++] = 0;
        request[offset++] = 0;
        request[offset++] = 1;
        request[offset++] = 0;
        request[offset++] = 1;

        const int sent = sendto(socketHandle, reinterpret_cast<const char*>(request.data()),
                                offset, 0, reinterpret_cast<const sockaddr*>(&serverAddress),
                                serverAddressLength);
        if (sent != offset) {
            closesocket(socketHandle);
            return false;
        }

        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(socketHandle, &readSet);
        timeval timeout{};
        timeout.tv_sec = timeoutMs / 1000;
        timeout.tv_usec = (timeoutMs % 1000) * 1000;
        const int ready = select(0, &readSet, nullptr, nullptr, &timeout);
        std::array<unsigned char, 2048> response{};
        sockaddr_storage responseSource{};
        int responseSourceLength = sizeof(responseSource);
        const int received = ready > 0
            ? recvfrom(socketHandle, reinterpret_cast<char*>(response.data()),
                       static_cast<int>(response.size()), 0,
                       reinterpret_cast<sockaddr*>(&responseSource), &responseSourceLength)
            : SOCKET_ERROR;
        closesocket(socketHandle);

        if (received < 12 || responseSource.ss_family != serverAddress.ss_family)
            return false;
        if (serverAddress.ss_family == AF_INET) {
            const auto* expected = reinterpret_cast<const sockaddr_in*>(&serverAddress);
            const auto* actual = reinterpret_cast<const sockaddr_in*>(&responseSource);
            if (actual->sin_addr.s_addr != expected->sin_addr.s_addr
                || actual->sin_port != expected->sin_port)
                return false;
        } else {
            const auto* expected = reinterpret_cast<const sockaddr_in6*>(&serverAddress);
            const auto* actual = reinterpret_cast<const sockaddr_in6*>(&responseSource);
            if (memcmp(&actual->sin6_addr, &expected->sin6_addr, sizeof(IN6_ADDR)) != 0
                || actual->sin6_port != expected->sin6_port)
                return false;
        }
        const unsigned short responseId =
            static_cast<unsigned short>((response[0] << 8) | response[1]);
        const unsigned short flags =
            static_cast<unsigned short>((response[2] << 8) | response[3]);
        const unsigned short answerCount =
            static_cast<unsigned short>((response[6] << 8) | response[7]);
        return responseId == id && (flags & 0x8000) != 0
            && (flags & 0x000f) == 0 && answerCount > 0;
    }

    bool isShecanServiceRunning() {
        SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
        if (!scm)
            return false;

        bool running = false;
        const wchar_t* serviceNames[] = {L"shecan-nextdns", L"shecan-game"};
        for (const wchar_t* serviceName : serviceNames) {
            SC_HANDLE service = OpenServiceW(scm, serviceName, SERVICE_QUERY_STATUS);
            if (!service)
                continue;

            SERVICE_STATUS_PROCESS status{};
            DWORD bytesNeeded = 0;
            if (QueryServiceStatusEx(service, SC_STATUS_PROCESS_INFO,
                                     reinterpret_cast<LPBYTE>(&status),
                                     sizeof(status), &bytesNeeded)
                && status.dwCurrentState == SERVICE_RUNNING) {
                running = true;
                CloseServiceHandle(service);
                break;
            }
            CloseServiceHandle(service);
        }
        CloseServiceHandle(scm);
        return running;
    }

    QStringList configuredShecanDnsServers() {
        ULONG bufferSize = 16 * 1024;
        std::vector<unsigned char> buffer(bufferSize);
        ULONG result = GetAdaptersAddresses(AF_UNSPEC,
            GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST, nullptr,
            reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data()), &bufferSize);
        if (result == ERROR_BUFFER_OVERFLOW) {
            buffer.resize(bufferSize);
            result = GetAdaptersAddresses(AF_UNSPEC,
                GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST, nullptr,
                reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data()), &bufferSize);
        }
        if (result != NO_ERROR)
            return {};

        IN_ADDR shecanAddresses[2]{};
        InetPtonA(AF_INET, "178.22.122.100", &shecanAddresses[0]);
        InetPtonA(AF_INET, "185.51.200.2", &shecanAddresses[1]);

        QStringList servers;
        auto appendUnique = [&servers](const QString& address) {
            if (!servers.contains(address))
                servers.append(address);
        };

        for (auto* adapter = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());
             adapter; adapter = adapter->Next) {
            if (adapter->OperStatus != IfOperStatusUp)
                continue;
            // Ignore loopback adapter itself when checking if actual network interfaces use Shecan
            if (adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK)
                continue;

            for (auto* dns = adapter->FirstDnsServerAddress; dns; dns = dns->Next) {
                if (!dns->Address.lpSockaddr)
                    continue;

                if (dns->Address.lpSockaddr->sa_family == AF_INET) {
                    const auto* address = reinterpret_cast<const sockaddr_in*>(
                        dns->Address.lpSockaddr);
                    const ULONG hostAddress = ntohl(address->sin_addr.s_addr);
                    if ((hostAddress >> 24) == 127)
                        appendUnique("127.0.0.1");
                    else if (address->sin_addr.s_addr == shecanAddresses[0].s_addr)
                        appendUnique("178.22.122.100");
                    else if (address->sin_addr.s_addr == shecanAddresses[1].s_addr)
                        appendUnique("185.51.200.2");
                } else if (dns->Address.lpSockaddr->sa_family == AF_INET6) {
                    const auto* address6 = reinterpret_cast<const sockaddr_in6*>(
                        dns->Address.lpSockaddr);
                    if (IN6_IS_ADDR_LOOPBACK(&address6->sin6_addr))
                        appendUnique("::1");
                }
            }
        }
        return servers;
    }
}

FirewallManager::FirewallManager(QObject* parent) : QObject(parent) {
    m_timer = new QTimer(this);
    m_timer->setSingleShot(true);
    connect(m_timer, &QTimer::timeout, this, &FirewallManager::startCheckTask);

    m_watcher = new QFutureWatcher<CheckResult>(this);
    connect(m_watcher, &QFutureWatcher<CheckResult>::finished,
            this, &FirewallManager::onCheckFinished);
}

FirewallManager::~FirewallManager() {
    cleanupOnExit();
}

void FirewallManager::cleanupOnExit() {
    m_running = false;
    if (m_timer) {
        m_timer->stop();
    }
    if (m_watcher) {
        if (m_watcher->isRunning()) {
            m_watcher->cancel();
            m_watcher->waitForFinished();
        }
    }
    std::lock_guard<std::mutex> lock(m_firewallMutex);
    restoreOriginalFirewallPolicy();
    removeFirewallRules(kAllAppRuleNames);
    clearOriginalFirewallPolicy();
    m_killSwitchActive = false;
}

bool FirewallManager::isAdmin() const {
    BOOL admin = FALSE;
    PSID group = nullptr;
    SID_IDENTIFIER_AUTHORITY nt = SECURITY_NT_AUTHORITY;
    if (AllocateAndInitializeSid(&nt, 2, SECURITY_BUILTIN_DOMAIN_RID,
            DOMAIN_ALIAS_RID_ADMINS, 0,0,0,0,0,0, &group)) {
        CheckTokenMembership(nullptr, group, &admin);
        FreeSid(group);
    }
    return admin == TRUE;
}

QStringList FirewallManager::shecanDnsServers() {
    return {"127.0.0.1", "::1", "178.22.122.100", "185.51.200.2"};
}

bool FirewallManager::runNetsh(const QString& args, DWORD timeoutMs) {
    return runCommandHidden("cmd /c netsh " + args + " >nul 2>&1", timeoutMs);
}

bool FirewallManager::ensureBaseRules() {
    if (!removeFirewallRules(kAllAppRuleNames)) {
        emit logMessage("error", "Could not remove existing application firewall rules");
        return false;
    }

    bool ok = true;
    auto ensure = [this, &ok](const QString& name, const QString& params) {
        if (!runNetsh("advfirewall firewall add rule name=" + name + " " + params)) {
            emit logMessage("error", "Failed to add firewall rule: " + name);
            ok = false;
        }
    };

    ensure(kRuleAllowLanV4,
        "dir=out action=allow remoteip=" + kLanV4 + " profile=any enable=yes");
    ensure(kRuleAllowLanV6,
        "dir=out action=allow remoteip=localsubnet profile=any enable=yes");
    ensure(kRuleAllowDnsUdp,
        "dir=out action=allow protocol=udp remoteport=53 remoteip=" + kShecanDnsCsv
        + " profile=any enable=yes");
    ensure(kRuleAllowDnsTcp,
        "dir=out action=allow protocol=tcp remoteport=53 remoteip=" + kShecanDnsCsv
        + " profile=any enable=yes");

    if (QFile::exists(kShecanNextDnsExe)) {
        ensure(kRuleAllowShecanApp1,
            "dir=out action=allow program=\"" + kShecanNextDnsExe + "\" profile=any enable=yes");
    }
    if (QFile::exists(kShecanDesktopExe)) {
        ensure(kRuleAllowShecanApp2,
            "dir=out action=allow program=\"" + kShecanDesktopExe + "\" profile=any enable=yes");
    }
    if (QFile::exists(kShecanGameExe)) {
        ensure(kRuleAllowShecanApp3,
            "dir=out action=allow program=\"" + kShecanGameExe + "\" profile=any enable=yes");
    }
    return ok;
}

bool FirewallManager::captureOriginalFirewallPolicy() {
    QSettings settings;
    const QVariantList saved = settings.value("firewall/originalProfiles").toList();
    if (saved.size() == 3) {
        m_originalProfiles = saved;
        return true;
    }

    bool mustUninitialize = false;
    if (!beginCom(&mustUninitialize)) {
        emit logMessage("error", "Could not initialize Windows Firewall API");
        return false;
    }

    INetFwPolicy2* policy = createFirewallPolicy();
    bool ok = policy != nullptr;
    QVariantList profiles;
    if (policy) {
        for (const NET_FW_PROFILE_TYPE2 profileType : kProfiles) {
            VARIANT_BOOL enabled = VARIANT_FALSE;
            NET_FW_ACTION outbound = NET_FW_ACTION_BLOCK;
            if (FAILED(policy->get_FirewallEnabled(profileType, &enabled))
                || FAILED(policy->get_DefaultOutboundAction(profileType, &outbound))) {
                ok = false;
                break;
            }
            QVariantMap profile;
            profile.insert("profile", static_cast<qlonglong>(profileType));
            profile.insert("enabled", enabled == VARIANT_TRUE);
            profile.insert("outbound", static_cast<int>(outbound));
            profiles.append(profile);
        }
        policy->Release();
    }

    if (mustUninitialize)
        CoUninitialize();
    if (!ok) {
        emit logMessage("error", "Could not read the current Windows Firewall policy");
        return false;
    }

    settings.setValue("firewall/originalProfiles", profiles);
    settings.sync();
    if (settings.status() != QSettings::NoError) {
        emit logMessage("error", "Could not save the original firewall policy");
        return false;
    }
    m_originalProfiles = profiles;
    return true;
}

bool FirewallManager::restoreOriginalFirewallPolicy() {
    if (m_originalProfiles.size() != 3) {
        emit logMessage("error", "No saved firewall policy is available to restore");
        return false;
    }
    if (!setProfilePolicy(m_originalProfiles, false)) {
        emit logMessage("error", "Failed to restore the saved Windows Firewall policy");
        return false;
    }
    return true;
}

bool FirewallManager::clearOriginalFirewallPolicy() {
    QSettings settings;
    settings.remove("firewall/originalProfiles");
    settings.sync();
    if (settings.status() != QSettings::NoError) {
        emit logMessage("error", "Could not clear the saved firewall policy");
        return false;
    }
    m_originalProfiles.clear();
    return true;
}

bool FirewallManager::applyKillSwitch(bool enabled) {
    if (!enabled)
        return restoreOriginalFirewallPolicy();
    if (m_originalProfiles.size() != 3) {
        emit logMessage("error", "The original firewall policy was not captured");
        return false;
    }
    if (setProfilePolicy(m_originalProfiles, true))
        return true;

    emit logMessage("error", "Failed to enable outbound blocking in Windows Firewall");
    return false;
}

void FirewallManager::initialize() {
    if (m_initialized) { emit initialized(true, "Already initialized"); return; }

    if (!isAdmin()) {
        emit logMessage("error", "Administrator privileges required");
        emit initialized(false, "Run as Administrator");
        return;
    }

    emit logMessage("info", "Running as administrator");
    emit logMessage("info", "Configuring firewall rules...");

    QtConcurrent::run([this]() {
        std::lock_guard<std::mutex> lock(m_firewallMutex);
        if (!captureOriginalFirewallPolicy())
            return false;
        if (!ensureBaseRules() || !applyKillSwitch(true)) {
            const bool rolledBack = restoreOriginalFirewallPolicy();
            const bool rulesRemoved = removeFirewallRules(kAllAppRuleNames);
            if (rolledBack && rulesRemoved)
                clearOriginalFirewallPolicy();
            return false;
        }
        return true;
    }).then(this, [this](bool success) {
        if (!success) {
            emit logMessage("error", "Firewall initialization failed; policy rollback was attempted");
            emit initialized(false, "Firewall configuration failed. See the event log.");
            return;
        }
        m_initialized = true;
        m_killSwitchActive = true;
        emit logMessage("success", "Firewall rules configured and outbound traffic blocked");
        emit killSwitchChanged(true);
        emit initialized(true, "Ready");
    });
}

void FirewallManager::start() {
    if (!m_initialized) {
        emit logMessage("warn", "Not initialized yet");
        return;
    }
    if (m_running.exchange(true)) return;

    emit logMessage("info", "Monitoring started");
    emit runningChanged(true);
    startCheckTask();
}

void FirewallManager::stop() {
    if (!m_running.exchange(false)) return;
    m_timer->stop();
    emit logMessage("info", "Monitoring stopped");
    emit runningChanged(false);
}

void FirewallManager::setAutoMode(bool enabled) {
    if (m_restoreInProgress) {
        emit autoModeChangeFailed(enabled);
        return;
    }
    {
        std::lock_guard<std::mutex> lock(m_firewallMutex);
        m_autoMode = enabled;
    }
    emit logMessage("info", enabled ? "Auto mode enabled" : "Auto mode disabled");

    if (enabled) {
        QtConcurrent::run([this]() {
            std::lock_guard<std::mutex> lock(m_firewallMutex);
            if (m_autoMode)
                return ensureBaseRules() ? 1 : -1;
            return 0;
        }).then(this, [this](int result) {
            if (result > 0) {
                if (m_running)
                    startCheckTask();
                else if (m_initialized)
                    start();
            } else if (result < 0) {
                {
                    std::lock_guard<std::mutex> lock(m_firewallMutex);
                    m_autoMode = false;
                }
                emit logMessage("error", "Could not prepare firewall rules for automatic mode");
                emit autoModeChangeFailed(true);
            }
        });
    } else {
        QtConcurrent::run([this]() {
            std::lock_guard<std::mutex> lock(m_firewallMutex);
            if (m_autoMode)
                return 0;
            if (!restoreOriginalFirewallPolicy()) {
                if (applyKillSwitch(true))
                    m_killSwitchActive = true;
                return -1;
            }
            if (!removeFirewallRules(kAllAppRuleNames)) {
                ensureBaseRules();
                if (applyKillSwitch(true))
                    m_killSwitchActive = true;
                return -1;
            }
            m_killSwitchActive = false;
            return 1;
        }).then(this, [this](int result) {
            if (result > 0) {
                emit killSwitchChanged(false);
            } else if (result < 0) {
                {
                    std::lock_guard<std::mutex> lock(m_firewallMutex);
                    m_autoMode = true;
                }
                emit logMessage("error", "Could not fully restore the firewall; automatic mode remains enabled");
                emit autoModeChangeFailed(false);
                if (m_running)
                    startCheckTask();
            }
        });
    }
}

void FirewallManager::triggerCheckNow() {
    if (!m_running) return;
    m_timer->stop();
    startCheckTask();
}

void FirewallManager::restoreFirewall() {
    stop();
    {
        std::lock_guard<std::mutex> lock(m_firewallMutex);
        m_autoMode = false;
    }
    if (m_restoreInProgress)
        return;
    m_restoreInProgress = true;
    emit logMessage("warn", "Restoring the saved firewall policy...");

    QtConcurrent::run([this]() {
        std::lock_guard<std::mutex> lock(m_firewallMutex);
        const bool restored = restoreOriginalFirewallPolicy();
        if (!restored)
            return false;

        if (!removeFirewallRules(kAllAppRuleNames)) {
            emit logMessage("error", "Firewall policy restored, but application rules could not be removed");
            if (applyKillSwitch(true))
                m_killSwitchActive = true;
            return false;
        }
        m_killSwitchActive = false;
        if (!clearOriginalFirewallPolicy())
            return false;
        return true;
    }).then(this, [this](bool success) {
        m_restoreInProgress = false;
        stop();
        if (success) {
            emit killSwitchChanged(false);
            emit logMessage("success", "Saved firewall policy restored");
            emit restoreCompleted(true, "Firewall restored");
        } else {
            if (!m_killSwitchActive)
                emit killSwitchChanged(false);
            emit logMessage("error", "Firewall restoration failed; the saved policy was retained");
            emit restoreCompleted(false, "Firewall restoration failed. Run as Administrator and retry.");
        }
    });
}

CheckResult FirewallManager::performBlockingCheck() {
    CheckResult r;

    const QStringList configuredServers = configuredShecanDnsServers();
    const bool localResolverConfigured = configuredServers.contains("127.0.0.1")
        || configuredServers.contains("::1");
    const bool serviceRunning = !localResolverConfigured || isShecanServiceRunning();
    bool configuredResolverHealthy = false;
    bool localResolverHealthy = false;

    for (const QString& ip : configuredServers) {
        if (!m_running)
            return r;
        const bool ok = queryDnsServer(ip, 1000);
        r.serverResults.append({ip, ok});
        if (ok)
            configuredResolverHealthy = true;
        if (ok && (ip == "127.0.0.1" || ip == "::1"))
            localResolverHealthy = true;
    }

    if (!m_running)
        return r;

    if (configuredServers.isEmpty()) {
        r.error = "Shecan DNS is not configured on an active network adapter";
    } else if (localResolverConfigured) {
        r.healthy = serviceRunning && localResolverHealthy;
        if (!r.healthy)
            r.error = serviceRunning
                ? "The configured local Shecan DNS proxy is not answering queries"
                : "The Shecan DNS service is not running";
    } else {
        r.healthy = configuredResolverHealthy;
        if (!r.healthy)
            r.error = "Configured Shecan DNS servers are not reachable";
    }

    if (m_autoMode && m_running) {
        std::lock_guard<std::mutex> lock(m_firewallMutex);
        if (m_autoMode && m_running) {
            const bool shouldBlock = !r.healthy;
            if (shouldBlock != m_killSwitchActive.load()) {
                if (applyKillSwitch(shouldBlock)) {
                    m_killSwitchActive = shouldBlock;
                    r.stateChanged = true;
                    r.newBlockState = shouldBlock;
                } else {
                    r.error = shouldBlock
                        ? "Could not apply the firewall kill switch"
                        : "Could not restore the saved firewall policy";
                }
            }
        }
    }

    return r;
}

void FirewallManager::startCheckTask() {
    if (!m_running) return;
    if (m_watcher->isRunning()) return;

    m_watcher->setFuture(QtConcurrent::run([this]() { return performBlockingCheck(); }));
}

void FirewallManager::onCheckFinished() {
    if (!m_watcher->isFinished()) return;

    const CheckResult r = m_watcher->result();
    emit checkCompleted(r.serverResults, r.healthy);
    if (!r.error.isEmpty())
        emit logMessage("error", r.error);

    if (r.stateChanged) {
        emit killSwitchChanged(r.newBlockState);
        if (r.newBlockState)
            emit logMessage("block", "Shecan DNS unreachable -> Internet BLOCKED");
        else
            emit logMessage("allow", "Shecan DNS healthy -> Saved firewall policy restored");
    }

    if (m_running) scheduleNextCheck();
}

void FirewallManager::scheduleNextCheck() {
    m_timer->start(kCheckInterval);
}
