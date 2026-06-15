#!/bin/bash
# Strip NeverC kernel module (.ko) for production deployment.
# Removes debug sections, local symbols, and non-essential metadata
# while preserving the sections the kernel module loader requires.
#
# Usage: ./strip-ko.sh module.ko [output.ko]

set -euo pipefail

if [ $# -lt 1 ]; then
  echo "Usage: $0 <input.ko> [output.ko]"
  exit 1
fi

INPUT="$1"
OUTPUT="${2:-$INPUT}"

find_llvm_tool() {
  local tool="$1"
  local paths=(
    "/opt/homebrew/opt/llvm/bin/$tool"
    "/opt/homebrew/opt/llvm@20/bin/$tool"
    "/opt/homebrew/Cellar/llvm@20/20.1.8/bin/$tool"
    "/opt/homebrew/Cellar/llvm/22.1.5/bin/$tool"
    "/usr/local/opt/llvm/bin/$tool"
  )
  if command -v "$tool" >/dev/null 2>&1; then
    echo "$tool"
    return
  fi
  for p in "${paths[@]}"; do
    if [ -x "$p" ]; then
      echo "$p"
      return
    fi
  done
  echo ""
}

STRIP="$(find_llvm_tool llvm-strip)"
OBJCOPY="$(find_llvm_tool llvm-objcopy)"

if [ -z "$STRIP" ] && [ -z "$OBJCOPY" ]; then
  echo "error: neither llvm-strip nor llvm-objcopy found"
  echo "  install LLVM or set STRIP=/path/to/llvm-strip"
  exit 1
fi

TMP=$(mktemp)
trap "rm -f $TMP" EXIT

cp "$INPUT" "$TMP"

if [ -n "$OBJCOPY" ]; then
  "$OBJCOPY" \
    --strip-debug \
    --strip-unneeded \
    --remove-section=.comment \
    --remove-section=.note.GNU-stack \
    --remove-section=.note.gnu.property \
    --remove-section=.eh_frame \
    --remove-section=.eh_frame_hdr \
    "$TMP" 2>/dev/null || true
fi

if [ -n "$STRIP" ]; then
  "$STRIP" \
    --strip-unneeded \
    --keep-symbol=init_module \
    --keep-symbol=cleanup_module \
    --keep-symbol=__this_module \
    "$TMP" 2>/dev/null || true
fi

cp "$TMP" "$OUTPUT"

BEFORE=$(wc -c < "$INPUT" | tr -d ' ')
AFTER=$(wc -c < "$OUTPUT" | tr -d ' ')
SAVED=$((BEFORE - AFTER))
if [ $BEFORE -gt 0 ]; then
  PCT=$((SAVED * 100 / BEFORE))
else
  PCT=0
fi

echo "stripped: $BEFORE -> $AFTER bytes (-${SAVED}B / -${PCT}%)"
