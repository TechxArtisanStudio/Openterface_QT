# Web Search Unit Tests

This directory contains comprehensive unit tests for the AI chat web search features.

## Test Files

### 1. `test_web_search_standalone.sh` (✅ Recommended)
**Standalone bash-based test suite** that tests all web search providers with real HTTP requests.

**What it tests:**
- ✅ Exa MCP endpoint initialization
- ✅ Parallel MCP endpoint initialization  
- ✅ DuckDuckGo Instant Answer API
- ✅ Wikipedia OpenSearch API
- ✅ SSL/TLS configuration
- ✅ Qt TLS plugin availability

**How to run:**
```bash
cd tests
./test_web_search_standalone.sh
```

**Expected output:**
```
==========================================
Web Search Provider Tests
==========================================

Test 1: Exa MCP initialization
✓ PASS: Exa MCP initialization works

Test 2: Parallel MCP initialization
✓ PASS: Parallel MCP initialization works

Test 3: DuckDuckGo Instant Answer API
✓ PASS: DuckDuckGo API works

Test 4: Wikipedia OpenSearch API
✓ PASS: Wikipedia API works

Test 5: SSL/TLS configuration
✓ PASS: HTTPS connections work (TLS configured)

Test 6: Qt TLS plugins
✓ PASS: Qt TLS plugin exists

==========================================
Test Summary
==========================================
Passed: 6
Failed: 0

All tests passed!
```

**When to use:**
- Quick verification that all providers work
- CI/CD pipeline testing
- Diagnosing TLS/SSL issues
- Verifying anonymous access to Exa and Parallel

---

### 2. `test_web_search_providers.cpp` (Qt Test Framework)
**Qt-based unit test suite** using the Qt Test framework.

**What it tests:**
- Individual provider functionality (DuckDuckGo, Wikipedia, Exa, Parallel)
- WebSearchManager singleton pattern
- Provider order and fallback chain
- Empty query handling
- SSL error detection
- Provider configuration
- Integration tests with real searches

**How to build and run:**
```bash
cd tests/build
cmake ..
make test_web_search_providers
./test_web_search_providers
```

**Note:** This test requires compiling the actual WebSearchProvider classes and may have dependency issues. Use the standalone bash test for quick verification.

---

### 3. `test_web_search_manager.cpp` (Simple Integration Test)
**Simple standalone test** that exercises the WebSearchManager with real searches.

**What it tests:**
- WebSearchManager instance creation
- Provider configuration
- Real search queries:
  - "open code" (the original failing case)
  - "Python programming"
  - "OpenAI"
  - "machine learning"
- Empty query error handling

**How to build and run:**
```bash
cd tests
g++ -std=c++17 -fPIC test_web_search_manager.cpp \
    ../ai/WebSearchManager.cpp \
    ../ai/WebSearchProviders.cpp \
    ../ui/globalsetting.cpp \
    -I.. -I../ai -I../ui \
    $(pkg-config --cflags --libs Qt6Core Qt6Network) \
    -o test_web_search_manager

./test_web_search_manager
```

---

## Test Coverage

### Provider Tests
| Provider | Endpoint | Anonymous | Protocol | Test Status |
|----------|----------|-----------|----------|-------------|
| Exa AI | `https://mcp.exa.ai/mcp` | ✅ Yes | MCP + SSE | ✅ Passing |
| Parallel AI | `https://search.parallel.ai/mcp` | ✅ Yes | MCP + JSON | ✅ Passing |
| DuckDuckGo | `https://api.duckduckgo.com/` | ✅ Yes | REST JSON | ✅ Passing |
| Wikipedia | `https://en.wikipedia.org/w/api.php` | ✅ Yes | REST JSON | ✅ Passing |

### Feature Tests
- ✅ Multi-provider fallback chain
- ✅ TLS/SSL configuration
- ✅ Anonymous access (no API keys required)
- ✅ MCP protocol support
- ✅ Error handling
- ✅ Provider configuration

---

## Running All Tests

### Quick Test (Recommended)
```bash
cd /home/bbot/projects/Openterface/Openterface_QT/tests
./test_web_search_standalone.sh
```

### Full Integration Test
1. Start the application:
   ```bash
   cd /home/bbot/projects/Openterface/Openterface_QT/build
   ./openterfaceQT-launcher.sh
   ```

2. Open AI Chat and try:
   - "search for opencode CLI"
   - "what is machine learning"
   - "find information about Python programming"

3. Check logs for successful searches:
   ```bash
   tail -f /tmp/openterfaceqt-app-*.log | grep "WebSearchManager"
   ```

---

## Troubleshooting

### Test Fails: "HTTPS connections failed (TLS issue)"

**Problem:** TLS backend not configured

**Solution:**
```bash
# Check launcher has TLS config
grep "QT_TLS_BACKEND" ../build-script/openterfaceQT-local-launcher.sh

# Should show:
# export QT_TLS_BACKEND="openssl"
```

### Test Fails: "Qt TLS plugin not found"

**Problem:** TLS plugins not installed

**Solution:**
```bash
# Fedora/RHEL
sudo dnf install qt6-qtbase

# Ubuntu/Debian
sudo apt install qt6-base-dev

# Verify plugin exists
ls /usr/lib64/qt6/plugins/tls/libqopensslbackend.so
```

### Test Fails: "Exa MCP initialization failed"

**Problem:** Network issue or Exa endpoint down

**Solution:**
```bash
# Test manually
curl -s -X POST "https://mcp.exa.ai/mcp" \
  -H "Content-Type: application/json" \
  -H "Accept: application/json, text/event-stream" \
  -d '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2024-11-05","capabilities":{},"clientInfo":{"name":"test","version":"1.0"}}}'

# Should return JSON with "protocolVersion"
```

---

## Adding New Tests

### To `test_web_search_standalone.sh`:

```bash
# Test N: Your test name
echo ""
echo "Test N: Your test name"
RESPONSE=$(curl -s --max-time 10 "YOUR_URL" 2>&1 || echo "CURL_FAILED")

if [ "$RESPONSE" != "CURL_FAILED" ] && echo "$RESPONSE" | grep -q "EXPECTED_PATTERN"; then
    pass "Your test works"
else
    fail "Your test failed"
    echo "Response: $(echo "$RESPONSE" | head -c 200)"
fi
```

### To Qt test suite:

```cpp
void TestWebSearch::testYourFeature()
{
    // Your test code
    QString result = manager->search("your query");
    QVERIFY(!result.isEmpty());
    QVERIFY(result.contains("expected content"));
}
```

Don't forget to add the test method to the class declaration in the header section.

---

## Test Results

### Latest Test Run (2026-09-02)
```
==========================================
Test Summary
==========================================
Passed: 6
Failed: 0

All tests passed!
```

**Provider Status:**
- ✅ Exa AI: Working (anonymous access)
- ✅ Parallel AI: Working (anonymous access)
- ✅ DuckDuckGo: Working
- ✅ Wikipedia: Working

**TLS Status:**
- ✅ HTTPS connections work
- ✅ Qt TLS plugins available
- ✅ QT_TLS_BACKEND=openssl configured

---

## Documentation

See also:
- `../docs/ai_chat_web_search_tool.md` - Full web search documentation
- `TLS_FIX_APPLIED.md` - TLS configuration details
- `SSL_ERROR_DETECTION.md` - SSL error handling
- `WEB_SEARCH_IMPLEMENTATION_SUMMARY.md` - Implementation summary
