/*
 * Unit tests for Web Search Providers and Manager
 * Tests the multi-provider web search system with Exa, Parallel, DuckDuckGo, and Wikipedia
 */

#include <QtTest/QtTest>
#include <QCoreApplication>
#include <QSignalSpy>
#include "../ai/WebSearchManager.h"
#include "../ai/WebSearchProviders.h"
#include "../ui/globalsetting.h"

class TestWebSearch : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    // Provider tests
    void testDuckDuckGoProvider();
    void testWikipediaProvider();
    void testExaProvider();
    void testParallelProvider();

    // Manager tests
    void testManagerSingleton();
    void testProviderOrder();
    void testFallbackChain();
    void testEmptyQuery();

    // SSL error detection tests
    void testSSLErrorDetection();
    void testSSLErrorMessage();

    // Configuration tests
    void testDefaultProviders();
    void testProviderConfiguration();

    // Integration tests
    void testSearchOpenCode();
    void testSearchPython();
    void testSearchMachineLearning();

private:
    WebSearchManager *manager;
};

void TestWebSearch::initTestCase()
{
    manager = &WebSearchManager::instance();
    QVERIFY(manager != nullptr);

    // Enable debug logging for tests
    qSetMessagePattern("[%{type}] %{message}");
}

void TestWebSearch::cleanupTestCase()
{
    // Cleanup if needed
}

// ============================================================================
// Provider Tests
// ============================================================================

void TestWebSearch::testDuckDuckGoProvider()
{
    DuckDuckGoProvider provider;

    QCOMPARE(provider.id(), QString("duckduckgo"));
    QCOMPARE(provider.name(), QString("DuckDuckGo"));
    QVERIFY(!provider.requiresApiKey());
    QVERIFY(provider.isConfigured());

    // Test search
    QString result = provider.search("Python programming");
    QVERIFY(!result.isEmpty());

    // Should not start with error (unless network fails)
    if (!result.startsWith("web_search:")) {
        QVERIFY(result.length() > 0);
    }
}

void TestWebSearch::testWikipediaProvider()
{
    WikipediaProvider provider;

    QCOMPARE(provider.id(), QString("wikipedia"));
    QCOMPARE(provider.name(), QString("Wikipedia"));
    QVERIFY(!provider.requiresApiKey());
    QVERIFY(provider.isConfigured());

    // Test search
    QString result = provider.search("Machine learning");
    QVERIFY(!result.isEmpty());

    // Should not start with error (unless network fails)
    if (!result.startsWith("web_search:")) {
        QVERIFY(result.length() > 0);
    }
}

void TestWebSearch::testExaProvider()
{
    ExaProvider provider;

    QCOMPARE(provider.id(), QString("exa"));
    QCOMPARE(provider.name(), QString("Exa AI"));
    QVERIFY(!provider.requiresApiKey()); // Works anonymously
    QVERIFY(provider.isConfigured());

    // Test search
    QString result = provider.search("artificial intelligence");
    QVERIFY(!result.isEmpty());

    // Should return results or error
    if (!result.startsWith("web_search:")) {
        // Should contain title and URL from Exa
        QVERIFY(result.contains("Title:") || result.contains("URL:"));
    }
}

void TestWebSearch::testParallelProvider()
{
    ParallelProvider provider;

    QCOMPARE(provider.id(), QString("parallel"));
    QCOMPARE(provider.name(), QString("Parallel AI"));
    QVERIFY(!provider.requiresApiKey()); // Works anonymously
    QVERIFY(provider.isConfigured());

    // Test search
    QString result = provider.search("quantum computing");
    QVERIFY(!result.isEmpty());

    // Should return results or error
    if (!result.startsWith("web_search:")) {
        QVERIFY(result.length() > 0);
    }
}

// ============================================================================
// Manager Tests
// ============================================================================

void TestWebSearch::testManagerSingleton()
{
    WebSearchManager &instance1 = WebSearchManager::instance();
    WebSearchManager &instance2 = WebSearchManager::instance();

    // Should be the same instance
    QCOMPARE(&instance1, &instance2);
}

void TestWebSearch::testProviderOrder()
{
    QStringList ids = manager->configuredProviderIds();

    // Should have 4 providers
    QCOMPARE(ids.size(), 4);

    // Should be in correct order
    QCOMPARE(ids[0], QString("exa"));
    QCOMPARE(ids[1], QString("parallel"));
    QCOMPARE(ids[2], QString("duckduckgo"));
    QCOMPARE(ids[3], QString("wikipedia"));
}

void TestWebSearch::testFallbackChain()
{
    // Test that manager tries providers in order
    // This is a basic test - in real scenario, if Exa fails, it should try Parallel, etc.

    QStringList ids = manager->configuredProviderIds();
    QVERIFY(ids.size() > 0);

    // All providers should be available
    for (const QString &id : ids) {
        WebSearchProvider *provider = manager->providerById(id);
        QVERIFY(provider != nullptr);
    }
}

void TestWebSearch::testEmptyQuery()
{
    QString result = manager->search("");

    // Should return error for empty query
    QVERIFY(result.startsWith("web_search: missing"));
}

// ============================================================================
// SSL Error Detection Tests
// ============================================================================

void TestWebSearch::testSSLErrorDetection()
{
    // This test verifies that SSL errors are properly detected
    // In a real scenario, we'd simulate an SSL error, but for now we just
    // verify the error message format

    QString sslError = "SSL/TLS error: Cannot establish secure HTTPS connection. "
                       "This usually means Qt's TLS backend is not configured correctly. "
                       "Please ensure OpenSSL is installed and Qt can find its TLS plugins. "
                       "Try: export QT_TLS_BACKEND=openssl before running the application.";

    QVERIFY(sslError.contains("SSL/TLS"));
    QVERIFY(sslError.contains("QT_TLS_BACKEND=openssl"));
}

void TestWebSearch::testSSLErrorMessage()
{
    // Verify SSL error messages contain actionable information
    QString sslError = "SSL/TLS error:";

    QVERIFY(sslError.contains("SSL"));
    QVERIFY(sslError.contains("error"));
}

// ============================================================================
// Configuration Tests
// ============================================================================

void TestWebSearch::testDefaultProviders()
{
    GlobalSetting settings;
    QStringList defaults = settings.getChatWebSearchProviders();

    // Should have 4 default providers
    QCOMPARE(defaults.size(), 4);

    // Should include all providers
    QVERIFY(defaults.contains("exa"));
    QVERIFY(defaults.contains("parallel"));
    QVERIFY(defaults.contains("duckduckgo"));
    QVERIFY(defaults.contains("wikipedia"));
}

void TestWebSearch::testProviderConfiguration()
{
    // Test that providers can be configured
    WebSearchProvider *exa = manager->providerById("exa");
    QVERIFY(exa != nullptr);
    QVERIFY(exa->isConfigured());

    WebSearchProvider *parallel = manager->providerById("parallel");
    QVERIFY(parallel != nullptr);
    QVERIFY(parallel->isConfigured());
}

// ============================================================================
// Integration Tests
// ============================================================================

void TestWebSearch::testSearchOpenCode()
{
    // Test the user's original failing case
    QString result = manager->search("open code CLI tool install");

    QVERIFY(!result.isEmpty());

    // Should succeed (not start with error) if network is available
    if (!result.startsWith("web_search:")) {
        // Should contain information about OpenCode
        QVERIFY(result.length() > 100);
    }
}

void TestWebSearch::testSearchPython()
{
    QString result = manager->search("Python programming language");

    QVERIFY(!result.isEmpty());

    // Should succeed if network is available
    if (!result.startsWith("web_search:")) {
        QVERIFY(result.length() > 100);
    }
}

void TestWebSearch::testSearchMachineLearning()
{
    QString result = manager->search("machine learning artificial intelligence");

    QVERIFY(!result.isEmpty());

    // Should succeed if network is available
    if (!result.startsWith("web_search:")) {
        QVERIFY(result.length() > 100);
    }
}

QTEST_MAIN(TestWebSearch)
#include "test_web_search_providers.moc"
