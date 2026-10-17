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
};

Q_DECLARE_METATYPE(BatteryReading)

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

    // Public for the small protocol test executable.
    static QByteArray buildBatteryRequest();
    static bool parseBatteryFrame(const QByteArray &frame, BatteryReading *reading,
                                  QString *error = nullptr);
    static QString hexDump(const QByteArray &data);

private:
    static QString modeFor(const MouseHidDevice &device);
};
