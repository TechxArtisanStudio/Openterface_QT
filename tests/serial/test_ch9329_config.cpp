#include <QtTest>

#include "serial/ch9329.h"

Q_LOGGING_CATEGORY(log_core_serial, "opf.test.serial")

class TestCH9329Config : public QObject
{
    Q_OBJECT

private slots:
    void usesSoftwareProtocolModeForPersistentConfiguration();
    void acceptsValidatedCommandResponse();
    void rejectsFailedOrMalformedResponses();
};

void TestCH9329Config::usesSoftwareProtocolModeForPersistentConfiguration()
{
    QCOMPARE(static_cast<uint8_t>(CMD_SET_PARA_CFG_PREFIX_9600[6]), uint8_t(0x00));
    QCOMPARE(static_cast<uint8_t>(CMD_SET_PARA_CFG_PREFIX_115200[6]), uint8_t(0x00));
}

void TestCH9329Config::acceptsValidatedCommandResponse()
{
    const QByteArray response = QByteArray::fromHex("57 AB 00 89 01 00 8C");

    QVERIFY(isSuccessfulCH9329Response(response, 0x09));
}

void TestCH9329Config::rejectsFailedOrMalformedResponses()
{
    QVERIFY(!isSuccessfulCH9329Response(QByteArray(), 0x09));
    QVERIFY(!isSuccessfulCH9329Response(QByteArray::fromHex("57 AB 00 C9 01 E5 B1"), 0x09));
    QVERIFY(!isSuccessfulCH9329Response(QByteArray::fromHex("57 AB 00 8F 01 00 92"), 0x09));
    QVERIFY(!isSuccessfulCH9329Response(QByteArray::fromHex("57 AB 00 89 01 00 00"), 0x09));
}

QTEST_MAIN(TestCH9329Config)
#include "test_ch9329_config.moc"
