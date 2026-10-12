/*
 * Test All Web Search Providers - Tests DuckDuckGo, Wikipedia, Exa, and Parallel
 */

#include <QCoreApplication>
#include <QProcess>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrl>
#include <QStringList>
#include <iostream>
#include "../ai/WebSearchManager.h"
#include "../ai/WebSearchProviders.h"
#include "../ui/globalsetting.h"

#define LOG(msg) std::cout << msg << std::endl
#define LOG_QSTRING(msg) std::cout << (msg).toStdString() << std::endl

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    LOG("");
    LOG("========================================");
    LOG("Testing All Web Search Providers");
    LOG("========================================");
    LOG("");

    WebSearchManager &manager = WebSearchManager::instance();

    // Show configured providers
    LOG("Configured provider IDs:");
    QStringList providerIds = manager.configuredProviderIds();
    for (const QString &id : providerIds) {
        LOG_QSTRING(QString("  - %1").arg(id));
    }
    LOG("");

    // Show available providers and their configuration status
    LOG("Available providers:");
    QList<WebSearchProvider*> providers = manager.availableProviders();
    for (auto *provider : providers) {
        QString status = provider->isConfigured() ? "CONFIGURED" : "NOT CONFIGURED";
        LOG_QSTRING(QString("  - %1 (%2) [%3]").arg(provider->name(), provider->id(), status));
    }
    LOG("");

    // Check API keys
    LOG("API Key Status:");
    QString exaKey = GlobalSetting::instance().getChatExaApiKey();
    QString parallelKey = GlobalSetting::instance().getChatParallelApiKey();
    LOG_QSTRING(QString("  Exa API Key: %1").arg(exaKey.isEmpty() ? "NOT SET" : "SET"));
    LOG_QSTRING(QString("  Parallel API Key: %1").arg(parallelKey.isEmpty() ? "NOT SET" : "SET"));
    LOG("");

    // Test queries
    QStringList testQueries;
    testQueries << "open code"
                << "Python programming"
                << "OpenAI"
                << "machine learning";

    // Test each provider individually
    for (auto *provider : providers) {
        LOG(QString("========================================").toUtf8().constData());
        LOG_QSTRING(QString("Testing Provider: %1 (%2)").arg(provider->name(), provider->id()));
        LOG(QString("========================================").toUtf8().constData());

        if (!provider->isConfigured()) {
            LOG("Provider not configured, skipping tests");
            LOG("");
            continue;
        }

        for (const QString &query : testQueries) {
            LOG("");
            LOG_QSTRING(QString("--- Search: '%1' ---").arg(query));

            QString result = provider->search(query);

            LOG("Result (first 800 chars):");
            std::cout << result.left(800).toStdString() << std::endl;
            LOG("");

            if (result.startsWith("web_search:")) {
                std::cout << "Status: FAILED" << std::endl;
            } else {
                std::cout << "Status: SUCCESS" << std::endl;
            }
            LOG("");
        }
    }

    // Test WebSearchManager fallback chain
    LOG("========================================");
    LOG("Testing WebSearchManager Fallback Chain");
    LOG("========================================");
    LOG("");

    for (const QString &query : testQueries) {
        LOG("");
        LOG_QSTRING(QString("--- Manager Search: '%1' ---").arg(query));

        QString result = manager.search(query);

        LOG("Result (first 800 chars):");
        std::cout << result.left(800).toStdString() << std::endl;
        LOG("");

        if (result.startsWith("web_search:")) {
            std::cout << "Status: FAILED" << std::endl;
        } else {
            std::cout << "Status: SUCCESS" << std::endl;
        }
        LOG("");
    }

    LOG("========================================");
    LOG("All tests completed");
    LOG("========================================");

    return 0;
}
