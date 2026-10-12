# How to Run with Debug Logging Enabled

## ✅ Debug Logging is Now Enabled!

The launcher script has been updated to automatically enable debug logging for AI chat.

## 🚀 Run the Application

Simply run the launcher script:

```bash
cd /home/bbot/projects/Openterface/Openterface_QT/build
./openterfaceQT-launcher.sh
```

You'll see this message when it starts:
```
=== Debug logging enabled for log_ai_chat ===
```

## 📋 What to Do

1. **Run the application** using the command above
2. **Try a web search** in the AI chat (e.g., search for "open code")
3. **Watch the terminal output** - you'll see detailed debug logs like:
   ```
   WebSearchManager: searching for open code
   WebSearchManager: trying provider: Exa AI
   Exa MCP: searching for open code
   Exa MCP: using enhanced query: information about open code software or technology
   Exa MCP: session initialized successfully
   Exa MCP: received 54321 bytes
   WebSearchManager: success with Exa AI
   ```

## 📄 Log Files

Logs are saved to:
- **Launcher log**: `/tmp/openterfaceqt-launcher-<timestamp>.log`
- **Application log**: `/tmp/openterfaceqt-app-<timestamp>.log`

You can view them with:
```bash
# View the latest app log
tail -f /tmp/openterfaceqt-app-*.log | grep -i "web_search\|exa\|parallel"
```

## 🔍 What to Look For

### Success Case:
```
WebSearchManager: searching for open code
WebSearchManager: trying provider: Exa AI
Exa MCP: searching for open code
Exa MCP: using enhanced query: information about open code software or technology
Exa MCP: session initialized successfully
Exa MCP: received 54321 bytes
WebSearchManager: success with Exa AI
```

### Failure Case (if it happens):
```
WebSearchManager: searching for open code
WebSearchManager: trying provider: Exa AI
Exa MCP: searching for open code
Exa MCP: failed to initialize session
WebSearchManager: Exa AI failed, trying next
WebSearchManager: trying provider: Parallel
Parallel MCP: searching for open code
...
```

## 📤 Share the Output

If you still see issues, copy the terminal output or the log file and share it. Look for lines containing:
- `WebSearchManager`
- `Exa MCP`
- `Parallel MCP`
- `web_search`

## 🛠️ Manual Testing

You can also test the providers directly:
```bash
cd /home/bbot/projects/Openterface/Openterface_QT/tests
./diagnose_web_search.sh
```

This will confirm the providers work correctly.
