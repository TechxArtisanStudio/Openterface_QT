/*
 * ========================================================================== *
 *    Test Web Search — Standalone test for web search functionality          *
 *                                                                            *
 *    Tests the DuckDuckGo and Wikipedia web search APIs used by the AI chat  *
 *    web_search tool.                                                        *
 *                                                                            *
 *    Run:  ./test_web_search                                                 *
 * ========================================================================== *
 */

#include <QCoreApplication>
#include <QProcess>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrl>
#include <QDebug>
#include <QStringList>

// DuckDuckGo Instant Answer API search
QString webSearchDuckDuckGo(const QString &query)
{
    if (query.isEmpty()) {
        return "web_search: missing query argument";
    }

    qDebug() << "web_search: searching DuckDuckGo for" << query;

    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);

    QString encodedQuery = QUrl::toPercentEncoding(query);
    QString url = QString("https://api.duckduckgo.com/?q=%1&format=json&no_html=1&skip_disambig=1")
                  .arg(encodedQuery);

    qDebug() << "web_search: fetching URL" << url;

    process.start("curl", QStringList() << "-s" << url);

    if (!process.waitForStarted(5000)) {
        return QString("web_search: failed to start curl - %1").arg(process.errorString());
    }

    if (!process.waitForFinished(15000)) {
        process.kill();
        return "web_search: request timed out after 15s";
    }

    QByteArray output = process.readAll();
    int exitCode = process.exitCode();

    qDebug() << "web_search: curl exit code" << exitCode << "output length" << output.length();

    if (exitCode != 0) {
        return QString("web_search: curl failed with exit code %1").arg(exitCode);
    }

    if (output.isEmpty()) {
        return "web_search: empty response from server";
    }

    QJsonDocument doc = QJsonDocument::fromJson(output);
    if (doc.isNull()) {
        qDebug() << "web_search: raw response:" << QString::fromUtf8(output).left(500);
        return "web_search: failed to parse response";
    }

    QJsonObject root = doc.object();
    QStringList results;

    if (root.contains("Abstract")) {
        QString abstract = root["Abstract"].toString();
        if (!abstract.isEmpty()) {
            results << QString("Answer: %1").arg(abstract);
        }
    }

    if (root.contains("AbstractText")) {
        QString abstractText = root["AbstractText"].toString();
        if (!abstractText.isEmpty() && abstractText != root["Abstract"].toString()) {
            results << QString("Summary: %1").arg(abstractText);
        }
    }

    if (root.contains("Definition")) {
        QString definition = root["Definition"].toString();
        if (!definition.isEmpty()) {
            results << QString("Definition: %1").arg(definition);
        }
    }

    if (root.contains("Answer")) {
        QString answer = root["Answer"].toString();
        if (!answer.isEmpty()) {
            results << QString("Direct Answer: %1").arg(answer);
        }
    }

    if (root.contains("RelatedTopics")) {
        QJsonArray topics = root["RelatedTopics"].toArray();
        qDebug() << "web_search: found" << topics.count() << "related topics";
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
        qDebug() << "web_search: no results found for query:" << query;
        return "web_search: no results found";
    }

    QString result = results.join("\n");
    qDebug() << "web_search: returning" << result.length() << "characters of results";
    return result;
}

// Wikipedia API search (fallback)
QString webSearchWikipedia(const QString &query)
{
    if (query.isEmpty()) {
        return "web_search: missing query argument";
    }

    qDebug() << "web_search: searching Wikipedia for" << query;

    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);

    QString encodedQuery = QUrl::toPercentEncoding(query);
    QString searchUrl = QString("https://en.wikipedia.org/w/api.php?action=opensearch&search=%1&limit=3&format=json")
                        .arg(encodedQuery);

    qDebug() << "web_search: searching Wikipedia with URL" << searchUrl;

    process.start("curl", QStringList() << "-s" << searchUrl);

    if (!process.waitForStarted(5000)) {
        return QString("web_search: failed to start curl - %1").arg(process.errorString());
    }

    if (!process.waitForFinished(15000)) {
        process.kill();
        return "web_search: request timed out after 15s";
    }

    QByteArray output = process.readAll();
    int exitCode = process.exitCode();

    if (exitCode != 0 || output.isEmpty()) {
        return "web_search: no results found";
    }

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
        qDebug() << "web_search: no Wikipedia articles found for:" << query;
        return "web_search: no results found";
    }

    QString firstTitle = titles[0].toString();
    QString encodedTitle = QUrl::toPercentEncoding(firstTitle);
    QString summaryUrl = QString("https://en.wikipedia.org/api/rest_v1/page/summary/%1")
                         .arg(encodedTitle);

    qDebug() << "web_search: fetching Wikipedia summary from" << summaryUrl;

    QProcess summaryProcess;
    summaryProcess.setProcessChannelMode(QProcess::MergedChannels);

    summaryProcess.start("curl", QStringList() << "-s" << summaryUrl);

    if (!summaryProcess.waitForStarted(5000)) {
        return "web_search: no results found";
    }

    if (!summaryProcess.waitForFinished(15000)) {
        summaryProcess.kill();
        return "web_search: no results found";
    }

    QByteArray summaryOutput = summaryProcess.readAll();
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

    QStringList results;
    results << QString("From Wikipedia: %1").arg(title);
    results << extract;

    if (titles.size() > 1) {
        results << "\nRelated articles:";
        for (int i = 1; i < qMin(titles.size(), 4); ++i) {
            results << QString("- %1: %2").arg(titles[i].toString(), urls[i].toString());
        }
    }

    QString result = results.join("\n");
    qDebug() << "web_search: Wikipedia returning" << result.length() << "characters";
    return result;
}

// Main web search with fallback
QString webSearch(const QString &query)
{
    QString ddgResult = webSearchDuckDuckGo(query);
    if (!ddgResult.startsWith("web_search: no results found")) {
        return ddgResult;
    }

    qDebug() << "web_search: DuckDuckGo returned no results, trying Wikipedia";
    return webSearchWikipedia(query);
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    qDebug() << "\n========================================";
    qDebug() << "Testing Web Search Functionality";
    qDebug() << "========================================\n";

    // Test case 1: Well-known topic (should work with DuckDuckGo)
    qDebug() << "\n--- Test 1: Search for 'Python programming' ---";
    QString result1 = webSearch("Python programming");
    qDebug() << "\nResult:";
    qDebug() << result1.left(500) << "...";
    qDebug() << "\nStatus:" << (result1.startsWith("web_search:") ? "FAILED" : "SUCCESS");

    // Test case 2: Current events / news (DuckDuckGo may fail, Wikipedia fallback)
    qDebug() << "\n\n--- Test 2: Search for 'OpenAI' ---";
    QString result2 = webSearch("OpenAI");
    qDebug() << "\nResult:";
    qDebug() << result2.left(500) << "...";
    qDebug() << "\nStatus:" << (result2.startsWith("web_search:") ? "FAILED" : "SUCCESS");

    // Test case 3: The user's test case - "open code"
    qDebug() << "\n\n--- Test 3: Search for 'open code' (user's test case) ---";
    QString result3 = webSearch("open code");
    qDebug() << "\nResult:";
    qDebug() << result3.left(500) << "...";
    qDebug() << "\nStatus:" << (result3.startsWith("web_search:") ? "FAILED" : "SUCCESS");

    // Test case 4: Technical topic
    qDebug() << "\n\n--- Test 4: Search for 'machine learning' ---";
    QString result4 = webSearch("machine learning");
    qDebug() << "\nResult:";
    qDebug() << result4.left(500) << "...";
    qDebug() << "\nStatus:" << (result4.startsWith("web_search:") ? "FAILED" : "SUCCESS");

    // Test case 5: Query that might not have results
    qDebug() << "\n\n--- Test 5: Search for 'xyz123nonexistent' ---";
    QString result5 = webSearch("xyz123nonexistent");
    qDebug() << "\nResult:";
    qDebug() << result5.left(500) << "...";
    qDebug() << "\nStatus:" << (result5.startsWith("web_search:") ? "EXPECTED FAILURE" : "UNEXPECTED SUCCESS");

    qDebug() << "\n\n========================================";
    qDebug() << "Tests completed";
    qDebug() << "========================================";

    return 0;
}
