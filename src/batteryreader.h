#pragma once

#include "batterydevice.h"

#include <QObject>

class BatteryReader : public QObject
{
    Q_OBJECT

public:
    explicit BatteryReader(QObject *parent = nullptr);

public slots:
    void refresh();

signals:
    void readingReady(const BatteryReading &reading);

private:
    bool m_busy = false;
};
