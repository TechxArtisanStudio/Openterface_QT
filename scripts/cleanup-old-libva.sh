#!/bin/bash
# =============================================================================
# Cleanup Old Bundled libva Libraries
# =============================================================================
# This script removes old bundled libva libraries that cause symbol conflicts
# with system libavutil (vaMapBuffer2 issue).
#
# After running this script, the application will use system libva libraries.
#
# Usage:
#   sudo ./cleanup-old-libva.sh
#
# =============================================================================

set -e

# Color codes for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

print_info() {
    echo -e "${BLUE}ℹ️  $1${NC}"
}

print_success() {
    echo -e "${GREEN}✅ $1${NC}"
}

print_warning() {
    echo -e "${YELLOW}⚠️  $1${NC}"
}

print_error() {
    echo -e "${RED}❌ $1${NC}"
}

# Check if running as root
if [ "$(id -u)" -ne 0 ]; then
    print_error "This script must be run as root (use sudo)"
    exit 1
fi

BUNDLE_DIR="/usr/lib/openterfaceqt"

print_info "Cleaning up old bundled libva libraries..."
echo ""

# Check if bundle directory exists
if [ ! -d "$BUNDLE_DIR" ]; then
    print_info "Bundle directory $BUNDLE_DIR does not exist - nothing to clean up"
    exit 0
fi

# Backup directory
BACKUP_DIR="${BUNDLE_DIR}/backup-libva-$(date +%Y%m%d-%H%M%S)"

# Check for old libva libraries
LIBVA_FILES=$(find "$BUNDLE_DIR" -maxdepth 1 -name "libva*.so*" -type f -o -name "libva*.so*" -type l 2>/dev/null || true)

if [ -z "$LIBVA_FILES" ]; then
    print_success "No bundled libva libraries found - already clean!"
    exit 0
fi

print_info "Found bundled libva libraries:"
echo "$LIBVA_FILES" | sed 's/^/  /'
echo ""

# Create backup directory
print_info "Creating backup at: $BACKUP_DIR"
mkdir -p "$BACKUP_DIR"

# Move old libraries to backup
print_info "Moving old libraries to backup..."
for file in $LIBVA_FILES; do
    if [ -e "$file" ]; then
        mv "$file" "$BACKUP_DIR/" 2>/dev/null || true
        print_success "Moved: $(basename "$file")"
    fi
done

echo ""
print_success "Old libva libraries moved to backup"
print_info "Backup location: $BACKUP_DIR"
echo ""

# Update ldconfig
print_info "Updating ldconfig cache..."
ldconfig
print_success "ldconfig cache updated"
echo ""

# Verify system libva is available
print_info "Verifying system libva availability..."
if ldconfig -p | grep -q "libva.so.2"; then
    SYSTEM_LIBVA=$(ldconfig -p | grep "libva.so.2" | head -1 | awk '{print $NF}')
    print_success "System libva found: $SYSTEM_LIBVA"

    # Check for vaMapBuffer2 symbol
    if nm -D "$SYSTEM_LIBVA" 2>/dev/null | grep -q "vaMapBuffer2"; then
        print_success "System libva has vaMapBuffer2 symbol (compatible with system libavutil)"
    else
        print_warning "System libva may not have vaMapBuffer2 symbol"
        print_warning "You may need to update your system libva package"
    fi
else
    print_error "System libva not found!"
    print_error "Please install libva packages:"
    echo "  Fedora: sudo dnf install libva libva-drm libva-x11"
    echo "  Ubuntu: sudo apt install libva2 libva-drm2 libva-x11-2"
    exit 1
fi

echo ""
print_success "Cleanup completed successfully!"
echo ""
print_info "The application will now use system libva libraries."
print_info "If you need to restore the old libraries, they are in:"
print_info "  $BACKUP_DIR"
echo ""
