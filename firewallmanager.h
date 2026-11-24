#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QFutureWatcher>
#include <QFuture>
#include <QList>
#include <QPair>
#include <QVariantList>
#include <atomic>
#include <mutex>
#include <windows.h>

struct CheckResult {
    bool healthy = false;
    QList<QPair<QString, bool>> serverResults; // server IP -> ok
    bool stateChanged = false;
    bool newBlockState = false;
    QString error;
};

class FirewallManager : public QObject {
    Q_OBJECT
public:
    explicit FirewallManager(QObject* parent = nullptr);
    ~FirewallManager();

    bool isAdmin() const;
    bool isRunning() const { return m_running.load(); }
    bool isKillSwitchActive() const { return m_killSwitchActive.load(); }

    static QStringList shecanDnsServers();
    void cleanupOnExit();

public slots:
    void initialize();
    void start();
    void stop();
    void setAutoMode(bool enabled);
    void triggerCheckNow();
    void restoreFirewall();     // بازیابی دستی

    signals:
        void initialized(bool success, const QString& message);
    void runningChanged(bool running);
    void killSwitchChanged(bool active);
    void checkCompleted(const QList<QPair<QString, bool>>& serverResults, bool healthy);
    void logMessage(const QString& level, const QString& message);
    void restoreCompleted(bool success, const QString& message);
    void autoModeChangeFailed(bool requestedMode);

private slots:
    void onCheckFinished();

private:
    void scheduleNextCheck();
    void startCheckTask();
    bool runNetsh(const QString& args, DWORD timeoutMs = 10000);
    bool ensureBaseRules();
    bool applyKillSwitch(bool enabled);
    bool captureOriginalFirewallPolicy();
    bool restoreOriginalFirewallPolicy();
    bool clearOriginalFirewallPolicy();
    CheckResult performBlockingCheck();

    std::atomic<bool> m_running{false};
    std::atomic<bool> m_autoMode{true};
    std::atomic<bool> m_killSwitchActive{false};
    std::mutex m_firewallMutex;
    bool m_initialized = false;
    bool m_restoreInProgress = false;
    QVariantList m_originalProfiles;

    QTimer* m_timer = nullptr;
    QFutureWatcher<CheckResult>* m_watcher = nullptr;
};