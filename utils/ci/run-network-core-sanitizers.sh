#!/usr/bin/env bash

set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "usage: $0 <repository-root>" >&2
  exit 2
fi

repo_root=$1
compiler=${CC:-clang}
std_root="$repo_root/std"
test_root="$repo_root/tests/neverc/std"
work_dir=$(mktemp -d "${TMPDIR:-/tmp}/neverc-network-sanitizers.XXXXXX")
trap 'rm -rf "$work_dir"' EXIT

common_flags=(
  -std=gnu11
  -O1
  -g
  -fno-omit-frame-pointer
  -Wall
  -Wextra
  -Werror
  -Wno-unused-parameter
  -Wno-unused-function
  "-I$std_root/include"
  "-I$std_root/src/net"
)

build_cases() {
  local label=$1
  shift
  "$compiler" "${common_flags[@]}" "$@" \
    "$test_root/test_thread.c" \
    "$std_root/src/thread/thread.c" \
    "$std_root/src/context/context.c" \
    -pthread -lm -o "$work_dir/thread-$label"
  "$compiler" "${common_flags[@]}" "$@" \
    "$test_root/test_net_internals.c" \
    "$std_root/src/net/tcp/tcp.c" \
    -pthread -lm -o "$work_dir/net-internals-$label"
}

build_cases asan-ubsan -fsanitize=address,undefined
ASAN_OPTIONS=${ASAN_OPTIONS:-detect_leaks=1} \
UBSAN_OPTIONS=${UBSAN_OPTIONS:-halt_on_error=1:print_stacktrace=1} \
  "$work_dir/thread-asan-ubsan"
ASAN_OPTIONS=${ASAN_OPTIONS:-detect_leaks=1} \
UBSAN_OPTIONS=${UBSAN_OPTIONS:-halt_on_error=1:print_stacktrace=1} \
  "$work_dir/net-internals-asan-ubsan"

build_cases tsan -fsanitize=thread
TSAN_OPTIONS=${TSAN_OPTIONS:-halt_on_error=1:history_size=7} \
  "$work_dir/thread-tsan"
TSAN_OPTIONS=${TSAN_OPTIONS:-halt_on_error=1:history_size=7} \
  "$work_dir/net-internals-tsan"

echo "network-core sanitizer gates passed"
