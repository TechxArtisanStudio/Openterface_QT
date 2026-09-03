# Web Search Provider Testing Guide

## Test Files Created

### 1. test_web_search.cpp
Tests DuckDuckGo and Wikipedia (free providers, no API key required)
```bash
cd tests
g++ -fPIC test_web_search.cpp -o test_web_search \
    -I/usr/include/qt6 -I/usr/include/qt6/QtCore \
    -lQt6Core $(pkg-config --cflags --libs Qt6Core)
./test_web_search
```

**Results:** All tests PASS ✅
- "open code" returns results about open-source software
- All common queries work correctly
- Fallback chain (DuckDuckGo → Wikipedia) works

### 2. test_exa_parallel.cpp
Tests Exa and Parallel providers (require API keys)
```bash
cd tests
g++ -fPIC test_exa_parallel.cpp -o test_exa_parallel \
    -I/usr/include/qt6 -I/usr/include/qt6/QtCore \
    -lQt6Core $(pkg-config --cflags --libs Qt6Core)

# Set API keys via environment variables
export EXA_API_KEY=your_exa_key
export PARALLEL_API_KEY=your_parallel_key
./test_exa_parallel

# Or pass as arguments
./test_exa_parallel <exa_key> <parallel_key>
```

## API Keys

### Exa (https://exa.ai)
- Sign up at https://exa.ai
- Get API key from dashboard
- Set via environment: `export EXA_API_KEY=your_key`
- Or configure in AI Chat settings

### Parallel (https://search.parallel.ai/mcp)
- Sign up at https://search.parallel.ai/mcp
- Get API key from dashboard
- Set via environment: `export PARALLEL_API_KEY=your_key`
- Or configure in AI Chat settings

## Configuration in Application

API keys can be configured in the AI Chat settings page:
1. Open AI Chat settings
2. Navigate to "Web Search" section
3. Enter API keys for Exa and/or Parallel
4. Configure provider priority (fallback chain)

## Testing Checklist

### Free Providers (No API Key Required)
- [x] DuckDuckGo works for "open code"
- [x] DuckDuckGo works for "Python programming"
- [x] DuckDuckGo works for "OpenAI"
- [x] DuckDuckGo works for "machine learning"
- [x] Wikipedia fallback works
- [x] Fallback chain works correctly

### Paid Providers (API Key Required)
- [ ] Exa API configured
- [ ] Exa works for "open code"
- [ ] Exa works for "Python programming"
- [ ] Exa works for "OpenAI"
- [ ] Exa works for "machine learning"
- [ ] Parallel API configured
- [ ] Parallel works for "open code"
- [ ] Parallel works for "Python programming"
- [ ] Parallel works for "OpenAI"
- [ ] Parallel works for "machine learning"

## Troubleshooting

### If "open code" returns "no results found" in AI Chat:

1. **Check query extraction:**
   - Enable debug logging for `log_ai_chat`
   - Look for: `web_search: extracted query:`
   - Verify query is not empty

2. **Check provider configuration:**
   ```bash
   # Test with curl manually
   curl -s "https://api.duckduckgo.com/?q=open+code&format=json" | head -20
   ```

3. **Check WebSearchManager logs:**
   - Look for: `WebSearchManager: trying provider:`
   - Look for: `WebSearchManager: success with` or `failed, trying next`

4. **Test individual providers:**
   ```bash
   ./test_web_search  # Tests DuckDuckGo and Wikipedia
   ./test_exa_parallel  # Tests Exa and Parallel (requires API keys)
   ```

## Current Status

**DuckDuckGo and Wikipedia:** ✅ Working correctly
**Exa:** ⏳ Requires API key to test
**Parallel:** ⏳ Requires API key to test

## Next Steps

To test Exa and Parallel:
1. Get API keys from https://exa.ai and https://search.parallel.ai/mcp
2. Set environment variables or configure in AI Chat settings
3. Run `./test_exa_parallel`
4. Verify all queries return results

If you provide the API keys, I can run the tests and verify the results.
