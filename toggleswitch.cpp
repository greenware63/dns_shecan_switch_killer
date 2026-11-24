#include "toggleswitch.h"
#include <QPainter>
#include <QMouseEvent>
#include <QPropertyAnimation>

ToggleSwitch::ToggleSwitch(QWidget* parent) : QWidget(parent) {
    setCursor(Qt::PointingHandCursor);
    setFixedSize(52, 28);
    m_anim = new QPropertyAnimation(this, "position", this);
    m_anim->setDuration(160);
    m_anim->setEasingCurve(QEasingCurve::InOutCubic);
}

void ToggleSwitch::setChecked(bool checked, bool animate) {
    if (m_checked == checked) return;
    m_checked = checked;
    if (animate) {
        m_anim->stop();
        m_anim->setStartValue(m_position);
        m_anim->setEndValue(checked ? 1.0 : 0.0);
        m_anim->start();
    } else {
        setPosition(checked ? 1.0 : 0.0);
    }
    emit toggled(m_checked);
}

void ToggleSwitch::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const qreal h = 24.0;
    const qreal y = (height() - h) / 2.0;
    QRectF track(0, y, width(), h);

    QColor off = m_darkMode ? QColor(0x37, 0x41, 0x51) : QColor(0xcb, 0xd5, 0xe1);
    QColor on(0x10, 0xb9, 0x81);
    QColor c(
        off.red()   + (on.red()   - off.red())   * m_position,
        off.green() + (on.green() - off.green()) * m_position,
        off.blue()  + (on.blue()  - off.blue())  * m_position);

    p.setPen(Qt::NoPen);
    p.setBrush(c);
    p.drawRoundedRect(track, h/2, h/2);

    const qreal thumbSize = h - 6;
    qreal thumbX = 3.0 + (width() - thumbSize - 6.0) * m_position;
    QRectF thumb(thumbX, y + 3.0, thumbSize, thumbSize);
    p.setBrush(QColor("#ffffff"));
    p.drawEllipse(thumb);
}

void ToggleSwitch::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton && rect().contains(e->pos()))
        setChecked(!m_checked);
}