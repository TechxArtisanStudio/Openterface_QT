/*
 * ========================================================================== *
 *    TestMcpToolHandler — QtTest suite for McpToolHandler                    *
 *                                                                            *
 *    Tests tool listing, call dispatch, and error handling for each tool     *
 *    defined in server/mcp/mcpToolHandler.{h,cpp}.                           *
 *                                                                            *
 *    Hardware-dependent calls are exercised without real hardware attached;  *
 *    tests verify structure / error-path behavior, not physical outcomes.    *
 *                                                                            *
 *    Run:  ./test_mcp_tool_handler                                           *
 *    Or:   ctest -R McpToolHandler --output-on-failure                       *
 * ========================================================================== *
 */

#include <QtTest>
#include <QJsonObject>
#include <QJsonArray>
#include <QString>
#include <QSet>

#include "server/mcp/mcpToolHandler.h"
#include "server/mcp/mcpConstants.h"

class TestMcpToolHandler : public QObject
{
    Q_OBJECT

private:
    McpToolHandler* m_handler = nullptr;

private slots:
    void initTestCase();
    void cleanupTestCase();

    // --- listTools ---
    void listTools_returnsAllTools();
    void listTools_toolCountIs18();
    void listTools_eachToolHasName();
    void listTools_eachToolHasDescription();
    void listTools_eachToolHasInputSchema();
    void listTools_expectedToolNames();
    void listTools_noDuplicateNames();

    // --- callTool dispatch ---
    void callTool_unknownTool_returnsError();
    void callTool_emptyName_returnsError();
    void callTool_unknownTool_errorStructure();

    // --- Mouse tools ---
    void mouseMoveAbsolute_validArgs();
    void mouseMoveAbsolute_missingArgs_defaultsToZero();
    void mouseClick_validArgs();
    void mouseClick_defaultButtonIsLeft();
    void mouseClick_rightButton();
    void mouseClick_middleButton();
    void mouseClick_multipleClicks();
    void mouseMoveRelative_validArgs();
    void mouseScroll_up();
    void mouseScroll_down();
    void mouseScroll_invalidDirection_defaultsToDown();

    // --- Keyboard tools ---
    void keyboardPressKey_integerCode();
    void keyboardPressKey_keyName();
    void keyboardPressKey_unknownKeyName_returnsError();
    void keyboardPressKey_emptyKey_returnsError();
    void keyboardPressKey_modifierCombo();
    void keyboardPressKey_autoRelease();
    void keyboardTypeText_validText();
    void keyboardTypeText_emptyText();
    void keyboardTypeText_longText();
    void keyboardFunctionKey_validKeys_data();
    void keyboardFunctionKey_invalidKey_returnsError();
    void keyboardFunctionKey_lowercaseAccepted();
    void keyboardCtrlAltDel_returnsSuccess();
    void keyboardSetLayout_validLayout();
    void keyboardSetLayout_emptyLayout_returnsError();
    void keyboardSendKeys_noScriptRunner_returnsError();

    // --- Screen capture tools ---
    void captureScreen_noCamera_returnsError();
    void captureLastImage_noImages_returnsError();
    void screenToMarkdown_noCamera_returnsError();

    // --- Script tools ---
    void executeScript_noScriptRunner_returnsError();
    void executeScript_emptyScript_returnsError();
    void validateScript_emptyScript();

    // --- System tools ---
    void systemStatus_returnsStructure();

    // --- Helpers ---
    void textResult_hasContentArray();
    void errorResult_hasIsErrorTrue();
};

// ============================================================================
// Fixture
// ============================================================================

void TestMcpToolHandler::initTestCase()
{
    m_handler = new McpToolHandler(this);
    // Deliberately do NOT inject CameraManager or ScriptRunner —
    // this exercises the "not available" error paths.
}

void TestMcpToolHandler::cleanupTestCase()
{
    // m_handler is parented to `this`, auto-deleted
}

// ============================================================================
// listTools tests
// ============================================================================

void TestMcpToolHandler::listTools_returnsAllTools()
{
    QJsonArray tools = m_handler->listTools();
    QVERIFY2(!tools.isEmpty(), "listTools() must return at least one tool");
}

void TestMcpToolHandler::listTools_toolCountIs18()
{
    QJsonArray tools = m_handler->listTools();
    QCOMPARE(tools.size(), 18);
}

void TestMcpToolHandler::listTools_eachToolHasName()
{
    QJsonArray tools = m_handler->listTools();
    for (const QJsonValue& v : tools) {
        QJsonObject tool = v.toObject();
        QVERIFY2(tool.contains("name"),
                 qPrintable(QString("Tool missing 'name': ") + QJsonDocument(tool).toJson()));
        QVERIFY2(!tool["name"].toString().isEmpty(), "Tool name must not be empty");
    }
}

void TestMcpToolHandler::listTools_eachToolHasDescription()
{
    QJsonArray tools = m_handler->listTools();
    for (const QJsonValue& v : tools) {
        QJsonObject tool = v.toObject();
        QVERIFY2(tool.contains("description"),
                 qPrintable("Tool missing 'description': " + tool["name"].toString()));
        QVERIFY2(!tool["description"].toString().isEmpty(),
                 qPrintable("Tool description must not be empty: " + tool["name"].toString()));
    }
}

void TestMcpToolHandler::listTools_eachToolHasInputSchema()
{
    QJsonArray tools = m_handler->listTools();
    for (const QJsonValue& v : tools) {
        QJsonObject tool = v.toObject();
        QVERIFY2(tool.contains("inputSchema"),
                 qPrintable("Tool missing 'inputSchema': " + tool["name"].toString()));
        QJsonObject schema = tool["inputSchema"].toObject();
        QCOMPARE(schema["type"].toString(), QStringLiteral("object"));
    }
}

void TestMcpToolHandler::listTools_expectedToolNames()
{
    QJsonArray tools = m_handler->listTools();
    QSet<QString> names;
    for (const QJsonValue& v : tools)
        names.insert(v.toObject()["name"].toString());

    // All documented tools must be present
    const QStringList expected = {
        MCP_TOOL_MOUSE_MOVE_ABSOLUTE,
        MCP_TOOL_MOUSE_CLICK,
        MCP_TOOL_MOUSE_MOVE_RELATIVE,
        MCP_TOOL_MOUSE_SCROLL,
        MCP_TOOL_KEYBOARD_PRESS_KEY,
        MCP_TOOL_KEYBOARD_TYPE_TEXT,
        MCP_TOOL_KEYBOARD_SEND_KEYS,
        MCP_TOOL_KEYBOARD_FUNCTION_KEY,
        MCP_TOOL_KEYBOARD_CTRL_ALT_DEL,
        MCP_TOOL_KEYBOARD_SET_LAYOUT,
        MCP_TOOL_CAPTURE_SCREEN,
        MCP_TOOL_CAPTURE_LAST_IMAGE,
        MCP_TOOL_EXECUTE_SCRIPT,
        MCP_TOOL_VALIDATE_SCRIPT,
        MCP_TOOL_USB_SWITCH,
        MCP_TOOL_SCREEN_TO_MARKDOWN,
        MCP_TOOL_FIRMWARE_CHECK,
    };
    // Note: MCP_TOOL_FIRMWARE_UPDATE is also defined but listTools may not expose it;
    // we test the 18 that appear.

    for (const QString& name : expected) {
        QVERIFY2(names.contains(name),
                 qPrintable("Expected tool not found in listTools(): " + name));
    }
}

void TestMcpToolHandler::listTools_noDuplicateNames()
{
    QJsonArray tools = m_handler->listTools();
    QSet<QString> names;
    for (const QJsonValue& v : tools) {
        QString name = v.toObject()["name"].toString();
        QVERIFY2(!names.contains(name),
                 qPrintable("Duplicate tool name in listTools(): " + name));
        names.insert(name);
    }
}

// ============================================================================
// callTool dispatch tests
// ============================================================================

void TestMcpToolHandler::callTool_unknownTool_returnsError()
{
    QJsonObject result = m_handler->callTool("nonexistent_tool", {});

    QVERIFY(result.contains("content"));
    QVERIFY(result.contains("isError"));
    QCOMPARE(result["isError"].toBool(), true);

    QJsonArray content = result["content"].toArray();
    QVERIFY(!content.isEmpty());
    QString text = content[0].toObject()["text"].toString();
    QVERIFY2(text.contains("Unknown tool"),
             qPrintable("Expected 'Unknown tool' error, got: " + text));
}

void TestMcpToolHandler::callTool_emptyName_returnsError()
{
    QJsonObject result = m_handler->callTool("", {});
    QCOMPARE(result["isError"].toBool(), true);
}

void TestMcpToolHandler::callTool_unknownTool_errorStructure()
{
    QJsonObject result = m_handler->callTool("bogus", {});

    // Must follow McpProtocol::toolError structure
    QVERIFY(result.contains("content"));
    QVERIFY(result["content"].isArray());
    QJsonArray arr = result["content"].toArray();
    QCOMPARE(arr.size(), 1);
    QCOMPARE(arr[0].toObject()["type"].toString(), QStringLiteral("text"));
    QVERIFY(!arr[0].toObject()["text"].toString().isEmpty());
    QCOMPARE(result["isError"].toBool(), true);
}

// ============================================================================
// Mouse tool tests
// ============================================================================

void TestMcpToolHandler::mouseMoveAbsolute_validArgs()
{
    QJsonObject args{{"x", 2048}, {"y", 1024}};
    QJsonObject result = m_handler->callTool(MCP_TOOL_MOUSE_MOVE_ABSOLUTE, args);

    // Without real hardware this still returns a text result (the call is dispatched
    // through HostManager which may be a no-op without hardware)
    QVERIFY(result.contains("content"));
    QString text = result["content"].toArray()[0].toObject()["text"].toString();
    QVERIFY(text.contains("2048"));
    QVERIFY(text.contains("1024"));
}

void TestMcpToolHandler::mouseMoveAbsolute_missingArgs_defaultsToZero()
{
    // When x/y are missing, toInt() returns 0 — tool should still succeed
    QJsonObject result = m_handler->callTool(MCP_TOOL_MOUSE_MOVE_ABSOLUTE, {});
    QVERIFY(result.contains("content"));
    QVERIFY(!result.contains("isError"));
}

void TestMcpToolHandler::mouseClick_validArgs()
{
    QJsonObject args{{"x", 100}, {"y", 200}};
    QJsonObject result = m_handler->callTool(MCP_TOOL_MOUSE_CLICK, args);

    QVERIFY(result.contains("content"));
    QString text = result["content"].toArray()[0].toObject()["text"].toString();
    QVERIFY(text.contains("100"));
    QVERIFY(text.contains("200"));
}

void TestMcpToolHandler::mouseClick_defaultButtonIsLeft()
{
    // No button specified → defaults to left
    QJsonObject args{{"x", 0}, {"y", 0}};
    QJsonObject result = m_handler->callTool(MCP_TOOL_MOUSE_CLICK, args);

    QVERIFY(result.contains("content"));
    QVERIFY(!result.contains("isError"));
}

void TestMcpToolHandler::mouseClick_rightButton()
{
    QJsonObject args{{"x", 0}, {"y", 0}, {"button", "right"}};
    QJsonObject result = m_handler->callTool(MCP_TOOL_MOUSE_CLICK, args);

    QVERIFY(!result.contains("isError"));
    QString text = result["content"].toArray()[0].toObject()["text"].toString();
    QVERIFY(text.contains("Mouse"));
}

void TestMcpToolHandler::mouseClick_middleButton()
{
    QJsonObject args{{"x", 0}, {"y", 0}, {"button", "middle"}};
    QJsonObject result = m_handler->callTool(MCP_TOOL_MOUSE_CLICK, args);

    QVERIFY(!result.contains("isError"));
}

void TestMcpToolHandler::mouseClick_multipleClicks()
{
    QJsonObject args{{"x", 0}, {"y", 0}, {"count", 3}};
    QJsonObject result = m_handler->callTool(MCP_TOOL_MOUSE_CLICK, args);

    QVERIFY(!result.contains("isError"));
    QString text = result["content"].toArray()[0].toObject()["text"].toString();
    QVERIFY(text.contains("3"));
}

void TestMcpToolHandler::mouseMoveRelative_validArgs()
{
    QJsonObject args{{"dx", 50}, {"dy", -30}};
    QJsonObject result = m_handler->callTool(MCP_TOOL_MOUSE_MOVE_RELATIVE, args);

    QVERIFY(result.contains("content"));
    QVERIFY(!result.contains("isError"));
}

void TestMcpToolHandler::mouseScroll_up()
{
    QJsonObject args{{"direction", "up"}, {"lines", 3}};
    QJsonObject result = m_handler->callTool(MCP_TOOL_MOUSE_SCROLL, args);

    QVERIFY(result.contains("content"));
    QString text = result["content"].toArray()[0].toObject()["text"].toString();
    QVERIFY(text.contains("up"));
}

void TestMcpToolHandler::mouseScroll_down()
{
    QJsonObject args{{"direction", "down"}};
    QJsonObject result = m_handler->callTool(MCP_TOOL_MOUSE_SCROLL, args);

    QVERIFY(!result.contains("isError"));
}

void TestMcpToolHandler::mouseScroll_invalidDirection_defaultsToDown()
{
    // The code does: `int dir = (direction == "up") ? 1 : -1;`
    // So any non-"up" string is treated as down (-1).
    QJsonObject args{{"direction", "sideways"}};
    QJsonObject result = m_handler->callTool(MCP_TOOL_MOUSE_SCROLL, args);

    // Should not error — "sideways" just becomes dir=-1 (down)
    QVERIFY(!result.contains("isError"));
}

// ============================================================================
// Keyboard tool tests
// ============================================================================

void TestMcpToolHandler::keyboardPressKey_integerCode()
{
    // Qt::Key_A = 65
    QJsonObject args{{"key", 65}};
    QJsonObject result = m_handler->callTool(MCP_TOOL_KEYBOARD_PRESS_KEY, args);

    QVERIFY(result.contains("content"));
    QVERIFY(!result.contains("isError"));
}

void TestMcpToolHandler::keyboardPressKey_keyName()
{
    QJsonObject args{{"key", "Enter"}};
    QJsonObject result = m_handler->callTool(MCP_TOOL_KEYBOARD_PRESS_KEY, args);

    QVERIFY(result.contains("content"));
    QVERIFY(!result.contains("isError"));
}

void TestMcpToolHandler::keyboardPressKey_unknownKeyName_returnsError()
{
    QJsonObject args{{"key", "NonExistentKey"}};
    QJsonObject result = m_handler->callTool(MCP_TOOL_KEYBOARD_PRESS_KEY, args);

    QCOMPARE(result["isError"].toBool(), true);
    QString text = result["content"].toArray()[0].toObject()["text"].toString();
    QVERIFY(text.contains("Unknown key"));
}

void TestMcpToolHandler::keyboardPressKey_emptyKey_returnsError()
{
    // Empty string key name → resolveKey returns 0 → error
    QJsonObject args{{"key", ""}};
    QJsonObject result = m_handler->callTool(MCP_TOOL_KEYBOARD_PRESS_KEY, args);

    QCOMPARE(result["isError"].toBool(), true);
}

void TestMcpToolHandler::keyboardPressKey_modifierCombo()
{
    // Ctrl+C: key=67 (C), modifiers=2 (Ctrl)
    QJsonObject args{{"key", 67}, {"modifiers", 2}};
    QJsonObject result = m_handler->callTool(MCP_TOOL_KEYBOARD_PRESS_KEY, args);

    QVERIFY(!result.contains("isError"));
}

void TestMcpToolHandler::keyboardPressKey_autoRelease()
{
    QJsonObject args{{"key", "A"}, {"autoRelease", false}};
    QJsonObject result = m_handler->callTool(MCP_TOOL_KEYBOARD_PRESS_KEY, args);

    QVERIFY(result.contains("content"));
    // When autoRelease is false, response says "pressed" not "pressed+released"
    QString text = result["content"].toArray()[0].toObject()["text"].toString();
    QVERIFY(text.contains("pressed"));
}

void TestMcpToolHandler::keyboardTypeText_validText()
{
    QJsonObject args{{"text", "Hello, World!"}};
    QJsonObject result = m_handler->callTool(MCP_TOOL_KEYBOARD_TYPE_TEXT, args);

    QVERIFY(result.contains("content"));
    QVERIFY(!result.contains("isError"));
}

void TestMcpToolHandler::keyboardTypeText_emptyText()
{
    // Empty text — behavior depends on implementation; should not crash
    QJsonObject args{{"text", ""}};
    QJsonObject result = m_handler->callTool(MCP_TOOL_KEYBOARD_TYPE_TEXT, args);

    QVERIFY(result.contains("content"));
}

void TestMcpToolHandler::keyboardTypeText_longText()
{
    QString longText(1000, QChar('A'));
    QJsonObject args{{"text", longText}};
    QJsonObject result = m_handler->callTool(MCP_TOOL_KEYBOARD_TYPE_TEXT, args);

    QVERIFY(result.contains("content"));
}

void TestMcpToolHandler::keyboardFunctionKey_validKeys_data()
{
    // Test all valid function keys F1-F12
    for (int i = 1; i <= 12; ++i) {
        QString key = QString("F%1").arg(i);
        QJsonObject args{{"key", key}};
        QJsonObject result = m_handler->callTool(MCP_TOOL_KEYBOARD_FUNCTION_KEY, args);

        QVERIFY2(!result.contains("isError"),
                 qPrintable(QString("F%1 should be valid").arg(i)));
    }
}

void TestMcpToolHandler::keyboardFunctionKey_invalidKey_returnsError()
{
    QJsonObject args{{"key", "F13"}};
    QJsonObject result = m_handler->callTool(MCP_TOOL_KEYBOARD_FUNCTION_KEY, args);

    QCOMPARE(result["isError"].toBool(), true);
    QString text = result["content"].toArray()[0].toObject()["text"].toString();
    QVERIFY(text.contains("Invalid function key"));
}

void TestMcpToolHandler::keyboardFunctionKey_lowercaseAccepted()
{
    // "f5" should be uppercased internally and accepted
    QJsonObject args{{"key", "f5"}};
    QJsonObject result = m_handler->callTool(MCP_TOOL_KEYBOARD_FUNCTION_KEY, args);

    QVERIFY(!result.contains("isError"));
}

void TestMcpToolHandler::keyboardCtrlAltDel_returnsSuccess()
{
    QJsonObject result = m_handler->callTool(MCP_TOOL_KEYBOARD_CTRL_ALT_DEL, {});

    QVERIFY(result.contains("content"));
    QVERIFY(!result.contains("isError"));
    QString text = result["content"].toArray()[0].toObject()["text"].toString();
    QVERIFY(text.contains("Ctrl+Alt+Del"));
}

void TestMcpToolHandler::keyboardSetLayout_validLayout()
{
    QJsonObject args{{"layout", "en-us"}};
    QJsonObject result = m_handler->callTool(MCP_TOOL_KEYBOARD_SET_LAYOUT, args);

    QVERIFY(result.contains("content"));
    QVERIFY(!result.contains("isError"));
}

void TestMcpToolHandler::keyboardSetLayout_emptyLayout_returnsError()
{
    QJsonObject args{{"layout", ""}};
    QJsonObject result = m_handler->callTool(MCP_TOOL_KEYBOARD_SET_LAYOUT, args);

    QCOMPARE(result["isError"].toBool(), true);
    QString text = result["content"].toArray()[0].toObject()["text"].toString();
    QVERIFY(text.contains("empty"));
}

void TestMcpToolHandler::keyboardSendKeys_noScriptRunner_returnsError()
{
    // Without ScriptRunner injected, should return error
    QJsonObject args{{"keys", "^c"}};
    QJsonObject result = m_handler->callTool(MCP_TOOL_KEYBOARD_SEND_KEYS, args);

    QCOMPARE(result["isError"].toBool(), true);
    QString text = result["content"].toArray()[0].toObject()["text"].toString();
    QVERIFY(text.contains("ScriptRunner"));
}

// ============================================================================
// Screen capture tool tests
// ============================================================================

void TestMcpToolHandler::captureScreen_noCamera_returnsError()
{
    // Without CameraManager injected, should return error
    QJsonObject result = m_handler->callTool(MCP_TOOL_CAPTURE_SCREEN, {});

    QCOMPARE(result["isError"].toBool(), true);
    QString text = result["content"].toArray()[0].toObject()["text"].toString();
    QVERIFY(text.contains("CameraManager") || text.contains("not initialized"));
}

void TestMcpToolHandler::captureLastImage_noImages_returnsError()
{
    // Without any saved images, should return error
    QJsonObject result = m_handler->callTool(MCP_TOOL_CAPTURE_LAST_IMAGE, {});

    QCOMPARE(result["isError"].toBool(), true);
}

void TestMcpToolHandler::screenToMarkdown_noCamera_returnsError()
{
    // Without CameraManager, screen_to_markdown should error
    QJsonObject result = m_handler->callTool(MCP_TOOL_SCREEN_TO_MARKDOWN, {});

    QCOMPARE(result["isError"].toBool(), true);
}

// ============================================================================
// Script tool tests
// ============================================================================

void TestMcpToolHandler::executeScript_noScriptRunner_returnsError()
{
    QJsonObject args{{"script", "Click, 100, 200"}};
    QJsonObject result = m_handler->callTool(MCP_TOOL_EXECUTE_SCRIPT, args);

    QCOMPARE(result["isError"].toBool(), true);
    QString text = result["content"].toArray()[0].toObject()["text"].toString();
    QVERIFY(text.contains("ScriptRunner"));
}

void TestMcpToolHandler::executeScript_emptyScript_returnsError()
{
    // Empty script should error even with ScriptRunner (if one were injected)
    QJsonObject args{{"script", ""}};
    QJsonObject result = m_handler->callTool(MCP_TOOL_EXECUTE_SCRIPT, args);

    // Without ScriptRunner, this returns ScriptRunner error first;
    // either way it's an error
    QCOMPARE(result["isError"].toBool(), true);
}

void TestMcpToolHandler::validateScript_emptyScript()
{
    QJsonObject args{{"script", ""}};
    QJsonObject result = m_handler->callTool(MCP_TOOL_VALIDATE_SCRIPT, args);

    // validate_script without ScriptRunner — should error or return validation result
    QVERIFY(result.contains("content"));
}

// ============================================================================
// System tools tests
// ============================================================================

void TestMcpToolHandler::systemStatus_returnsStructure()
{
    QJsonObject result = m_handler->callTool(MCP_TOOL_SYSTEM_STATUS, {});

    QVERIFY(result.contains("content"));
    QVERIFY(!result.contains("isError"));

    // The result should contain a text content with JSON status
    QJsonArray content = result["content"].toArray();
    QVERIFY(!content.isEmpty());
    QCOMPARE(content[0].toObject()["type"].toString(), QStringLiteral("text"));

    QString text = content[0].toObject()["text"].toString();
    QVERIFY(!text.isEmpty());
}

// ============================================================================
// Result structure helpers
// ============================================================================

void TestMcpToolHandler::textResult_hasContentArray()
{
    // Verify all text results follow the standard structure
    QJsonObject result = m_handler->callTool(MCP_TOOL_KEYBOARD_CTRL_ALT_DEL, {});

    QVERIFY(result.contains("content"));
    QVERIFY(result["content"].isArray());
    QVERIFY(!result.contains("isError"));
}

void TestMcpToolHandler::errorResult_hasIsErrorTrue()
{
    // Verify all errors follow the standard structure
    QJsonObject result = m_handler->callTool("nonexistent", {});

    QVERIFY(result.contains("isError"));
    QCOMPARE(result["isError"].toBool(), true);
    QVERIFY(result.contains("content"));
    QVERIFY(result["content"].isArray());
}

QTEST_MAIN(TestMcpToolHandler)
#include "test_mcp_tool_handler.moc"
