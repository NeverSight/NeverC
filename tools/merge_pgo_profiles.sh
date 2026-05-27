#!/usr/bin/env bash
# Merge many LLVM PGO .profraw files into one .profdata file.
#
# On Windows (and other platforms with short argv limits), passing hundreds of
# paths to "llvm-profdata merge" fails with "Argument list too long".  This
# script merges in fixed-size batches, then merges the intermediate results.
#
# Usage:
#   ./tools/merge_pgo_profiles.sh \
#     --llvm-profdata /path/to/llvm-profdata \
#     --profile-dir /path/to/pgo-profiles \
#     --output /path/to/merged.profdata \
#     [--batch-size 48]
set -euo pipefail

usage() {
  echo "Usage: $0 --llvm-profdata PATH --profile-dir DIR --output FILE [--batch-size N]" >&2
  exit 1
}

LLVM_PROFDATA=""
PROFILE_DIR=""
OUTPUT=""
BATCH_SIZE=48

while [ $# -gt 0 ]; do
  case "$1" in
    --llvm-profdata) LLVM_PROFDATA="$2"; shift 2 ;;
    --profile-dir)   PROFILE_DIR="$2"; shift 2 ;;
    --output)        OUTPUT="$2"; shift 2 ;;
    --batch-size)    BATCH_SIZE="$2"; shift 2 ;;
    -h|--help) usage ;;
    *) echo "Unknown argument: $1" >&2; usage ;;
  esac
done

if [ -z "$LLVM_PROFDATA" ] || [ -z "$PROFILE_DIR" ] || [ -z "$OUTPUT" ]; then
  usage
fi

if [ ! -x "$LLVM_PROFDATA" ] && [ ! -f "$LLVM_PROFDATA" ] && [ ! -f "${LLVM_PROFDATA}.exe" ]; then
  echo "ERROR: llvm-profdata not found: $LLVM_PROFDATA" >&2
  exit 1
fi
if [ -f "${LLVM_PROFDATA}.exe" ]; then
  LLVM_PROFDATA="${LLVM_PROFDATA}.exe"
fi

profraws=()
while IFS= read -r -d '' f; do
  profraws+=("$f")
done < <(find "$PROFILE_DIR" -maxdepth 1 -name '*.profraw' -print0 | sort -z)

count=${#profraws[@]}
if [ "$count" -eq 0 ]; then
  echo "ERROR: no .profraw files in $PROFILE_DIR" >&2
  exit 1
fi

echo "Merging $count profiles → $OUTPUT (batch size $BATCH_SIZE)"

merge_batch() {
  local out="$1"
  shift
  "$LLVM_PROFDATA" merge -output="$out" "$@"
}

if [ "$count" -le "$BATCH_SIZE" ]; then
  merge_batch "$OUTPUT" "${profraws[@]}"
  exit 0
fi

CHUNK_DIR="$PROFILE_DIR/.merge-chunks"
rm -rf "$CHUNK_DIR"
mkdir -p "$CHUNK_DIR"

chunks=()
batch=()
idx=0

flush_batch() {
  [ ${#batch[@]} -eq 0 ] && return
  local chunk="$CHUNK_DIR/chunk_${idx}.profdata"
  merge_batch "$chunk" "${batch[@]}"
  chunks+=("$chunk")
  batch=()
  idx=$((idx + 1))
}

for f in "${profraws[@]}"; do
  batch+=("$f")
  if [ ${#batch[@]} -ge "$BATCH_SIZE" ]; then
    flush_batch
  fi
done
flush_batch

echo "Merging ${#chunks[@]} intermediate chunk(s)..."
merge_batch "$OUTPUT" "${chunks[@]}"
rm -rf "$CHUNK_DIR"

if [ -f "$OUTPUT" ]; then
  ls -lh "$OUTPUT" 2>/dev/null || true
fi
