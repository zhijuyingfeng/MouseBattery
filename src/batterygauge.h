#pragma once

#include <QColor>
#include <QWidget>

class QVariantAnimation;

class BatteryGauge : public QWidget
{
    Q_OBJECT

public:
    explicit BatteryGauge(QWidget *parent = nullptr);

    void setReading(int level, bool charging, bool available);
    void setBusy(bool busy);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    void animateTo(int level);
    QColor accentColor() const;

    int m_level = -1;
    int m_displayedLevel = 0;
    bool m_charging = false;
    bool m_available = false;
    bool m_busy = false;
    QVariantAnimation *m_animation = nullptr;
};
