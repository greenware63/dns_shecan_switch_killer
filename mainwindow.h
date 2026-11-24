#pragma once

#include <QMainWindow>
#include <QDateTime>
#include <QList>
#include <QPair>
#include <QSystemTrayIcon>

class FirewallManager;
class ToggleSwitch;
class QLabel;
class QPushButton;
class QTextEdit;
class QSystemTrayIcon;
class QFrame;
class QTimer;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow();

protected:
    void closeEvent(QCloseEvent* e) override;
    void showEvent(QShowEvent* e) override;

private slots:
    void onInitialized(bool success, const QString& msg);
    void onKillSwitchChanged(bool active);
    void onCheckCompleted(const QList<QPair<QString, bool>>& servers, bool healthy);
    void onLogMessage(const QString& level, const QString& msg);
    void onAutoToggled(bool enabled);
    void onManualCheck();
    void onManualRestore();
    void updateUptime();
    void onTrayActivated(QSystemTrayIcon::ActivationReason reason);
    void onThemeToggled(bool checked);

private:
    void setupUi();
    void setupTray();
    void applyTheme(bool dark);
    void setStatusBlocked();
    void setStatusProtected();
    void setStatusChecking();
    void appendLog(const QString& level, const QString& msg);
    void setServerStatus(QLabel* dotLbl, QLabel* lbl, bool ok, bool unknown = false);

    FirewallManager* m_manager = nullptr;

    QFrame*     m_statusCard = nullptr;
    QLabel*     m_statusIcon = nullptr;
    QLabel*     m_statusTitle = nullptr;
    QLabel*     m_statusSubtitle = nullptr;

    QPushButton*  m_themeToggle = nullptr;
    bool          m_isDarkMode = true;

    ToggleSwitch* m_autoToggle = nullptr;
    QPushButton*  m_checkBtn = nullptr;
    QPushButton*  m_restoreBtn = nullptr;

    QLabel* m_statUptime = nullptr;
    QLabel* m_statChecks = nullptr;
    QLabel* m_statLast   = nullptr;

    QLabel* m_srv1Dot = nullptr;
    QLabel* m_srv1Name = nullptr;
    QLabel* m_srv1Status = nullptr;
    QLabel* m_srv2Dot = nullptr;
    QLabel* m_srv2Name = nullptr;
    QLabel* m_srv2Status = nullptr;
    QLabel* m_srv3Dot = nullptr;
    QLabel* m_srv3Name = nullptr;
    QLabel* m_srv3Status = nullptr;

    QTextEdit* m_logView = nullptr;
    QSystemTrayIcon* m_tray = nullptr;
    QTimer* m_uptimeTimer = nullptr;

    QDateTime m_startTime;
    int m_checkCount = 0;
};