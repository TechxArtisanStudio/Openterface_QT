/*
 * Test Exa and Parallel Web Search Providers
 * Standalone test for API providers that require API keys
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

// Test Exa API
QString testExaSearch(const QString &query, const QString &apiKey)
{
    if (apiKey.isEmpty()) {
        return "Exa API key not configured";
    }

    LOG("Exa: searching for:");
    LOG_QSTRING(query);

    // Exa API endpoint
    QString url = "https://api.exa.ai/search";

    // Build request body
    QJsonObject requestBody;
    requestBody["query"] = query;
    requestBody["numResults"] = 5;
    requestBody["contents"] = QJsonObject{{"text", true}};

    QJsonDocument doc(requestBody);
    QByteArray body = doc.toJson(QJsonDocument::Compact);

    // Headers
    QList<QPair<QString, QString>> headers;
    headers.append(qMakePair(QString("x-api-key"), apiKey));
    headers.append(qMakePair(QString("Content-Type"), QString("application/json")));
    headers.append(qMakePair(QString("Accept"), QString("application/json")));

    LOG("Exa: sending POST request to https://api.exa.ai/search");

    QByteArray output = executeHttpRequest(url, "POST", body, headers);
    if (output.isEmpty()) {
        LOG("Exa: empty response");
        return "FAILED: empty response";
    }

    LOG_QSTRING(QString("Exa: received %1 bytes").arg(output.length()));

    QJsonDocument responseDoc = QJsonDocument::fromJson(output);
    if (responseDoc.isNull() || !responseDoc.isObject()) {
        LOG("Exa: failed to parse response");
        LOG("Raw response (first 500 chars):");
        std::cout << QString::fromUtf8(output).left(500).toStdString() << std::endl;
        return "FAILED: invalid JSON";
    }

    QJsonObject root = responseDoc.object();

    // Check for errors
    if (root.contains("error")) {
        QString error = root["error"].toString();
        LOG_QSTRING(QString("Exa: API error: %1").arg(error));
        return QString("FAILED: %1").arg(error);
    }

    // Parse results
    QJsonArray resultsArr = root["results"].toArray();
    if (resultsArr.isEmpty()) {
        LOG("Exa: no results found");
        return "FAILED: no results";
    }

    LOG_QSTRING(QString("Exa: found %1 results").arg(resultsArr.count()));

    QStringList results;
    int count = 0;
    for (const auto &val : resultsArr) {
        if (count >= 5) break;
        QJsonObject item = val.toObject();

        QString title = item["title"].toString();
        QString text = item["text"].toString();
        QString link = item["url"].toString();

        if (!text.isEmpty()) {
            if (!title.isEmpty()) {
                results << QString("**%1**").arg(title);
            }
            if (text.length() > 300) {
                text = text.left(297) + "...";
            }
            results << text;
            if (!link.isEmpty()) {
                results << QString("Source: %1").arg(link);
            }
            results << "";
            count++;
        }
    }

    if (results.isEmpty()) {
        return "FAILED: no usable results";
    }

    QString result = results.join("\n").trimmed();
    LOG_QSTRING(QString("Exa: returning %1 characters of results").arg(result.length()));
    return result;
}

// Test Parallel API
QString testParallelSearch(const QString &query, const QString &apiKey)
{
    if (apiKey.isEmpty()) {
        return "Parallel API key not configured";
    }

    LOG("Parallel: searching for:");
    LOG_QSTRING(query);

    // Parallel API endpoint
    QString url = "https://api.parallel.ai/v1/search";

    // Build request body
    QJsonObject requestBody;
    requestBody["query"] = query;
    requestBody["limit"] = 5;

    QJsonDocument doc(requestBody);
    QByteArray body = doc.toJson(QJsonDocument::Compact);

    // Headers
    QList<QPair<QString, QString>> headers;
    headers.append(qMakePair(QString("Authorization"), QString("Bearer %1").arg(apiKey)));
    headers.append(qMakePair(QString("Content-Type"), QString("application/json")));
    headers.append(qMakePair(QString("Accept"), QString("application/json")));

    LOG("Parallel: sending POST request to https://api.parallel.ai/v1/search");

    QByteArray output = executeHttpRequest(url, "POST", body, headers);
    if (output.isEmpty()) {
        LOG("Parallel: empty response");
        return "FAILED: empty response";
    }

    LOG_QSTRING(QString("Parallel: received %1 bytes").arg(output.length()));

    QJsonDocument responseDoc = QJsonDocument::fromJson(output);
    if (responseDoc.isNull() || !responseDoc.isObject()) {
        LOG("Parallel: failed to parse response");
        LOG("Raw response (first 500 chars):");
        std::cout << QString::fromUtf8(output).left(500).toStdString() << std::endl;
        return "FAILED: invalid JSON";
    }

    QJsonObject root = responseDoc.object();

    // Check for errors
    if (root.contains("error")) {
        QString error = root["error"].toString();
        LOG_QSTRING(QString("Parallel: API error: %1").arg(error));
        return QString("FAILED: %1").arg(error);
    }

    // Parse results
    QJsonArray resultsArr = root["results"].toArray();
    if (resultsArr.isEmpty()) {
        LOG("Parallel: no results found");
        return "FAILED: no results";
    }

    LOG_QSTRING(QString("Parallel: found %1 results").arg(resultsArr.count()));

    QStringList results;
    int count = 0;
    for (const auto &val : resultsArr) {
        if (count >= 5) break;
        QJsonObject item = val.toObject();

        QString title = item["title"].toString();
        QString content = item["content"].toString();
        QString link = item["url"].toString();

        if (!content.isEmpty()) {
            if (!title.isEmpty()) {
                results << QString("**%1**").arg(title);
            }
            if (content.length() > 300) {
                content = content.left(297) + "...";
            }
            results << content;
            if (!link.isEmpty()) {
                results << QString("Source: %1").arg(link);
            }
            results << "";
            count++;
        }
    }

    if (results.isEmpty()) {
        return "FAILED: no usable results";
    }

    QString result = results.join("\n").trimmed();
    LOG_QSTRING(QString("Parallel: returning %1 characters of results").arg(result.length()));
    return result;
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    LOG("");
    LOG("========================================");
    LOG("Testing Exa and Parallel Web Search APIs");
    LOG("========================================");
    LOG("");

    // Read API keys from command line or environment
    QString exaApiKey = qgetenv("EXA_API_KEY");
    QString parallelApiKey = qgetenv("PARALLEL_API_KEY");

    if (argc > 1) {
        exaApiKey = argv[1];
    }
    if (argc > 2) {
        parallelApiKey = argv[2];
    }

    LOG("API Key Status:");
    LOG_QSTRING(QString("  Exa API Key: %1").arg(exaApiKey.isEmpty() ? "NOT SET" : "SET"));
    LOG_QSTRING(QString("  Parallel API Key: %1").arg(parallelApiKey.isEmpty() ? "NOT SET" : "SET"));
    LOG("");

    if (exaApiKey.isEmpty() && parallelApiKey.isEmpty()) {
        LOG("ERROR: No API keys configured!");
        LOG("");
        LOG("Set environment variables:");
        LOG("  export EXA_API_KEY=your_exa_key");
        LOG("  export PARALLEL_API_KEY=your_parallel_key");
        LOG("");
        LOG("Or pass as arguments:");
        LOG("  ./test_exa_parallel <exa_key> <parallel_key>");
        LOG("");
        LOG("Get API keys:");
        LOG("  Exa: https://exa.ai");
        LOG("  Parallel: https://search.parallel.ai/mcp");
        return 1;
    }

    // Test queries
    QStringList testQueries;
    testQueries << "open code"
                << "Python programming"
                << "OpenAI"
                << "machine learning";

    // Test Exa
    if (!exaApiKey.isEmpty()) {
        LOG("========================================");
        LOG("Testing Exa API");
        LOG("========================================");

        for (const QString &query : testQueries) {
            LOG("");
            LOG_QSTRING(QString("--- Exa Search: '%1' ---").arg(query));
            QString result = testExaSearch(query, exaApiKey);
            LOG("");
            LOG("Result (first 800 chars):");
            std::cout << result.left(800).toStdString() << std::endl;
            LOG("");
            if (result.startsWith("FAILED:")) {
                std::cout << "Status: FAILED" << std::endl;
            } else {
                std::cout << "Status: SUCCESS" << std::endl;
            }
            LOG("");
        }
    }

    // Test Parallel
    if (!parallelApiKey.isEmpty()) {
        LOG("========================================");
        LOG("Testing Parallel API");
        LOG("========================================");

        for (const QString &query : testQueries) {
            LOG("");
            LOG_QSTRING(QString("--- Parallel Search: '%1' ---").arg(query));
            QString result = testParallelSearch(query, parallelApiKey);
            LOG("");
            LOG("Result (first 800 chars):");
            std::cout << result.left(800).toStdString() << std::endl;
            LOG("");
            if (result.startsWith("FAILED:")) {
                std::cout << "Status: FAILED" << std::endl;
            } else {
                std::cout << "Status: SUCCESS" << std::endl;
            }
            LOG("");
        }
    }

    LOG("========================================");
    LOG("Tests completed");
    LOG("========================================");

    return 0;
}
