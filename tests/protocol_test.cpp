#include "batterydevice.h"

#include <QByteArray>
#include <QCoreApplication>

#include <cstdio>

namespace {

int failures = 0;

void check(bool condition, const char *message)
{
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

void fixChecksum(QByteArray *frame)
{
    quint8 sum = 0;
    for (int index = 0; index < 16; ++index) {
        sum = static_cast<quint8>(sum + static_cast<quint8>(frame->at(index)));
    }
    (*frame)[16] = static_cast<char>((0x55 - sum) & 0xff);
}

} // namespace

namespace {

QByteArray g_dpiWord = QByteArray::fromHex("4f4f00b7"); // 800 DPI

QByteArray makeDpiResponse(quint16 address, const QByteArray &data)
{
    QByteArray response(17, '\0');
    response[0] = static_cast<char>(0x08);
    response[1] = static_cast<char>(0x08);
    response[2] = static_cast<char>(0x00);
    response[3] = static_cast<char>((address >> 8) & 0xff);
    response[4] = static_cast<char>(address & 0xff);
    response[5] = static_cast<char>(data.size());
    for (int index = 0; index < data.size() && index < 10; ++index) {
        response[6 + index] = data.at(index);
    }
    fixChecksum(&response);
    return response;
}

} // namespace

// The protocol test uses an in-memory transport so DPI frame offsets and
// encoding can be verified without a physical mouse.
bool BatteryDevice::transact(const MouseHidDevice &, const QByteArray &packet,
                             quint8, int, bool expectResponse,
                             QByteArray *response, QString *error, int)
{
    if (packet.size() != 17 || static_cast<quint8>(packet.at(0)) != 0x08) {
        if (error != nullptr) {
            *error = QStringLiteral("bad test packet");
        }
        return false;
    }

    const quint8 operation = static_cast<quint8>(packet.at(1));
    const quint16 address = static_cast<quint16>(
        (static_cast<quint8>(packet.at(3)) << 8)
        | static_cast<quint8>(packet.at(4)));
    const int length = static_cast<quint8>(packet.at(5));

    if (operation == 0x07 && address == 0x0010 && length == 4) {
        g_dpiWord = packet.mid(6, 4);
        return true;
    }

    if (operation != 0x08) {
        if (error != nullptr) {
            *error = QStringLiteral("unexpected test operation");
        }
        return false;
    }

    QByteArray data;
    if (address == 0x0000) {
        data = QByteArray::fromHex("015404510154");
    } else if (address == 0x0010) {
        data = g_dpiWord;
    } else {
        if (error != nullptr) {
            *error = QStringLiteral("unexpected test address");
        }
        return false;
    }

    if (expectResponse && response != nullptr) {
        *response = makeDpiResponse(address, data);
    }
    return true;
}

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);

    const QByteArray request = BatteryDevice::buildBatteryRequest();
    check(request.size() == 17, "request length is 17 bytes");
    check(static_cast<quint8>(request.at(0)) == 0x08, "request report ID");
    check(static_cast<quint8>(request.at(1)) == 0x04, "request battery opcode");
    check(static_cast<quint8>(request.at(16)) == 0x49, "request checksum");

    quint8 requestSum = 0;
    for (const char byte : request) {
        requestSum = static_cast<quint8>(requestSum + static_cast<quint8>(byte));
    }
    check(requestSum == 0x55, "request checksum sums to 0x55");

    // 78%, charging, 4.052 V.
    QByteArray response(17, '\0');
    response[0] = static_cast<char>(0x08);
    response[1] = static_cast<char>(0x04);
    response[6] = static_cast<char>(78);
    response[7] = static_cast<char>(1);
    response[8] = static_cast<char>(0x0f);
    response[9] = static_cast<char>(0xd4);
    fixChecksum(&response);

    BatteryReading reading;
    QString error;
    check(BatteryDevice::parseBatteryFrame(response, &reading, &error),
          "valid response is parsed");
    check(reading.available, "reading is available");
    check(reading.percentage == 78, "percentage is 78");
    check(reading.charging, "charging flag is true");
    check(reading.voltageMv == 4052, "voltage is 4052 mV");

    BatteryReading withoutReportId;
    check(BatteryDevice::parseBatteryFrame(response.mid(1), &withoutReportId, &error),
          "report-id-stripped response is parsed");
    check(withoutReportId.percentage == 78,
          "report-id-stripped percentage is 78");

    BatteryReading badChecksum;
    QByteArray bad = response;
    bad[16] = static_cast<char>(0x00);
    check(!BatteryDevice::parseBatteryFrame(bad, &badChecksum, &error),
          "bad checksum is rejected");

    BatteryReading badPercentage;
    QByteArray invalidPercentage = response;
    invalidPercentage[6] = static_cast<char>(101);
    fixChecksum(&invalidPercentage);
    check(!BatteryDevice::parseBatteryFrame(invalidPercentage, &badPercentage, &error),
          "percentage above 100 is rejected");

    g_dpiWord = QByteArray::fromHex("4f4f00b7");
    MouseHidDevice mockDevice;
    DpiState dpi;
    check(BatteryDevice::readDpi(mockDevice, &dpi), "DPI is read from mock device");
    if (!dpi.error.isEmpty()) {
        std::fprintf(stderr, "DPI error: %s\n", qPrintable(dpi.error));
    }
    check(dpi.available, "DPI state is available");
    check(dpi.current == 800, "DPI current value is 800");
    check(dpi.stages == 4, "DPI stage count is 4");
    check(dpi.stageIndex == 1, "DPI current stage is 1");

    DpiState written;
    check(BatteryDevice::writeDpi(mockDevice, 1600, &written),
          "DPI is written to mock device");
    check(written.current == 1600, "DPI write reads back 1600");

    if (failures == 0) {
        std::printf("All protocol tests passed.\n");
        return 0;
    }
    return 1;
}
