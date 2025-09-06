#!/bin/bash
# Network diagnostic test script

echo "🔍 Network Diagnostic Test"
echo "=========================="

echo "1. Testing basic connectivity..."
if ping -c 3 8.8.8.8 > /dev/null 2>&1; then
    echo "   ✅ Internet connectivity OK"
else
    echo "   ❌ No internet connectivity"
fi

echo "2. Testing DNS resolution..."
if nslookup api.github.com > /dev/null 2>&1; then
    echo "   ✅ DNS resolution OK"
else
    echo "   ❌ DNS resolution failed"
fi

echo "3. Testing GitHub API access..."
if curl --connect-timeout 10 --max-time 30 -s "https://api.github.com" > /dev/null; then
    echo "   ✅ GitHub API reachable"
else
    echo "   ❌ GitHub API not reachable"
fi

echo "4. Testing release API endpoint..."
REPO="TechxArtisanStudio/Openterface_QT"
API_URL="https://api.github.com/repos/${REPO}/releases/latest"
echo "   API URL: $API_URL"

RESPONSE=$(curl -s --connect-timeout 30 --max-time 60 "$API_URL")
if [ $? -eq 0 ] && [ -n "$RESPONSE" ]; then
    VERSION=$(echo "$RESPONSE" | grep '"tag_name":' | sed -E 's/.*"([^"]+)".*/\1/' | head -1)
    echo "   ✅ API response received"
    echo "   Latest version: $VERSION"
    
    # Test download URL
    DOWNLOAD_URL="https://github.com/${REPO}/releases/download/${VERSION}/openterfaceQT-portable"
    echo "   Testing download URL availability..."
    if curl --output /dev/null --silent --head --fail --connect-timeout 30 "$DOWNLOAD_URL"; then
        echo "   ✅ Download URL is accessible"
        
        # Get file size
        SIZE=$(curl -sI "$DOWNLOAD_URL" | grep -i content-length | awk '{print $2}' | tr -d '\r')
        echo "   File size: $SIZE bytes"
        
        if [ "$SIZE" -gt 10000000 ]; then  # > 10MB
            echo "   ⚠️  Large file detected - download may be slow in container"
        fi
    else
        echo "   ❌ Download URL not accessible"
    fi
else
    echo "   ❌ API request failed"
    echo "   Response: $(echo "$RESPONSE" | head -3)"
fi

echo ""
echo "Test completed."
