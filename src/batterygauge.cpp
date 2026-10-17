#include "batterygauge.h"

#include <QColor>
#include <QEasingCurve>
#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <QVariantAnimation>

BatteryGauge::BatteryGauge(QWidget *parent)
    : QWidget(parent)
    , m_animation(new QVariantAnimation(this))
{
    setAttribute(Qt::WA_OpaquePaintEvent, false);
    setAccessibleName(QStringLiteral("鼠标剩余电量"));

    m_animation->setDuration(550);
    m_animation->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_animation, &QVariantAnimation::valueChanged, this,
            [this](const QVariant &value) {
                m_displayedLevel = value.toInt();
                update();
            });
}

void BatteryGauge::setReading(int level, bool charging, bool available)
{
    const bool wasAvailable = m_available;
    m_charging = charging;
    m_available = available && level >= 0;

    if (!m_available) {
        m_animation->stop();
        m_level = -1;
        m_displayedLevel = 0;
        update();
        return;
    }

    m_level = qBound(0, level, 100);

    if (!wasAvailable) {
        m_animation->stop();
        m_displayedLevel = m_level;
        update();
        return;
    }

    if (m_displayedLevel == m_level) {
        update();
        return;
    }

    animateTo(m_level);
}

void BatteryGauge::setBusy(bool busy)
{
    if (m_busy == busy) {
        return;
    }
    m_busy = busy;
    update();
}

QSize BatteryGauge::sizeHint() const
{
    return {260, 260};
}

QSize BatteryGauge::minimumSizeHint() const
{
    return {190, 190};
}

void BatteryGauge::animateTo(int level)
{
    m_animation->stop();
    m_animation->setStartValue(m_displayedLevel);
    m_animation->setEndValue(level);
    m_animation->start();
}

QColor BatteryGauge::accentColor() const
{
    if (!m_available) {
        return QColor(QStringLiteral("#64748B"));
    }
    if (m_charging) {
        return QColor(QStringLiteral("#34D399"));
    }
    if (m_displayedLevel <= 20) {
        return QColor(QStringLiteral("#F87171"));
    }
    if (m_displayedLevel <= 40) {
        return QColor(QStringLiteral("#FBBF24"));
    }
    return QColor(QStringLiteral("#22D3EE"));
}

void BatteryGauge::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const qreal side = qMin(width(), height());
    const QPointF center(width() / 2.0, height() / 2.0);
    const qreal penWidth = qMax(10.0, side * 0.062);
    const QRectF square(center.x() - side / 2.0, center.y() - side / 2.0, side, side);
    const QRectF arcRect = square.adjusted(penWidth / 2.0 + 10.0,
                                           penWidth / 2.0 + 10.0,
                                           -(penWidth / 2.0 + 10.0),
                                           -(penWidth / 2.0 + 10.0));

    QRadialGradient background(center, side * 0.49);
    background.setColorAt(0.0, QColor(30, 41, 59, 155));
    background.setColorAt(0.75, QColor(15, 23, 42, 95));
    background.setColorAt(1.0, QColor(15, 23, 42, 0));
    painter.setPen(Qt::NoPen);
    painter.setBrush(background);
    painter.drawEllipse(center, side * 0.47, side * 0.47);

    const QColor accent = accentColor();

    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(QColor(148, 163, 184, 42), penWidth, Qt::SolidLine,
                        Qt::RoundCap));
    painter.drawArc(arcRect, 225 * 16, -270 * 16);

    if (m_available) {
        const int span = qRound(-270.0 * 16.0 * m_displayedLevel / 100.0);
        if (span != 0) {
            painter.setPen(QPen(QColor(accent.red(), accent.green(), accent.blue(), 38),
                                penWidth + 9.0, Qt::SolidLine, Qt::RoundCap));
            painter.drawArc(arcRect, 225 * 16, span);

            QLinearGradient progressGradient(arcRect.topLeft(), arcRect.bottomRight());
            progressGradient.setColorAt(0.0, accent.lighter(125));
            progressGradient.setColorAt(0.55, accent);
            progressGradient.setColorAt(1.0, accent.darker(115));
            painter.setPen(QPen(QBrush(progressGradient), penWidth, Qt::SolidLine,
                                Qt::RoundCap));
            painter.drawArc(arcRect, 225 * 16, span);
        }
    }

    QFont captionFont = font();
    captionFont.setPixelSize(qMax(10, qRound(side * 0.044)));
    captionFont.setLetterSpacing(QFont::AbsoluteSpacing, 1.4);
    painter.setFont(captionFont);
    painter.setPen(QColor(QStringLiteral("#94A3B8")));
    painter.drawText(QRectF(center.x() - 80, center.y() - side * 0.285, 160, 20),
                     Qt::AlignHCenter | Qt::AlignVCenter,
                     QStringLiteral("剩余电量"));

    const bool hasReading = m_available;
    const QString number = hasReading ? QString::number(m_displayedLevel)
                                      : QStringLiteral("--");

    QFont numberFont = font();
    numberFont.setPixelSize(qRound(side * 0.235));
    numberFont.setWeight(QFont::DemiBold);

    QFont percentFont = font();
    percentFont.setPixelSize(qRound(side * 0.086));
    percentFont.setWeight(QFont::Medium);

    const QFontMetricsF numberMetrics(numberFont);
    const QFontMetricsF percentMetrics(percentFont);
    const qreal numberWidth = numberMetrics.horizontalAdvance(number);
    const qreal percentWidth = percentMetrics.horizontalAdvance(QStringLiteral("%"));
    const qreal totalWidth = numberWidth + percentWidth + 2.0;
    const qreal numberX = center.x() - totalWidth / 2.0;
    const qreal baseline = center.y() + numberMetrics.ascent() * 0.42;

    painter.setFont(numberFont);
    painter.setPen(hasReading ? QColor(QStringLiteral("#F8FAFC"))
                              : QColor(QStringLiteral("#64748B")));
    painter.drawText(QPointF(numberX, baseline), number);

    painter.setFont(percentFont);
    painter.setPen(hasReading ? accent : QColor(QStringLiteral("#64748B")));
    painter.drawText(QPointF(numberX + numberWidth + 2.0, baseline),
                     QStringLiteral("%"));

    QString status;
    QColor statusColor;
    QColor chipColor;
    if (!hasReading) {
        status = m_busy ? QStringLiteral("正在读取…") : QStringLiteral("等待连接");
        statusColor = QColor(QStringLiteral("#94A3B8"));
        chipColor = QColor(148, 163, 184, 24);
    } else if (m_charging) {
        status = QStringLiteral("正在充电");
        statusColor = QColor(QStringLiteral("#6EE7B7"));
        chipColor = QColor(52, 211, 153, 34);
    } else {
        status = QStringLiteral("使用电池");
        statusColor = QColor(QStringLiteral("#CBD5E1"));
        chipColor = QColor(148, 163, 184, 28);
    }

    QFont statusFont = font();
    statusFont.setPixelSize(qMax(12, qRound(side * 0.052)));
    statusFont.setWeight(QFont::Medium);
    const QFontMetricsF statusMetrics(statusFont);
    const qreal textWidth = statusMetrics.horizontalAdvance(status);
    const qreal horizontalPadding = 13.0;
    const qreal iconSlot = 15.0;
    const qreal iconGap = 6.0;
    const qreal chipWidth = textWidth + horizontalPadding * 2.0 + iconSlot + iconGap;
    const qreal chipHeight = qMax(26.0, side * 0.105);
    const QRectF chip(center.x() - chipWidth / 2.0,
                      center.y() + side * 0.165,
                      chipWidth,
                      chipHeight);

    painter.setPen(Qt::NoPen);
    painter.setBrush(chipColor);
    painter.drawRoundedRect(chip, chipHeight / 2.0, chipHeight / 2.0);

    const qreal iconCenterX = chip.left() + horizontalPadding + iconSlot / 2.0;
    if (hasReading && m_charging) {
        const qreal unit = chipHeight / 18.0;
        const qreal boltWidth = 8.0 * unit;
        const qreal left = iconCenterX - boltWidth / 2.0;
        const qreal top = chip.top() + 4.0;
        QPainterPath bolt;
        bolt.moveTo(left + 5.0 * unit, top);
        bolt.lineTo(left, top + 8.0 * unit);
        bolt.lineTo(left + 4.0 * unit, top + 8.0 * unit);
        bolt.lineTo(left + 2.0 * unit, top + 14.0 * unit);
        bolt.lineTo(left + 8.0 * unit, top + 5.5 * unit);
        bolt.lineTo(left + 4.0 * unit, top + 5.5 * unit);
        bolt.closeSubpath();
        painter.fillPath(bolt, QColor(QStringLiteral("#FDE68A")));
    } else {
        painter.setBrush(statusColor);
        painter.drawEllipse(QPointF(iconCenterX, chip.center().y()), 3.2, 3.2);
    }

    const qreal textLeft = chip.left() + horizontalPadding + iconSlot + iconGap;
    painter.setFont(statusFont);
    painter.setPen(statusColor);
    painter.drawText(QRectF(textLeft, chip.top(), textWidth + 1.0, chip.height()),
                     Qt::AlignVCenter | Qt::AlignLeft, status);
}
