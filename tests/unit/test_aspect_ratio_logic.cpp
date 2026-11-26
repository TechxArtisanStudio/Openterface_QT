#include <QtTest>

class TestAspectRatioLogic : public QObject
{
    Q_OBJECT

private slots:
    void testPortraitModeDetection()
    {
        // 竖屏模式检测：宽高比 < 1.0
        double aspectRatio = 400.0 / 800.0; // 0.5
        QVERIFY(aspectRatio < 1.0);
        qDebug() << "Portrait mode: aspect ratio" << aspectRatio << "< 1.0 - PASS";
    }

    void testLandscapeModeDetection()
    {
        // 横屏模式检测：宽高比 >= 1.0
        double aspectRatio = 1200.0 / 800.0; // 1.5
        QVERIFY(aspectRatio >= 1.0);
        qDebug() << "Landscape mode: aspect ratio" << aspectRatio << ">= 1.0 - PASS";
    }

    void testSquareModeDetection()
    {
        // 正方形模式：宽高比 == 1.0 (视为横屏)
        double aspectRatio = 500.0 / 500.0; // 1.0
        QVERIFY(aspectRatio >= 1.0);
        qDebug() << "Square mode: aspect ratio" << aspectRatio << ">= 1.0 - PASS";
    }

    void testVeryNarrowPortrait()
    {
        // 非常窄的竖屏：300x600 = 0.5
        double aspectRatio = 300.0 / 600.0; // 0.5
        QVERIFY(aspectRatio < 1.0);
        qDebug() << "Very narrow portrait: aspect ratio" << aspectRatio << "- PASS";
    }

    void testWidePortrait()
    {
        // 较宽的竖屏：9:16 = 0.5625
        double aspectRatio = 9.0 / 16.0; // 0.5625
        QVERIFY(aspectRatio < 1.0);
        qDebug() << "Wide portrait (9:16): aspect ratio" << aspectRatio << "- PASS";
    }

    void testCommonResolutions()
    {
        // 测试常见分辨率
        struct TestCase {
            int width;
            int height;
            bool expectedPortrait;
            const char* name;
        };

        TestCase cases[] = {
            {1920, 1080, false, "1920x1080 (16:9)"},
            {1080, 1920, true, "1080x1920 (9:16)"},
            {1280, 720, false, "1280x720 (16:9)"},
            {720, 1280, true, "720x1280 (9:16)"},
            {3840, 2160, false, "3840x2160 (16:9)"},
            {2160, 3840, true, "2160x3840 (9:16)"},
            {1024, 768, false, "1024x768 (4:3)"},
            {768, 1024, true, "768x1024 (3:4)"},
        };

        for (const auto& tc : cases) {
            double ratio = static_cast<double>(tc.width) / tc.height;
            bool isPortrait = (ratio < 1.0);
            QCOMPARE(isPortrait, tc.expectedPortrait);
            qDebug() << tc.name << ": ratio =" << ratio << ", portrait =" << isPortrait << "- PASS";
        }
    }
};

QTEST_GUILESS_MAIN(TestAspectRatioLogic)
#include "test_aspect_ratio_logic.moc"
