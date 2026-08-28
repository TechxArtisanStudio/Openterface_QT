/**
 * @brief 键盘发送流程集成测试
 *
 * 测试两条键盘路径：
 * 路径1 (SystemKeyBlocker OFF):
 *   QKeyEvent → VideoPane::keyPressEvent → InputHandler → HostManager
 *   → KeyboardManager → SerialPortManager::sendCommandAsync
 *
 * 路径2 (SystemKeyBlocker ON):
 *   SystemKeyBlocker::keyCaptured → VideoPane::handleCapturedKey → InputHandler
 *   → HostManager → KeyboardManager → SerialPortManager::sendCommandAsync
 *
 * 验证：
 * - 两条路径都能正确产生 HID 报告数据
 * - 两条路径产生的数据一致
 * - Tab/Backtab 键有正确的 press/release 对
 * - Alt+Tab 和 Win 键在 SystemKeyBlocker ON 时被拦截，在 OFF 时正常发送
 */

#include <QTest>
#include <QSignalSpy>
#include <QKeyEvent>
#include <QApplication>
#include <QDebug>
#include <QAtomicInteger>

// 定义 main.cpp 中的全局变量（因为测试排除了 main.o）
QAtomicInteger<int> g_applicationShuttingDown(0);

#include "ui/videopane.h"
#include "ui/inputhandler.h"
#include "host/HostManager.h"
#include "target/KeyboardManager.h"
#include "target/KeyboardLayouts.h"
#include "serial/SerialPortManager.h"
#include "SysKeyBlocker/SystemKeyBlocker.h"

class TestKeyboardSendFlow : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    // === 路径1 测试：SystemKeyBlocker OFF，通过 keyPressEvent/keyReleaseEvent ===
    void path1_off_normalKeyProducesHidReport();
    void path1_off_tabKeyHasPressReleasePair();
    void path1_off_altTabIsSent();
    void path1_off_winKeyIsSent();
    void path1_off_allCommonKeysSent();

    // === 路径2 测试：SystemKeyBlocker ON，通过 handleCapturedKey ===
    void path2_on_normalKeyProducesHidReport();
    void path2_on_tabKeyHasPressReleasePair();
    void path2_on_altTabIsForwarded();
    void path2_on_winKeyIsForwarded();
    void path2_on_allCommonKeysSent();

    // === 两条路径对比测试 ===
    void bothPaths_produceSameHidReport();

private:
    VideoPane *m_videoPane = nullptr;

    // 模拟按键按下（路径1：通过 QKeyEvent）
    void sendKeyPress(int key, int modifiers = Qt::NoModifier);
    void sendKeyRelease(int key, int modifiers = Qt::NoModifier);

    // 模拟捕获的按键（路径2：通过 handleCapturedKey）
    void sendCapturedKeyPress(int key, int modifiers = Qt::NoModifier);
    void sendCapturedKeyRelease(int key, int modifiers = Qt::NoModifier);

    // 验证 HID 报告格式
    bool isValidHidReport(const QByteArray &data);

    // 获取 HID 报告中的修饰字节
    uint8_t getModifierByte(const QByteArray &data);

    // 获取 HID 报告中的键码
    QList<uint8_t> getKeyCodes(const QByteArray &data);
};

void TestKeyboardSendFlow::initTestCase()
{
    // 加载键盘布局（主程序在 main.cpp 中加载，测试需要手动加载）
    KeyboardLayoutManager::getInstance().loadLayouts(":/config/keyboards");

    // 确保 SystemKeyBlocker 默认不激活
    SystemKeyBlocker::instance().stop();
    QVERIFY(!SystemKeyBlocker::instance().isActive());

    // 创建 VideoPane（会内部创建 InputHandler）
    m_videoPane = new VideoPane();
    QVERIFY(m_videoPane != nullptr);

    // VideoPane 需要显示才能接收键盘事件
    m_videoPane->resize(640, 480);
    m_videoPane->show();
    QVERIFY(QTest::qWaitForWindowExposed(m_videoPane));
}

void TestKeyboardSendFlow::cleanupTestCase()
{
    SystemKeyBlocker::instance().stop();
    delete m_videoPane;
}

void TestKeyboardSendFlow::init()
{
    // 每个测试前清空状态
    SystemKeyBlocker::instance().stop();
    // 释放所有按键状态
    KeyboardManager::getInstance().releaseAllKeys();
}

void TestKeyboardSendFlow::cleanup()
{
    SystemKeyBlocker::instance().stop();
}

// === Helper 方法 ===

void TestKeyboardSendFlow::sendKeyPress(int key, int modifiers)
{
    QKeyEvent event(QEvent::KeyPress, key, static_cast<Qt::KeyboardModifiers>(modifiers));
    QApplication::sendEvent(m_videoPane, &event);
}

void TestKeyboardSendFlow::sendKeyRelease(int key, int modifiers)
{
    QKeyEvent event(QEvent::KeyRelease, key, static_cast<Qt::KeyboardModifiers>(modifiers));
    QApplication::sendEvent(m_videoPane, &event);
}

void TestKeyboardSendFlow::sendCapturedKeyPress(int key, int modifiers)
{
    m_videoPane->handleCapturedKey(key, modifiers, true, 0);
}

void TestKeyboardSendFlow::sendCapturedKeyRelease(int key, int modifiers)
{
    m_videoPane->handleCapturedKey(key, modifiers, false, 0);
}

bool TestKeyboardSendFlow::isValidHidReport(const QByteArray &data)
{
    // HID 报告格式：57 AB 00 02 08 XX 00 K1 K2 K3 K4 K5 K6
    // 长度必须是 13 字节
    if (data.size() != 13) return false;
    // 头部必须是 57 AB
    if ((uint8_t)data[0] != 0x57 || (uint8_t)data[1] != 0xAB) return false;
    return true;
}

uint8_t TestKeyboardSendFlow::getModifierByte(const QByteArray &data)
{
    if (data.size() < 6) return 0;
    return static_cast<uint8_t>(data[5]);
}

QList<uint8_t> TestKeyboardSendFlow::getKeyCodes(const QByteArray &data)
{
    QList<uint8_t> keyCodes;
    if (data.size() < 13) return keyCodes;
    for (int i = 7; i < 13; i++) {
        uint8_t code = static_cast<uint8_t>(data[i]);
        if (code != 0) {
            keyCodes.append(code);
        }
    }
    return keyCodes;
}

// ============================================================================
// 路径1 测试：SystemKeyBlocker OFF，通过 keyPressEvent/keyReleaseEvent
// ============================================================================

void TestKeyboardSendFlow::path1_off_normalKeyProducesHidReport()
{
    QVERIFY(!SystemKeyBlocker::instance().isActive());

    QSignalSpy spy(&SerialPortManager::getInstance(), &SerialPortManager::sendCommandAsync);

    // 按下 A 键
    sendKeyPress(Qt::Key_A);
    // 等待事件处理
    QCoreApplication::processEvents();

    // 验证发送了数据
    QVERIFY2(spy.count() > 0, "路径1: 按键按下应该产生 HID 报告");

    // 验证数据格式
    auto args = spy.takeLast();
    QByteArray data = args.at(0).toByteArray();
    QVERIFY2(isValidHidReport(data), "路径1: 数据格式应该是有效的 HID 报告");

    // 验证键码不为空
    QList<uint8_t> keyCodes = getKeyCodes(data);
    QVERIFY2(!keyCodes.isEmpty(), "路径1: HID 报告应该包含键码");

    qDebug() << "路径1 普通按键 HID 报告:" << data.toHex(' ');
}

void TestKeyboardSendFlow::path1_off_tabKeyHasPressReleasePair()
{
    QVERIFY(!SystemKeyBlocker::instance().isActive());

    QSignalSpy spy(&SerialPortManager::getInstance(), &SerialPortManager::sendCommandAsync);

    // Tab 键按下
    sendKeyPress(Qt::Key_Tab);
    QCoreApplication::processEvents();
    int pressCount = spy.count();
    QVERIFY2(pressCount > 0, "路径1: Tab 键按下应该产生 HID 报告");

    // Tab 键释放
    sendKeyRelease(Qt::Key_Tab);
    QCoreApplication::processEvents();
    int totalCount = spy.count();
    QVERIFY2(totalCount > pressCount, "路径1: Tab 键释放应该产生 HID 报告");

    qDebug() << "路径1 Tab 键: press=" << pressCount << " release=" << (totalCount - pressCount);
}

void TestKeyboardSendFlow::path1_off_altTabIsSent()
{
    QVERIFY(!SystemKeyBlocker::instance().isActive());

    QSignalSpy spy(&SerialPortManager::getInstance(), &SerialPortManager::sendCommandAsync);

    // Alt+Tab 序列
    sendKeyPress(Qt::Key_Alt);
    QCoreApplication::processEvents();
    sendKeyPress(Qt::Key_Tab, Qt::AltModifier);
    QCoreApplication::processEvents();
    sendKeyRelease(Qt::Key_Tab, Qt::AltModifier);
    QCoreApplication::processEvents();
    sendKeyRelease(Qt::Key_Alt);
    QCoreApplication::processEvents();

    // 验证产生了数据
    QVERIFY2(spy.count() >= 4, "路径1: Alt+Tab 应该产生至少 4 个 HID 报告（Alt按下, Tab按下, Tab释放, Alt释放）");

    qDebug() << "路径1 Alt+Tab: 产生" << spy.count() << "个 HID 报告";
}

void TestKeyboardSendFlow::path1_off_winKeyIsSent()
{
    QVERIFY(!SystemKeyBlocker::instance().isActive());

    QSignalSpy spy(&SerialPortManager::getInstance(), &SerialPortManager::sendCommandAsync);

    // Win 键按下释放
    sendKeyPress(Qt::Key_Meta);
    QCoreApplication::processEvents();
    sendKeyRelease(Qt::Key_Meta);
    QCoreApplication::processEvents();

    QVERIFY2(spy.count() >= 2, "路径1: Win 键应该产生 HID 报告");
    qDebug() << "路径1 Win 键: 产生" << spy.count() << "个 HID 报告";
}

void TestKeyboardSendFlow::path1_off_allCommonKeysSent()
{
    QVERIFY(!SystemKeyBlocker::instance().isActive());

    QList<int> keys = {
        Qt::Key_A, Qt::Key_Z, Qt::Key_0, Qt::Key_9,
        Qt::Key_F1, Qt::Key_F12, Qt::Key_Tab, Qt::Key_Enter,
        Qt::Key_Escape, Qt::Key_Space, Qt::Key_Backspace,
        Qt::Key_Left, Qt::Key_Right, Qt::Key_Up, Qt::Key_Down
    };

    for (int key : keys) {
        QSignalSpy spy(&SerialPortManager::getInstance(), &SerialPortManager::sendCommandAsync);

        sendKeyPress(key);
        QCoreApplication::processEvents();
        sendKeyRelease(key);
        QCoreApplication::processEvents();

        QVERIFY2(spy.count() >= 2,
                 qPrintable(QString("路径1: 键 %1 应该产生 HID 报告").arg(key)));
    }
    qDebug() << "路径1: 所有" << keys.size() << "个常用按键都成功发送";
}

// ============================================================================
// 路径2 测试：SystemKeyBlocker ON，通过 handleCapturedKey
// ============================================================================

void TestKeyboardSendFlow::path2_on_normalKeyProducesHidReport()
{
    // 启动 SystemKeyBlocker
    bool started = SystemKeyBlocker::instance().start(0);
    if (!started) {
        QSKIP("SystemKeyBlocker 在当前平台不可用（可能是 Wayland）");
    }
    QVERIFY(SystemKeyBlocker::instance().isActive());

    QSignalSpy spy(&SerialPortManager::getInstance(), &SerialPortManager::sendCommandAsync);

    // 通过 handleCapturedKey 发送按键
    sendCapturedKeyPress(Qt::Key_A);
    QCoreApplication::processEvents();

    QVERIFY2(spy.count() > 0, "路径2: 按键按下应该产生 HID 报告");

    auto args = spy.takeLast();
    QByteArray data = args.at(0).toByteArray();
    QVERIFY2(isValidHidReport(data), "路径2: 数据格式应该是有效的 HID 报告");

    QList<uint8_t> keyCodes = getKeyCodes(data);
    QVERIFY2(!keyCodes.isEmpty(), "路径2: HID 报告应该包含键码");

    qDebug() << "路径2 普通按键 HID 报告:" << data.toHex(' ');
}

void TestKeyboardSendFlow::path2_on_tabKeyHasPressReleasePair()
{
    bool started = SystemKeyBlocker::instance().start(0);
    if (!started) {
        QSKIP("SystemKeyBlocker 在当前平台不可用");
    }

    QSignalSpy spy(&SerialPortManager::getInstance(), &SerialPortManager::sendCommandAsync);

    sendCapturedKeyPress(Qt::Key_Tab);
    QCoreApplication::processEvents();
    int pressCount = spy.count();
    QVERIFY2(pressCount > 0, "路径2: Tab 键按下应该产生 HID 报告");

    sendCapturedKeyRelease(Qt::Key_Tab);
    QCoreApplication::processEvents();
    int totalCount = spy.count();
    QVERIFY2(totalCount > pressCount, "路径2: Tab 键释放应该产生 HID 报告");

    qDebug() << "路径2 Tab 键: press=" << pressCount << " release=" << (totalCount - pressCount);
}

void TestKeyboardSendFlow::path2_on_altTabIsForwarded()
{
    bool started = SystemKeyBlocker::instance().start(0);
    if (!started) {
        QSKIP("SystemKeyBlocker 在当前平台不可用");
    }

    QSignalSpy spy(&SerialPortManager::getInstance(), &SerialPortManager::sendCommandAsync);

    // Alt+Tab 序列通过 handleCapturedKey
    sendCapturedKeyPress(Qt::Key_Alt);
    QCoreApplication::processEvents();
    sendCapturedKeyPress(Qt::Key_Tab, Qt::AltModifier);
    QCoreApplication::processEvents();
    sendCapturedKeyRelease(Qt::Key_Tab, Qt::AltModifier);
    QCoreApplication::processEvents();
    sendCapturedKeyRelease(Qt::Key_Alt);
    QCoreApplication::processEvents();

    QVERIFY2(spy.count() >= 4, "路径2: Alt+Tab 应该产生至少 4 个 HID 报告");
    qDebug() << "路径2 Alt+Tab: 产生" << spy.count() << "个 HID 报告";
}

void TestKeyboardSendFlow::path2_on_winKeyIsForwarded()
{
    bool started = SystemKeyBlocker::instance().start(0);
    if (!started) {
        QSKIP("SystemKeyBlocker 在当前平台不可用");
    }

    QSignalSpy spy(&SerialPortManager::getInstance(), &SerialPortManager::sendCommandAsync);

    sendCapturedKeyPress(Qt::Key_Meta);
    QCoreApplication::processEvents();
    sendCapturedKeyRelease(Qt::Key_Meta);
    QCoreApplication::processEvents();

    QVERIFY2(spy.count() >= 2, "路径2: Win 键应该产生 HID 报告");
    qDebug() << "路径2 Win 键: 产生" << spy.count() << "个 HID 报告";
}

void TestKeyboardSendFlow::path2_on_allCommonKeysSent()
{
    bool started = SystemKeyBlocker::instance().start(0);
    if (!started) {
        QSKIP("SystemKeyBlocker 在当前平台不可用");
    }

    QList<int> keys = {
        Qt::Key_A, Qt::Key_Z, Qt::Key_0, Qt::Key_9,
        Qt::Key_F1, Qt::Key_F12, Qt::Key_Tab, Qt::Key_Enter,
        Qt::Key_Escape, Qt::Key_Space, Qt::Key_Backspace,
        Qt::Key_Left, Qt::Key_Right, Qt::Key_Up, Qt::Key_Down
    };

    for (int key : keys) {
        QSignalSpy spy(&SerialPortManager::getInstance(), &SerialPortManager::sendCommandAsync);

        sendCapturedKeyPress(key);
        QCoreApplication::processEvents();
        sendCapturedKeyRelease(key);
        QCoreApplication::processEvents();

        QVERIFY2(spy.count() >= 2,
                 qPrintable(QString("路径2: 键 %1 应该产生 HID 报告").arg(key)));
    }
    qDebug() << "路径2: 所有" << keys.size() << "个常用按键都成功发送";
}

// ============================================================================
// 两条路径对比测试
// ============================================================================

void TestKeyboardSendFlow::bothPaths_produceSameHidReport()
{
    QSignalSpy spy(&SerialPortManager::getInstance(), &SerialPortManager::sendCommandAsync);

    // === 路径1: SystemKeyBlocker OFF ===
    QVERIFY(!SystemKeyBlocker::instance().isActive());

    sendKeyPress(Qt::Key_A);
    QCoreApplication::processEvents();
    QVERIFY(spy.count() > 0);
    QByteArray path1Data = spy.last().at(0).toByteArray();
    spy.clear();

    // 释放按键
    sendKeyRelease(Qt::Key_A);
    QCoreApplication::processEvents();
    spy.clear();

    // === 路径2: SystemKeyBlocker ON ===
    bool started = SystemKeyBlocker::instance().start(0);
    if (!started) {
        QSKIP("SystemKeyBlocker 在当前平台不可用，无法进行对比测试");
    }
    QVERIFY(SystemKeyBlocker::instance().isActive());

    // 重置键盘状态
    KeyboardManager::getInstance().releaseAllKeys();

    sendCapturedKeyPress(Qt::Key_A);
    QCoreApplication::processEvents();
    QVERIFY(spy.count() > 0);
    QByteArray path2Data = spy.last().at(0).toByteArray();
    spy.clear();

    sendCapturedKeyRelease(Qt::Key_A);
    QCoreApplication::processEvents();
    spy.clear();

    // === 对比两条路径的数据 ===
    qDebug() << "路径1 HID 报告:" << path1Data.toHex(' ');
    qDebug() << "路径2 HID 报告:" << path2Data.toHex(' ');

    // 两条路径应该产生相同格式的 HID 报告
    QCOMPARE(path1Data.size(), path2Data.size());
    QCOMPARE(getModifierByte(path1Data), getModifierByte(path2Data));
    QCOMPARE(getKeyCodes(path1Data), getKeyCodes(path2Data));

    qDebug() << "两条路径产生相同的 HID 报告 ✓";
}

QTEST_MAIN(TestKeyboardSendFlow)
#include "test_keyboard_send_flow.moc"
