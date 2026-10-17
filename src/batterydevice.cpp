#include "batterydevice.h"

namespace {

constexpr quint8 kReportId = 0x08;
constexpr quint8 kBatteryCommand = 0x04;
constexpr quint8 kExpectedChecksum = 0x55;

} // namespace

QString BatteryDevice::modeFor(const MouseHidDevice &device)
{
    if (device.productId == 0x11fe
        || device.name.contains(QStringLiteral("dongle"), Qt::CaseInsensitive)
        || device.name.contains(QStringLiteral("wireless"), Qt::CaseInsensitive)) {
        return QStringLiteral("2.4G 无线");
    }
    if (device.name.contains(QStringLiteral("wired"), Qt::CaseInsensitive)) {
        return QStringLiteral("USB 有线");
    }
    return QStringLiteral("USB HID");
}

QByteArray BatteryDevice::buildBatteryRequest()
{
    QByteArray packet(17, '\0');
    packet[0] = static_cast<char>(kReportId);
    packet[1] = static_cast<char>(kBatteryCommand);

    quint8 sum = 0;
    for (int index = 0; index < 16; ++index) {
        sum = static_cast<quint8>(sum + static_cast<quint8>(packet.at(index)));
    }
    packet[16] = static_cast<char>((kExpectedChecksum - sum) & 0xff);
    return packet;
}

bool BatteryDevice::parseBatteryFrame(const QByteArray &frame, BatteryReading *reading,
                                      QString *error)
{
    if (reading == nullptr) {
        return false;
    }

    reading->rawFrame = frame;
    const auto fail = [error](const QString &message) {
        if (error != nullptr) {
            *error = message;
        }
        return false;
    };

    QByteArray data = frame;
    if (data.size() > 17) {
        data.truncate(17);
    }

    // Some HID stacks strip the report ID. Normalize both forms.
    if (data.size() == 16 && static_cast<quint8>(data.at(0)) == kBatteryCommand) {
        data.prepend(static_cast<char>(kReportId));
    }

    if (data.size() != 17) {
        return fail(QStringLiteral("响应长度异常（%1 字节）").arg(data.size()));
    }
    if (static_cast<quint8>(data.at(0)) != kReportId) {
        return fail(QStringLiteral("响应不是电量报告"));
    }
    if (static_cast<quint8>(data.at(1)) != kBatteryCommand) {
        return fail(QStringLiteral("响应命令号不匹配"));
    }
    if (static_cast<quint8>(data.at(2)) != 0) {
        return fail(QStringLiteral("设备返回错误状态 0x%1")
                        .arg(static_cast<quint8>(data.at(2)), 2, 16, QLatin1Char('0')));
    }

    quint8 checksum = 0;
    for (const char byte : data) {
        checksum = static_cast<quint8>(checksum + static_cast<quint8>(byte));
    }
    if (checksum != kExpectedChecksum) {
        return fail(QStringLiteral("响应校验和错误（0x%1）")
                        .arg(checksum, 2, 16, QLatin1Char('0')));
    }

    const int percentage = static_cast<quint8>(data.at(6));
    if (percentage > 100) {
        return fail(QStringLiteral("设备返回了无效电量：%1%").arg(percentage));
    }

    reading->percentage = percentage;
    reading->charging = static_cast<quint8>(data.at(7)) == 1;
    reading->voltageMv = (static_cast<quint8>(data.at(8)) << 8)
        | static_cast<quint8>(data.at(9));
    reading->available = true;
    reading->error.clear();
    return true;
}

QString BatteryDevice::hexDump(const QByteArray &data)
{
    return QString::fromLatin1(data.toHex(' ')).toUpper();
}
