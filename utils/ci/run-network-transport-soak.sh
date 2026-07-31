#!/usr/bin/env bash

set -euo pipefail

if [[ $# -lt 2 || $# -gt 3 ]]; then
  echo "usage: $0 <neverc> <repository-root> [soak-ms]" >&2
  exit 2
fi

neverc=$1
repo_root=$2
soak_ms=${3:-5000}

if [[ ! $soak_ms =~ ^[1-9][0-9]*$ ]]; then
  echo "soak-ms must be a positive integer: $soak_ms" >&2
  exit 2
fi

std_root="$repo_root/std"
test_root="$repo_root/tests/neverc/std"
work_dir=$(mktemp -d "${TMPDIR:-/tmp}/neverc-network-soak.XXXXXX")
trap 'rm -rf "$work_dir"' EXIT

case "$(uname -s)" in
  MINGW*|MSYS*|CYGWIN*)
    exe_suffix=.exe
    platform_flags=(-Wno-deprecated-declarations)
    ;;
  *)
    exe_suffix=
    platform_flags=(-lm -lpthread)
    ;;
esac

"$neverc" \
  "-I$std_root/include" \
  "-I$std_root/src/net" \
  -std=gnu11 \
  -Wall \
  -Wextra \
  -Werror \
  -Wno-unused-parameter \
  -Wno-unused-function \
  -O1 \
  -fno-builtin-std \
  -o "$work_dir/net_transport$exe_suffix" \
  "$test_root/test_net_transport.c" \
  "$std_root/src/net/tcp/tcp.c" \
  "$std_root/src/net/tcp/tcp_context.c" \
  "$std_root/src/net/udp/udp.c" \
  "$std_root/src/net/udp/udp_context.c" \
  "$std_root/src/thread/thread.c" \
  "$std_root/src/context/context.c" \
  "${platform_flags[@]}"

NEVERC_NET_SOAK_MS=$soak_ms "$work_dir/net_transport$exe_suffix"
echo "network-transport soak passed: ${soak_ms}ms"
