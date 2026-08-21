#include <QtTest>
#include "device/DeviceInfo.h"

namespace {
DeviceInfo dev(const QString &chain, const QString &serial, const QString &hid, const QString &cam, const QString &companion = QString())
{
    DeviceInfo d;
    d.portChain = chain;
    d.serialPortPath = serial;
    d.hidDevicePath = hid;
    d.cameraDevicePath = cam;
    d.companionPortChain = companion;
    d.hasCompanionDevice = !companion.isEmpty();
    return d;
}
}

class TestDeviceInfo : public QObject
{
    Q_OBJECT
private slots:
    void displayPortChainStripsZerosAndJoins()
    {
        QCOMPARE(DeviceInfo::displayPortChain("010203"), QStringLiteral("1-2-3"));
        QCOMPARE(DeviceInfo::displayPortChain("1-3"), QStringLiteral("1-3"));
        QCOMPARE(DeviceInfo::displayPortChain(""), QString());
    }

    void oneEntryPerUnit()
    {
        QList<DeviceInfo> in;
        in << dev("1-3", "/dev/ttyACM0", "/dev/hidraw2", "/dev/video0")
           << dev("1-4", "/dev/ttyACM1", "/dev/hidraw3", "/dev/video2");
        const auto out = selectableDevices(in);
        QCOMPARE(out.size(), 2);
        QCOMPARE(out[0].portChain, QStringLiteral("1-3"));
        QCOMPARE(out[1].portChain, QStringLiteral("1-4"));
    }

    void companionChainsAreFolded()
    {
        // USB 3 composite unit: serial at 2-1 with its video/HID companion at 2-2
        QList<DeviceInfo> in;
        in << dev("2-1", "/dev/ttyACM0", QString(), QString(), "2-2")
           << dev("2-2", QString(), "/dev/hidraw2", "/dev/video0");
        const auto out = selectableDevices(in);
        QCOMPARE(out.size(), 1);
        QCOMPARE(out[0].portChain, QStringLiteral("2-1"));
    }

    void duplicatesKeepTheMostCompleteEntry()
    {
        QList<DeviceInfo> in;
        in << dev("1-3", "/dev/ttyACM0", QString(), QString())
           << dev("1-3", "/dev/ttyACM0", "/dev/hidraw2", "/dev/video0")
           << dev("1-3", QString(), "/dev/hidraw2", QString());
        const auto out = selectableDevices(in);
        QCOMPARE(out.size(), 1);
        QCOMPARE(out[0].getInterfaceCount(), 3);
    }

    void emptyPortChainsAreDropped()
    {
        QList<DeviceInfo> in;
        in << dev("", "/dev/ttyACM9", QString(), QString());
        QVERIFY(selectableDevices(in).isEmpty());
    }
};

QTEST_APPLESS_MAIN(TestDeviceInfo)
#include "tst_deviceinfo.moc"
