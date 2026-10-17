#include "batteryreader.h"

#include <QDateTime>

BatteryReader::BatteryReader(QObject *parent)
    : QObject(parent)
{
}

void BatteryReader::refresh()
{
    if (m_busy) {
        return;
    }
    m_busy = true;

    BatteryReading result;
    result.updatedAt = QDateTime::currentDateTime();

    const QList<MouseHidDevice> devices = BatteryDevice::discover();
    if (devices.isEmpty()) {
        result.error = QStringLiteral(
            "未检测到兼容的鼠标接收器。请确认 2.4G 接收器已插好。");
#ifdef Q_OS_MACOS
        result.error += QStringLiteral(
            "\nmacOS 还需要在“系统设置 → 隐私与安全性 → 输入监控”中允许 "
            "MouseBattery，然后重新启动程序。");
#else
        result.error += QStringLiteral("如果使用 Linux，请检查 udev 权限（见 README）。");
#endif
        m_busy = false;
        emit readingReady(result);
        return;
    }

    // Normally there is exactly one matching vendor interface. If several are
    // present, keep trying until one of them answers.
    for (const MouseHidDevice &device : devices) {
        const BatteryReading reading = BatteryDevice::query(device);
        if (reading.available) {
            m_busy = false;
            emit readingReady(reading);
            return;
        }
        result = reading;
    }

    m_busy = false;
    emit readingReady(result);
}
