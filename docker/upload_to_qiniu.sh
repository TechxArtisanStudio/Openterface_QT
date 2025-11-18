#!/bin/bash

# Standalone Qiniu Upload Script
# Extracts the upload logic from GitHub Actions workflow for local testing and reuse

# Don't use set -e - we need better error handling and reporting
# set -e  # Exit on any error

# Set error handling with explicit exit messages
trap 'echo "ERROR: Script failed at line $LINENO with exit code $?"; exit 1' ERR

# Configuration
QINIU_UPLOAD_URL="https://upload.qiniup.com"
QINIU_BUCKET_HARDCODED="openterface"  # Hardcoded bucket name
CURL_CONNECT_TIMEOUT=30
CURL_MAX_TIME=120
MAX_FILE_SIZE=2097152  # 2MB limit for Qiniu

# Detect timeout command (GNU timeout on Linux, use bash timeout on macOS, or gtimeout if installed)
if command -v timeout &> /dev/null; then
    TIMEOUT_CMD="timeout"
elif command -v gtimeout &> /dev/null; then
    TIMEOUT_CMD="gtimeout"
else
    # Fallback: use bash built-in timeout simulation (not perfect but works)
    TIMEOUT_CMD=""
fi

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

# Function to generate upload token from AK and SK
generate_upload_token() {
    local ak="$1"
    local sk="$2"
    local bucket="$3"
    local deadline=$(( $(date +%s) + 3600 ))  # 1 hour validity
    
    # Create the policy JSON
    local policy="{\"scope\":\"${bucket}\",\"deadline\":${deadline}}"
    
    # Base64 encode the policy
    local encoded_policy=$(echo -n "$policy" | base64 | tr '+/' '-_' | tr -d '=')
    
    # Create HMAC-SHA1 signature
    local signature=$(echo -n "$encoded_policy" | openssl dgst -sha1 -hmac "$sk" -binary | base64 | tr '+/' '-_' | tr -d '=')
    
    # Generate the upload token
    local upload_token="${ak}:${signature}:${encoded_policy}"
    
    echo "$upload_token"
}

# Function to print usage
usage() {
    echo "Usage: $0 [OPTIONS] IMAGE_FILE"
    echo ""
    echo "Upload an image to Qiniu (bucket: openterface) and get the shareable URL"
    echo ""
    echo "OPTIONS (Method 1: Pre-generated Token):"
    echo "  -t, --token TOKEN    Qiniu upload token (or set QINIU_UPLOAD_TOKEN env var)"
    echo ""
    echo "OPTIONS (Method 2: Generate from AK/SK - RECOMMENDED):"
    echo "  -a, --ak AK          Qiniu Access Key (or set QINIU_AK env var)"
    echo "  -s, --sk SK          Qiniu Secret Key (or set QINIU_SK env var)"
    echo ""
    echo "OPTIONAL OPTIONS:"
    echo "  -d, --domain DOMAIN  Qiniu domain for URLs (or set QINIU_DOMAIN env var)"
    echo "                       Defaults to: openterface.qiniu.com"
    echo ""
    echo "OTHER OPTIONS:"
    echo "  -h, --help           Show this help message"
    echo "  -v, --verbose        Enable verbose output"
    echo "  --debug              Enable debug output"
    echo ""
    echo "ENVIRONMENT VARIABLES:"
    echo "  Method 1 (Token):"
    echo "    QINIU_UPLOAD_TOKEN   Pre-generated upload token"
    echo "  Method 2 (AK/SK) - RECOMMENDED:"
    echo "    QINIU_AK             Qiniu Access Key"
    echo "    QINIU_SK             Qiniu Secret Key"
    echo "  Optional:"
    echo "    QINIU_DOMAIN         Your Qiniu domain (defaults to openterface.qiniu.com)"
    echo ""
    echo "EXAMPLES:"
    echo "  # Method 1: Using pre-generated token"
    echo "  $0 -t YOUR_TOKEN image.png"
    echo ""
    echo "  # Method 2: Using AK/SK (recommended, token generated automatically)"
    echo "  $0 -a YOUR_AK -s YOUR_SK photo.jpeg"
    echo ""
    echo "  # Using environment variables (recommended for CI/CD)"
    echo "  QINIU_AK=ak QINIU_SK=sk $0 photo.jpeg"
    echo ""
    exit 1
}

# Parse command line arguments
VERBOSE=false
DEBUG=false
UPLOAD_TOKEN=""
QINIU_AK=""
QINIU_SK=""
DOMAIN=""

while [[ $# -gt 0 ]]; do
    case $1 in
        -t|--token)
            UPLOAD_TOKEN="$2"
            shift 2
            ;;
        -a|--ak)
            QINIU_AK="$2"
            shift 2
            ;;
        -s|--sk)
            QINIU_SK="$2"
            shift 2
            ;;
        -d|--domain)
            DOMAIN="$2"
            shift 2
            ;;
        -v|--verbose)
            VERBOSE=true
            shift
            ;;
        --debug)
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
    log_error "Current directory: $(pwd)"
    log_error "Files in current directory:"
    ls -la | head -20
    exit 1
fi

# Check if image file is readable
if [ ! -r "$IMAGE_FILE" ]; then
    log_error "Image file is not readable: $IMAGE_FILE"
    log_error "File permissions:"
    ls -la "$IMAGE_FILE"
    exit 1
fi

# Check file is not empty
FILE_SIZE=$(stat -f%z "$IMAGE_FILE" 2>/dev/null || stat -c%s "$IMAGE_FILE" 2>/dev/null || echo 0)
if [ "$FILE_SIZE" -eq 0 ]; then
    log_error "Image file is empty (0 bytes): $IMAGE_FILE"
    exit 1
fi

log_info "Image file validated: $IMAGE_FILE ($(($FILE_SIZE / 1024))KB)"

# Get credentials from environment if not provided via command line
if [ -z "$UPLOAD_TOKEN" ]; then
    UPLOAD_TOKEN="$QINIU_UPLOAD_TOKEN"
fi

if [ -z "$QINIU_AK" ]; then
    QINIU_AK="${QINIU_AK}"
fi

if [ -z "$QINIU_SK" ]; then
    QINIU_SK="${QINIU_SK}"
fi

# Set bucket (hardcoded)
BUCKET="$QINIU_BUCKET_HARDCODED"

# Set domain with default
if [ -z "$DOMAIN" ]; then
    DOMAIN="${QINIU_DOMAIN:-download.openterface.com}"
fi

# Check authentication: either token OR (AK and SK) must be provided
AUTH_METHOD=""
if [ -n "$UPLOAD_TOKEN" ]; then
    AUTH_METHOD="token"
    log_info "Using authentication method: Pre-generated Upload Token"
elif [ -n "$QINIU_AK" ] && [ -n "$QINIU_SK" ]; then
    AUTH_METHOD="ak_sk"
    log_info "Using authentication method: Generate token from AK/SK"
    
    # Generate the upload token
    log_info "Generating upload token from AK/SK..."
    if ! UPLOAD_TOKEN=$(generate_upload_token "$QINIU_AK" "$QINIU_SK" "$BUCKET"); then
        log_error "Failed to generate upload token from AK/SK"
        exit 1
    fi
    log_success "Upload token generated successfully"
    
    if $VERBOSE; then
        log_info "Generated token length: ${#UPLOAD_TOKEN} characters"
    fi
else
    log_error "No authentication credentials provided!"
    echo ""
    log_error "Please provide either:"
    log_error "  1. Pre-generated upload token: -t TOKEN or QINIU_UPLOAD_TOKEN env var"
    log_error "  2. AK and SK: -a AK -s SK or QINIU_AK/QINIU_SK env vars"
    echo ""
    log_info "Get credentials from: https://portal.qiniu.com/"
    exit 1
fi

log_info "Starting Qiniu upload process..."
log_info "Image file: $IMAGE_FILE"
log_info "Bucket: $BUCKET"
log_info "Domain: $DOMAIN"
log_info "Upload token length: ${#UPLOAD_TOKEN} characters"

# Get file information
FILE_SIZE=$(stat -c%s "$IMAGE_FILE" 2>/dev/null || stat -f%z "$IMAGE_FILE" 2>/dev/null || echo "unknown")
FILE_SIZE_HUMAN=$(ls -lh "$IMAGE_FILE" | awk '{print $5}')
log_info "File size: $FILE_SIZE_HUMAN ($FILE_SIZE bytes)"

# Check if file size exceeds recommended limits
if [ "$FILE_SIZE" -gt 2097152 ]; then
    log_warning "File size ($FILE_SIZE bytes) is large (Qiniu recommends max 2MB)"
fi

# Get filename for Qiniu key (use timestamp to avoid conflicts)
TIMESTAMP=$(date +%s%N)
FILE_NAME=$(basename "$IMAGE_FILE")
FILE_EXT="${FILE_NAME##*.}"
QINIU_KEY="uploads/$(date +%Y%m%d)/${TIMESTAMP}.${FILE_EXT}"

log_info "Using Qiniu key: $QINIU_KEY"

if $DEBUG; then
    log_info "Curl command: curl -s -w '\\nHTTP_STATUS:%{http_code}\\nRESPONSE_TIME:%{time_total}' --connect-timeout $CURL_CONNECT_TIMEOUT --max-time $CURL_MAX_TIME -X POST '$QINIU_UPLOAD_URL' -F 'token=$UPLOAD_TOKEN' -F 'key=$QINIU_KEY' -F 'file=@$IMAGE_FILE'"
fi

# Upload to Qiniu
log_info "Attempting upload (attempt 1: direct connection)..."
CURL_EXIT_CODE=0
if [ -n "$TIMEOUT_CMD" ]; then
    UPLOAD_RESPONSE=$($TIMEOUT_CMD 120 curl -s --noproxy "*" -w "\nHTTP_STATUS:%{http_code}\nRESPONSE_TIME:%{time_total}" \
        --connect-timeout $CURL_CONNECT_TIMEOUT \
        --max-time $CURL_MAX_TIME \
        -X POST "$QINIU_UPLOAD_URL" \
        -F "token=$UPLOAD_TOKEN" \
        -F "key=$QINIU_KEY" \
        -F "file=@$IMAGE_FILE" \
        2>&1) || CURL_EXIT_CODE=$?
else
    UPLOAD_RESPONSE=$(curl -s --noproxy "*" -w "\nHTTP_STATUS:%{http_code}\nRESPONSE_TIME:%{time_total}" \
        --connect-timeout $CURL_CONNECT_TIMEOUT \
        --max-time $CURL_MAX_TIME \
        -X POST "$QINIU_UPLOAD_URL" \
        -F "token=$UPLOAD_TOKEN" \
        -F "key=$QINIU_KEY" \
        -F "file=@$IMAGE_FILE" \
        2>&1) || CURL_EXIT_CODE=$?
fi

if [ $CURL_EXIT_CODE -ne 0 ]; then
    log_warning "Curl failed with exit code: $CURL_EXIT_CODE"
    if [ $CURL_EXIT_CODE -eq 124 ]; then
        log_error "Upload timed out after 120 seconds"
    fi
fi

# If that fails or returns no response, try with proxy (fallback)
if [ -z "$UPLOAD_RESPONSE" ] || echo "$UPLOAD_RESPONSE" | grep -q "000"; then
    log_warning "Direct connection failed or no HTTP response, trying with proxy..."
    CURL_EXIT_CODE=0
    if [ -n "$TIMEOUT_CMD" ]; then
        UPLOAD_RESPONSE=$($TIMEOUT_CMD 120 curl -s -w "\nHTTP_STATUS:%{http_code}\nRESPONSE_TIME:%{time_total}" \
            --connect-timeout $CURL_CONNECT_TIMEOUT \
            --max-time $CURL_MAX_TIME \
            -X POST "$QINIU_UPLOAD_URL" \
            -F "token=$UPLOAD_TOKEN" \
            -F "key=$QINIU_KEY" \
            -F "file=@$IMAGE_FILE" \
            2>&1) || CURL_EXIT_CODE=$?
    else
        UPLOAD_RESPONSE=$(curl -s -w "\nHTTP_STATUS:%{http_code}\nRESPONSE_TIME:%{time_total}" \
            --connect-timeout $CURL_CONNECT_TIMEOUT \
            --max-time $CURL_MAX_TIME \
            -X POST "$QINIU_UPLOAD_URL" \
            -F "token=$UPLOAD_TOKEN" \
            -F "key=$QINIU_KEY" \
            -F "file=@$IMAGE_FILE" \
            2>&1) || CURL_EXIT_CODE=$?
    fi
    
    if [ $CURL_EXIT_CODE -ne 0 ]; then
        log_warning "Curl retry failed with exit code: $CURL_EXIT_CODE"
    fi
fi

if [ -z "$UPLOAD_RESPONSE" ]; then
    log_error "No response received from Qiniu API"
    exit 1
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

# Parse the response - Qiniu uses different formats depending on success/failure
# Success: {"hash":"...", "key":"...", ...}
# Failure: {"error":"...", "code":...}

# Try to determine success - look for "hash" or "key" field (indicates success)
if echo "$JSON_RESPONSE" | grep -q '"hash"'; then
    UPLOAD_SUCCESS="true"
elif echo "$JSON_RESPONSE" | grep -q '"key"'; then
    UPLOAD_SUCCESS="true"
else
    UPLOAD_SUCCESS="false"
fi

# Show response preview for debugging
if $VERBOSE; then
    RESPONSE_PREVIEW=$(echo "$JSON_RESPONSE" | head -c 200)
    log_info "Response preview: $RESPONSE_PREVIEW..."
fi

if [ "$UPLOAD_SUCCESS" = "true" ]; then
    log_success "Upload successful!"

    # Extract hash and key from response
    HASH=$(echo "$JSON_RESPONSE" | sed -n 's/.*"hash":"\([^"]*\)".*/\1/p' | head -1)
    KEY=$(echo "$JSON_RESPONSE" | sed -n 's/.*"key":"\([^"]*\)".*/\1/p' | head -1)

    if $VERBOSE; then
        log_info "Hash: $HASH"
        log_info "Key: $KEY"
    fi

    # Construct the final URL using the domain
    FINAL_URL="${DOMAIN}/${KEY}"

    if [ -n "$FINAL_URL" ]; then
        log_success "Final URL: $FINAL_URL"

        # Output the URL for easy copying (this is the main output)
        echo ""
        echo "🔗 IMAGE URL: $FINAL_URL"
        echo ""

        # Additional info
        echo "📦 Qiniu Key: $KEY"
        echo "� Hash: $HASH"
        echo ""

        # Success exit with URL as output
        echo "$FINAL_URL"
        exit 0
    else
        log_warning "Upload successful but URL could not be constructed"
        if $DEBUG; then
            log_info "Raw response: $JSON_RESPONSE"
        fi
        exit 1
    fi
else
    # Extract error message from Qiniu response
    ERROR_MESSAGE=$(echo "$JSON_RESPONSE" | sed -n 's/.*"error":"\([^"]*\)".*/\1/p' | head -1)
    
    # If still empty, just show the raw response
    if [ -z "$ERROR_MESSAGE" ]; then
        ERROR_MESSAGE="Check debug output for details"
    fi

    log_error "Upload failed"
    log_error "Success: $UPLOAD_SUCCESS"
    log_error "Error: $ERROR_MESSAGE"

    if $DEBUG; then
        log_error "Full response: $JSON_RESPONSE"
    else
        # Always show response on HTTP 400 for debugging
        if [ "$HTTP_STATUS" = "400" ]; then
            log_error "Raw response: $JSON_RESPONSE"
        fi
    fi

    # Provide troubleshooting tips
    echo ""
    echo "🔧 Troubleshooting Tips:"
    if [ "$HTTP_STATUS" = "400" ]; then
        echo "  - The upload token might be invalid or expired"
        echo "  - Check your upload token at: https://portal.qiniu.com/"
        echo "  - The image format might not be supported"
        echo "  - Try with a smaller image file"
    elif [ "$HTTP_STATUS" = "401" ]; then
        echo "  - Authentication failed - upload token is invalid"
        echo "  - Get a new upload token from: https://portal.qiniu.com/"
    elif [ "$HTTP_STATUS" = "413" ]; then
        echo "  - Image file is too large for Qiniu"
        echo "  - Try with a smaller image (max 2MB)"
    elif [ "$HTTP_STATUS" = "429" ]; then
        echo "  - Too many requests - rate limit exceeded"
        echo "  - Wait a few minutes and try again"
    elif [ "$HTTP_STATUS" = "000" ] || [ "$HTTP_STATUS" = "unknown" ]; then
        echo "  - Check your internet connection"
        echo "  - Qiniu API might be temporarily unavailable"
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
