#!/usr/bin/env bash
# Boot a released arm64 GKI Image and require module load + unload markers.
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
INIT_SOURCE="$SCRIPT_DIR/gki-qemu-init.c"
INITRAMFS_WRITER="$SCRIPT_DIR/build-gki-initramfs.py"

usage() {
	cat <<'EOF'
Usage: run-gki-qemu-smoke.sh --image PATH --module PATH --output-dir DIR [options]

Required:
  --image PATH       released arm64 GKI dist/Image
  --module PATH      zero-import NeverC smoke .ko
  --output-dir DIR   directory for initramfs and qemu.log

Options:
  --cross-cc BIN     static AArch64 C compiler (default: aarch64-linux-gnu-gcc)
  --qemu BIN         QEMU system emulator (default: qemu-system-aarch64)
  --python BIN       Python 3 interpreter (default: python3)
  --timeout-bin BIN  GNU timeout executable (default: timeout)
  --timeout SECONDS  boot deadline (default: 120)
  -h, --help         show this help
EOF
}

die() {
	printf 'run-gki-qemu-smoke: error: %s\n' "$*" >&2
	exit 2
}

IMAGE=
MODULE=
OUTPUT_DIR=
CROSS_CC=aarch64-linux-gnu-gcc
QEMU=qemu-system-aarch64
PYTHON=python3
TIMEOUT_BIN=timeout
TIMEOUT_SECONDS=120

while [ "$#" -gt 0 ]; do
	case "$1" in
	--image) [ "$#" -ge 2 ] || die "--image needs a path"; IMAGE=$2; shift 2 ;;
	--module) [ "$#" -ge 2 ] || die "--module needs a path"; MODULE=$2; shift 2 ;;
	--output-dir) [ "$#" -ge 2 ] || die "--output-dir needs a path"; OUTPUT_DIR=$2; shift 2 ;;
	--cross-cc) [ "$#" -ge 2 ] || die "--cross-cc needs a binary"; CROSS_CC=$2; shift 2 ;;
	--qemu) [ "$#" -ge 2 ] || die "--qemu needs a binary"; QEMU=$2; shift 2 ;;
	--python) [ "$#" -ge 2 ] || die "--python needs a binary"; PYTHON=$2; shift 2 ;;
	--timeout-bin) [ "$#" -ge 2 ] || die "--timeout-bin needs a binary"; TIMEOUT_BIN=$2; shift 2 ;;
	--timeout) [ "$#" -ge 2 ] || die "--timeout needs seconds"; TIMEOUT_SECONDS=$2; shift 2 ;;
	-h | --help) usage; exit 0 ;;
	*) die "unknown argument: $1" ;;
	esac
done

[ -n "$IMAGE" ] && [ -r "$IMAGE" ] || die "missing or unreadable kernel Image: ${IMAGE:-<unset>}"
[ -n "$MODULE" ] && [ -r "$MODULE" ] || die "missing or unreadable smoke module: ${MODULE:-<unset>}"
[ -n "$OUTPUT_DIR" ] || die "missing --output-dir"
case "$TIMEOUT_SECONDS" in
'' | *[!0-9]*) die "--timeout must be a positive integer" ;;
esac
[ "$TIMEOUT_SECONDS" -gt 0 ] || die "--timeout must be greater than zero"

resolve_tool() {
	local value=$1 label=$2
	if [[ "$value" == */* ]]; then
		[ -x "$value" ] || die "$label is not executable: $value"
		printf '%s\n' "$value"
		return
	fi
	command -v "$value" 2>/dev/null || die "$label is not on PATH: $value"
}

CROSS_CC=$(resolve_tool "$CROSS_CC" "AArch64 static compiler")
QEMU=$(resolve_tool "$QEMU" "qemu-system-aarch64")
PYTHON=$(resolve_tool "$PYTHON" "Python 3")
TIMEOUT_BIN=$(resolve_tool "$TIMEOUT_BIN" "GNU timeout")
[ -r "$INIT_SOURCE" ] || die "guest init source is missing: $INIT_SOURCE"
[ -r "$INITRAMFS_WRITER" ] || die "initramfs writer is missing: $INITRAMFS_WRITER"

mkdir -p "$OUTPUT_DIR"
OUTPUT_DIR=$(cd "$OUTPUT_DIR" && pwd)
INIT_BINARY="$OUTPUT_DIR/init"
INITRAMFS="$OUTPUT_DIR/initramfs.cpio.gz"
QEMU_LOG="$OUTPUT_DIR/qemu.log"

printf '[host] compiler: '
"$CROSS_CC" --version | sed -n '1p'
printf '[host] qemu: '
"$QEMU" --version | sed -n '1p'
printf '[host] timeout: '
"$TIMEOUT_BIN" --version | sed -n '1p'

"$CROSS_CC" \
	-static -no-pie -Os -Wall -Wextra -Werror \
	-o "$INIT_BINARY" "$INIT_SOURCE"
"$PYTHON" "$INITRAMFS_WRITER" \
	--init "$INIT_BINARY" \
	--module "$MODULE" \
	--output "$INITRAMFS"

set +e
"$TIMEOUT_BIN" --foreground "${TIMEOUT_SECONDS}s" \
	"$QEMU" \
	-machine virt,gic-version=3 \
	-cpu cortex-a57 \
	-accel tcg,thread=multi \
	-smp 1 \
	-m 768M \
	-display none \
	-monitor none \
	-serial stdio \
	-no-reboot \
	-kernel "$IMAGE" \
	-initrd "$INITRAMFS" \
	-append "console=ttyAMA0 earlycon=pl011,0x09000000 rdinit=/init panic=-1 oops=panic" \
	>"$QEMU_LOG" 2>&1
QEMU_STATUS=$?
set -e

fail_with_log() {
	printf 'run-gki-qemu-smoke: error: %s\n' "$1" >&2
	printf '%s\n' '--- qemu.log (tail) ---' >&2
	tail -n 120 "$QEMU_LOG" >&2 || true
	exit 1
}

if grep -Eq 'NEVERC_GKI_(LOAD|UNLOAD)_FAIL' "$QEMU_LOG"; then
	fail_with_log "guest emitted a load/unload failure marker"
fi
if [ "$QEMU_STATUS" -eq 124 ] || [ "$QEMU_STATUS" -eq 137 ]; then
	fail_with_log "QEMU timed out after ${TIMEOUT_SECONDS}s"
fi
if [ "$QEMU_STATUS" -ne 0 ]; then
	fail_with_log "QEMU exited with status $QEMU_STATUS"
fi
if ! grep -Eq '^NEVERC_GKI_LOAD_PASS\r?$' "$QEMU_LOG" || \
	! grep -Eq '^NEVERC_GKI_UNLOAD_PASS\r?$' "$QEMU_LOG"; then
	fail_with_log "missing guest success markers"
fi

printf '[host] PASS: module loaded and unloaded; log=%s\n' "$QEMU_LOG"
