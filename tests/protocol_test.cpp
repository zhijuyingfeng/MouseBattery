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

    if (failures == 0) {
        std::printf("All protocol tests passed.\n");
        return 0;
    }
    return 1;
}
