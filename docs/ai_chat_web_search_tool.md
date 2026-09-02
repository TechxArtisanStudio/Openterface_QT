# AI Chat Web Search Tool

## Overview
Added a web search tool to the AI Chat Assistant that allows the AI to search the internet for information when needed. This enables the AI to provide up-to-date information and answer questions about topics it doesn't have knowledge about.

## Features

### Web Search Tool
- **Tool ID**: `web_search`
- **Location**: System/Host Tools group
- **Function**: Search the internet for information using DuckDuckGo Instant Answer API
- **Arguments**: 
  - `query` (string): The search query

### Implementation Details

#### API Integration
- Uses DuckDuckGo Instant Answer API (free, no API key required)
- Endpoint: `https://api.duckduckgo.com/?q={query}&format=json&no_html=1&skip_disambig=1`
- Returns structured JSON with answers and related topics

#### Response Format
The tool extracts and returns:
1. **Abstract**: Main answer/summary (if available)
2. **Abstract Text**: Detailed summary (if different from abstract)
3. **Related Topics**: Up to 5 relevant topics with descriptions

#### Error Handling
- Validates query parameter
- Handles network timeouts (15 second limit)
- Validates JSON response parsing
- Truncates results over 4096 characters

### Code Changes

#### Files Modified

1. **ui/chat/ChatSettingsPage.h**
   - Added snapshot variable: `bool m_snap_webSearch;`

2. **ui/chat/ChatSettingsPage.cpp**
   - Added web search tool to System/Host Tools group in `populateToolsTree()`
   - Updated snapshot initialization in `captureSnapshot()`
   - Updated `setToolCheck()` call in `revertToSnapshot()`
   - Updated comparison in `valuesMatchSnapshot()`

3. **ai/ChatToolExecution.h**
   - Added method declaration: `QString webSearch(const QString &query) const;`

4. **ai/ChatToolExecution.cpp**
   - Added `#include <QUrl>` for URL encoding
   - Implemented `webSearch()` method using DuckDuckGo API
   - Added tool handler in `executeToolCalls()` for `web_search`, `search`, and `internet_search` tool names

5. **ai/ChatConversationBuilder.cpp**
   - Added web_search tool documentation in `agentToolInstruction()`
   - Included in the available tools list when enabled

6. **docs/feature.md**
   - Updated System/Host Tools section to mention web search

### Usage Example

When the AI needs information it doesn't have, it can call:

```json
{
  "tool": "web_search",
  "query": "how to configure Linux firewall"
}
```

The tool will return a formatted response like:
```
Answer: [Main answer from DuckDuckGo]
Summary: [Detailed summary if available]
- [Related topic 1]
- [Related topic 2]
- [Related topic 3]
```

### Benefits

1. **Up-to-date Information**: AI can access current information beyond its training data
2. **No API Key Required**: Uses free DuckDuckGo API
3. **Portable**: Uses curl command which is available on most systems
4. **Configurable**: Can be enabled/disabled in Preferences → AI Chat
5. **Fast**: 15-second timeout ensures quick responses
6. **Structured Results**: Returns organized information with main answer and related topics

### Testing Checklist

- [x] Tool appears in System/Host Tools group in preferences
- [x] Tool can be enabled/disabled independently
- [x] Snapshot/revert preserves tool state correctly
- [x] Dirty state tracking works correctly
- [x] Tool executes web searches successfully
- [x] Results are properly formatted and truncated if needed
- [x] Error handling works for invalid queries
- [x] Error handling works for network failures
- [x] Build completes without errors

### Implementation Notes

#### Why DuckDuckGo API?
- Free and no API key required
- Fast response times
- Good quality instant answers
- JSON format for easy parsing
- Privacy-friendly (no tracking)

#### Why use curl instead of QNetworkAccessManager?
- Simpler implementation
- Already available on all target platforms
- Consistent with how run_bash works
- No need for additional Qt network setup
- Easier to handle timeouts and errors

#### Tool Name Aliases
The tool responds to multiple names for flexibility:
- `web_search` (primary)
- `search` (short form)
- `internet_search` (descriptive)

This matches the pattern used by other tools (e.g., `run_bash`, `bash`, `shell`, `exec_command`).

### Future Enhancements

Potential improvements for future versions:
1. Support for multiple search providers (Google, Bing, etc.)
2. Configurable API keys for premium search services
3. Image search capabilities
4. News search capabilities
5. Caching of search results to reduce API calls
6. Configurable number of related topics to return
7. Support for advanced search operators

### Migration Notes

- No data migration needed
- Tool settings are stored by tool ID (`web_search`) in GlobalSetting
- Tool is enabled by default when first added
- Existing tool configurations remain unchanged
- Backward compatible with existing settings
