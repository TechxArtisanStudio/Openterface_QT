/*
* ========================================================================== *
*                                                                            *
*    This file is part of the Openterface Mini KVM App QT version            *
*                                                                            *
*    Copyright (C) 2024   <info@openterface.com>                             *
*                                                                            *
*    This program is free software: you can redistribute it and/or modify    *
*    it under the terms of the GNU General Public License as published by    *
*    the Free Software Foundation version 3.                                 *
*                                                                            *
*    This program is distributed in the hope that it will be useful, but     *
*    WITHOUT ANY WARRANTY; without even the implied warranty of              *
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU        *
*    General Public License for more details.                                *
*                                                                            *
*    You should have received a copy of the GNU General Public License       *
*    along with this program. If not, see <http://www.gnu.org/licenses/>.    *
*                                                                            *
* ========================================================================== *
*/

#include "WebSearchProviders.h"
#include "../ui/globalsetting.h"
#include <QProcess>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrl>
#include <QLoggingCategory>

Q_DECLARE_LOGGING_CATEGORY(log_ai_chat)

// Helper to execute curl and return response
static QByteArray executeCurl(const QString &url, const QStringList &extraArgs = QStringList(),
                               int timeoutMs = 15000, QString *errorMessage = nullptr)
{
    QProcess process;
    // Keep channels separate so we can read stderr for SSL errors
    process.setProcessChannelMode(QProcess::SeparateChannels);

    QStringList args;
    args << "-s" << "--max-time" << QString::number(timeoutMs / 1000);
    args << extraArgs;
    args << url;

#ifdef Q_OS_WIN
    process.start("cmd.exe", QStringList() << "/c" << "curl" << args);
#else
    process.start("curl", args);
#endif

    if (!process.waitForStarted(5000)) {
        QString error = QString("Failed to start curl: %1").arg(process.errorString());
        qCWarning(log_ai_chat) << "executeCurl:" << error;
        if (errorMessage) *errorMessage = error;
        return QByteArray();
    }

    if (!process.waitForFinished(timeoutMs)) {
        QString error = QString("curl timed out after %1ms").arg(timeoutMs);
        qCWarning(log_ai_chat) << "executeCurl:" << error;
        if (errorMessage) *errorMessage = error;
        process.kill();
        return QByteArray();
    }

    QByteArray stdOutput = process.readAllStandardOutput();
    QByteArray stdError = process.readAllStandardError();
    QString errorStr = QString::fromUtf8(stdError);

    if (process.exitCode() != 0) {
        QString error;

        // Check for SSL/TLS errors in stderr
        if (errorStr.contains("SSL", Qt::CaseInsensitive) ||
            errorStr.contains("TLS", Qt::CaseInsensitive) ||
            errorStr.contains("certificate", Qt::CaseInsensitive) ||
            errorStr.contains("Handshake", Qt::CaseInsensitive) ||
            errorStr.contains("issuer", Qt::CaseInsensitive)) {
            error = "SSL/TLS error: Cannot establish secure HTTPS connection. "
                    "This usually means Qt's TLS backend is not configured correctly. "
                    "Please ensure OpenSSL is installed and Qt can find its TLS plugins. "
                    "Try: export QT_TLS_BACKEND=openssl before running the application.";
        } else {
            error = QString("curl failed with exit code %1: %2")
                    .arg(process.exitCode())
                    .arg(errorStr.trimmed());
        }

        qCWarning(log_ai_chat) << "executeCurl:" << error;
        if (errorMessage) *errorMessage = error;
        return QByteArray();
    }

    // Also check for SSL errors even if exit code is 0 (some curl versions)
    if (stdOutput.isEmpty() && !stdError.isEmpty()) {
        if (errorStr.contains("SSL", Qt::CaseInsensitive) ||
            errorStr.contains("TLS", Qt::CaseInsensitive) ||
            errorStr.contains("certificate", Qt::CaseInsensitive)) {
            QString error = "SSL/TLS error: Cannot establish secure HTTPS connection. "
                           "This usually means Qt's TLS backend is not configured correctly. "
                           "Please ensure OpenSSL is installed and Qt can find its TLS plugins. "
                           "Try: export QT_TLS_BACKEND=openssl before running the application.";
            qCWarning(log_ai_chat) << "executeCurl:" << error;
            if (errorMessage) *errorMessage = error;
            return QByteArray();
        }
    }

    return stdOutput;
}

// Helper for HTTP requests with custom method and body
static QByteArray executeHttpRequest(const QString &url, const QString &method,
                                      const QByteArray &body,
                                      const QList<QPair<QString, QString>> &headers,
                                      int timeoutMs = 15000, QString *errorMessage = nullptr)
{
    QProcess process;
    // Keep channels separate so we can read stderr specifically
    process.setProcessChannelMode(QProcess::SeparateChannels);

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

#ifdef Q_OS_WIN
    process.start("cmd.exe", QStringList() << "/c" << "curl" << args);
#else
    process.start("curl", args);
#endif

    if (!process.waitForStarted(5000)) {
        QString error = QString("Failed to start curl: %1").arg(process.errorString());
        qCWarning(log_ai_chat) << "executeHttpRequest:" << error;
        if (errorMessage) *errorMessage = error;
        return QByteArray();
    }

    if (!process.waitForFinished(timeoutMs)) {
        QString error = QString("curl timed out after %1ms").arg(timeoutMs);
        qCWarning(log_ai_chat) << "executeHttpRequest:" << error;
        if (errorMessage) *errorMessage = error;
        process.kill();
        return QByteArray();
    }

    QByteArray stdOutput = process.readAllStandardOutput();
    QByteArray stdError = process.readAllStandardError();
    QString errorStr = QString::fromUtf8(stdError);

    if (process.exitCode() != 0) {
        QString error;

        // Check for SSL/TLS errors in stderr or stdout
        QString combinedOutput = errorStr + QString::fromUtf8(stdOutput);
        if (combinedOutput.contains("SSL", Qt::CaseInsensitive) ||
            combinedOutput.contains("TLS", Qt::CaseInsensitive) ||
            combinedOutput.contains("certificate", Qt::CaseInsensitive) ||
            combinedOutput.contains("Handshake", Qt::CaseInsensitive) ||
            combinedOutput.contains("issuer", Qt::CaseInsensitive)) {
            error = "SSL/TLS error: Cannot establish secure HTTPS connection. "
                    "This usually means Qt's TLS backend is not configured correctly. "
                    "Please ensure OpenSSL is installed and Qt can find its TLS plugins. "
                    "Try: export QT_TLS_BACKEND=openssl before running the application.";
        } else {
            error = QString("curl failed with exit code %1: %2")
                    .arg(process.exitCode())
                    .arg(errorStr.trimmed());
        }

        qCWarning(log_ai_chat) << "executeHttpRequest:" << error;
        if (errorMessage) *errorMessage = error;
        return QByteArray();
    }

    // Also check for SSL errors even if exit code is 0 (some curl versions)
    if (stdOutput.isEmpty() && !stdError.isEmpty()) {
        if (errorStr.contains("SSL", Qt::CaseInsensitive) ||
            errorStr.contains("TLS", Qt::CaseInsensitive) ||
            errorStr.contains("certificate", Qt::CaseInsensitive)) {
            QString error = "SSL/TLS error: Cannot establish secure HTTPS connection. "
                           "This usually means Qt's TLS backend is not configured correctly. "
                           "Please ensure OpenSSL is installed and Qt can find its TLS plugins. "
                           "Try: export QT_TLS_BACKEND=openssl before running the application.";
            qCWarning(log_ai_chat) << "executeHttpRequest:" << error;
            if (errorMessage) *errorMessage = error;
            return QByteArray();
        }
    }

    return stdOutput;
}

// ============================================================================
// DuckDuckGo Provider
// ============================================================================

QString DuckDuckGoProvider::search(const QString &query) const
{
    if (query.isEmpty()) {
        return "web_search: missing query argument";
    }

    QString encodedQuery = QUrl::toPercentEncoding(query);
    QString url = QString("https://api.duckduckgo.com/?q=%1&format=json&no_html=1&skip_disambig=1")
                  .arg(encodedQuery);

    QString curlError;
    QByteArray output = executeCurl(url, QStringList(), 15000, &curlError);
    if (output.isEmpty()) {
        // Return clear error message to user
        if (curlError.contains("SSL/TLS")) {
            return QString("web_search: %1").arg(curlError);
        }
        return "web_search: failed to fetch results";
    }

    QJsonDocument doc = QJsonDocument::fromJson(output);
    if (doc.isNull()) {
        return "web_search: failed to parse response";
    }

    QJsonObject root = doc.object();
    QStringList results;

    // Extract abstract (main answer)
    if (root.contains("Abstract")) {
        QString abstract = root["Abstract"].toString();
        if (!abstract.isEmpty()) {
            results << QString("Answer: %1").arg(abstract);
        }
    }

    // Extract abstract text if different
    if (root.contains("AbstractText")) {
        QString abstractText = root["AbstractText"].toString();
        if (!abstractText.isEmpty() && abstractText != root["Abstract"].toString()) {
            results << QString("Summary: %1").arg(abstractText);
        }
    }

    // Extract Definition if available
    if (root.contains("Definition")) {
        QString definition = root["Definition"].toString();
        if (!definition.isEmpty()) {
            results << QString("Definition: %1").arg(definition);
        }
    }

    // Extract Answer if available
    if (root.contains("Answer")) {
        QString answer = root["Answer"].toString();
        if (!answer.isEmpty()) {
            results << QString("Direct Answer: %1").arg(answer);
        }
    }

    // Extract related topics (up to 5)
    if (root.contains("RelatedTopics")) {
        QJsonArray topics = root["RelatedTopics"].toArray();
        int count = 0;
        for (const auto &topic : topics) {
            if (count >= 5) break;
            QJsonObject topicObj = topic.toObject();
            if (topicObj.contains("Text")) {
                QString text = topicObj["Text"].toString();
                if (!text.isEmpty()) {
                    results << QString("- %1").arg(text);
                    count++;
                }
            }
        }
    }

    if (results.isEmpty()) {
        return "web_search: no results found";
    }

    QString result = results.join("\n");
    if (result.length() > 4096) {
        result = result.left(4096) + "\n[results truncated at 4096 chars]";
    }

    return result;
}

// ============================================================================
// Wikipedia Provider
// ============================================================================

QString WikipediaProvider::search(const QString &query) const
{
    if (query.isEmpty()) {
        return "web_search: missing query argument";
    }

    QString encodedQuery = QUrl::toPercentEncoding(query);
    QString searchUrl = QString("https://en.wikipedia.org/w/api.php?action=opensearch&search=%1&limit=3&format=json")
                        .arg(encodedQuery);

    QString curlError;
    QByteArray output = executeCurl(searchUrl, QStringList(), 15000, &curlError);
    if (output.isEmpty()) {
        // Return clear error message to user
        if (curlError.contains("SSL/TLS")) {
            return QString("web_search: %1").arg(curlError);
        }
        return "web_search: no results found";
    }

    // Parse the OpenSearch response: [query, [titles], [descriptions], [urls]]
    QJsonDocument doc = QJsonDocument::fromJson(output);
    if (doc.isNull() || !doc.isArray()) {
        return "web_search: no results found";
    }

    QJsonArray arr = doc.array();
    if (arr.size() < 4) {
        return "web_search: no results found";
    }

    QJsonArray titles = arr[1].toArray();
    QJsonArray urls = arr[3].toArray();

    if (titles.isEmpty()) {
        return "web_search: no results found";
    }

    // Get the summary for the first matching article
    QString firstTitle = titles[0].toString();
    QString encodedTitle = QUrl::toPercentEncoding(firstTitle);
    QString summaryUrl = QString("https://en.wikipedia.org/api/rest_v1/page/summary/%1")
                         .arg(encodedTitle);

    QString summaryError;
    QByteArray summaryOutput = executeCurl(summaryUrl, QStringList(), 15000, &summaryError);
    if (summaryOutput.isEmpty()) {
        // Return clear error message to user
        if (summaryError.contains("SSL/TLS")) {
            return QString("web_search: %1").arg(summaryError);
        }
        return "web_search: no results found";
    }

    QJsonDocument summaryDoc = QJsonDocument::fromJson(summaryOutput);
    if (summaryDoc.isNull() || !summaryDoc.isObject()) {
        return "web_search: no results found";
    }

    QJsonObject summaryObj = summaryDoc.object();
    QString title = summaryObj["title"].toString();
    QString extract = summaryObj["extract"].toString();

    if (extract.isEmpty()) {
        return "web_search: no results found";
    }

    // Build the result
    QStringList results;
    results << QString("From Wikipedia: %1").arg(title);
    results << extract;

    // Add URLs for additional articles if there are multiple matches
    if (titles.size() > 1) {
        results << "\nRelated articles:";
        for (int i = 1; i < qMin(titles.size(), 4); ++i) {
            results << QString("- %1: %2").arg(titles[i].toString(), urls[i].toString());
        }
    }

    QString result = results.join("\n");
    if (result.length() > 4096) {
        result = result.left(4096) + "\n[results truncated at 4096 chars]";
    }

    return result;
}

// ============================================================================
// Exa Provider (MCP - Model Context Protocol with SSE)
// ============================================================================

// Helper to parse SSE response and extract JSON data
static QString parseSseResponse(const QByteArray &response)
{
    QString result;
    QString responseStr = QString::fromUtf8(response);
    QStringList lines = responseStr.split('\n');

    for (const QString &line : lines) {
        if (line.startsWith("data: ")) {
            QString data = line.mid(6).trimmed();
            if (!data.isEmpty()) {
                result = data;
                break;  // Take the first data line
            }
        }
    }

    return result;
}

QString ExaProvider::search(const QString &query) const
{
    if (query.isEmpty()) {
        return "web_search: missing query argument";
    }


    // Exa MCP endpoint (works anonymously or with API key)
    QString url = "https://mcp.exa.ai/mcp";

    // Initialize MCP session
    QJsonObject initRequest;
    initRequest["jsonrpc"] = "2.0";
    initRequest["method"] = "initialize";
    QJsonObject initParams;
    initParams["protocolVersion"] = "2024-11-05";
    initParams["capabilities"] = QJsonObject();
    QJsonObject clientInfo;
    clientInfo["name"] = "Openterface";
    clientInfo["version"] = "1.0";
    initParams["clientInfo"] = clientInfo;
    initRequest["params"] = initParams;
    initRequest["id"] = 1;

    QJsonDocument initDoc(initRequest);
    QByteArray initBody = initDoc.toJson(QJsonDocument::Compact);

    // Headers for initialization
    QList<QPair<QString, QString>> initHeaders;
    initHeaders.append(qMakePair(QString("Content-Type"), QString("application/json")));
    initHeaders.append(qMakePair(QString("Accept"), QString("application/json, text/event-stream")));

    // Check if API key is configured (for paid tier)
    QString apiKey = GlobalSetting::instance().getChatExaApiKey();
    if (!apiKey.isEmpty()) {
        initHeaders.append(qMakePair(QString("x-api-key"), apiKey));
    }

    // Initialize MCP session
    QString initError;
    QByteArray initOutput = executeHttpRequest(url, "POST", initBody, initHeaders, 15000, &initError);
    if (initOutput.isEmpty()) {
        qCWarning(log_ai_chat) << "Exa MCP: failed to initialize session -" << initError;
        // Return clear error message to user
        if (initError.contains("SSL/TLS")) {
            return QString("web_search: %1").arg(initError);
        }
        return "web_search: failed to initialize Exa MCP session";
    }

    // Parse initialization response (also SSE format)
    QString initJsonData = parseSseResponse(initOutput);
    if (initJsonData.isEmpty()) {
        qCWarning(log_ai_chat) << "Exa MCP: no data in initialization SSE response";
        return "web_search: failed to parse Exa initialization response";
    }

    QJsonDocument initResponseDoc = QJsonDocument::fromJson(initJsonData.toUtf8());
    if (initResponseDoc.isNull() || !initResponseDoc.isObject()) {
        qCWarning(log_ai_chat) << "Exa MCP: failed to parse initialization JSON";
        return "web_search: failed to parse Exa initialization response";
    }

    QJsonObject initRoot = initResponseDoc.object();
    if (initRoot.contains("error")) {
        QJsonObject error = initRoot["error"].toObject();
        QString errorMsg = error["message"].toString();
        qCWarning(log_ai_chat) << "Exa MCP: initialization error:" << errorMsg;
        return QString("web_search: Exa initialization error - %1").arg(errorMsg);
    }


    // Now call the web_search_exa tool
    QJsonObject toolRequest;
    toolRequest["jsonrpc"] = "2.0";
    toolRequest["method"] = "tools/call";
    QJsonObject toolParams;
    toolParams["name"] = "web_search_exa";

    // Build search arguments
    // Exa works best with descriptive, semantic queries
    // Enhance short queries to be more descriptive
    QString enhancedQuery = query;
    if (query.length() < 20 && !query.contains(" ")) {
        // Short single-word query - enhance it
        enhancedQuery = QString("information about %1 software or technology").arg(query);
    } else if (query.length() < 30) {
        // Short phrase - make it more descriptive
        enhancedQuery = QString("what is %1").arg(query);
    }

    QJsonObject searchArgs;
    searchArgs["query"] = enhancedQuery;
    searchArgs["numResults"] = 5;


    toolParams["arguments"] = searchArgs;
    toolRequest["params"] = toolParams;
    toolRequest["id"] = 2;

    QJsonDocument toolDoc(toolRequest);
    QByteArray toolBody = toolDoc.toJson(QJsonDocument::Compact);

    // Headers for tool call
    QList<QPair<QString, QString>> toolHeaders;
    toolHeaders.append(qMakePair(QString("Content-Type"), QString("application/json")));
    toolHeaders.append(qMakePair(QString("Accept"), QString("application/json, text/event-stream")));
    if (!apiKey.isEmpty()) {
        toolHeaders.append(qMakePair(QString("x-api-key"), apiKey));
    }

    QString toolError;
    QByteArray output = executeHttpRequest(url, "POST", toolBody, toolHeaders, 15000, &toolError);
    if (output.isEmpty()) {
        qCWarning(log_ai_chat) << "Exa MCP: empty response -" << toolError;
        // Return clear error message to user
        if (toolError.contains("SSL/TLS")) {
            return QString("web_search: %1").arg(toolError);
        }
        return "web_search: failed to fetch results from Exa";
    }


    // Parse SSE response to extract JSON
    QString jsonData = parseSseResponse(output);
    if (jsonData.isEmpty()) {
        qCWarning(log_ai_chat) << "Exa MCP: no data in SSE response";
        return "web_search: failed to parse SSE response";
    }

    QJsonDocument responseDoc = QJsonDocument::fromJson(jsonData.toUtf8());
    if (responseDoc.isNull() || !responseDoc.isObject()) {
        qCWarning(log_ai_chat) << "Exa MCP: failed to parse JSON response";
        return "web_search: failed to parse Exa response";
    }

    QJsonObject root = responseDoc.object();

    // Check for JSON-RPC errors
    if (root.contains("error")) {
        QJsonObject error = root["error"].toObject();
        QString errorMsg = error["message"].toString();
        qCWarning(log_ai_chat) << "Exa MCP: error:" << errorMsg;
        return QString("web_search: Exa error - %1").arg(errorMsg);
    }

    // Parse the result
    QJsonObject result = root["result"].toObject();
    QJsonArray content = result["content"].toArray();

    if (content.isEmpty()) {
        return "web_search: no results found";
    }

    // The content is an array with a single text element containing the search results
    QString resultText = content[0].toObject()["text"].toString();
    if (resultText.isEmpty()) {
        return "web_search: no results found";
    }


    // Exa returns formatted text with titles, URLs, and highlights
    // Clean up the text if it's too long
    if (resultText.length() > 4096) {
        resultText = resultText.left(4096) + "\n[results truncated at 4096 chars]";
    }

    return resultText;
}

// ============================================================================
// Parallel Provider (MCP - Model Context Protocol)
// ============================================================================

QString ParallelProvider::search(const QString &query) const
{
    if (query.isEmpty()) {
        return "web_search: missing query argument";
    }


    // Parallel MCP endpoint (works anonymously or with API key)
    QString url = "https://search.parallel.ai/mcp";

    // Generate a session ID for rate limiting (anonymous access)
    // Using a fixed session ID per application session would be better,
    // but for now we use a simple hash of the query
    QString sessionId = QString("openterface-%1").arg(qHash(query));

    // Initialize MCP session (required before tool calls)
    QJsonObject initRequest;
    initRequest["jsonrpc"] = "2.0";
    initRequest["method"] = "initialize";
    QJsonObject initParams;
    initParams["protocolVersion"] = "2024-11-05";
    initParams["capabilities"] = QJsonObject();
    QJsonObject clientInfo;
    clientInfo["name"] = "Openterface";
    clientInfo["version"] = "1.0";
    initParams["clientInfo"] = clientInfo;
    initRequest["params"] = initParams;
    initRequest["id"] = 1;

    QJsonDocument initDoc(initRequest);
    QByteArray initBody = initDoc.toJson(QJsonDocument::Compact);

    // Headers for initialization
    QList<QPair<QString, QString>> initHeaders;
    initHeaders.append(qMakePair(QString("Content-Type"), QString("application/json")));
    initHeaders.append(qMakePair(QString("Accept"), QString("application/json")));

    // Check if API key is configured (for paid tier)
    QString apiKey = GlobalSetting::instance().getChatParallelApiKey();
    if (!apiKey.isEmpty()) {
        initHeaders.append(qMakePair(QString("Authorization"), QString("Bearer %1").arg(apiKey)));
    }

    // Initialize MCP session
    QString initError;
    QByteArray initOutput = executeHttpRequest(url, "POST", initBody, initHeaders, 15000, &initError);
    if (initOutput.isEmpty()) {
        qCWarning(log_ai_chat) << "Parallel MCP: failed to initialize session -" << initError;
        // Return clear error message to user
        if (initError.contains("SSL/TLS")) {
            return QString("web_search: %1").arg(initError);
        }
        return "web_search: failed to initialize Parallel MCP session";
    }

    // Parse initialization response
    QJsonDocument initResponseDoc = QJsonDocument::fromJson(initOutput);
    if (initResponseDoc.isNull() || !initResponseDoc.isObject()) {
        qCWarning(log_ai_chat) << "Parallel MCP: failed to parse initialization response";
        return "web_search: failed to parse Parallel initialization response";
    }

    QJsonObject initRoot = initResponseDoc.object();
    if (initRoot.contains("error")) {
        QJsonObject error = initRoot["error"].toObject();
        QString errorMsg = error["message"].toString();
        qCWarning(log_ai_chat) << "Parallel MCP: initialization error:" << errorMsg;
        return QString("web_search: Parallel initialization error - %1").arg(errorMsg);
    }


    // Now call the web_search tool
    QJsonObject toolRequest;
    toolRequest["jsonrpc"] = "2.0";
    toolRequest["method"] = "tools/call";
    QJsonObject toolParams;
    toolParams["name"] = "web_search";

    // Build search arguments
    QJsonObject searchArgs;
    searchArgs["objective"] = query;
    QJsonArray searchQueries;
    searchQueries.append(query);
    // Add related queries for better results
    if (!query.contains(" ")) {
        // Single word query - add variations
        searchQueries.append(query + " software");
        searchQueries.append(query + " programming");
    } else {
        // Multi-word query - use as-is plus a shorter version
        QStringList words = query.split(" ", Qt::SkipEmptyParts);
        if (words.size() > 2) {
            searchQueries.append(words.mid(0, 2).join(" "));
        }
    }
    searchArgs["search_queries"] = searchQueries;
    searchArgs["session_id"] = sessionId;
    searchArgs["model_name"] = "claude-opus-4.7";  // For analytics

    toolParams["arguments"] = searchArgs;
    toolRequest["params"] = toolParams;
    toolRequest["id"] = 2;

    QJsonDocument toolDoc(toolRequest);
    QByteArray toolBody = toolDoc.toJson(QJsonDocument::Compact);

    // Headers for tool call
    QList<QPair<QString, QString>> toolHeaders;
    toolHeaders.append(qMakePair(QString("Content-Type"), QString("application/json")));
    toolHeaders.append(qMakePair(QString("Accept"), QString("application/json")));
    toolHeaders.append(qMakePair(QString("Mcp-Session-Id"), sessionId));
    if (!apiKey.isEmpty()) {
        toolHeaders.append(qMakePair(QString("Authorization"), QString("Bearer %1").arg(apiKey)));
    }

    QString toolError;
    QByteArray output = executeHttpRequest(url, "POST", toolBody, toolHeaders, 15000, &toolError);
    if (output.isEmpty()) {
        qCWarning(log_ai_chat) << "Parallel MCP: empty response -" << toolError;
        // Return clear error message to user
        if (toolError.contains("SSL/TLS")) {
            return QString("web_search: %1").arg(toolError);
        }
        return "web_search: failed to fetch results from Parallel";
    }


    QJsonDocument responseDoc = QJsonDocument::fromJson(output);
    if (responseDoc.isNull() || !responseDoc.isObject()) {
        qCWarning(log_ai_chat) << "Parallel MCP: failed to parse response";
        return "web_search: failed to parse Parallel response";
    }

    QJsonObject root = responseDoc.object();

    // Check for JSON-RPC errors
    if (root.contains("error")) {
        QJsonObject error = root["error"].toObject();
        QString errorMsg = error["message"].toString();
        qCWarning(log_ai_chat) << "Parallel MCP: error:" << errorMsg;
        return QString("web_search: Parallel error - %1").arg(errorMsg);
    }

    // Parse the result
    QJsonObject result = root["result"].toObject();
    QJsonArray content = result["content"].toArray();

    if (content.isEmpty()) {
        return "web_search: no results found";
    }

    // The content is an array with a single text element containing JSON
    QString resultText = content[0].toObject()["text"].toString();
    if (resultText.isEmpty()) {
        return "web_search: no results found";
    }

    // Parse the search results JSON
    QJsonDocument searchResultDoc = QJsonDocument::fromJson(resultText.toUtf8());
    if (searchResultDoc.isNull() || !searchResultDoc.isObject()) {
        qCWarning(log_ai_chat) << "Parallel MCP: failed to parse search results";
        return "web_search: failed to parse search results";
    }

    QJsonObject searchResult = searchResultDoc.object();
    QJsonArray resultsArr = searchResult["results"].toArray();

    if (resultsArr.isEmpty()) {
        return "web_search: no results found";
    }


    // Format the results
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

            // Add excerpts (usually 1-2 per result)
            for (const auto &excerptVal : excerpts) {
                QString excerpt = excerptVal.toString();
                // Clean up markdown formatting
                excerpt.replace("\\\"", "\"");
                excerpt.replace("\\n", " ");
                // Truncate long excerpts
                if (excerpt.length() > 500) {
                    excerpt = excerpt.left(497) + "...";
                }
                results << excerpt;
            }

            if (!url.isEmpty()) {
                results << QString("Source: %1").arg(url);
            }
            results << ""; // blank line between results
            count++;
        }
    }

    if (results.isEmpty()) {
        return "web_search: no results found";
    }

    QString result_str = results.join("\n").trimmed();
    if (result_str.length() > 4096) {
        result_str = result_str.left(4096) + "\n[results truncated at 4096 chars]";
    }

    return result_str;
}
