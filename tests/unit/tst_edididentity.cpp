#include <QtTest>
#include "ui/advance/edid/edididentity.h"
#include "ui/advance/edid/edidutils.h"

using edid::EdidIdentity;

namespace {

// Build a minimal but well-formed EDID block 0 with the given identity.
QByteArray makeEdidBlock(const char *pnp, quint16 product, quint32 serial,
                         const QString &name, const QString &serialStr)
{
    QByteArray b(128, '\0');
    const char hdr[8] = {0x00, '\xFF', '\xFF', '\xFF', '\xFF', '\xFF', '\xFF', 0x00};
    b.replace(0, 8, QByteArray(hdr, 8));
    auto enc = [](char c) { return static_cast<quint16>(c - 'A' + 1); };
    quint16 mfg = static_cast<quint16>((enc(pnp[0]) << 10) | (enc(pnp[1]) << 5) | enc(pnp[2]));
    b[8] = static_cast<char>(mfg >> 8);
    b[9] = static_cast<char>(mfg & 0xFF);
    b[10] = static_cast<char>(product & 0xFF);
    b[11] = static_cast<char>(product >> 8);
    b[12] = static_cast<char>(serial & 0xFF);
    b[13] = static_cast<char>((serial >> 8) & 0xFF);
    b[14] = static_cast<char>((serial >> 16) & 0xFF);
    b[15] = static_cast<char>((serial >> 24) & 0xFF);
    b[18] = 1; b[19] = 4;          // EDID 1.4
    // Descriptors at 54/72/90/108: leave 54 as a dummy detailed timing
    // (non-zero pixel clock) and use the others for name/serial.
    b[54] = 0x01; b[55] = 0x1D;
    if (!name.isEmpty())      edid::EDIDUtils::updateEDIDDisplayName(b, name);
    if (!serialStr.isEmpty()) edid::EDIDUtils::updateEDIDSerialNumber(b, serialStr);
    b[127] = static_cast<char>(edid::EDIDUtils::calculateEDIDChecksum(b));
    return b;
}

QByteArray makeImage(const QByteArray &edidBlock, int prefix, int suffix)
{
    QByteArray img(prefix, '\x5A');
    img += edidBlock;
    img += QByteArray(suffix, '\xA5');
    return img;
}

} // namespace

class TestEdidIdentity : public QObject
{
    Q_OBJECT
private slots:
    void parsesBrandedImage()
    {
        QByteArray img = makeImage(makeEdidBlock("OPT", 0x2109, 0x01020304,
                                                 "BRAIN-G4-KVM", "1C7A4EE0C3C9"), 300, 941);
        EdidIdentity id = EdidIdentity::fromImage(img);
        QVERIFY(id.valid);
        QCOMPARE(id.edidOffset, 300);
        QCOMPARE(id.firmwareSize, img.size());
        QCOMPARE(id.manufacturerId, QStringLiteral("OPT"));
        QCOMPARE(id.productCode, quint16(0x2109));
        QCOMPARE(id.serialU32, quint32(0x01020304));
        QCOMPARE(id.displayName, QStringLiteral("BRAIN-G4-KVM"));
        QCOMPARE(id.serialString, QStringLiteral("1C7A4EE0C3C9"));
        QCOMPARE(id.rawImage, img);
        QVERIFY(id.summary().contains("BRAIN-G4-KVM"));
    }

    void emptyDescriptorsGiveEmptyStrings()
    {
        QByteArray img = makeImage(makeEdidBlock("ABC", 1, 0, QString(), QString()), 0, 0);
        EdidIdentity id = EdidIdentity::fromImage(img);
        QVERIFY(id.valid);
        QCOMPARE(id.edidOffset, 0);
        QVERIFY(id.displayName.isEmpty());
        QVERIFY(id.serialString.isEmpty());
    }

    void noEdidHeaderIsInvalid()
    {
        QByteArray img(1369, '\x11');
        EdidIdentity id = EdidIdentity::fromImage(img);
        QVERIFY(!id.valid);
        QCOMPARE(id.edidOffset, -1);
        QCOMPARE(id.firmwareSize, 1369);
        QVERIFY(id.summary().contains("not found"));
    }

    void truncatedBlockIsInvalid()
    {
        QByteArray block = makeEdidBlock("OPT", 1, 1, "X", QString());
        QByteArray img = makeImage(block.left(100), 10, 0);  // header present, block cut
        QVERIFY(!EdidIdentity::fromImage(img).valid);
    }

    void roundTripThroughUpdateHelpers()
    {
        // What the write path does (EDIDUtils update + checksum) must be
        // what the read path sees.
        QByteArray img = makeImage(makeEdidBlock("OPT", 0x2109, 7, "OLD-NAME", "OLD-SER"), 64, 64);
        int off = edid::EDIDUtils::findEDIDBlock0(img);
        QByteArray block = img.mid(off, 128);
        edid::EDIDUtils::updateEDIDDisplayName(block, "BRAIN-A1-KVM");
        edid::EDIDUtils::updateEDIDSerialNumber(block, "NEW-SER");
        block[127] = static_cast<char>(edid::EDIDUtils::calculateEDIDChecksum(block));
        img.replace(off, 128, block);
        EdidIdentity id = EdidIdentity::fromImage(img);
        QCOMPARE(id.displayName, QStringLiteral("BRAIN-A1-KVM"));
        QCOMPARE(id.serialString, QStringLiteral("NEW-SER"));
    }
};

QTEST_APPLESS_MAIN(TestEdidIdentity)
#include "tst_edididentity.moc"
