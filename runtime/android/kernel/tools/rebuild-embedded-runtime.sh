#!/bin/bash
# Rebuild embedded NVK bitcode, relink NeverC, and reject stale output.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../../../.." && pwd)"
BUILD_DIR="${1:-$REPO_ROOT/build-neverc}"
NM="${NM:-nm}"

case "$BUILD_DIR" in
  /*) ;;
  *) BUILD_DIR="$(pwd)/$BUILD_DIR" ;;
esac

if [ ! -d "$BUILD_DIR" ]; then
  echo "error: build directory not found: $BUILD_DIR" >&2
  exit 1
fi

TMPDIR=$(mktemp -d)
trap 'rm -rf "$TMPDIR"' EXIT

echo "=== Rebuild embedded NVK runtime ==="
echo "Build directory: $BUILD_DIR"

cmake --build "$BUILD_DIR" --target neverc-bootstrap-nvk-kernel-bc
cmake --build "$BUILD_DIR" --target neverc

NEVERC="$BUILD_DIR/bin/neverc"
if [ ! -x "$NEVERC" ]; then
  echo "error: rebuilt compiler not found: $NEVERC" >&2
  exit 1
fi

PROBE="$TMPDIR/android-kernel-chardev-510.ko"
"$NEVERC" \
  --target=aarch64-linux-android \
  -fandroid-kernel-driver-mode \
  -DNVK_KERNEL=510 \
  -Wall \
  -Wno-unused \
  -r \
  -nostdlib \
  -o "$PROBE" \
  "$REPO_ROOT/examples/android-kernel-chardev/main.c"

python3 "$SCRIPT_DIR/check-user-copy-backend.py" \
  --artifact "$PROBE" \
  --nm "$NM"

echo "embedded NVK runtime rebuild and artifact check passed"
