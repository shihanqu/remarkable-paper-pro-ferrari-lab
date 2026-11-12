#!/usr/bin/env bash
set -euo pipefail

# One-command build & install script for Ferrari Lab on reMarkable Paper Pro
# Usage:
#   ./scripts/install.sh [RMPP_HOST]
# Example:
#   ./scripts/install.sh 10.11.99.1      # Over USB-C
#   ./scripts/install.sh 192.168.1.100   # Over Wi-Fi

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

RMPP_HOST="${1:-${RMPP_HOST:-10.11.99.1}}"
RMPP_USER="${RMPP_USER:-root}"
RMPP_DEST="/home/root/xovi/exthome/appload/ferrari_lab"

# Locate or bootstrap a standalone Zig 0.13.0 cross-compiler if needed
if command -v zig >/dev/null 2>&1; then
    ZIG_BIN="$(command -v zig)"
elif [[ -x "$REPO_ROOT/.toolchains/zig/zig" ]]; then
    ZIG_BIN="$REPO_ROOT/.toolchains/zig/zig"
elif command -v aarch64-linux-gnu-g++ >/dev/null 2>&1; then
    ZIG_BIN=""
else
    echo "==> No aarch64 cross-compiler found; bootstrapping standalone Zig 0.13.0 into .toolchains/zig ..."
    OS="$(uname -s)"
    ARCH="$(uname -m)"
    case "${OS}_${ARCH}" in
        Darwin_arm64)  ZIG_TAR="zig-macos-aarch64-0.13.0.tar.xz" ;;
        Darwin_x86_64) ZIG_TAR="zig-macos-x86_64-0.13.0.tar.xz" ;;
        Linux_x86_64)  ZIG_TAR="zig-linux-x86_64-0.13.0.tar.xz" ;;
        Linux_aarch64) ZIG_TAR="zig-linux-aarch64-0.13.0.tar.xz" ;;
        *)
            echo "Unsupported host platform ${OS}_${ARCH}. Install 'zig' or 'aarch64-linux-gnu-g++' and re-run." >&2
            exit 1
            ;;
    esac
    mkdir -p "$REPO_ROOT/.toolchains"
    curl -fL "https://ziglang.org/download/0.13.0/${ZIG_TAR}" -o "$REPO_ROOT/.toolchains/${ZIG_TAR}"
    mkdir -p "$REPO_ROOT/.toolchains/zig"
    tar -xf "$REPO_ROOT/.toolchains/${ZIG_TAR}" -C "$REPO_ROOT/.toolchains/zig" --strip-components=1
    rm -f "$REPO_ROOT/.toolchains/${ZIG_TAR}"
    ZIG_BIN="$REPO_ROOT/.toolchains/zig/zig"
fi

echo "==> Building ferrari_lab for aarch64-linux-gnu (Cortex-A53 + NEON)..."
if [[ -n "${ZIG_BIN}" ]]; then
    "$ZIG_BIN" c++ -target aarch64-linux-gnu.2.38 \
        -O3 -ffast-math -mcpu=cortex_a53+neon -std=c++17 -Wall -Wno-unused-function \
        ferrari_lab/main.cpp -o ferrari_lab/ferrari_lab -lpthread -lm
else
    aarch64-linux-gnu-g++ \
        -O3 -ffast-math -mcpu=cortex_a53+neon -std=c++17 -Wall -Wno-unused-function \
        ferrari_lab/main.cpp -o ferrari_lab/ferrari_lab -lpthread -lm
fi

echo "==> Deploying to ${RMPP_USER}@${RMPP_HOST}:${RMPP_DEST} ..."
ssh "${RMPP_USER}@${RMPP_HOST}" "mkdir -p '${RMPP_DEST}' && (killall ferrari_lab 2>/dev/null || true)"
scp ferrari_lab/ferrari_lab ferrari_lab/external.manifest.json ferrari_lab/icon.png \
    "${RMPP_USER}@${RMPP_HOST}:${RMPP_DEST}/"
ssh "${RMPP_USER}@${RMPP_HOST}" "chmod +x '${RMPP_DEST}/ferrari_lab'"

echo ""
echo "==> Ferrari Lab installed successfully!"
echo "    1. If this is your first install, restart xochitl so AppLoad rescans manifests:"
echo "       ssh ${RMPP_USER}@${RMPP_HOST} 'systemctl restart xochitl'"
echo "    2. Open AppLoad on your reMarkable Paper Pro and tap 'Ferrari Lab'."
