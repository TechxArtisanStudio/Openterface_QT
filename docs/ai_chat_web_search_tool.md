# AI Chat Web Search Tool

## Overview
Multi-provider web search system for AI Chat Assistant that allows the AI to search the internet for information when needed. The system uses a fallback chain of providers, starting with AI-optimized services (Exa, Parallel) and falling back to free providers (DuckDuckGo, Wikipedia).

## Architecture

### Provider System
The web search uses a pluggable provider architecture with automatic fallback:

1. **Exa AI** (Primary) - AI-optimized semantic search via MCP
   - Endpoint: `https://mcp.exa.ai/mcp`
   - Protocol: MCP (Model Context Protocol) with SSE responses
   - **Works anonymously** - no API key required for basic access
   - Returns high-quality results with titles, URLs, and highlights
   - Best for technical and software-related queries

2. **Parallel AI** (Secondary) - AI-optimized search via MCP
   - Endpoint: `https://search.parallel.ai/mcp`
   - Protocol: MCP with JSON responses
   - **Works anonymously** - no API key required
   - Good alternative when Exa fails

3. **DuckDuckGo** (Fallback) - Instant Answer API
   - Endpoint: `https://api.duckduckgo.com/?q={query}&format=json`
   - Free, no API key required
   - Returns instant answers and related topics
   - Limited to "instant answer" style results

4. **Wikipedia** (Fallback) - Encyclopedia search
   - Endpoint: `https://en.wikipedia.org/w/api.php?action=opensearch`
   - Free, no API key required
   - Returns article titles and summaries
   - Good for general knowledge topics

### Provider Order
Default fallback chain: `exa → parallel → duckduckgo → wikipedia`

Configured in: `ui/globalsetting.cpp` → `getChatWebSearchProviders()`

### MCP Protocol
Exa and Parallel use the Model Context Protocol (MCP):
- JSON-RPC 2.0 based
- Two-step process: `initialize` → `tools/call`
- Exa uses Server-Sent Events (SSE) for responses
- Parallel uses standard JSON responses

## Implementation Details

### Core Files

1. **ai/WebSearchProvider.h** - Base provider interface
   ```cpp
   class WebSearchProvider {
       virtual QString name() const = 0;
       virtual QString id() const = 0;
       virtual bool requiresApiKey() const = 0;
       virtual bool isConfigured() const = 0;
       virtual QString search(const QString &query) const = 0;
   };
   ```

2. **ai/WebSearchProviders.h/cpp** - Concrete providers
   - `DuckDuckGoProvider` - Free instant answers
   - `WikipediaProvider` - Free encyclopedia search
   - `ExaProvider` - AI-optimized MCP search (anonymous)
   - `ParallelProvider` - AI-optimized MCP search (anonymous)

3. **ai/WebSearchManager.h/cpp** - Provider orchestration
   - Singleton managing provider registry
   - Executes searches through fallback chain
   - Returns first successful result
   - Propagates SSL errors immediately

4. **ai/ChatToolExecution.cpp** - Integration point
   - Calls `WebSearchManager::instance().search(query)`
   - Tool IDs: `web_search`, `search`, `internet_search`

### HTTP Request Helpers

Two helper functions handle curl execution with SSL error detection:

```cpp
static QByteArray executeCurl(const QString &url, 
                               const QStringList &extraArgs = QStringList(),
                               int timeoutMs = 15000, 
                               QString *errorMessage = nullptr);

static QByteArray executeHttpRequest(const QString &url, 
                                      const QString &method,
                                      const QByteArray &body,
                                      const QList<QPair<QString, QString>> &headers,
                                      int timeoutMs = 15000, 
                                      QString *errorMessage = nullptr);
```

**Key features:**
- Use `QProcess::SeparateChannels` to read stdout/stderr separately
- Detect SSL/TLS errors in stderr
- Return clear error messages for TLS configuration issues
- 15-second timeout by default

### SSL/TLS Error Detection

When HTTPS connections fail due to TLS configuration issues, users see:

```
web_search: SSL/TLS error: Cannot establish secure HTTPS connection. 
This usually means Qt's TLS backend is not configured correctly. 
Please ensure OpenSSL is installed and Qt can find its TLS plugins. 
Try: export QT_TLS_BACKEND=openssl before running the application.
```

Instead of generic "no results found" errors.

## TLS/SSL Configuration

### Launcher Script Requirements

The launcher script (`build-script/openterfaceQT-local-launcher.sh`) must configure TLS:

```bash
# Qt plugin paths - include both lib and lib64 for TLS support
export QT_PLUGIN_PATH="/usr/lib/qt6/plugins:/usr/lib64/qt6/plugins"

# Ensure Qt can find TLS/SSL plugins - critical for HTTPS connections
export QT_TLS_BACKEND="openssl"
if [ -d "/usr/lib/qt6/plugins/tls" ]; then
    export QT_PLUGIN_PATH="$QT_PLUGIN_PATH:/usr/lib/qt6/plugins/tls"
fi
if [ -d "/usr/lib64/qt6/plugins/tls" ]; then
    export QT_PLUGIN_PATH="$QT_PLUGIN_PATH:/usr/lib64/qt6/plugins/tls"
fi
```

### Verifying TLS Setup

Check that TLS plugins exist:
```bash
ls /usr/lib64/qt6/plugins/tls/
# Should show: libqopensslbackend.so
```

Test HTTPS from command line:
```bash
curl -s https://mcp.exa.ai/mcp -X POST -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2024-11-05","capabilities":{},"clientInfo":{"name":"test","version":"1.0"}}}'
```

## Settings Integration

### GlobalSetting Methods

```cpp
// Get configured provider order (returns default if not set)
QStringList getChatWebSearchProviders() const;

// Set provider order
void setChatWebSearchProviders(const QStringList &providerIds);

// API keys (optional, for paid tiers)
QString getChatExaApiKey() const;
void setChatExaApiKey(const QString &key);

QString getChatParallelApiKey() const;
void setChatParallelApiKey(const QString &key);
```

### Default Configuration

```cpp
// Default provider order (in globalsetting.cpp)
QStringList defaults = {"exa", "parallel", "duckduckgo", "wikipedia"};
```

### ChatSettingsPage UI

Users can configure providers in: **Preferences → AI Chat → Tools → Web Search**

- Reorder providers with drag-and-drop
- Enable/disable individual providers
- Configure API keys for paid tiers (optional)

## Usage Example

When the AI needs information it doesn't have, it calls:

```json
{
  "tool": "web_search",
  "query": "opencode CLI AI coding assistant install"
}
```

The WebSearchManager:
1. Tries Exa AI → returns rich results with titles, URLs, highlights
2. If Exa fails → tries Parallel
3. If Parallel fails → tries DuckDuckGo
4. If DuckDuckGo fails → tries Wikipedia
5. Returns first successful result or error message

**Example response from Exa:**
```
web_search: [Exa AI]
Title: Intro | AI coding agent built for the terminal - OpenCode
URL: https://opencode.ai/docs/
Published: N/A
Author: N/A
Highlights:
open source AI
...
terminal-based interface
```

## Error Handling

### SSL/TLS Errors
- Detected in both `executeCurl()` and `executeHttpRequest()`
- Checked in stderr separately from stdout
- Returned immediately without trying other providers
- Clear message tells user how to fix TLS configuration

### Network Errors
- 15-second timeout per provider
- Automatic fallback to next provider
- Generic "failed to fetch results" for non-SSL errors

### Provider Errors
- Each provider validates responses
- Returns `web_search: no results found` if no matches
- Returns `web_search: failed to parse response` for invalid JSON
- Returns `web_search: error: [message]` for API errors

## Debugging

### Enable Debug Logging

Set environment variable before running:
```bash
export QT_LOGGING_RULES="log_ai_chat.debug=true"
./openterfaceQT-launcher.sh
```

### Log Output

Look for these log messages:
```
WebSearchManager: searching for "query"
WebSearchManager: provider order: QList("exa", "parallel", "duckduckgo", "wikipedia")
WebSearchManager: trying provider: "Exa AI"
Exa MCP: searching for "query"
executeHttpRequest: starting curl "POST" to "https://mcp.exa.ai/mcp"
Exa MCP: session initialized successfully
Exa MCP: received 5471 bytes
WebSearchManager: success with "Exa AI"
```

### Common Issues

1. **"No functional TLS backend was found"**
   - Fix: Set `QT_TLS_BACKEND=openssl` in launcher
   - Verify: TLS plugins exist in `/usr/lib64/qt6/plugins/tls/`

2. **"All providers failed"**
   - Check network connectivity
   - Enable debug logging to see which providers were tried
   - Check for SSL errors in logs

3. **Exa returns "Not Acceptable" error**
   - Exa requires `Accept: application/json, text/event-stream` header
   - Already configured in `ExaProvider::search()`

## Benefits

1. **High-quality results** - Exa and Parallel are AI-optimized
2. **No API keys required** - All providers work anonymously
3. **Robust fallback** - Multiple providers ensure reliability
4. **Clear error messages** - Users know when it's a TLS issue
5. **Configurable** - Users can reorder or disable providers
6. **Fast** - 15-second timeout per provider
7. **Privacy-friendly** - No tracking, anonymous access

## Testing

### Manual Test
```bash
# Test Exa MCP directly
curl -s -X POST "https://mcp.exa.ai/mcp" \
  -H "Content-Type: application/json" \
  -H "Accept: application/json, text/event-stream" \
  -d '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2024-11-05","capabilities":{},"clientInfo":{"name":"test","version":"1.0"}}}'

# Test Parallel MCP directly
curl -s -X POST "https://search.parallel.ai/mcp" \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2024-11-05","capabilities":{},"clientInfo":{"name":"test","version":"1.0"}}}'
```

### Automated Tests
- `tests/test_web_search.cpp` - Comprehensive standalone test
- `tests/test_exa_parallel.cpp` - Exa/Parallel MCP test
- `tests/test_parallel_mcp.cpp` - Parallel anonymous access test
- `tests/diagnose_web_search.sh` - Diagnostic script

## Future Enhancements

Potential improvements:
1. Cache search results to reduce API calls
2. Add more providers (Google Custom Search, Bing, etc.)
3. Image search capabilities
4. News search capabilities
5. Configurable result count per provider
6. Support for advanced search operators
7. Rate limiting to avoid API throttling

## Migration Notes

- No data migration needed
- Tool settings stored by tool ID (`web_search`)
- Provider settings stored in `chat/webSearchProviders`
- Default providers: exa, parallel, duckduckgo, wikipedia
- Backward compatible with existing DuckDuckGo-only configs
- API keys are optional (anonymous access works)
