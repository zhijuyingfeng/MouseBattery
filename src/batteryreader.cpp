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
        BatteryReading reading = BatteryDevice::query(device);
        if (reading.available) {
            DpiState dpi;
            if (BatteryDevice::readDpi(device, &dpi)) {
                reading.dpiAvailable = true;
                reading.dpi = dpi.current;
                reading.dpiStages = dpi.stages;
                reading.dpiStageIndex = dpi.stageIndex;
            } else {
                reading.dpiAvailable = false;
                reading.dpiError = dpi.error;
            }

            m_busy = false;
            emit readingReady(reading);
            return;
        }
        result = reading;
    }

    m_busy = false;
    emit readingReady(result);
}

void BatteryReader::applyDpi(int dpi)
{
    const QList<MouseHidDevice> devices = BatteryDevice::discover();
    if (devices.isEmpty()) {
        emit dpiWriteFinished(false, dpi, QStringLiteral("未检测到鼠标接收器。"));
        return;
    }

    QString lastError;
    for (const MouseHidDevice &device : devices) {
        DpiState state;
        if (BatteryDevice::writeDpi(device, dpi, &state)) {
            emit dpiWriteFinished(true, state.current,
                                  state.current == dpi
                                      ? QString()
                                      : QStringLiteral("设备实际写入的 DPI 为 %1。")
                                            .arg(state.current));
            return;
        }
        lastError = state.error;
    }

    emit dpiWriteFinished(false, dpi, lastError);
}
