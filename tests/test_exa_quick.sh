#!/bin/bash
# Quick test for Exa MCP

echo "Testing Exa MCP..."
echo ""

# Initialize
echo "1. Initializing MCP session..."
INIT_RESPONSE=$(curl -s -X POST https://mcp.exa.ai/mcp \
  -H "Content-Type: application/json" \
  -H "Accept: application/json, text/event-stream" \
  -d '{"jsonrpc":"2.0","method":"initialize","params":{"protocolVersion":"2024-11-05","capabilities":{},"clientInfo":{"name":"test","version":"1.0"}},"id":1}')

if [ $? -eq 0 ] && [ -n "$INIT_RESPONSE" ]; then
    echo "✓ Initialization successful"
else
    echo "✗ Initialization failed"
    exit 1
fi

echo ""
echo "2. Testing web search for 'open code'..."
SEARCH_RESPONSE=$(curl -s -X POST https://mcp.exa.ai/mcp \
  -H "Content-Type: application/json" \
  -H "Accept: application/json, text/event-stream" \
  -d '{
    "jsonrpc":"2.0",
    "method":"tools/call",
    "params":{
      "name":"web_search_exa",
      "arguments":{
        "query":"open code",
        "numResults":3
      }
    },
    "id":2
  }')

if [ $? -eq 0 ] && [ -n "$SEARCH_RESPONSE" ]; then
    echo "✓ Search request successful"
    echo ""
    echo "Response (first 500 chars):"
    echo "$SEARCH_RESPONSE" | head -c 500
    echo ""
else
    echo "✗ Search failed"
    exit 1
fi

echo ""
echo "✅ Exa MCP test completed successfully"
