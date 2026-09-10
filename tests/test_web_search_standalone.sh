#!/bin/bash
# Standalone test for web search providers
# Tests the actual provider implementations with real HTTP requests

echo "=========================================="
echo "Web Search Provider Tests"
echo "=========================================="
echo ""

# Colors for output
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

PASS_COUNT=0
FAIL_COUNT=0

pass() {
    echo -e "${GREEN}✓ PASS${NC}: $1"
    ((PASS_COUNT++))
}

fail() {
    echo -e "${RED}✗ FAIL${NC}: $1"
    ((FAIL_COUNT++))
}

info() {
    echo -e "${YELLOW}ℹ INFO${NC}: $1"
}

# Test 1: Exa MCP endpoint
echo "Test 1: Exa MCP initialization"
RESPONSE=$(curl -s --max-time 10 -X POST "https://mcp.exa.ai/mcp" \
  -H "Content-Type: application/json" \
  -H "Accept: application/json, text/event-stream" \
  -d '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2024-11-05","capabilities":{},"clientInfo":{"name":"test","version":"1.0"}}}' 2>&1 || echo "CURL_FAILED")

if [ "$RESPONSE" != "CURL_FAILED" ] && echo "$RESPONSE" | grep -q "protocolVersion"; then
    pass "Exa MCP initialization works"
else
    fail "Exa MCP initialization failed"
    echo "Response: $(echo "$RESPONSE" | head -c 200)"
fi

# Test 2: Parallel MCP endpoint
echo ""
echo "Test 2: Parallel MCP initialization"
RESPONSE=$(curl -s --max-time 10 -X POST "https://search.parallel.ai/mcp" \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2024-11-05","capabilities":{},"clientInfo":{"name":"test","version":"1.0"}}}' || echo "")

if [ -n "$RESPONSE" ] && echo "$RESPONSE" | grep -q "result"; then
    pass "Parallel MCP initialization works"
else
    fail "Parallel MCP initialization failed"
    echo "Response: $(echo "$RESPONSE" | head -c 200)"
fi

# Test 3: DuckDuckGo API
echo ""
echo "Test 3: DuckDuckGo Instant Answer API"
RESPONSE=$(curl -s --max-time 10 "https://api.duckduckgo.com/?q=Python&format=json&no_html=1" || echo "")

if [ -n "$RESPONSE" ] && [ ${#RESPONSE} -gt 50 ]; then
    pass "DuckDuckGo API works"
else
    fail "DuckDuckGo API failed"
    echo "Response: $(echo "$RESPONSE" | head -c 200)"
fi

# Test 4: Wikipedia API
echo ""
echo "Test 4: Wikipedia OpenSearch API"
RESPONSE=$(curl -s --max-time 10 "https://en.wikipedia.org/w/api.php?action=opensearch&search=Machine+learning&limit=3&format=json" || echo "")

if [ -n "$RESPONSE" ] && echo "$RESPONSE" | grep -q "Machine"; then
    pass "Wikipedia API works"
else
    fail "Wikipedia API failed"
    echo "Response: $(echo "$RESPONSE" | head -c 200)"
fi

# Test 5: SSL/TLS configuration
echo ""
echo "Test 5: SSL/TLS configuration"
HTTP_CODE=$(curl -s --max-time 5 https://mcp.exa.ai/mcp -o /dev/null -w "%{http_code}" 2>&1 || echo "000")
if [ "$HTTP_CODE" != "000" ]; then
    pass "HTTPS connections work (TLS configured)"
else
    fail "HTTPS connections failed (TLS issue)"
fi

# Test 6: Check for TLS plugins
echo ""
echo "Test 6: Qt TLS plugins"
if [ -f "/usr/lib64/qt6/plugins/tls/libqopensslbackend.so" ]; then
    pass "Qt TLS plugin exists"
else
    fail "Qt TLS plugin not found"
fi

# Summary
echo ""
echo "=========================================="
echo "Test Summary"
echo "=========================================="
echo -e "Passed: ${GREEN}$PASS_COUNT${NC}"
echo -e "Failed: ${RED}$FAIL_COUNT${NC}"
echo ""

if [ $FAIL_COUNT -eq 0 ]; then
    echo -e "${GREEN}All tests passed!${NC}"
    exit 0
else
    echo -e "${RED}Some tests failed${NC}"
    exit 1
fi
