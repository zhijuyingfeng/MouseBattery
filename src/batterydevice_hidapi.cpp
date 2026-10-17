#include "batterydevice.h"

#include <QElapsedTimer>
#include <QSet>
#include <QString>

#include <algorithm>
#include <mutex>

#if __has_include(<hidapi/hidapi.h>)
#include <hidapi/hidapi.h>
#else
#include <hidapi.h>
#endif

namespace {

constexpr quint16 kCompxVendorId = 0x373b;
constexpr quint16 kScyroxVendorId = 0x3554;
constexpr quint16 kKnownWirelessPid = 0x11fe;

void initializeHidapi()
{
    static std::once_flag initialized;
    std::call_once(initialized, []() {
        hid_init();
    });
}

QString hidErrorText(hid_device *device)
{
    const wchar_t *message = hid_error(device);
    if (message == nullptr) {
        return QStringLiteral("未知 HID 错误");
    }
    return QString::fromWCharArray(message);
}

QString productName(const hid_device_info *info)
{
    if (info->product_string != nullptr && info->product_string[0] != L'\0') {
        return QString::fromWCharArray(info->product_string).simplified();
    }
    return QStringLiteral("Compx Wireless Mouse");
}

bool supportedUsagePage(quint16 usagePage)
{
    return usagePage == 0xff02 || usagePage == 0xff04;
}

bool knownWirelessInterface(const hid_device_info *info)
{
    if (info->vendor_id != kCompxVendorId) {
        return false;
    }
    if (info->product_id != kKnownWirelessPid && info->product_id != 0x11fd) {
        return false;
    }
    return info->interface_number == 1 || info->interface_number < 0;
}

struct EnumerationGuard
{
    hid_device_info *devices = nullptr;
    ~EnumerationGuard()
    {
        if (devices != nullptr) {
            hid_free_enumeration(devices);
        }
    }
};

struct DeviceGuard
{
    hid_device *device = nullptr;
    ~DeviceGuard()
    {
        if (device != nullptr) {
            hid_close(device);
        }
    }
};

} // namespace

QList<MouseHidDevice> BatteryDevice::discover()
{
    initializeHidapi();

    QList<MouseHidDevice> result;
    QSet<QByteArray> seenPaths;

    EnumerationGuard guard;
    guard.devices = hid_enumerate(0, 0);
    for (const hid_device_info *info = guard.devices; info != nullptr; info = info->next) {
        if (info->vendor_id != kCompxVendorId && info->vendor_id != kScyroxVendorId) {
            continue;
        }

        if (!supportedUsagePage(info->usage_page) && !knownWirelessInterface(info)) {
            continue;
        }
        if (info->path == nullptr) {
            continue;
        }

        const QByteArray path(info->path);
        if (seenPaths.contains(path)) {
            continue;
        }
        seenPaths.insert(path);

        MouseHidDevice device;
        device.backendPath = path;
        device.displayPath = QString::fromUtf8(info->path);
        device.name = productName(info);
        device.vendorId = info->vendor_id;
        device.productId = info->product_id;
        device.usagePage = info->usage_page;
        device.interfaceNumber = info->interface_number;
        device.mode = modeFor(device);
        result.append(device);
    }

    std::sort(result.begin(), result.end(),
              [](const MouseHidDevice &left, const MouseHidDevice &right) {
                  if (left.interfaceNumber != right.interfaceNumber) {
                      return left.interfaceNumber == 1;
                  }
                  return left.backendPath < right.backendPath;
              });

    return result;
}

bool BatteryDevice::transact(const MouseHidDevice &device, const QByteArray &packet,
                             quint8 expectedOp, int expectedAddress,
                             bool expectResponse, QByteArray *response,
                             QString *error, int timeoutMs)
{
    initializeHidapi();

    DeviceGuard guard;
    guard.device = hid_open_path(device.backendPath.constData());
    if (guard.device == nullptr) {
        if (error != nullptr) {
            *error = QStringLiteral(
                "无法打开鼠标 HID 设备。请确认接收器已插好，并关闭可能独占设备的厂商驱动。");
#ifdef Q_OS_MACOS
            *error += QStringLiteral(
                "\nmacOS 还需要在“系统设置 → 隐私与安全性 → 输入监控”中允许 "
                "MouseBattery，然后重新启动程序。");
#endif
        }
        return false;
    }

    const int written = hid_write(
        guard.device,
        reinterpret_cast<const unsigned char *>(packet.constData()),
        static_cast<size_t>(packet.size()));
    if (written < 0) {
        if (error != nullptr) {
            *error = QStringLiteral("发送 HID 命令失败：%1")
                         .arg(hidErrorText(guard.device));
        }
        return false;
    }

    if (!expectResponse) {
        return true;
    }

    QElapsedTimer timer;
    timer.start();
    QByteArray buffer(64, '\0');
    QString lastError;

    while (timer.elapsed() < timeoutMs) {
        const int remaining = qMax(1, timeoutMs - static_cast<int>(timer.elapsed()));
        const int count = hid_read_timeout(
            guard.device,
            reinterpret_cast<unsigned char *>(buffer.data()),
            static_cast<size_t>(buffer.size()),
            remaining);

        if (count < 0) {
            if (error != nullptr) {
                *error = QStringLiteral("读取鼠标响应失败：%1")
                             .arg(hidErrorText(guard.device));
            }
            return false;
        }
        if (count == 0) {
            break;
        }

        QByteArray frame(buffer.constData(), count);
        if (frame.size() > 17) {
            frame.truncate(17);
        }
        if (frame.size() == 16
            && (static_cast<quint8>(frame.at(0)) == expectedOp
                || static_cast<quint8>(frame.at(0)) == 0x07
                || static_cast<quint8>(frame.at(0)) == 0x08)) {
            frame.prepend(static_cast<char>(0x08));
        }
        if (frame.size() != 17 || static_cast<quint8>(frame.at(0)) != 0x08) {
            continue;
        }

        quint8 checksum = 0;
        for (const char byte : frame) {
            checksum = static_cast<quint8>(checksum + static_cast<quint8>(byte));
        }
        if (checksum != 0x55) {
            lastError = QStringLiteral("响应校验和错误。");
            continue;
        }

        const quint8 responseOp = static_cast<quint8>(frame.at(1));
        if (responseOp != expectedOp
            && !(expectedOp == 0x08 && responseOp == 0x07)) {
            continue;
        }
        if (expectedAddress >= 0) {
            const int responseAddress = (static_cast<quint8>(frame.at(3)) << 8)
                | static_cast<quint8>(frame.at(4));
            if (responseAddress != expectedAddress) {
                continue;
            }
        }

        if (response != nullptr) {
            *response = frame;
        }
        return true;
    }

    if (error != nullptr) {
        *error = QStringLiteral(
            "鼠标没有响应。它可能已休眠，或接收器正被其他程序占用。"
            "动一下鼠标后重试。");
        if (!lastError.isEmpty()) {
            *error += QStringLiteral("\n最后一次响应：%1").arg(lastError);
        }
    }
    return false;
}

BatteryReading BatteryDevice::query(const MouseHidDevice &device, int timeoutMs)
{
    BatteryReading reading;
    reading.mode = device.mode.isEmpty() ? modeFor(device) : device.mode;
    reading.deviceName = device.name;
    reading.devicePath = device.displayPath;
    reading.updatedAt = QDateTime::currentDateTime();

    QByteArray response;
    QString error;
    if (!transact(device, buildBatteryRequest(), 0x04, -1, true, &response, &error,
                  timeoutMs)) {
        reading.error = error;
        reading.updatedAt = QDateTime::currentDateTime();
        return reading;
    }

    QString parseError;
    if (!parseBatteryFrame(response, &reading, &parseError)) {
        reading.available = false;
        reading.error = parseError;
    }
    reading.updatedAt = QDateTime::currentDateTime();
    return reading;
}
