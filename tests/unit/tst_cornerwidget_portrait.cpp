#include <QtTest>
#include <algorithm>
#include <cmath>
#include <QAbstractItemView>
#include <QApplication>
#include <QColor>
#include <QElapsedTimer>
#include <QHBoxLayout>
#include <QMainWindow>
#include <QMenuBar>
#include <QMouseEvent>
#include <QPushButton>
#include <QScopeGuard>
#include <QSet>
#include <QVBoxLayout>
#include "ui/cornerwidget/cornerwidgetmanager.h"

namespace {
QWidget *verticalWidget(const CornerWidgetManager &manager)
{
    QWidget *root = qobject_cast<QWidget *>(manager.parent());
    for (QWidget *widget : root->findChildren<QWidget *>()) {
        if (widget->windowFlags() & Qt::Tool) {
            return widget;
        }
    }
    return nullptr;
}

QWidgetList toolbarControls(const CornerWidgetManager &manager)
{
    QWidget *root = qobject_cast<QWidget *>(manager.parent());
    return {
        root->findChild<QComboBox *>("keyboardLayoutComboBox"),
        root->findChild<QPushButton *>("ScreenScaleButton"),
        root->findChild<QPushButton *>("ZoomInButton"),
        root->findChild<QPushButton *>("ZoomOutButton"),
        root->findChild<QPushButton *>("ZoomReductionButton"),
        root->findChild<QPushButton *>("virtualKeyboardButton"),
        root->findChild<QPushButton *>("captureButton"),
        root->findChild<QPushButton *>("fullScreenButton"),
        root->findChild<QPushButton *>("pasteButton"),
        root->findChild<QPushButton *>("screensaverButton"),
        root->findChild<QPushButton *>("recordingButton"),
        root->findChild<QPushButton *>("muteButton"),
        root->findChild<QPushButton *>("aiChatButton"),
        root->findChild<ToggleSwitch *>()
    };
}

void sendMouse(QWidget *widget, QEvent::Type type, const QPoint &globalPos,
               Qt::MouseButton button, Qt::MouseButtons buttons)
{
    const QPoint localPos = widget->mapFromGlobal(globalPos);
    QMouseEvent event(type, localPos, globalPos, button, buttons, Qt::NoModifier);
    QApplication::sendEvent(widget, &event);
}

double contrastRatio(const QColor &first, const QColor &second)
{
    const auto luminance = [](const QColor &color) {
        const auto channel = [](int value) {
            const double normalized = value / 255.0;
            return normalized <= 0.04045
                ? normalized / 12.92
                : std::pow((normalized + 0.055) / 1.055, 2.4);
        };
        return 0.2126 * channel(color.red())
             + 0.7152 * channel(color.green())
             + 0.0722 * channel(color.blue());
    };
    const double firstLuminance = luminance(first);
    const double secondLuminance = luminance(second);
    return (std::max(firstLuminance, secondLuminance) + 0.05)
         / (std::min(firstLuminance, secondLuminance) + 0.05);
}
}

class TestCornerWidgetPortrait : public QObject
{
    Q_OBJECT

private slots:
    void testInitialPositionUsesCurrentMainWindowFrame();
    void testVerticalWidgetFollowsMainWindow();
    void testDraggingMovesWholePanelWithoutBreakingClicks();
    void testOrientationSwitchPreservesControlsAndFullscreenState();
    void testFloatingLandscapeRestoresMenuBarLayout();
    void testKeyboardLayoutPopupReadableInDarkTheme();
    void testRepeatedPositionUpdatesAreLightweight();
    void testFullscreenArgumentDoesNotEnablePortraitFullscreen();
};

void TestCornerWidgetPortrait::testInitialPositionUsesCurrentMainWindowFrame()
{
    QMainWindow mainWindow;
    mainWindow.move(120, 90);
    mainWindow.resize(400, 800);
    QMenuBar menuBar(&mainWindow);
    mainWindow.setMenuBar(&menuBar);
    CornerWidgetManager manager(&mainWindow);
    manager.setMenuBar(&menuBar);
    mainWindow.show();
    QVERIFY(QTest::qWaitForWindowExposed(&mainWindow));

    manager.updatePosition(10, 10, menuBar.height(), false, true);
    QWidget *vertical = verticalWidget(manager);
    QVERIFY(vertical);
    QTRY_VERIFY(vertical->isVisible());
    QCOMPARE(vertical->pos(), QPoint(mainWindow.frameGeometry().right() + 1,
                                     mainWindow.frameGeometry().y() + menuBar.height() + 10));

    manager.updatePosition(10, 10, menuBar.height(), false, true);
    QCOMPARE(vertical->pos(), QPoint(mainWindow.frameGeometry().right() + 1,
                                     mainWindow.frameGeometry().y() + menuBar.height() + 10));
}

void TestCornerWidgetPortrait::testVerticalWidgetFollowsMainWindow()
{
    QMainWindow mainWindow;
    mainWindow.move(100, 100);
    mainWindow.resize(400, 800);
    QMenuBar menuBar(&mainWindow);
    mainWindow.setMenuBar(&menuBar);
    CornerWidgetManager manager(&mainWindow);
    manager.setMenuBar(&menuBar);
    mainWindow.show();
    QVERIFY(QTest::qWaitForWindowExposed(&mainWindow));
    manager.updatePosition(400, 800, menuBar.height(), false, true);

    QWidget *vertical = verticalWidget(manager);
    QVERIFY(vertical);
    QTRY_VERIFY(vertical->isVisible());
    const QPoint original = vertical->pos();
    mainWindow.move(mainWindow.pos() + QPoint(80, 40));
    QTRY_COMPARE(vertical->pos(), original + QPoint(80, 40));

    mainWindow.resize(mainWindow.width() + 50, mainWindow.height());
    QTRY_COMPARE(vertical->pos(), QPoint(mainWindow.frameGeometry().right() + 1,
                                         mainWindow.frameGeometry().y() + menuBar.height() + 10));
}

void TestCornerWidgetPortrait::testDraggingMovesWholePanelWithoutBreakingClicks()
{
    QMainWindow mainWindow;
    mainWindow.move(100, 100);
    mainWindow.resize(400, 800);
    QMenuBar menuBar(&mainWindow);
    mainWindow.setMenuBar(&menuBar);
    CornerWidgetManager manager(&mainWindow);
    manager.setMenuBar(&menuBar);
    mainWindow.show();
    QVERIFY(QTest::qWaitForWindowExposed(&mainWindow));
    manager.updatePosition(400, 800, menuBar.height(), false, true);

    QWidget *vertical = verticalWidget(manager);
    QPushButton *button = mainWindow.findChild<QPushButton *>("ZoomInButton");
    QVERIFY(vertical);
    QTRY_VERIFY(vertical->isVisible());
    QVERIFY(button);
    const QPoint original = vertical->pos();
    const QPoint start = button->mapToGlobal(button->rect().center());

    sendMouse(button, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
    sendMouse(button, QEvent::MouseMove, start + QPoint(25, 35), Qt::NoButton, Qt::LeftButton);
    sendMouse(vertical, QEvent::MouseButtonRelease, start + QPoint(25, 35), Qt::LeftButton, Qt::NoButton);
    QCOMPARE(vertical->pos(), original + QPoint(25, 35));

    const QPoint moved = vertical->pos();
    mainWindow.move(mainWindow.pos() + QPoint(20, 10));
    QTRY_COMPARE(vertical->pos(), moved + QPoint(20, 10));

    QSignalSpy clicks(&manager, &CornerWidgetManager::zoomInClicked);
    const QPoint clickPos = button->mapToGlobal(button->rect().center());
    sendMouse(button, QEvent::MouseButtonPress, clickPos, Qt::LeftButton, Qt::LeftButton);
    sendMouse(button, QEvent::MouseButtonRelease, clickPos, Qt::LeftButton, Qt::NoButton);
    QCOMPARE(clicks.count(), 1);
    QCOMPARE(vertical->pos(), moved + QPoint(20, 10));
}

void TestCornerWidgetPortrait::testOrientationSwitchPreservesControlsAndFullscreenState()
{
    QMainWindow mainWindow;
    mainWindow.resize(400, 800);
    QMenuBar menuBar(&mainWindow);
    mainWindow.setMenuBar(&menuBar);
    CornerWidgetManager manager(&mainWindow);
    manager.setMenuBar(&menuBar);
    mainWindow.show();
    QVERIFY(QTest::qWaitForWindowExposed(&mainWindow));

    const QWidgetList controls = toolbarControls(manager);
    QCOMPARE(controls.size(), 14);
    for (QWidget *control : controls) {
        QVERIFY(control);
    }

    manager.updatePosition(400, 800, menuBar.height(), false, true);
    QWidget *vertical = verticalWidget(manager);
    QWidget *horizontal = manager.getCornerWidget();
    QVERIFY(vertical);
    QTRY_VERIFY(vertical->isVisible());
    QVERIFY(!horizontal->isVisible());
    for (QWidget *control : controls) {
        QVERIFY(vertical->isAncestorOf(control));
    }
    QSet<qint64> iconKeys;
    for (QPushButton *button : vertical->findChildren<QPushButton *>()) {
        QCOMPARE(button->iconSize(), QSize(24, 24));
        QCOMPARE(button->minimumSize(), QSize(40, 40));
        QVERIFY(!button->icon().isNull());
        QVERIFY(!button->icon().pixmap(24, 24).isNull());
        iconKeys.insert(button->icon().cacheKey());
    }
    QCOMPARE(iconKeys.size(), 12);
    QPushButton *fullscreen = mainWindow.findChild<QPushButton *>("fullScreenButton");
    QVERIFY(fullscreen);
    QVERIFY(!fullscreen->isEnabled());

    manager.updatePosition(1200, 700, menuBar.height(), false, false);
    QVERIFY(!vertical->isVisible());
    QVERIFY(horizontal->isVisible());
    QVERIFY(fullscreen->isEnabled());
    QVERIFY(horizontal->layout());
    QCOMPARE(horizontal->layout()->count(), controls.size());
    for (QWidget *control : controls) {
        QVERIFY(horizontal->isAncestorOf(control));
    }

    manager.updatePosition(400, 800, menuBar.height(), false, true);
    QVERIFY(vertical->isVisible());
    QVERIFY(!fullscreen->isEnabled());
    for (QWidget *control : controls) {
        QVERIFY(vertical->isAncestorOf(control));
    }
}

void TestCornerWidgetPortrait::testFloatingLandscapeRestoresMenuBarLayout()
{
    QMainWindow mainWindow;
    mainWindow.move(100, 100);
    mainWindow.resize(1200, 800);
    QMenuBar menuBar(&mainWindow);
    mainWindow.setMenuBar(&menuBar);
    CornerWidgetManager manager(&mainWindow);
    manager.setMenuBar(&menuBar);
    mainWindow.show();
    QVERIFY(QTest::qWaitForWindowExposed(&mainWindow));
    manager.updatePosition(1200, 800, menuBar.height(), false, false);

    QWidget *horizontal = manager.getCornerWidget();
    QCOMPARE(menuBar.cornerWidget(Qt::TopRightCorner), horizontal);
    QCOMPARE(horizontal->layout()->count(), 14);

    manager.updatePosition(700, 800, menuBar.height(), false, false);
    QCOMPARE(horizontal->parentWidget(), &mainWindow);
    QVERIFY(horizontal->isVisible());
    QVERIFY(menuBar.cornerWidget(Qt::TopRightCorner) == nullptr);
    QCOMPARE(horizontal->layout()->count(), 14);

    manager.updatePosition(1200, 800, menuBar.height(), false, false);
    QCOMPARE(menuBar.cornerWidget(Qt::TopRightCorner), horizontal);
    QVERIFY(horizontal->isVisible());

    manager.updatePosition(1200, 800, menuBar.height(), true, false);
    QCOMPARE(horizontal->parentWidget(), &mainWindow);
    QVERIFY(horizontal->isVisible());
    QVERIFY(menuBar.cornerWidget(Qt::TopRightCorner) == nullptr);
}

void TestCornerWidgetPortrait::testKeyboardLayoutPopupReadableInDarkTheme()
{
    const QPalette originalPalette = QApplication::palette();
    const auto restorePalette = qScopeGuard([&originalPalette] {
        QApplication::setPalette(originalPalette);
    });

    QPalette darkPalette = originalPalette;
    darkPalette.setColor(QPalette::Window, QColor("#202020"));
    darkPalette.setColor(QPalette::Base, QColor("#202020"));
    darkPalette.setColor(QPalette::Text, QColor("#ffffff"));
    darkPalette.setColor(QPalette::WindowText, QColor("#ffffff"));
    darkPalette.setColor(QPalette::Highlight, QColor("#0078d4"));
    darkPalette.setColor(QPalette::HighlightedText, QColor("#ffffff"));
    QApplication::setPalette(darkPalette);

    QMainWindow mainWindow;
    QPalette mainWindowPalette = darkPalette;
    mainWindowPalette.setColor(QPalette::Window, QColor("#151a24"));
    mainWindowPalette.setColor(QPalette::WindowText, QColor("#f3f4f6"));
    mainWindowPalette.setColor(QPalette::Base, QColor("#ffffff"));
    mainWindowPalette.setColor(QPalette::Text, QColor("#000000"));
    mainWindow.setPalette(mainWindowPalette);
    mainWindow.resize(400, 800);
    QMenuBar menuBar(&mainWindow);
    mainWindow.setMenuBar(&menuBar);
    CornerWidgetManager manager(&mainWindow);
    manager.setMenuBar(&menuBar);
    manager.initializeKeyboardLayouts({"English (US)", "Deutsch (DE)"}, "English (US)");
    mainWindow.show();
    QVERIFY(QTest::qWaitForWindowExposed(&mainWindow));
    manager.updatePosition(400, 800, menuBar.height(), false, true);

    QComboBox *combo = mainWindow.findChild<QComboBox *>("keyboardLayoutComboBox");
    QWidget *vertical = verticalWidget(manager);
    QVERIFY(combo);
    QVERIFY(vertical);
    QVERIFY(!vertical->styleSheet().contains("QWidget {"));
    QVERIFY(!vertical->testAttribute(Qt::WA_TranslucentBackground));
    QCOMPARE(vertical->palette().color(QPalette::Window), mainWindowPalette.color(QPalette::Window));
    QCOMPARE(combo->palette().color(QPalette::Base), mainWindowPalette.color(QPalette::Window));
    QCOMPARE(combo->palette().color(QPalette::Text), mainWindowPalette.color(QPalette::WindowText));
    QCOMPARE(combo->width(), 50);
    QCOMPARE(combo->view()->minimumWidth(), 180);

    combo->showPopup();
    QAbstractItemView *view = combo->view();
    QTRY_VERIFY(view->isVisible());
    QVERIFY(view->width() >= 180);
    QCOMPARE(view->palette().color(QPalette::Base), mainWindowPalette.color(QPalette::Window));
    QCOMPARE(view->palette().color(QPalette::Text), mainWindowPalette.color(QPalette::WindowText));
    QVERIFY(contrastRatio(view->palette().color(QPalette::Base),
                          view->palette().color(QPalette::Text)) >= 4.5);
    QVERIFY(contrastRatio(view->palette().color(QPalette::Highlight),
                          view->palette().color(QPalette::HighlightedText)) >= 4.5);
    combo->hidePopup();
}

void TestCornerWidgetPortrait::testRepeatedPositionUpdatesAreLightweight()
{
    QMainWindow mainWindow;
    mainWindow.resize(400, 800);
    QMenuBar menuBar(&mainWindow);
    mainWindow.setMenuBar(&menuBar);
    CornerWidgetManager manager(&mainWindow);
    manager.setMenuBar(&menuBar);
    mainWindow.show();
    QVERIFY(QTest::qWaitForWindowExposed(&mainWindow));
    manager.updatePosition(400, 800, menuBar.height(), false, true);

    QElapsedTimer timer;
    timer.start();
    for (int i = 0; i < 500; ++i) {
        manager.updatePosition(400 + i % 2, 800, menuBar.height(), false, true);
    }
    QVERIFY2(timer.elapsed() < 500, "Repeated toolbar positioning must not rebuild layouts or icons");
}

void TestCornerWidgetPortrait::testFullscreenArgumentDoesNotEnablePortraitFullscreen()
{
    QMainWindow mainWindow;
    mainWindow.resize(400, 800);
    QMenuBar menuBar(&mainWindow);
    mainWindow.setMenuBar(&menuBar);
    CornerWidgetManager manager(&mainWindow);
    manager.setMenuBar(&menuBar);
    mainWindow.show();
    QVERIFY(QTest::qWaitForWindowExposed(&mainWindow));
    manager.updatePosition(400, 800, menuBar.height(), true, true);

    QWidget *vertical = verticalWidget(manager);
    QPushButton *fullscreen = mainWindow.findChild<QPushButton *>("fullScreenButton");
    QVERIFY(vertical);
    QTRY_VERIFY(vertical->isVisible());
    QVERIFY(!fullscreen->isEnabled());
}

QTEST_MAIN(TestCornerWidgetPortrait)
#include "tst_cornerwidget_portrait.moc"
