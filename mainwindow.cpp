#include "mainwindow.h"
#include "firewallmanager.h"
#include "toggleswitch.h"

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QFrame>
#include <QTextEdit>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QTimer>
#include <QCloseEvent>
#include <QShowEvent>
#include <QStyle>
#include <QScrollArea>
#include <QTime>
#include <QMessageBox>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSettings>
#include <QFile>
#include <QApplication>

#ifdef Q_OS_WIN
#include <windows.h>
#include <dwmapi.h>
#pragma comment(lib, "Dwmapi.lib")
#endif

static QFrame* makeCard(const QString& name = "card") {
    auto* f = new QFrame;
    f->setObjectName(name);
    return f;
}

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle("Shecan Kill Switch");
    resize(520, 820);
    setMinimumSize(460, 640);

    QSettings settings("ShecanKS", "Shecan Kill Switch");
    m_isDarkMode = settings.value("ui/darkMode", true).toBool();

    m_manager = new FirewallManager(this);
    m_startTime = QDateTime::currentDateTime();

    setupUi();
    applyTheme(m_isDarkMode);
    setupTray();

    connect(m_manager, &FirewallManager::initialized,   this, &MainWindow::onInitialized);
    connect(m_manager, &FirewallManager::killSwitchChanged, this, &MainWindow::onKillSwitchChanged);
    connect(m_manager, &FirewallManager::checkCompleted, this, &MainWindow::onCheckCompleted);
    connect(m_manager, &FirewallManager::logMessage,    this, &MainWindow::onLogMessage);
    connect(m_manager, &FirewallManager::restoreCompleted, this,
            [this](bool success, const QString& message) {
        if (success) {
            const QSignalBlocker blocker(m_autoToggle);
            m_autoToggle->setChecked(false, false);
        } else {
            QMessageBox::critical(this, "Firewall restore failed", message);
            if (m_manager->isKillSwitchActive()) {
                const bool wasChecked = m_autoToggle->isChecked();
                m_autoToggle->setChecked(true, false);
                if (wasChecked)
                    m_manager->setAutoMode(true);
            } else {
                const QSignalBlocker blocker(m_autoToggle);
                m_autoToggle->setChecked(false, false);
            }
        }
    });
    connect(m_manager, &FirewallManager::autoModeChangeFailed, this,
            [this](bool requestedMode) {
        const QSignalBlocker blocker(m_autoToggle);
        m_autoToggle->setChecked(!requestedMode, false);
    });

    m_uptimeTimer = new QTimer(this);
    connect(m_uptimeTimer, &QTimer::timeout, this, &MainWindow::updateUptime);
    m_uptimeTimer->start(1000);

    setStatusChecking();
    appendLog("info", "Starting application...");
    QTimer::singleShot(150, m_manager, &FirewallManager::initialize);
}

MainWindow::~MainWindow() {
    if (m_tray) {
        m_tray->hide();
    }
    if (m_manager) {
        m_manager->cleanupOnExit();
    }
}

void MainWindow::setupUi() {
    auto* central = new QWidget;
    central->setObjectName("centralWidget");
    setCentralWidget(central);

    auto* outer = new QVBoxLayout(central);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setStyleSheet("QScrollArea { background: transparent; }");
    outer->addWidget(scroll);

    auto* content = new QWidget;
    scroll->setWidget(content);

    auto* main = new QVBoxLayout(content);
    main->setContentsMargins(24, 24, 24, 24);
    main->setSpacing(16);

    // ---- Header ----
    auto* header = new QHBoxLayout;
    auto* titleCol = new QVBoxLayout;
    titleCol->setSpacing(2);
    auto* title = new QLabel("Shecan Kill Switch");
    title->setProperty("class", "appTitle");
    auto* subtitle = new QLabel("شکن • محافظ خودکار DNS");
    subtitle->setProperty("class", "appSubtitle");
    titleCol->addWidget(title);
    titleCol->addWidget(subtitle);
    header->addLayout(titleCol);
    header->addStretch();

    m_themeToggle = new QPushButton(this);
    m_themeToggle->setObjectName("themeToggle");
    m_themeToggle->setCursor(Qt::PointingHandCursor);
    m_themeToggle->setCheckable(true);
    m_themeToggle->setChecked(m_isDarkMode);
    m_themeToggle->setText(m_isDarkMode ? "🌙 تم تاریک" : "☀️ تم روشن");
    connect(m_themeToggle, &QPushButton::toggled, this, &MainWindow::onThemeToggled);
    header->addWidget(m_themeToggle);

    main->addLayout(header);

    // ---- Status card ----
    m_statusCard = makeCard("statusCard");
    m_statusCard->setProperty("state", "checking");
    m_statusCard->setMinimumHeight(200);

    auto* statusLayout = new QVBoxLayout(m_statusCard);
    statusLayout->setContentsMargins(24, 28, 24, 28);
    statusLayout->setSpacing(10);
    statusLayout->setAlignment(Qt::AlignCenter);

    m_statusIcon = new QLabel("⋯");
    m_statusIcon->setProperty("class", "statusIcon");
    m_statusIcon->setAlignment(Qt::AlignCenter);

    m_statusTitle = new QLabel("در حال بررسی...");
    m_statusTitle->setProperty("class", "statusTitle");
    m_statusTitle->setAlignment(Qt::AlignCenter);

    m_statusSubtitle = new QLabel("لطفاً صبر کنید");
    m_statusSubtitle->setProperty("class", "statusSubtitle");
    m_statusSubtitle->setAlignment(Qt::AlignCenter);
    m_statusSubtitle->setWordWrap(true);

    statusLayout->addWidget(m_statusIcon, 0, Qt::AlignCenter);
    statusLayout->addWidget(m_statusTitle, 0, Qt::AlignCenter);
    statusLayout->addWidget(m_statusSubtitle, 0, Qt::AlignCenter);
    main->addWidget(m_statusCard);

    // ---- Auto toggle ----
    auto* autoCard = makeCard();
    auto* autoRow = new QHBoxLayout(autoCard);
    autoRow->setContentsMargins(18, 14, 18, 14);
    autoRow->setSpacing(12);

    auto* autoTextCol = new QVBoxLayout;
    autoTextCol->setSpacing(2);
    auto* autoLabel = new QLabel("حالت خودکار");
    autoLabel->setProperty("class", "cardTitle");
    auto* autoDesc = new QLabel("در صورت قطعی DNS، اینترنت را مسدود کن");
    autoDesc->setProperty("class", "cardDesc");
    autoTextCol->addWidget(autoLabel);
    autoTextCol->addWidget(autoDesc);

    m_autoToggle = new ToggleSwitch;
    m_autoToggle->setChecked(true, false);
    connect(m_autoToggle, &ToggleSwitch::toggled, this, &MainWindow::onAutoToggled);

    autoRow->addLayout(autoTextCol);
    autoRow->addStretch();
    autoRow->addWidget(m_autoToggle);
    main->addWidget(autoCard);

    // ---- Stats row ----
    auto* statsRow = new QHBoxLayout;
    statsRow->setSpacing(12);

    auto makeStat = [&](const QString& label, QLabel** valueLbl) {
        auto* c = makeCard();
        auto* l = new QVBoxLayout(c);
        l->setContentsMargins(14, 12, 14, 12);
        l->setSpacing(4);
        auto* t = new QLabel(label);
        t->setProperty("class", "statLabel");
        *valueLbl = new QLabel("—");
        (*valueLbl)->setProperty("class", "statValue");
        l->addWidget(t);
        l->addWidget(*valueLbl);
        return c;
    };

    statsRow->addWidget(makeStat("UPTIME", &m_statUptime));
    statsRow->addWidget(makeStat("CHECKS", &m_statChecks));
    statsRow->addWidget(makeStat("LAST",   &m_statLast));
    main->addLayout(statsRow);

    // ---- DNS servers ----
    auto* dnsCard = makeCard();
    auto* dnsLayout = new QVBoxLayout(dnsCard);
    dnsLayout->setContentsMargins(18, 16, 18, 16);
    dnsLayout->setSpacing(12);

    auto* dnsTitle = new QLabel("وضعیت سرورهای DNS");
    dnsTitle->setProperty("class", "cardTitle");
    dnsLayout->addWidget(dnsTitle);

    auto makeServerRow = [&](const QString& ip, QLabel** dotLbl, QLabel** nameLbl, QLabel** stLbl) {
        auto* row = new QHBoxLayout;
        row->setSpacing(10);
        *dotLbl = new QLabel("●");
        (*dotLbl)->setProperty("class", "dot");
        (*dotLbl)->setObjectName(ip);
        *nameLbl = new QLabel(ip);
        (*nameLbl)->setProperty("class", "srvName");
        *stLbl = new QLabel("—");
        (*stLbl)->setProperty("class", "srvStatus");
        row->addWidget(*dotLbl);
        row->addWidget(*nameLbl);
        row->addStretch();
        row->addWidget(*stLbl);
        return row;
    };

    dnsLayout->addLayout(makeServerRow("127.0.0.1 (Shecan Local DNS)", &m_srv1Dot, &m_srv1Name, &m_srv1Status));
    dnsLayout->addLayout(makeServerRow("178.22.122.100 (Primary)", &m_srv2Dot, &m_srv2Name, &m_srv2Status));
    dnsLayout->addLayout(makeServerRow("185.51.200.2 (Secondary)", &m_srv3Dot, &m_srv3Name, &m_srv3Status));
    main->addWidget(dnsCard);

    // ---- Log ----
    auto* logCard = makeCard();
    auto* logLayout = new QVBoxLayout(logCard);
    logLayout->setContentsMargins(18, 16, 18, 16);
    logLayout->setSpacing(10);

    auto* logHeader = new QHBoxLayout;
    auto* logTitle = new QLabel("لاگ رویدادها");
    logTitle->setProperty("class", "cardTitle");
    auto* clearBtn = new QPushButton("پاک کردن");
    clearBtn->setProperty("class", "ghost");
    clearBtn->setCursor(Qt::PointingHandCursor);
    connect(clearBtn, &QPushButton::clicked, this, [this]() { m_logView->clear(); });
    logHeader->addWidget(logTitle);
    logHeader->addStretch();
    logHeader->addWidget(clearBtn);
    logLayout->addLayout(logHeader);

    m_logView = new QTextEdit;
    m_logView->setReadOnly(true);
    m_logView->setObjectName("logView");
    m_logView->document()->setMaximumBlockCount(500);
    m_logView->setMinimumHeight(180);
    logLayout->addWidget(m_logView);
    main->addWidget(logCard);

    // ---- Action buttons ----
    auto* actionsRow = new QHBoxLayout;
    actionsRow->setSpacing(10);

    m_checkBtn = new QPushButton("بررسی فوری");
    m_checkBtn->setProperty("class", "primary");
    m_checkBtn->setCursor(Qt::PointingHandCursor);
    m_checkBtn->setMinimumHeight(44);
    connect(m_checkBtn, &QPushButton::clicked, this, &MainWindow::onManualCheck);

    m_restoreBtn = new QPushButton("بازیابی فایروال");
    m_restoreBtn->setProperty("class", "secondary");
    m_restoreBtn->setCursor(Qt::PointingHandCursor);
    m_restoreBtn->setMinimumHeight(44);
    connect(m_restoreBtn, &QPushButton::clicked, this, &MainWindow::onManualRestore);

    actionsRow->addWidget(m_checkBtn);
    actionsRow->addWidget(m_restoreBtn);
    main->addLayout(actionsRow);

    main->addStretch();
}

void MainWindow::setupTray() {
    m_tray = new QSystemTrayIcon(this);
    m_tray->setIcon(style()->standardIcon(QStyle::SP_ComputerIcon));
    m_tray->setToolTip("Shecan Kill Switch");

    auto* menu = new QMenu(this);
    auto* showAct = menu->addAction("Show");
    auto* restoreAct = menu->addAction("Restore Firewall");
    menu->addSeparator();
    auto* quitAct = menu->addAction("Quit");

    connect(showAct, &QAction::triggered, this, [this]() { showNormal(); raise(); });
    connect(restoreAct, &QAction::triggered, m_manager, &FirewallManager::restoreFirewall);
    connect(quitAct, &QAction::triggered, this, [this]() {
        close();
    });

    m_tray->setContextMenu(menu);
    connect(m_tray, &QSystemTrayIcon::activated, this, &MainWindow::onTrayActivated);
    m_tray->show();
}

void MainWindow::showEvent(QShowEvent* e) {
    QMainWindow::showEvent(e);
#ifdef Q_OS_WIN
    HWND hwnd = reinterpret_cast<HWND>(winId());
    BOOL dark = m_isDarkMode ? TRUE : FALSE;
    DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark));
#endif
}

void MainWindow::onThemeToggled(bool checked) {
    applyTheme(checked);
    appendLog("info", checked ? "تغییر به تم تاریک" : "تغییر به تم روشن");
}

void MainWindow::applyTheme(bool dark) {
    m_isDarkMode = dark;

    QFile qss(dark ? ":/style.qss" : ":/style_light.qss");
    if (qss.open(QFile::ReadOnly | QFile::Text)) {
        qApp->setStyleSheet(QString::fromUtf8(qss.readAll()));
    }

    if (m_themeToggle) {
        const QSignalBlocker blocker(m_themeToggle);
        m_themeToggle->setChecked(dark);
        m_themeToggle->setText(dark ? "🌙 تم تاریک" : "☀️ تم روشن");
    }

    if (m_autoToggle) {
        m_autoToggle->setDarkMode(dark);
    }

#ifdef Q_OS_WIN
    HWND hwnd = reinterpret_cast<HWND>(winId());
    if (hwnd) {
        BOOL dwmDark = dark ? TRUE : FALSE;
        DwmSetWindowAttribute(hwnd, 20, &dwmDark, sizeof(dwmDark));
    }
#endif

    if (m_statusCard) {
        m_statusCard->style()->unpolish(m_statusCard);
        m_statusCard->style()->polish(m_statusCard);
    }
    if (m_srv1Dot) { m_srv1Dot->style()->unpolish(m_srv1Dot); m_srv1Dot->style()->polish(m_srv1Dot); }
    if (m_srv2Dot) { m_srv2Dot->style()->unpolish(m_srv2Dot); m_srv2Dot->style()->polish(m_srv2Dot); }
    if (m_srv3Dot) { m_srv3Dot->style()->unpolish(m_srv3Dot); m_srv3Dot->style()->polish(m_srv3Dot); }
    if (m_srv1Status) { m_srv1Status->style()->unpolish(m_srv1Status); m_srv1Status->style()->polish(m_srv1Status); }
    if (m_srv2Status) { m_srv2Status->style()->unpolish(m_srv2Status); m_srv2Status->style()->polish(m_srv2Status); }
    if (m_srv3Status) { m_srv3Status->style()->unpolish(m_srv3Status); m_srv3Status->style()->polish(m_srv3Status); }

    QSettings settings("ShecanKS", "Shecan Kill Switch");
    settings.setValue("ui/darkMode", dark);
}

void MainWindow::closeEvent(QCloseEvent* e) {
    if (m_tray) {
        m_tray->hide();
    }
    if (m_uptimeTimer) {
        m_uptimeTimer->stop();
    }
    if (m_manager) {
        m_manager->cleanupOnExit();
    }
    e->accept();
    qApp->quit();
}

void MainWindow::onInitialized(bool ok, const QString& msg) {
    if (ok) {
        appendLog("success", "Initialization complete");
        m_manager->start();
    } else {
        appendLog("error", msg);
        QMessageBox::critical(this, "خطا", msg);
        m_statusTitle->setText("محافظت فعال نشد");
        m_statusSubtitle->setText("وضعیت فایروال قابل تأیید نیست.");
    }
}

void MainWindow::onKillSwitchChanged(bool active) {
    if (active) setStatusBlocked();
    else        setStatusProtected();
    m_tray->setToolTip(active
        ? "Shecan Kill Switch - BLOCKED"
        : "Shecan Kill Switch - PROTECTED");
}

void MainWindow::onCheckCompleted(const QList<QPair<QString,bool>>& servers, bool healthy) {
    m_checkCount++;
    m_statChecks->setText(QString::number(m_checkCount));
    m_statLast->setText(QTime::currentTime().toString("HH:mm:ss"));

    for (const auto& p : servers) {
        if (p.first == "127.0.0.1" || p.first == "::1")
            setServerStatus(m_srv1Dot, m_srv1Status, p.second);
        else if (p.first == "178.22.122.100") setServerStatus(m_srv2Dot, m_srv2Status, p.second);
        else if (p.first == "185.51.200.2")   setServerStatus(m_srv3Dot, m_srv3Status, p.second);
    }

    if (healthy && !m_manager->isKillSwitchActive())
        setStatusProtected();
    else if (!healthy || m_manager->isKillSwitchActive())
        setStatusBlocked();
}

void MainWindow::onLogMessage(const QString& level, const QString& msg) {
    appendLog(level, msg);
}

void MainWindow::onAutoToggled(bool enabled) {
    m_manager->setAutoMode(enabled);
    appendLog("info", enabled ? "Auto mode: ON" : "Auto mode: OFF");
}

void MainWindow::onManualCheck() {
    appendLog("info", "Manual check triggered");
    m_manager->triggerCheckNow();
}

void MainWindow::onManualRestore() {
    auto ret = QMessageBox::question(this, "بازیابی فایروال",
        "اینترنت به حالت عادی برمی‌گردد. ادامه می‌دهید؟",
        QMessageBox::Yes | QMessageBox::No);
    if (ret == QMessageBox::Yes) {
        const QSignalBlocker blocker(m_autoToggle);
        m_autoToggle->setChecked(false, false);
        m_manager->restoreFirewall();
    }
}

void MainWindow::updateUptime() {
    qint64 secs = m_startTime.secsTo(QDateTime::currentDateTime());
    int h = secs / 3600;
    int m = (secs % 3600) / 60;
    int s = secs % 60;
    m_statUptime->setText(QString("%1:%2:%3")
        .arg(h, 2, 10, QChar('0'))
        .arg(m, 2, 10, QChar('0'))
        .arg(s, 2, 10, QChar('0')));
}

void MainWindow::onTrayActivated(QSystemTrayIcon::ActivationReason reason) {
    if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) {
        if (isVisible()) hide();
        else { showNormal(); raise(); activateWindow(); }
    }
}

void MainWindow::setStatusBlocked() {
    m_statusCard->setProperty("state", "blocked");
    m_statusCard->style()->unpolish(m_statusCard);
    m_statusCard->style()->polish(m_statusCard);
    m_statusIcon->setText("✕");
    m_statusTitle->setText("اینترنت مسدود است");
    m_statusSubtitle->setText("شکن متوقف یا قطع شده است. خروج اینترنت برای جلوگیری از نشت IP مسدود شد.");
}

void MainWindow::setStatusProtected() {
    m_statusCard->setProperty("state", "protected");
    m_statusCard->style()->unpolish(m_statusCard);
    m_statusCard->style()->polish(m_statusCard);
    m_statusIcon->setText("✓");
    m_statusTitle->setText("محافظت فعال و متصل");
    m_statusSubtitle->setText("شکن فعال است و DNS از طریق شکن عبور می‌کند. اینترنت آزاد است.");
}

void MainWindow::setStatusChecking() {
    m_statusCard->setProperty("state", "checking");
    m_statusCard->style()->unpolish(m_statusCard);
    m_statusCard->style()->polish(m_statusCard);
    m_statusIcon->setText("⋯");
    m_statusTitle->setText("در حال بررسی...");
    m_statusSubtitle->setText("ارتباط با سرورهای DNS شکن");
}

void MainWindow::setServerStatus(QLabel* dotLbl, QLabel* lbl, bool ok, bool unknown) {
    const QString state = unknown ? "unknown" : (ok ? "ok" : "fail");
    if (dotLbl) {
        dotLbl->setProperty("state", state);
        dotLbl->style()->unpolish(dotLbl);
        dotLbl->style()->polish(dotLbl);
    }
    if (lbl) {
        lbl->setText(unknown ? "—" : (ok ? "سالم" : "خطا"));
        lbl->setProperty("state", state);
        lbl->style()->unpolish(lbl);
        lbl->style()->polish(lbl);
    }
}

void MainWindow::appendLog(const QString& level, const QString& msg) {
    QColor color;
    if      (level == "success" || level == "allow") color = QColor("#10b981");
    else if (level == "warn")                        color = QColor("#f59e0b");
    else if (level == "error" || level == "block")   color = QColor("#ef4444");
    else                                             color = m_isDarkMode ? QColor("#94a3b8") : QColor("#334155");

    QString time = QTime::currentTime().toString("HH:mm:ss");
    QString timeColor = m_isDarkMode ? "#64748b" : "#94a3b8";
    QString html = QString(
        "<span style='color:%1'>[%2]</span> "
        "<span style='color:%3'>%4</span>")
        .arg(timeColor, time, color.name(), msg.toHtmlEscaped());

    m_logView->append(html);
    m_logView->verticalScrollBar()->setValue(m_logView->verticalScrollBar()->maximum());
}