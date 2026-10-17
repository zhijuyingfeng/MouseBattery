#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QMetaType>
#include <QString>
#include <QtGlobal>

struct BatteryReading
{
    bool available = false;
    int percentage = -1;
    bool charging = false;
    int voltageMv = 0;
    QString mode;
    QString deviceName;
    QString devicePath;
    QString error;
    QByteArray rawFrame;
    QDateTime updatedAt;

    bool dpiAvailable = false;
    int dpi = 0;
    int dpiStages = 0;
    int dpiStageIndex = 0;
    QString dpiError;
};

Q_DECLARE_METATYPE(BatteryReading)

struct DpiState
{
    bool available = false;
    int current = 0;
    int stages = 0;
    int stageIndex = 0;
    QString error;
};

struct MouseHidDevice
{
    // Linux: /dev/hidrawN. Windows/macOS: the opaque path from hidapi.
    QByteArray backendPath;
    // Safe to show in the UI tooltip.
    QString displayPath;
    QString name;
    QString mode;
    quint16 vendorId = 0;
    quint16 productId = 0;
    quint16 usagePage = 0;
    int interfaceNumber = -1;
};

class BatteryDevice
{
public:
    static QList<MouseHidDevice> discover();

    static BatteryReading query(const MouseHidDevice &device, int timeoutMs = 900);

    static bool readDpi(const MouseHidDevice &device, DpiState *state, int timeoutMs = 1200);
    static bool writeDpi(const MouseHidDevice &device, int dpi, DpiState *state,
                         int timeoutMs = 1200);

    // Public for the small protocol test executable.
    static QByteArray buildBatteryRequest();
    static bool parseBatteryFrame(const QByteArray &frame, BatteryReading *reading,
                                  QString *error = nullptr);
    static QString hexDump(const QByteArray &data);

private:
    static QString modeFor(const MouseHidDevice &device);

    // Platform backends implement this. Packets include the report ID.
    // expectedAddress may be -1 (battery query has no address).
    static bool transact(const MouseHidDevice &device, const QByteArray &packet,
                         quint8 expectedOp, int expectedAddress, bool expectResponse,
                         QByteArray *response, QString *error, int timeoutMs);
};
