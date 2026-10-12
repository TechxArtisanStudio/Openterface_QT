#!/bin/bash
# Capture web search logs for debugging

echo "=========================================="
echo "Web Search Debug Capture"
echo "=========================================="
echo ""
echo "This will run the application and capture all logs."
echo "Please try a web search in the AI chat, then close the app."
echo ""
echo "Press Enter to continue..."
read

LOG_FILE="/tmp/web_search_debug_$(date +%Y%m%d_%H%M%S).log"

echo "Logging to: $LOG_FILE"
echo ""

# Run with debug logging
cd /home/bbot/projects/Openterface/Openterface_QT/build
QT_LOGGING_RULES="log_ai_chat.debug=true" ./openterfaceQT 2>&1 | tee "$LOG_FILE"

echo ""
echo "=========================================="
echo "Debug capture complete!"
echo "=========================================="
echo ""
echo "Log saved to: $LOG_FILE"
echo ""
echo "Please share this file or the relevant portions."
echo "Look for lines containing:"
echo "  - WebSearchManager"
echo "  - Exa MCP"
echo "  - Parallel MCP"
echo "  - executeHttpRequest"
echo "  - executeCurl"
echo ""
echo "To view the log:"
echo "  cat $LOG_FILE | grep -E 'WebSearch|Exa|Parallel|executeHttp|executeCurl'"
echo ""
