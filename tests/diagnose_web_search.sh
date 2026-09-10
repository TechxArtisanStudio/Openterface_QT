#!/bin/bash
# Comprehensive diagnostic for web search providers

echo "=========================================="
echo "Web Search Provider Diagnostic"
echo "=========================================="
echo ""

# Test 1: Exa MCP with simple "open code" query
echo "Test 1: Exa MCP - 'open code' query"
echo "--------------------------------------"
echo ""

# Initialize
INIT=$(curl -s -X POST https://mcp.exa.ai/mcp \
  -H "Content-Type: application/json" \
  -H "Accept: application/json, text/event-stream" \
  -d '{"jsonrpc":"2.0","method":"initialize","params":{"protocolVersion":"2024-11-05","capabilities":{},"clientInfo":{"name":"test","version":"1.0"}},"id":1}')

if [ -z "$INIT" ]; then
    echo "❌ FAILED: Initialization returned empty"
    exit 1
fi

echo "✓ Initialization successful"
echo ""

# Search with enhanced query (like the code does)
echo "Searching with enhanced query: 'information about open code software or technology'"
SEARCH=$(curl -s -X POST https://mcp.exa.ai/mcp \
  -H "Content-Type: application/json" \
  -H "Accept: application/json, text/event-stream" \
  -d '{
    "jsonrpc":"2.0",
    "method":"tools/call",
    "params":{
      "name":"web_search_exa",
      "arguments":{
        "query":"information about open code software or technology",
        "numResults":5
      }
    },
    "id":2
  }')

if [ -z "$SEARCH" ]; then
    echo "❌ FAILED: Search returned empty"
    exit 1
fi

# Check if it contains OpenCode
if echo "$SEARCH" | grep -q "OpenCode"; then
    echo "✓ SUCCESS: Found OpenCode in results"
    echo ""
    echo "Result preview (first 500 chars):"
    echo "$SEARCH" | head -c 500
    echo ""
else
    echo "❌ FAILED: OpenCode not found in results"
    echo ""
    echo "Result preview:"
    echo "$SEARCH" | head -c 500
    echo ""
fi

echo ""
echo "=========================================="
echo "Test 2: Parallel MCP - 'open code' query"
echo "=========================================="
echo ""

# Initialize Parallel
INIT_P=$(curl -s -X POST https://search.parallel.ai/mcp \
  -H "Content-Type: application/json" \
  -H "Accept: application/json" \
  -d '{"jsonrpc":"2.0","method":"initialize","params":{"protocolVersion":"2024-11-05","capabilities":{},"clientInfo":{"name":"test","version":"1.0"}},"id":1}')

if [ -z "$INIT_P" ]; then
    echo "❌ FAILED: Parallel initialization returned empty"
else
    echo "✓ Parallel initialization successful"
fi

echo ""
echo "Searching for 'open code'..."
SEARCH_P=$(curl -s -X POST https://search.parallel.ai/mcp \
  -H "Content-Type: application/json" \
  -H "Accept: application/json" \
  -H "Mcp-Session-Id: test-diagnostic-123" \
  -d '{
    "jsonrpc":"2.0",
    "method":"tools/call",
    "params":{
      "name":"web_search",
      "arguments":{
        "objective":"Find information about open source code and open-source software",
        "search_queries":["open code","open source software","open source code"],
        "session_id":"test-diagnostic-123",
        "model_name":"claude-opus-4.7"
      }
    },
    "id":2
  }')

if [ -z "$SEARCH_P" ]; then
    echo "❌ FAILED: Parallel search returned empty"
    exit 1
fi

# Check if it contains OpenCode
if echo "$SEARCH_P" | grep -q "OpenCode\|opencode"; then
    echo "✓ SUCCESS: Found OpenCode in Parallel results"
    echo ""
    echo "Result preview (first 500 chars):"
    echo "$SEARCH_P" | head -c 500
    echo ""
else
    echo "❌ FAILED: OpenCode not found in Parallel results"
    echo ""
    echo "Result preview:"
    echo "$SEARCH_P" | head -c 500
    echo ""
fi

echo ""
echo "=========================================="
echo "Diagnostic Complete"
echo "=========================================="
