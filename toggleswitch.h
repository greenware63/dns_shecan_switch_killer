#pragma once
#include <QWidget>
#include <QPropertyAnimation>

class ToggleSwitch : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal position READ position WRITE setPosition)
public:
    explicit ToggleSwitch(QWidget* parent = nullptr);
    bool isChecked() const { return m_checked; }
    qreal position() const { return m_position; }
    void setPosition(qreal p) { m_position = p; update(); }
    void setDarkMode(bool dark) { m_darkMode = dark; update(); }
    QSize sizeHint() const override { return QSize(52, 28); }

public slots:
    void setChecked(bool checked, bool animate = true);

    signals:
        void toggled(bool checked);

protected:
    void paintEvent(QPaintEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;

private:
    bool m_checked = false;
    bool m_darkMode = true;
    qreal m_position = 0.0;
    QPropertyAnimation* m_anim;
};