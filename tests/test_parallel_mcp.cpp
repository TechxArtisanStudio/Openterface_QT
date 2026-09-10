/*
 * Test Parallel MCP Provider - Tests the new MCP-based Parallel search
 */

#include <QCoreApplication>
#include <QProcess>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrl>
#include <QStringList>
#include <iostream>

#define LOG(msg) std::cout << msg << std::endl
#define LOG_QSTRING(msg) std::cout << (msg).toStdString() << std::endl

// Execute curl with custom headers and body
QByteArray executeHttpRequest(const QString &url, const QString &method,
                              const QByteArray &body,
                              const QList<QPair<QString, QString>> &headers,
                              int timeoutMs = 15000)
{
    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);

    QStringList args;
    args << "-s" << "--max-time" << QString::number(timeoutMs / 1000);
    args << "-X" << method;

    for (const auto &header : headers) {
        args << "-H" << QString("%1: %2").arg(header.first, header.second);
    }

    if (!body.isEmpty()) {
        args << "-d" << QString::fromUtf8(body);
    }

    args << url;

    process.start("curl", args);

    if (!process.waitForStarted(5000)) {
        return QByteArray();
    }

    if (!process.waitForFinished(timeoutMs)) {
        process.kill();
        return QByteArray();
    }

    if (process.exitCode() != 0) {
        return QByteArray();
    }

    return process.readAll();
}

// Test Parallel MCP search
QString testParallelMcpSearch(const QString &query)
{
    LOG("Parallel MCP: searching for:");
    LOG_QSTRING(query);

    QString url = "https://search.parallel.ai/mcp";
    QString sessionId = QString("openterface-test-%1").arg(qHash(query));

    // Step 1: Initialize MCP session
    LOG("Parallel MCP: initializing session...");
    QJsonObject initRequest;
    initRequest["jsonrpc"] = "2.0";
    initRequest["method"] = "initialize";
    QJsonObject initParams;
    initParams["protocolVersion"] = "2024-11-05";
    initParams["capabilities"] = QJsonObject();
    QJsonObject clientInfo;
    clientInfo["name"] = "OpenterfaceTest";
    clientInfo["version"] = "1.0";
    initParams["clientInfo"] = clientInfo;
    initRequest["params"] = initParams;
    initRequest["id"] = 1;

    QJsonDocument initDoc(initRequest);
    QByteArray initBody = initDoc.toJson(QJsonDocument::Compact);

    QList<QPair<QString, QString>> initHeaders;
    initHeaders.append(qMakePair(QString("Content-Type"), QString("application/json")));
    initHeaders.append(qMakePair(QString("Accept"), QString("application/json")));

    QByteArray initOutput = executeHttpRequest(url, "POST", initBody, initHeaders);
    if (initOutput.isEmpty()) {
        return "FAILED: could not initialize MCP session";
    }

    LOG("Parallel MCP: session initialized successfully");

    // Step 2: Call web_search tool
    LOG("Parallel MCP: calling web_search tool...");
    QJsonObject toolRequest;
    toolRequest["jsonrpc"] = "2.0";
    toolRequest["method"] = "tools/call";
    QJsonObject toolParams;
    toolParams["name"] = "web_search";

    QJsonObject searchArgs;
    searchArgs["objective"] = query;
    QJsonArray searchQueries;
    searchQueries.append(query);
    if (!query.contains(" ")) {
        searchQueries.append(query + " software");
        searchQueries.append(query + " programming");
    } else {
        QStringList words = query.split(" ", Qt::SkipEmptyParts);
        if (words.size() > 2) {
            searchQueries.append(words.mid(0, 2).join(" "));
        }
    }
    searchArgs["search_queries"] = searchQueries;
    searchArgs["session_id"] = sessionId;
    searchArgs["model_name"] = "claude-opus-4.7";

    toolParams["arguments"] = searchArgs;
    toolRequest["params"] = toolParams;
    toolRequest["id"] = 2;

    QJsonDocument toolDoc(toolRequest);
    QByteArray toolBody = toolDoc.toJson(QJsonDocument::Compact);

    QList<QPair<QString, QString>> toolHeaders;
    toolHeaders.append(qMakePair(QString("Content-Type"), QString("application/json")));
    toolHeaders.append(qMakePair(QString("Accept"), QString("application/json")));
    toolHeaders.append(qMakePair(QString("Mcp-Session-Id"), sessionId));

    QByteArray output = executeHttpRequest(url, "POST", toolBody, toolHeaders);
    if (output.isEmpty()) {
        return "FAILED: empty response from web_search";
    }

    LOG_QSTRING(QString("Parallel MCP: received %1 bytes").arg(output.length()));

    QJsonDocument responseDoc = QJsonDocument::fromJson(output);
    if (responseDoc.isNull() || !responseDoc.isObject()) {
        return "FAILED: invalid JSON response";
    }

    QJsonObject root = responseDoc.object();

    if (root.contains("error")) {
        QJsonObject error = root["error"].toObject();
        QString errorMsg = error["message"].toString();
        return QString("FAILED: %1").arg(errorMsg);
    }

    QJsonObject result = root["result"].toObject();
    QJsonArray content = result["content"].toArray();

    if (content.isEmpty()) {
        return "FAILED: no content in response";
    }

    QString resultText = content[0].toObject()["text"].toString();
    if (resultText.isEmpty()) {
        return "FAILED: empty result text";
    }

    // Parse the search results
    QJsonDocument searchResultDoc = QJsonDocument::fromJson(resultText.toUtf8());
    if (searchResultDoc.isNull() || !searchResultDoc.isObject()) {
        return "FAILED: could not parse search results";
    }

    QJsonObject searchResult = searchResultDoc.object();
    QJsonArray resultsArr = searchResult["results"].toArray();

    if (resultsArr.isEmpty()) {
        return "FAILED: no search results";
    }

    LOG_QSTRING(QString("Parallel MCP: found %1 results").arg(resultsArr.count()));

    // Format results
    QStringList results;
    int count = 0;
    for (const auto &val : resultsArr) {
        if (count >= 5) break;
        QJsonObject item = val.toObject();

        QString title = item["title"].toString();
        QString url = item["url"].toString();
        QJsonArray excerpts = item["excerpts"].toArray();

        if (!title.isEmpty() || !excerpts.isEmpty()) {
            if (!title.isEmpty()) {
                results << QString("**%1**").arg(title);
            }

            for (const auto &excerptVal : excerpts) {
                QString excerpt = excerptVal.toString();
                excerpt.replace("\\\"", "\"");
                excerpt.replace("\\n", " ");
                if (excerpt.length() > 300) {
                    excerpt = excerpt.left(297) + "...";
                }
                results << excerpt;
            }

            if (!url.isEmpty()) {
                results << QString("Source: %1").arg(url);
            }
            results << "";
            count++;
        }
    }

    if (results.isEmpty()) {
        return "FAILED: no usable results";
    }

    QString result_str = results.join("\n").trimmed();
    LOG_QSTRING(QString("Parallel MCP: returning %1 characters").arg(result_str.length()));
    return result_str;
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    LOG("");
    LOG("========================================");
    LOG("Testing Parallel MCP Provider (Anonymous)");
    LOG("========================================");
    LOG("");
    LOG("This test uses https://search.parallel.ai/mcp");
    LOG("No API key required - works anonymously!");
    LOG("");

    QStringList testQueries;
    testQueries << "open code"
                << "Python programming"
                << "OpenAI"
                << "machine learning";

    int passed = 0;
    int failed = 0;

    for (const QString &query : testQueries) {
        LOG("");
        LOG_QSTRING(QString("--- Search: '%1' ---").arg(query));
        QString result = testParallelMcpSearch(query);
        LOG("");
        LOG("Result (first 800 chars):");
        std::cout << result.left(800).toStdString() << std::endl;
        LOG("");

        if (result.startsWith("FAILED:")) {
            std::cout << "Status: FAILED ❌" << std::endl;
            failed++;
        } else {
            std::cout << "Status: SUCCESS ✅" << std::endl;
            passed++;
        }
        LOG("");
    }

    LOG("========================================");
    LOG("Test Summary");
    LOG("========================================");
    LOG_QSTRING(QString("Passed: %1").arg(passed));
    LOG_QSTRING(QString("Failed: %1").arg(failed));
    LOG("");

    if (failed == 0) {
        LOG("✅ All tests PASSED!");
        LOG("");
        LOG("Parallel MCP works anonymously without API keys!");
        return 0;
    } else {
        LOG("❌ Some tests FAILED");
        return 1;
    }
}
