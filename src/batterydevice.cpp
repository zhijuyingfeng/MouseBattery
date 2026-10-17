#include "batterydevice.h"

#include <QThread>


namespace {

constexpr quint8 kReportId = 0x08;
constexpr quint8 kBatteryCommand = 0x04;
constexpr quint8 kReadCommand = 0x08;
constexpr quint8 kWriteCommand = 0x07;
constexpr quint8 kExpectedChecksum = 0x55;
constexpr quint8 kFrameChecksum = 0x4d;

constexpr quint16 kSystemConfigAddress = 0x0000;
constexpr quint16 kDpiBaseAddress = 0x000c;
constexpr int kDpiMin = 50;
constexpr int kDpiMax = 42000;

struct EncodedDpiAxis
{
    quint8 byte = 0;
    quint8 nibble = 0;
};

QByteArray buildPacket(quint8 operation, quint16 address, const QByteArray &data,
                       int declaredLength)
{
    QByteArray frame(16, '\0');
    frame[0] = static_cast<char>(operation);
    frame[2] = static_cast<char>((address >> 8) & 0xff);
    frame[3] = static_cast<char>(address & 0xff);
    frame[4] = static_cast<char>(declaredLength >= 0 ? declaredLength : data.size());

    const int dataLength = qMin(data.size(), 10);
    for (int index = 0; index < dataLength; ++index) {
        frame[5 + index] = data.at(index);
    }

    quint8 sum = 0;
    for (int index = 0; index < 15; ++index) {
        sum = static_cast<quint8>(sum + static_cast<quint8>(frame.at(index)));
    }
    frame[15] = static_cast<char>((kFrameChecksum - sum) & 0xff);

    QByteArray packet(1, static_cast<char>(kReportId));
    packet += frame;
    return packet;
}

int decodeDpiAxis(quint8 byte, quint8 nibble)
{
    const quint8 value = nibble & 0x0f;
    const int high = (value >> 2) & 0x03;
    const bool step50 = (value & 0x02) != 0;
    const bool doubled = (value & 0x01) != 0;
    const int raw = (high << 8) | byte;

    int base = 0;
    if (step50) {
        base = 10050 + raw * 50;
    } else {
        base = (raw + 1) * 10;
    }
    return doubled ? base * 2 : base;
}

EncodedDpiAxis encodeDpiAxis(int dpi)
{
    int value = qBound(kDpiMin, dpi, kDpiMax);
    bool doubled = false;
    if (value > 30000) {
        doubled = true;
        value = qRound(value / 2.0);
    }
    if (value > 10000 && value < 10050) {
        value = 10000;
    }

    EncodedDpiAxis encoded;
    if (value <= 10000) {
        const int code = value / 10 - 1;
        encoded.byte = static_cast<quint8>(code & 0xff);
        encoded.nibble = static_cast<quint8>(((code >> 8) & 0x03) << 2
                                             | (doubled ? 0x01 : 0x00));
    } else {
        const int step = (value - 10050) / 50;
        encoded.byte = static_cast<quint8>(step & 0xff);
        encoded.nibble = static_cast<quint8>(((step >> 8) & 0x03) << 2 | 0x02
                                             | (doubled ? 0x01 : 0x00));
    }
    return encoded;
}

QByteArray encodeDpiWord(int dpi)
{
    const EncodedDpiAxis x = encodeDpiAxis(dpi);
    const EncodedDpiAxis y = x;
    const quint8 mode = static_cast<quint8>((y.nibble << 4) | x.nibble);
    const quint8 sum = static_cast<quint8>(x.byte + y.byte + mode);
    const quint8 checksum = static_cast<quint8>((kExpectedChecksum - sum) & 0xff);

    QByteArray word;
    word.reserve(4);
    word.append(static_cast<char>(x.byte));
    word.append(static_cast<char>(y.byte));
    word.append(static_cast<char>(mode));
    word.append(static_cast<char>(checksum));
    return word;
}

bool decodeDpiWord(const QByteArray &data, int *dpi, QString *error)
{
    if (data.size() < 4) {
        if (error != nullptr) {
            *error = QStringLiteral("DPI 响应长度异常。");
        }
        return false;
    }

    const quint8 byteX = static_cast<quint8>(data.at(0));
    const quint8 byteY = static_cast<quint8>(data.at(1));
    const quint8 mode = static_cast<quint8>(data.at(2));
    const quint8 checksum = static_cast<quint8>(data.at(3));
    if (static_cast<quint8>(byteX + byteY + mode + checksum)
        != kExpectedChecksum) {
        if (error != nullptr) {
            *error = QStringLiteral("DPI 数据校验失败：%1")
                         .arg(QString::fromLatin1(data.toHex()));
        }
        return false;
    }

    const int dpiX = decodeDpiAxis(byteX, mode & 0x0f);
    const int dpiY = decodeDpiAxis(byteY, (mode >> 4) & 0x0f);
    *dpi = dpiX;
    if (dpiX != dpiY) {
        // This UI only edits a single value; show the X axis but keep the
        // device's independent Y value untouched when writing.
        *dpi = dpiX;
    }
    return true;
}

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
    return buildPacket(kBatteryCommand, 0, {}, 0);
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

bool BatteryDevice::readDpi(const MouseHidDevice &device, DpiState *state,
                            int timeoutMs)
{
    if (state == nullptr) {
        return false;
    }

    *state = DpiState{};
    const auto fail = [state](const QString &message) {
        state->available = false;
        state->error = message;
        return false;
    };

    const auto readRegister = [&](quint16 address, int length, QByteArray *data,
                                  QString *error) {
        QByteArray response;
        const QByteArray request = buildPacket(kReadCommand, address, {}, length);
        if (!transact(device, request, kReadCommand, address, true, &response,
                      error, timeoutMs)) {
            return false;
        }

        const QByteArray frame = response.mid(1);
        if (frame.size() < 6) {
            *error = QStringLiteral("DPI 响应帧过短。");
            return false;
        }
        const int responseLength = static_cast<quint8>(frame.at(4));
        if (responseLength < length || frame.size() < 5 + responseLength) {
            *error = QStringLiteral("DPI 响应数据不完整。");
            return false;
        }
        *data = frame.mid(5, responseLength);
        return true;
    };

    QString error;
    QByteArray systemConfig;
    if (!readRegister(kSystemConfigAddress, 6, &systemConfig, &error)) {
        return fail(error);
    }

    state->stages = qBound(1, static_cast<int>(static_cast<quint8>(systemConfig.at(2))), 8);
    state->stageIndex = qBound(
        0, static_cast<int>(static_cast<quint8>(systemConfig.at(4))), state->stages - 1);

    QThread::msleep(15);

    QByteArray word;
    const quint16 dpiAddress = static_cast<quint16>(
        kDpiBaseAddress + state->stageIndex * 4);
    if (!readRegister(dpiAddress, 4, &word, &error)) {
        return fail(error);
    }

    int dpi = 0;
    if (!decodeDpiWord(word, &dpi, &error)) {
        return fail(error);
    }
    if (dpi < kDpiMin || dpi > kDpiMax) {
        return fail(QStringLiteral("设备返回了不支持的 DPI：%1").arg(dpi));
    }

    state->current = dpi;
    state->available = true;
    state->error.clear();
    return true;
}

bool BatteryDevice::writeDpi(const MouseHidDevice &device, int dpi, DpiState *state,
                             int timeoutMs)
{
    if (state == nullptr) {
        return false;
    }
    *state = DpiState{};

    if (dpi < kDpiMin || dpi > kDpiMax) {
        state->error = QStringLiteral("DPI 必须在 %1–%2 之间。")
                           .arg(kDpiMin)
                           .arg(kDpiMax);
        return false;
    }

    DpiState current;
    if (!readDpi(device, &current, timeoutMs)) {
        *state = current;
        return false;
    }

    const quint16 address = static_cast<quint16>(
        kDpiBaseAddress + current.stageIndex * 4);
    const QByteArray word = encodeDpiWord(dpi);
    const QByteArray request = buildPacket(kWriteCommand, address, word, word.size());

    QString error;
    if (!transact(device, request, kWriteCommand, address, false, nullptr, &error,
                  timeoutMs)) {
        state->error = error;
        return false;
    }

    QThread::msleep(30);

    DpiState after;
    if (!readDpi(device, &after, timeoutMs)) {
        state->error = after.error;
        return false;
    }

    *state = after;
    return true;
}

QString BatteryDevice::hexDump(const QByteArray &data)
{
    return QString::fromLatin1(data.toHex(' ')).toUpper();
}
