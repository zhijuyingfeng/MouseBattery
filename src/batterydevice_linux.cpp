#include "batterydevice.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QRegularExpression>
#include <QStringList>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

namespace {

constexpr quint16 kCompxVendorId = 0x373b;
constexpr quint16 kScyroxVendorId = 0x3554;
constexpr quint8 kReportId = 0x08;

QByteArray readFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return file.readAll();
}

QString errnoText(int errorNumber)
{
    return QString::fromLocal8Bit(std::strerror(errorNumber));
}

QString openErrorText(int errorNumber)
{
    if (errorNumber == EACCES || errorNumber == EPERM) {
        return QStringLiteral(
            "没有访问鼠标 HID 设备的权限。请安装项目中的 udev 规则，"
            "重新插拔接收器后再试。");
    }
    if (errorNumber == ENOENT) {
        return QStringLiteral(
            "鼠标设备节点不存在。请重新插拔 2.4G 接收器后再试。");
    }
    return QStringLiteral("无法打开鼠标设备：%1").arg(errnoText(errorNumber));
}

struct FdGuard
{
    int fd = -1;
    ~FdGuard()
    {
        if (fd >= 0) {
            ::close(fd);
        }
    }
};

bool supportsBatteryReport(const QByteArray &descriptor)
{
    for (int index = 0; index + 1 < descriptor.size(); ++index) {
        if (static_cast<quint8>(descriptor.at(index)) != 0x85
            || static_cast<quint8>(descriptor.at(index + 1)) != kReportId) {
            continue;
        }

        const int searchEnd = qMin(index + 96, descriptor.size() - 1);
        for (int inner = index + 2; inner < searchEnd; ++inner) {
            if (static_cast<quint8>(descriptor.at(inner)) == 0x95
                && static_cast<quint8>(descriptor.at(inner + 1)) == 0x10) {
                return true;
            }
        }
    }
    return false;
}

} // namespace

QList<MouseHidDevice> BatteryDevice::discover()
{
    QList<MouseHidDevice> result;

    const QString hidrawRoot = QStringLiteral("/sys/class/hidraw");
    QStringList entries = QDir(hidrawRoot).entryList(
        {QStringLiteral("hidraw*")}, QDir::Dirs | QDir::NoDotAndDotDot, QDir::NoSort);

    std::sort(entries.begin(), entries.end(), [](const QString &left, const QString &right) {
        bool leftOk = false;
        bool rightOk = false;
        const int leftNumber = left.mid(6).toInt(&leftOk);
        const int rightNumber = right.mid(6).toInt(&rightOk);
        if (leftOk && rightOk) {
            return leftNumber < rightNumber;
        }
        return left < right;
    });

    for (const QString &entry : entries) {
        const QString hidrawPath = QDir(hidrawRoot).filePath(entry);
        const QString devicePath = hidrawPath + QStringLiteral("/device");
        const QString ueventPath = devicePath + QStringLiteral("/uevent");

        const QByteArray uevent = readFile(ueventPath);
        if (uevent.isEmpty()) {
            continue;
        }

        QString hidId;
        QString hidName;
        QString hidPhys;
        const QList<QByteArray> lines = uevent.split('\n');
        for (const QByteArray &rawLine : lines) {
            const QByteArray line = rawLine.trimmed();
            if (line.startsWith("HID_ID=")) {
                hidId = QString::fromLatin1(line.mid(7));
            } else if (line.startsWith("HID_NAME=")) {
                hidName = QString::fromUtf8(line.mid(9)).simplified();
            } else if (line.startsWith("HID_PHYS=")) {
                hidPhys = QString::fromLatin1(line.mid(9));
            }
        }

        const QStringList idParts = hidId.split(QLatin1Char(':'));
        if (idParts.size() != 3) {
            continue;
        }

        bool vendorOk = false;
        bool productOk = false;
        const quint16 vendorId = idParts.at(1).toUShort(&vendorOk, 16);
        const quint16 productId = idParts.at(2).toUShort(&productOk, 16);
        if (!vendorOk || !productOk) {
            continue;
        }

        if (vendorId != kCompxVendorId && vendorId != kScyroxVendorId) {
            continue;
        }

        const QByteArray descriptor = readFile(devicePath
                                               + QStringLiteral("/report_descriptor"));
        if (!supportsBatteryReport(descriptor)) {
            continue;
        }

        MouseHidDevice device;
        const QString deviceNode = QStringLiteral("/dev/") + entry;
        device.backendPath = deviceNode.toLocal8Bit();
        device.displayPath = deviceNode;
        device.name = hidName.isEmpty()
            ? QStringLiteral("Compx Wireless Mouse")
            : hidName;
        device.vendorId = vendorId;
        device.productId = productId;

        const QRegularExpression inputExpression(QStringLiteral("input(\\d+)"));
        const QRegularExpressionMatch inputMatch = inputExpression.match(hidPhys);
        if (inputMatch.hasMatch()) {
            device.interfaceNumber = inputMatch.captured(1).toInt();
        }
        device.mode = modeFor(device);

        result.append(device);
    }

    return result;
}

BatteryReading BatteryDevice::query(const MouseHidDevice &device, int timeoutMs)
{
    BatteryReading reading;
    reading.mode = device.mode.isEmpty() ? modeFor(device) : device.mode;
    reading.deviceName = device.name;
    reading.devicePath = device.displayPath;
    reading.updatedAt = QDateTime::currentDateTime();

    FdGuard guard;
    guard.fd = ::open(device.backendPath.constData(),
                      O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (guard.fd < 0) {
        reading.error = openErrorText(errno);
        return reading;
    }

    const QByteArray request = buildBatteryRequest();
    QElapsedTimer timer;
    timer.start();

    qsizetype written = 0;
    while (written < request.size() && timer.elapsed() < timeoutMs) {
        pollfd descriptor{};
        descriptor.fd = guard.fd;
        descriptor.events = POLLOUT;

        const int remaining = timeoutMs - static_cast<int>(timer.elapsed());
        const int pollResult = ::poll(&descriptor, 1, qMax(remaining, 1));
        if (pollResult < 0) {
            if (errno == EINTR) {
                continue;
            }
            reading.error = QStringLiteral("等待 HID 设备可写时失败：%1")
                                .arg(errnoText(errno));
            return reading;
        }
        if (pollResult == 0) {
            break;
        }

        const ssize_t count = ::write(guard.fd,
                                      request.constData() + written,
                                      static_cast<size_t>(request.size() - written));
        if (count < 0) {
            if (errno == EINTR || errno == EAGAIN) {
                continue;
            }
            reading.error = QStringLiteral("发送电量查询失败：%1").arg(errnoText(errno));
            return reading;
        }
        written += count;
    }

    if (written != request.size()) {
        reading.error = QStringLiteral("发送电量查询超时。");
        return reading;
    }

    QString lastParseError;
    char buffer[128];
    while (timer.elapsed() < timeoutMs) {
        pollfd descriptor{};
        descriptor.fd = guard.fd;
        descriptor.events = POLLIN;

        const int remaining = timeoutMs - static_cast<int>(timer.elapsed());
        const int pollResult = ::poll(&descriptor, 1, qMax(remaining, 1));
        if (pollResult < 0) {
            if (errno == EINTR) {
                continue;
            }
            reading.error = QStringLiteral("等待鼠标响应时失败：%1")
                                .arg(errnoText(errno));
            return reading;
        }
        if (pollResult == 0) {
            break;
        }

        const ssize_t count = ::read(guard.fd, buffer, sizeof(buffer));
        if (count < 0) {
            if (errno == EINTR || errno == EAGAIN) {
                continue;
            }
            if (errno == ENODEV || errno == EIO) {
                reading.error = QStringLiteral("鼠标已断开连接。");
            } else {
                reading.error = QStringLiteral("读取鼠标响应失败：%1")
                                    .arg(errnoText(errno));
            }
            return reading;
        }
        if (count == 0) {
            continue;
        }

        const QByteArray frame(buffer, static_cast<int>(count));
        QString parseError;
        if (parseBatteryFrame(frame, &reading, &parseError)) {
            reading.updatedAt = QDateTime::currentDateTime();
            return reading;
        }
        lastParseError = parseError;
    }

    reading.available = false;
    reading.error = QStringLiteral(
        "鼠标没有响应。它可能已休眠，或接收器正被其他程序占用。"
        "动一下鼠标后点击“立即刷新”再试。");
    if (!lastParseError.isEmpty()) {
        reading.error += QStringLiteral("\n最后一次响应：%1").arg(lastParseError);
    }
    reading.updatedAt = QDateTime::currentDateTime();
    return reading;
}
