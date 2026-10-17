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
    void applyDpi(int dpi);

signals:
    void readingReady(const BatteryReading &reading);
    void dpiWriteFinished(bool success, int dpi, const QString &error);

private:
    bool m_busy = false;
};
