#!/usr/bin/env bash
set -euo pipefail

show_usage() {
    cat <<'EOF'
Usage:
  ./docker/build-ipk.sh <OPENWRT_SDK_URL> [package-target]

Example:
  ./docker/build-ipk.sh https://downloads.openwrt.org/releases/19.07.8/targets/ar71xx/generic/openwrt-sdk-19.07.8-ar71xx-generic_gcc-7.5.0_musl.Linux-x86_64.tar.xz package/vibration-monitor/compile

Notes:
  - On macOS or other non-Linux hosts, the script automatically launches Ubuntu in Docker.
  - The package target defaults to package/vibration-monitor/compile.
  - The resulting .ipk is copied to <repo>/out/.
EOF
}

if [[ $# -lt 1 ]]; then
    show_usage
    exit 1
fi

SDK_URL="$1"
PACKAGE_TARGET="${2:-package/vibration-monitor/compile}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

if [[ -z "${FRONTOP_IN_CONTAINER:-}" ]]; then
    if command -v docker >/dev/null 2>&1; then
        docker run --rm -it \
            -v "$REPO_ROOT":/workspace \
            -w /workspace \
            -e FRONTOP_IN_CONTAINER=1 \
            ubuntu:22.04 \
            bash /workspace/docker/build-ipk.sh "$SDK_URL" "$PACKAGE_TARGET"
        exit 0
    fi

    echo "Docker is not installed or not available on PATH." >&2
    echo "Install Docker Desktop, then rerun this script." >&2
    exit 1
fi

WORKDIR="$REPO_ROOT"
OUT_DIR="$WORKDIR/out"

mkdir -p "$OUT_DIR"

# Build entirely in a container-local case-sensitive directory to bypass macOS mount limitations
BUILD_BASE="$(mktemp -d /tmp/frontop-build-XXXX)"
SDK_DIR="$BUILD_BASE/openwrt-sdk"
echo "Building in container-local case-sensitive directory: $BUILD_BASE"

# Ensure cleanup on exit
trap 'rm -rf "$BUILD_BASE"' EXIT

apt-get update
apt-get install -y --no-install-recommends \
    build-essential \
    ca-certificates \
    file \
    gawk \
    gettext \
    git \
    libc6-dev \
    libncurses-dev \
    libncurses5-dev \
    make \
    patch \
    perl \
    python2 \
    python3 \
    rsync \
    tar \
    unzip \
    wget \
    xz-utils \
    zlib1g-dev \
    libelf-dev \
    libpcre3-dev \
    quilt \
    asciidoc \
    docbook2x \
    util-linux \
    bzip2 \
    flex \
    bison \
    pkg-config \
    libssl-dev \
    intltool

cd /tmp
wget -O openwrt-sdk.tar.xz "$SDK_URL"
tar -xf openwrt-sdk.tar.xz -C "$(dirname "$SDK_DIR")"

SDK_SOURCE_DIR="$(find "$BUILD_BASE" -maxdepth 1 -type d -name 'openwrt-sdk-*' | head -n 1)"
if [[ -z "${SDK_SOURCE_DIR:-}" ]]; then
    echo "Failed to locate extracted OpenWrt SDK directory" >&2
    exit 1
fi

mv "$SDK_SOURCE_DIR" "$SDK_DIR"

# Copy vibration-monitor source to the case-sensitive build directory
mkdir -p "$SDK_DIR/package"
cp -r "$WORKDIR/vibration-monitor" "$SDK_DIR/package/vibration-monitor"

cd "$SDK_DIR"
./scripts/feeds update -a
./scripts/feeds install -a

make defconfig
make "$PACKAGE_TARGET" V=s

IPK_PATH="$(find bin -name 'vibration-monitor_*.ipk' | head -n 1)"
if [[ -z "${IPK_PATH:-}" ]]; then
    echo "Build finished, but no .ipk was found under $SDK_DIR/bin" >&2
    exit 1
fi

cp "$IPK_PATH" "$OUT_DIR/"
echo "Built package: $OUT_DIR/$(basename "$IPK_PATH")"