#!/bin/bash
#
# build.sh - Build all drivers and test applications for i.MX6ULL
#
# Prerequisites:
#   1. ARM cross-compiler: arm-linux-gnueabihf-gcc
#   2. Compiled i.MX6ULL kernel source tree
#
# Usage:
#   ./build.sh                          # interactive
#   KERNEL_DIR=/path/to/kernel ./build.sh
#   KERNEL_DIR=/path/to/kernel CROSS_COMPILE=arm-linux-gnueabihf- ARCH=arm ./build.sh

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
DRIVER_DIR="$SCRIPT_DIR/driver"
APP_DIR="$SCRIPT_DIR/app"

# ---- Configuration ----
KERNEL_DIR="${KERNEL_DIR:-/home/alientek/linux/imx6ull}"
CROSS_COMPILE="${CROSS_COMPILE:-arm-linux-gnueabihf-}"
ARCH="${ARCH:-arm}"

# ---- Colors ----
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

echo_info()  { echo -e "${GREEN}[INFO]${NC} $*"; }
echo_warn()  { echo -e "${YELLOW}[WARN]${NC} $*"; }
echo_error() { echo -e "${RED}[ERROR]${NC} $*"; }

# ---- Check environment ----
check_env() {
    echo_info "Checking build environment..."

    if [ ! -d "$KERNEL_DIR" ]; then
        echo_error "Kernel source not found: $KERNEL_DIR"
        echo "Set KERNEL_DIR to your compiled i.MX6ULL kernel tree."
        echo "  export KERNEL_DIR=/path/to/linux-imx"
        exit 1
    fi

    if ! command -v "${CROSS_COMPILE}gcc" &>/dev/null; then
        echo_warn "${CROSS_COMPILE}gcc not found in PATH"
        echo "Install the ARM cross-compiler or adjust CROSS_COMPILE."
    else
        echo_info "Cross-compiler: ${CROSS_COMPILE}gcc"
    fi

    echo_info "Kernel source: $KERNEL_DIR"
}

# ---- Build kernel modules ----
build_modules() {
    echo_info "Building kernel modules..."
    cd "$DRIVER_DIR"
    make KERNEL_DIR="$KERNEL_DIR" ARCH="$ARCH" CROSS_COMPILE="$CROSS_COMPILE"
    echo_info "Modules built:"
    ls -la *.ko 2>/dev/null || echo_error "No .ko files found"
}

# ---- Build test applications ----
build_apps() {
    echo_info "Building test applications..."
    cd "$APP_DIR"
    make CC="${CROSS_COMPILE}gcc"
    echo_info "Applications built:"
    ls -la test_* 2>/dev/null || echo_error "No test apps found"
}

# ---- Main ----
main() {
    echo_info "=== i.MX6ULL Driver Demo Build ==="
    check_env
    build_modules
    build_apps

    echo ""
    echo_info "Build complete!"
    echo ""
    echo "Deploy to board:"
    echo "  scp driver/*.ko root@<board_ip>:/lib/modules/\$(uname -r)/"
    echo "  scp app/test_* root@<board_ip>:/root/"
    echo ""
    echo "On the board:"
    echo "  depmod"
    echo "  modprobe comp_drv"
    echo "  modprobe key_input"
    echo "  modprobe ap3216c"
    echo ""
    echo "  ./test_led_rw on"
    echo "  ./test_led_rw blink"
    echo "  ./test_key /dev/input/eventX"
    echo "  ./test_async &   # then run test_led_rw on"
    echo "  ./test_ap3216c"
}

main "$@"
