# SSL Error Detection - Clear User Messages

## Status: ✅ IMPLEMENTED

All web search providers now detect SSL/TLS errors and show clear, actionable messages to users instead of generic "no results found" errors.

## Error Detection

The following SSL/TLS error patterns are now detected:
- `SSL` or `TLS` in error messages (case-insensitive)
- `certificate` errors
- `Handshake` failures
- `issuer` certificate problems

## Clear Error Messages

When SSL/TLS errors occur, users now see:

### Instead of:
```
web_search: no results found
```

### Users See:
```
web_search: SSL/TLS error: Cannot establish secure HTTPS connection. 
This usually means Qt's TLS backend is not configured correctly. 
Please ensure OpenSSL is installed and Qt can find its TLS plugins. 
Try: export QT_TLS_BACKEND=openssl before running the application.
```

## Implementation Details

### Updated Functions

1. **executeCurl()** - Added `errorMessage` output parameter
   - Uses `QProcess::SeparateChannels` to read stdout/stderr separately
   - Detects SSL errors in stderr
   - Returns detailed error message
   - Also checks for SSL errors even when exit code is 0

2. **executeHttpRequest()** - Added `errorMessage` output parameter
   - Uses `QProcess::SeparateChannels` to read stdout/stderr separately
   - Detects SSL/TLS errors in stderr
   - Returns detailed error message
   - Logs stderr and stdout for debugging

3. **All Provider search() methods** - Updated to:
   - Pass errorMessage parameter to curl functions
   - Check for SSL errors in error message
   - Return clear SSL error messages to users immediately

### Providers Updated

- ✅ **DuckDuckGoProvider** - Detects SSL errors
- ✅ **WikipediaProvider** - Detects SSL errors
- ✅ **ExaProvider** - Detects SSL errors (initialization and search)
- ✅ **ParallelProvider** - Detects SSL errors (initialization and search)

### WebSearchManager Enhancement

The WebSearchManager now:
- Returns SSL errors immediately without trying other providers
- Preserves provider-specific error messages
- Only returns generic "all providers failed" for non-SSL errors

```cpp
// If we see an SSL error, return it immediately
if (result.contains("SSL/TLS error")) {
    qCWarning(log_ai_chat) << "WebSearchManager: SSL error from" << provider->name();
    return result;
}
```

## User Experience

### Before
```
User: Search for "open code"
AI: web_search: no results found (all providers failed)
User: ??? (confused - is it the query? the network? the providers?)
```

### After
```
User: Search for "open code"
AI: web_search: SSL/TLS error: Cannot establish secure HTTPS connection. 
    This usually means Qt's TLS backend is not configured correctly. 
    Please ensure OpenSSL is installed and Qt can find its TLS plugins. 
    Try: export QT_TLS_BACKEND=openssl before running the application.
User: Ah! I need to fix my TLS configuration!
```

## How to Fix TLS Issues

If users see SSL/TLS errors, they should:

1. **Set TLS backend explicitly:**
   ```bash
   export QT_TLS_BACKEND=openssl
   ./openterfaceQT-launcher.sh
   ```

2. **Ensure OpenSSL is installed:**
   ```bash
   # Fedora/RHEL
   sudo dnf install openssl qt6-qtbase
   
   # Ubuntu/Debian
   sudo apt install openssl qt6-base-dev
   ```

3. **Check Qt TLS plugins exist:**
   ```bash
   ls /usr/lib64/qt6/plugins/tls/
   # Should show: libqopensslbackend.so
   ```

4. **Verify launcher script has TLS config:**
   ```bash
   grep "QT_TLS_BACKEND" build-script/openterfaceQT-local-launcher.sh
   # Should show: export QT_TLS_BACKEND="openssl"
   ```

## Code Changes

### Key Implementation

```cpp
// executeCurl uses SeparateChannels
process.setProcessChannelMode(QProcess::SeparateChannels);

// Read stdout and stderr separately
QByteArray stdOutput = process.readAllStandardOutput();
QByteArray stdError = process.readAllStandardError();
QString errorStr = QString::fromUtf8(stdError);

// Check for SSL/TLS errors in stderr
if (errorStr.contains("SSL", Qt::CaseInsensitive) ||
    errorStr.contains("TLS", Qt::CaseInsensitive) ||
    errorStr.contains("certificate", Qt::CaseInsensitive) ||
    errorStr.contains("Handshake", Qt::CaseInsensitive) ||
    errorStr.contains("issuer", Qt::CaseInsensitive)) {
    error = "SSL/TLS error: Cannot establish secure HTTPS connection. "
            "This usually means Qt's TLS backend is not configured correctly. "
            "Please ensure OpenSSL is installed and Qt can find its TLS plugins. "
            "Try: export QT_TLS_BACKEND=openssl before running the application.";
}

// Also check for SSL errors even if exit code is 0
if (stdOutput.isEmpty() && !stdError.isEmpty()) {
    if (errorStr.contains("SSL", Qt::CaseInsensitive) || ...) {
        // Return SSL error
    }
}
```

## Benefits

1. **Clear diagnosis** - Users immediately know it's an SSL issue
2. **Actionable guidance** - Error message tells users how to fix it
3. **Better UX** - No more confusion about "no results"
4. **Faster support** - Support team can quickly identify the issue
5. **Self-service** - Users can fix the issue themselves
6. **Debugging** - Logs show exact error from curl stderr

## Testing

To test SSL error detection:

1. **Break TLS intentionally:**
   ```bash
   # Remove TLS plugins temporarily
   sudo mv /usr/lib64/qt6/plugins/tls /usr/lib64/qt6/plugins/tls.bak
   
   # Run application
   ./openterfaceQT-launcher.sh
   
   # Try web search - should see clear SSL error
   ```

2. **Restore TLS:**
   ```bash
   sudo mv /usr/lib64/qt6/plugins/tls.bak /usr/lib64/qt6/plugins/tls
   ```

3. **Check logs:**
   ```bash
   export QT_LOGGING_RULES="log_ai_chat.debug=true"
   ./openterfaceQT-launcher.sh
   # Look for "SSL/TLS error" in logs
   ```

## Result

✅ Users now get **clear, actionable error messages** instead of confusing "no results found" when there are SSL/TLS configuration issues.
