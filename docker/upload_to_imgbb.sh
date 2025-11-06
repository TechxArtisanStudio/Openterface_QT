#!/bin/bash

# Standalone ImgBB Upload Script
# Extracts the upload logic from GitHub Actions workflow for local testing and reuse

set -e  # Exit on any error

# Configuration
IMGBB_API_URL="https://api.imgbb.com/1/upload"
CURL_CONNECT_TIMEOUT=30
CURL_MAX_TIME=120

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Logging functions
log_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

log_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

log_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Function to print usage
usage() {
    echo "Usage: $0 [OPTIONS] IMAGE_FILE"
    echo ""
    echo "Upload an image to ImgBB and get the shareable URL"
    echo ""
    echo "OPTIONS:"
    echo "  -k, --api-key KEY    ImgBB API key (or set IMGBB_API_KEY env var)"
    echo "  -h, --help           Show this help message"
    echo "  -v, --verbose        Enable verbose output"
    echo "  -d, --debug          Enable debug output"
    echo ""
    echo "ENVIRONMENT VARIABLES:"
    echo "  IMGBB_API_KEY        Your ImgBB API key"
    echo ""
    echo "EXAMPLES:"
    echo "  $0 screenshot.jpg"
    echo "  $0 -k YOUR_API_KEY image.png"
    echo "  IMGBB_API_KEY=your_key $0 photo.jpeg"
    echo ""
    exit 1
}

# Parse command line arguments
VERBOSE=false
DEBUG=false
API_KEY=""

while [[ $# -gt 0 ]]; do
    case $1 in
        -k|--api-key)
            API_KEY="$2"
            shift 2
            ;;
        -v|--verbose)
            VERBOSE=true
            shift
            ;;
        -d|--debug)
            DEBUG=true
            VERBOSE=true
            shift
            ;;
        -h|--help)
            usage
            ;;
        -*)
            log_error "Unknown option: $1"
            usage
            ;;
        *)
            IMAGE_FILE="$1"
            shift
            ;;
    esac
done

# Check if image file is provided
if [ -z "$IMAGE_FILE" ]; then
    log_error "No image file specified"
    usage
fi

# Check if image file exists
if [ ! -f "$IMAGE_FILE" ]; then
    log_error "Image file not found: $IMAGE_FILE"
    exit 1
fi

# Check if image file is readable
if [ ! -r "$IMAGE_FILE" ]; then
    log_error "Image file is not readable: $IMAGE_FILE"
    exit 1
fi

# Get API key from environment if not provided via command line
if [ -z "$API_KEY" ]; then
    API_KEY="$IMGBB_API_KEY"
fi

# Check if API key is available
if [ -z "$API_KEY" ]; then
    log_error "No API key provided. Use -k option or set IMGBB_API_KEY environment variable"
    log_info "Get your API key from: https://api.imgbb.com/"
    exit 1
fi

# Validate API key format (ImgBB keys are typically 32 characters)
if [ ${#API_KEY} -ne 32 ]; then
    log_warning "API key length is ${#API_KEY} characters (expected 32). This might indicate an invalid key."
fi

log_info "Starting ImgBB upload process..."
log_info "Image file: $IMAGE_FILE"
log_info "API key length: ${#API_KEY}"

# Get file information
FILE_SIZE=$(stat -c%s "$IMAGE_FILE" 2>/dev/null || echo "unknown")
FILE_SIZE_HUMAN=$(ls -lh "$IMAGE_FILE" | awk '{print $5}')
log_info "File size: $FILE_SIZE_HUMAN ($FILE_SIZE bytes)"

# Test connectivity to ImgBB API
if $VERBOSE; then
    log_info "Testing connectivity to ImgBB API..."
    # Skip connectivity test for now to avoid hanging
    log_info "Skipping connectivity test (may hang with proxy)"
fi

# Perform upload
log_info "Uploading to ImgBB..."

UPLOAD_METHOD="binary"
log_info "Using binary file upload method (most reliable for large files)"

if $DEBUG; then
    log_info "Curl command: curl -s -w '\\nHTTP_STATUS:%{http_code}\\nRESPONSE_TIME:%{time_total}' --connect-timeout $CURL_CONNECT_TIMEOUT --max-time $CURL_MAX_TIME -X POST '$IMGBB_API_URL' -F 'key=$API_KEY' -F 'image=@$IMAGE_FILE'"
fi

# First try without proxy
UPLOAD_RESPONSE=$(timeout 120 curl -s --noproxy "*" -w "\nHTTP_STATUS:%{http_code}\nRESPONSE_TIME:%{time_total}" \
    --connect-timeout $CURL_CONNECT_TIMEOUT \
    --max-time $CURL_MAX_TIME \
    -X POST "$IMGBB_API_URL" \
    -F "key=$API_KEY" \
    -F "image=@$IMAGE_FILE" \
    2>&1 || echo "")

# If that fails, try with proxy (fallback)
if [ -z "$UPLOAD_RESPONSE" ] || echo "$UPLOAD_RESPONSE" | grep -q "000"; then
    log_warning "Direct connection failed, trying with proxy..."
    UPLOAD_RESPONSE=$(timeout 120 curl -s -w "\nHTTP_STATUS:%{http_code}\nRESPONSE_TIME:%{time_total}" \
        --connect-timeout $CURL_CONNECT_TIMEOUT \
        --max-time $CURL_MAX_TIME \
        -X POST "$IMGBB_API_URL" \
        -F "key=$API_KEY" \
        -F "image=@$IMAGE_FILE" \
        2>&1 || echo '{"success": false, "error": {"message": "curl command failed"}}')
fi

# Extract HTTP status and response
HTTP_STATUS=$(echo "$UPLOAD_RESPONSE" | grep "HTTP_STATUS:" | cut -d: -f2 2>/dev/null || echo "unknown")
RESPONSE_TIME=$(echo "$UPLOAD_RESPONSE" | grep "RESPONSE_TIME:" | cut -d: -f2 2>/dev/null || echo "unknown")
JSON_RESPONSE=$(echo "$UPLOAD_RESPONSE" | sed '/HTTP_STATUS:/,$d' 2>/dev/null || echo "$UPLOAD_RESPONSE")

# Debug output
if $DEBUG; then
    log_info "Raw upload response:"
    echo "$UPLOAD_RESPONSE"
    echo "---"
fi

log_info "HTTP Status: $HTTP_STATUS"
log_info "Response Time: ${RESPONSE_TIME}s"
log_info "Response Length: ${#JSON_RESPONSE} characters"

# If no valid JSON response, create error response
if [ ${#JSON_RESPONSE} -lt 10 ] || ! echo "$JSON_RESPONSE" | grep -q '"success"'; then
    if [ "$HTTP_STATUS" = "000" ] || [ "$HTTP_STATUS" = "unknown" ]; then
        JSON_RESPONSE='{"success": false, "error": {"message": "Connection failed - no response from server"}}'
    else
        JSON_RESPONSE='{"success": false, "error": {"message": "Invalid response from server"}}'
    fi
fi

# Parse the response
UPLOAD_SUCCESS=$(echo "$JSON_RESPONSE" | grep -o '"success":[^,}]*' | cut -d':' -f2 | tr -d ' ",' 2>/dev/null || echo "false")

# Show response preview for debugging
if $VERBOSE; then
    RESPONSE_PREVIEW=$(echo "$JSON_RESPONSE" | head -c 200)
    log_info "Response preview: $RESPONSE_PREVIEW..."
fi

if [ "$UPLOAD_SUCCESS" = "true" ]; then
    log_success "Upload successful!"

    # Extract image URLs from response - ImgBB format
    IMAGE_URL=$(echo "$JSON_RESPONSE" | grep -o '"url":"[^"]*"' | cut -d'"' -f4 | head -1 | sed 's|\\\/|/|g')
    DISPLAY_URL=$(echo "$JSON_RESPONSE" | grep -o '"display_url":"[^"]*"' | cut -d'"' -f4 | head -1 | sed 's|\\\/|/|g')
    VIEWER_URL=$(echo "$JSON_RESPONSE" | grep -o '"url_viewer":"[^"]*"' | cut -d'"' -f4 | head -1 | sed 's|\\\/|/|g')

    # Use display_url if available, otherwise use url
    FINAL_URL="${DISPLAY_URL:-$IMAGE_URL}"

    if $VERBOSE; then
        log_info "Image URL: $IMAGE_URL"
        log_info "Display URL: $DISPLAY_URL"
        log_info "Viewer URL: $VIEWER_URL"
    fi

    if [ -n "$FINAL_URL" ]; then
        log_success "Final URL: $FINAL_URL"

        # Output the URL for easy copying (this is the main output)
        echo ""
        echo "🔗 IMAGE URL: $FINAL_URL"
        echo ""

        # Additional links if available
        if [ -n "$VIEWER_URL" ]; then
            echo "🔗 VIEWER PAGE: $VIEWER_URL"
            echo ""
        fi

        # Success exit with URL as output
        echo "$FINAL_URL"
        exit 0
    else
        log_warning "Upload successful but URL not found in response"
        if $DEBUG; then
            log_info "Raw response: $JSON_RESPONSE"
        fi
        exit 1
    fi
else
    # Extract error message from ImgBB response
    ERROR_MESSAGE=$(echo "$JSON_RESPONSE" | grep -o '"message":"[^"]*"' | cut -d'"' -f4 | head -1)

    log_error "Upload failed"
    log_error "Success: $UPLOAD_SUCCESS"
    log_error "Error: $ERROR_MESSAGE"

    if $DEBUG; then
        log_error "Full response: $JSON_RESPONSE"
    fi

    # Provide troubleshooting tips
    echo ""
    echo "🔧 Troubleshooting Tips:"
    if [ "$HTTP_STATUS" = "400" ]; then
        echo "  - The API key might be invalid or expired"
        echo "  - Check your API key at: https://api.imgbb.com/"
        echo "  - The image format might not be supported"
        echo "  - Try with a smaller image file"
    elif [ "$HTTP_STATUS" = "401" ]; then
        echo "  - Authentication failed - API key is invalid"
        echo "  - Get a new API key from: https://api.imgbb.com/"
    elif [ "$HTTP_STATUS" = "413" ]; then
        echo "  - Image file is too large for ImgBB"
        echo "  - Try with a smaller image (max ~32MB)"
    elif [ "$HTTP_STATUS" = "429" ]; then
        echo "  - Too many requests - rate limit exceeded"
        echo "  - Wait a few minutes and try again"
    elif [ "$HTTP_STATUS" = "000" ] || [ "$HTTP_STATUS" = "unknown" ]; then
        echo "  - Check your internet connection"
        echo "  - ImgBB API might be temporarily unavailable"
        echo "  - Try disabling proxy: export https_proxy=''"
    elif [ "$HTTP_STATUS" = "403" ]; then
        echo "  - Access forbidden - your IP might be blocked"
        echo "  - Try using a VPN or different network"
    fi
    echo "  - Try with --debug flag for more details"
    echo "  - Alternative: Use GitHub artifacts instead of external hosting"
    echo "  - For CI/CD: Screenshots are automatically saved as artifacts"
    echo ""

    exit 1
fi
