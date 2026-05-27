#!/bin/bash
#
# PGO (Profile-Guided Optimisation) two-phase build for NeverC.
#
# Usage:
#   ./tools/build_pgo.sh                 # full pipeline: generate → train → orderfile → use → benchmark
#   ./tools/build_pgo.sh generate        # Phase 1 only: build instrumented neverc
#   ./tools/build_pgo.sh train           # Phase 1b only: run workloads to collect profiles
#   ./tools/build_pgo.sh orderfile       # Phase 1c only: generate order file from profdata (macOS)
#   ./tools/build_pgo.sh use             # Phase 2 only: build optimised neverc from .profdata
#   ./tools/build_pgo.sh benchmark       # A/B compare baseline vs PGO build
#   ./tools/build_pgo.sh clean           # remove PGO build dirs and profiles
#
# Requires: cmake, ninja, xcrun (macOS) or llvm-profdata (Linux/Windows)
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CACHE="$REPO_ROOT/neverc/cmake/caches/NeverC.cmake"

BUILD_PGO_GEN="$REPO_ROOT/build-pgo-gen"
BUILD_PGO_USE="$REPO_ROOT/build-pgo-use"
BUILD_BASELINE="$REPO_ROOT/build-neverc"
PROFILE_DIR="$REPO_ROOT/pgo-profiles"
PROFDATA="$PROFILE_DIR/merged.profdata"

SQLITE_DIR="$REPO_ROOT/local_docs/sqlite"
REDIS_DIR="$REPO_ROOT/local_docs/redis"
ZLIB_DIR="$REPO_ROOT/local_docs/zlib"
CURL_DIR="$REPO_ROOT/local_docs/curl"

JOBS="${JOBS:-$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo "${NUMBER_OF_PROCESSORS:-8}")}"

find_profdata_tool() {
  if command -v llvm-profdata &>/dev/null; then
    echo "llvm-profdata"
  elif [ -n "${LLVM_ROOT:-}" ] && command -v "${LLVM_ROOT}/bin/llvm-profdata" &>/dev/null; then
    echo "${LLVM_ROOT}/bin/llvm-profdata"
  elif xcrun --find llvm-profdata &>/dev/null 2>&1; then
    xcrun --find llvm-profdata
  else
    echo "ERROR: llvm-profdata not found" >&2; exit 1
  fi
}

ensure_profdata_tool() {
  if [ -z "${LLVM_PROFDATA:-}" ]; then
    LLVM_PROFDATA="$(find_profdata_tool)"
  fi
}

ts() { python3 -c 'import time; print(int(time.time()*1000))'; }

phase_generate() {
  echo "══════════════════════════════════════════════════════"
  echo "  Phase 1: Building instrumented neverc (PGO generate)"
  echo "══════════════════════════════════════════════════════"

  # -C cache preload runs its conditionals before -D takes effect, so we
  # must pass PGO compiler flags explicitly (as documented in NeverC.cmake).
  # LTO is disabled: incompatible with profile instrumentation on some
  # platforms and slows the generate build.
  # mimalloc is disabled: MI_OSX_INTERPOSE can interfere with the LLVM
  # profile runtime's atexit flush (profraw ends up 0 bytes on macOS).
  cmake -S "$REPO_ROOT/llvm" -B "$BUILD_PGO_GEN" -G Ninja \
    -C "$CACHE" \
    -DNEVERC_ENABLE_LTO=OFF \
    -DNEVERC_ENABLE_MIMALLOC=OFF \
    -DCMAKE_C_FLAGS_RELEASE="-O2 -DNDEBUG -march=native -fprofile-instr-generate -DNEVERC_PGO_TRAINING -ffunction-sections -fdata-sections" \
    -DCMAKE_CXX_FLAGS_RELEASE="-O2 -DNDEBUG -march=native -fprofile-instr-generate -DNEVERC_PGO_TRAINING -ffunction-sections -fdata-sections" \
    -DCMAKE_EXE_LINKER_FLAGS="-fprofile-instr-generate" \
    2>&1 | tail -5

  ninja -C "$BUILD_PGO_GEN" -j"$JOBS" neverc 2>&1 | tail -3
  echo "✓ Instrumented neverc: $BUILD_PGO_GEN/bin/neverc"
}

phase_train() {
  echo "══════════════════════════════════════════════════════"
  echo "  Phase 1b: Training — collecting profiles"
  echo "══════════════════════════════════════════════════════"

  ensure_profdata_tool
  local NC="$BUILD_PGO_GEN/bin/neverc"
  [ ! -x "$NC" ] && [ -x "${NC}.exe" ] && NC="${NC}.exe"
  if [ ! -x "$NC" ]; then
    echo "ERROR: instrumented neverc not found at $NC" >&2
    echo "       Run '$0 generate' first." >&2
    exit 1
  fi

  rm -rf "$PROFILE_DIR"
  mkdir -p "$PROFILE_DIR"

  local count=0

  train_file() {
    local src="$1"; shift
    local label="$1"; shift
    for mode in "$@"; do
      count=$((count + 1))
      local tag="${label}_$(basename "$src" .c)_${mode}"
      local profraw="$PROFILE_DIR/${tag}.profraw"

      case "$mode" in
        preproc)  LLVM_PROFILE_FILE="$profraw" "$NC" -E  -w -fno-neverc-types -o /dev/null "$src" 2>/dev/null || true ;;
        sema)     LLVM_PROFILE_FILE="$profraw" "$NC" -fsyntax-only -w -fno-neverc-types "$src" 2>/dev/null || true ;;
        irgen)    LLVM_PROFILE_FILE="$profraw" "$NC" -emit-llvm -S -w -fno-neverc-types -o /dev/null "$src" 2>/dev/null || true ;;
        O0)       LLVM_PROFILE_FILE="$profraw" "$NC" -c -O0 -w -fno-neverc-types -o /dev/null "$src" 2>/dev/null || true ;;
        O2)       LLVM_PROFILE_FILE="$profraw" "$NC" -c -O2 -w -fno-neverc-types -o /dev/null "$src" 2>/dev/null || true ;;
        O3)       LLVM_PROFILE_FILE="$profraw" "$NC" -c -O3 -w -fno-neverc-types -o /dev/null "$src" 2>/dev/null || true ;;
      esac
    done
  }

  # SQLite — large single-TU workload (all pipeline stages)
  if [ -f "$SQLITE_DIR/sqlite3.c" ]; then
    echo "  → SQLite (sqlite3.c ~260k lines, all modes)..."
    train_file "$SQLITE_DIR/sqlite3.c" "sqlite" preproc sema irgen O0 O2 O3
    train_file "$SQLITE_DIR/shell.c"   "sqlite" O2
  fi

  # Redis — many medium files (compile-only, covers header-heavy workloads)
  if [ -d "$REDIS_DIR/src" ]; then
    echo "  → Redis ($(ls "$REDIS_DIR"/src/*.c | wc -l | tr -d ' ') files, O2)..."
    for f in "$REDIS_DIR"/src/*.c; do
      train_file "$f" "redis" O2
    done
  fi

  # zlib — small files
  if [ -d "$ZLIB_DIR" ]; then
    echo "  → zlib..."
    for f in "$ZLIB_DIR"/*.c; do
      train_file "$f" "zlib" O2
    done
  fi

  # curl — medium project with many headers
  if [ -d "$CURL_DIR/lib" ]; then
    echo "  → curl lib ($(ls "$CURL_DIR"/lib/*.c | wc -l | tr -d ' ') files, O2)..."
    for f in "$CURL_DIR"/lib/*.c; do
      LLVM_PROFILE_FILE="$PROFILE_DIR/curl_$(basename "$f" .c).profraw" \
        "$NC" -c -O2 -w -fno-neverc-types \
          -I"$CURL_DIR/include" -I"$CURL_DIR/lib" -DBUILDING_LIBCURL \
          -o /dev/null "$f" 2>/dev/null || true
      count=$((count + 1))
    done
  fi

  echo "  Collected $count profraw files."

  local profraw_count
  profraw_count=$(ls "$PROFILE_DIR"/*.profraw 2>/dev/null | wc -l | tr -d ' ')
  if [ "$profraw_count" -eq 0 ]; then
    echo "ERROR: no .profraw files generated. Check workload paths." >&2
    exit 1
  fi

  echo "  Merging $profraw_count profiles → $PROFDATA"
  "$REPO_ROOT/tools/merge_pgo_profiles.sh" \
    --llvm-profdata "$LLVM_PROFDATA" \
    --profile-dir "$PROFILE_DIR" \
    --output "$PROFDATA"
  echo "✓ Profile data: $PROFDATA ($(du -h "$PROFDATA" | cut -f1))"
}

phase_orderfile() {
  echo "══════════════════════════════════════════════════════"
  echo "  Phase 1c: Generating order file from PGO profile"
  echo "══════════════════════════════════════════════════════"

  ensure_profdata_tool
  if [ ! -f "$PROFDATA" ]; then
    echo "ERROR: $PROFDATA not found. Run '$0 train' first." >&2
    exit 1
  fi

  if [ "$(uname)" != "Darwin" ]; then
    echo "  SKIP: order file is macOS-only (Linux uses BOLT instead)."
    return
  fi

  mkdir -p "$PROFILE_DIR"
  local ORDER_FILE="$PROFILE_DIR/neverc.order"

  # Extract function names and execution counts from profdata,
  # sort by count (hot first), and format as a Mach-O order file
  # (one mangled symbol per line, prefixed with underscore).
  python3 -c "
import subprocess, re, sys

result = subprocess.run(
    ['$LLVM_PROFDATA', 'show', '--all-functions', '--counts', '$PROFDATA'],
    capture_output=True, text=True)

entries = []
current_func = None
for line in result.stdout.splitlines():
    m = re.match(r'^  (\\S+):', line)
    if m:
        current_func = m.group(1)
        continue
    m = re.match(r'^\\s+Function count:\\s+(\\d+)', line)
    if m and current_func:
        entries.append((int(m.group(1)), current_func))
        current_func = None

entries.sort(key=lambda x: x[0], reverse=True)
with open('$ORDER_FILE', 'w') as f:
    for count, name in entries:
        # Mach-O symbols have a leading underscore
        f.write('_' + name + '\\n')
print(f'Generated {len(entries)} entries (top: {entries[0][1]} @ {entries[0][0]:,} calls)')
  "

  local sym_count
  sym_count=$(wc -l < "$ORDER_FILE" | tr -d ' ')
  echo "✓ Order file: $ORDER_FILE ($sym_count symbols, hot-first)"
}

phase_use() {
  echo "══════════════════════════════════════════════════════"
  echo "  Phase 2: Building optimised neverc (PGO use)"
  echo "══════════════════════════════════════════════════════"

  if [ ! -f "$PROFDATA" ]; then
    echo "ERROR: $PROFDATA not found. Run '$0 train' first." >&2
    exit 1
  fi

  local gc_flag="-Wl,--gc-sections"
  local order_flag=""
  case "$(uname -s)" in
    Darwin)
      gc_flag="-Wl,-dead_strip"
      local ORDER_FILE="$PROFILE_DIR/neverc.order"
      if [ -f "$ORDER_FILE" ]; then
        order_flag="-DNEVERC_ORDER_FILE=$ORDER_FILE"
        echo "  Using order file: $ORDER_FILE"
      fi
      ;;
    MINGW*|MSYS*)
      gc_flag=""
      ;;
  esac

  cmake -S "$REPO_ROOT/llvm" -B "$BUILD_PGO_USE" -G Ninja \
    -C "$CACHE" \
    -DCMAKE_C_FLAGS_RELEASE="-O2 -DNDEBUG -march=native -fprofile-instr-use=$PROFDATA -ffunction-sections -fdata-sections" \
    -DCMAKE_CXX_FLAGS_RELEASE="-O2 -DNDEBUG -march=native -fprofile-instr-use=$PROFDATA -ffunction-sections -fdata-sections" \
    -DCMAKE_EXE_LINKER_FLAGS="$gc_flag" \
    ${order_flag:+"$order_flag"} \
    2>&1 | tail -5

  ninja -C "$BUILD_PGO_USE" -j"$JOBS" neverc 2>&1 | tail -3
  echo "✓ PGO-optimised neverc: $BUILD_PGO_USE/bin/neverc"

  local baseline_size pgo_size
  local baseline_bin="$BUILD_BASELINE/bin/neverc"
  [ ! -x "$baseline_bin" ] && [ -x "${baseline_bin}.exe" ] && baseline_bin="${baseline_bin}.exe"
  if [ -x "$baseline_bin" ]; then
    local pgo_bin="$BUILD_PGO_USE/bin/neverc"
    [ ! -x "$pgo_bin" ] && [ -x "${pgo_bin}.exe" ] && pgo_bin="${pgo_bin}.exe"
    baseline_size=$(stat -f '%z' "$baseline_bin" 2>/dev/null || stat -c '%s' "$baseline_bin" 2>/dev/null)
    pgo_size=$(stat -f '%z' "$pgo_bin" 2>/dev/null || stat -c '%s' "$pgo_bin" 2>/dev/null)
    echo "  Binary size: baseline=$(numfmt --to=iec "$baseline_size" 2>/dev/null || echo "${baseline_size}B") → PGO=$(numfmt --to=iec "$pgo_size" 2>/dev/null || echo "${pgo_size}B")"
  fi
}

phase_benchmark() {
  echo "══════════════════════════════════════════════════════"
  echo "  Benchmark: Baseline vs PGO"
  echo "══════════════════════════════════════════════════════"

  local NC_BASE="$BUILD_BASELINE/bin/neverc"
  local NC_PGO="$BUILD_PGO_USE/bin/neverc"
  [ ! -x "$NC_BASE" ] && [ -x "${NC_BASE}.exe" ] && NC_BASE="${NC_BASE}.exe"
  [ ! -x "$NC_PGO" ] && [ -x "${NC_PGO}.exe" ] && NC_PGO="${NC_PGO}.exe"

  for bin_label in "Baseline:$NC_BASE" "PGO:$NC_PGO"; do
    IFS=: read label bin <<< "$bin_label"
    if [ ! -x "$bin" ]; then
      echo "  SKIP $label: $bin not found"
      continue
    fi

    echo ""
    echo "  ── $label ──"
    local runs=5

    # SQLite -O3
    if [ -f "$SQLITE_DIR/sqlite3.c" ]; then
      local times=()
      for i in $(seq 1 $runs); do
        local s; s=$(ts)
        "$bin" -c -O3 -w -fno-neverc-types -o /dev/null "$SQLITE_DIR/sqlite3.c" 2>/dev/null
        local e; e=$(ts)
        times+=($((e - s)))
      done
      IFS=$'\n' sorted=($(sort -n <<<"${times[*]}")); unset IFS
      local median=${sorted[$((runs / 2))]}
      local best=${sorted[0]}
      echo "    SQLite -O3: median=${median}ms  best=${best}ms  [${times[*]}]"
    fi

    # Redis batch -O2
    if [ -d "$REDIS_DIR/src" ]; then
      local times=()
      local redis_files=("$REDIS_DIR"/src/{server,cluster,module,networking,object}.c)
      for i in $(seq 1 $runs); do
        local s; s=$(ts)
        for f in "${redis_files[@]}"; do
          [ -f "$f" ] && "$bin" -c -O2 -w -fno-neverc-types -o /dev/null "$f" 2>/dev/null
        done
        local e; e=$(ts)
        times+=($((e - s)))
      done
      IFS=$'\n' sorted=($(sort -n <<<"${times[*]}")); unset IFS
      local median=${sorted[$((runs / 2))]}
      echo "    Redis 5-file -O2: median=${median}ms  [${times[*]}]"
    fi
  done

  echo ""
}

phase_clean() {
  echo "Cleaning PGO build artifacts..."
  rm -rf "$BUILD_PGO_GEN" "$BUILD_PGO_USE" "$PROFILE_DIR"
  echo "Done."
}

case "${1:-all}" in
  generate)  phase_generate ;;
  train)     phase_train ;;
  orderfile) phase_orderfile ;;
  use)       phase_use ;;
  benchmark) phase_benchmark ;;
  clean)     phase_clean ;;
  all)
    phase_generate
    phase_train
    phase_orderfile
    phase_use
    phase_benchmark
    ;;
  *)
    echo "Usage: $0 {all|generate|train|orderfile|use|benchmark|clean}"
    exit 1
    ;;
esac
