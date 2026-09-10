/*
 * Test Web Search Manager - Tests the actual WebSearchManager implementation
 */

#include <QCoreApplication>
#include <QString>
#include <iostream>
#include "../ai/WebSearchManager.h"
#include "../ui/globalsetting.h"

#define LOG(msg) std::cout << msg << std::endl
#define LOG_QSTRING(msg) std::cout << (msg).toStdString() << std::endl

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    LOG("");
    LOG("========================================");
    LOG("Testing WebSearchManager Implementation");
    LOG("========================================");
    LOG("");

    WebSearchManager &manager = WebSearchManager::instance();

    // Show configured providers
    LOG("Configured providers:");
    QStringList providerIds = manager.configuredProviderIds();
    for (const QString &id : providerIds) {
        LOG_QSTRING(QString("  - %1").arg(id));
    }
    LOG("");

    // Test case 1: "open code" - the user's failing case
    LOG("--- Test 1: Search for 'open code' ---");
    QString result1 = manager.search("open code");
    LOG("Result:");
    std::cout << result1.left(800).toStdString() << std::endl;
    LOG("");
    std::cout << "Status: " << (result1.startsWith("web_search:") ? "FAILED" : "SUCCESS") << std::endl;
    LOG("");

    // Test case 2: "Python programming"
    LOG("--- Test 2: Search for 'Python programming' ---");
    QString result2 = manager.search("Python programming");
    LOG("Result:");
    std::cout << result2.left(800).toStdString() << std::endl;
    LOG("");
    std::cout << "Status: " << (result2.startsWith("web_search:") ? "FAILED" : "SUCCESS") << std::endl;
    LOG("");

    // Test case 3: "OpenAI"
    LOG("--- Test 3: Search for 'OpenAI' ---");
    QString result3 = manager.search("OpenAI");
    LOG("Result:");
    std::cout << result3.left(800).toStdString() << std::endl;
    LOG("");
    std::cout << "Status: " << (result3.startsWith("web_search:") ? "FAILED" : "SUCCESS") << std::endl;
    LOG("");

    // Test case 4: "machine learning"
    LOG("--- Test 4: Search for 'machine learning' ---");
    QString result4 = manager.search("machine learning");
    LOG("Result:");
    std::cout << result4.left(800).toStdString() << std::endl;
    LOG("");
    std::cout << "Status: " << (result4.startsWith("web_search:") ? "FAILED" : "SUCCESS") << std::endl;
    LOG("");

    // Test case 5: Empty query
    LOG("--- Test 5: Empty query (should fail) ---");
    QString result5 = manager.search("");
    LOG("Result:");
    std::cout << result5.toStdString() << std::endl;
    LOG("");
    std::cout << "Status: " << (result5.startsWith("web_search: missing") ? "EXPECTED FAILURE" : "UNEXPECTED SUCCESS") << std::endl;
    LOG("");

    LOG("========================================");
    LOG("Tests completed");
    LOG("========================================");

    return 0;
}
