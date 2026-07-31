#!/usr/bin/env bash

set -euo pipefail

if [[ $# -lt 2 || $# -gt 3 ]]; then
  echo "usage: $0 <neverc> <repository-root> [rounds]" >&2
  exit 2
fi

neverc=$1
repo_root=$2
rounds=${3:-25}

if [[ ! $rounds =~ ^[1-9][0-9]*$ ]]; then
  echo "rounds must be a positive integer: $rounds" >&2
  exit 2
fi

std_root="$repo_root/std"
test_root="$repo_root/tests/neverc/std"
work_dir=$(mktemp -d "${TMPDIR:-/tmp}/neverc-network-core.XXXXXX")
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

common_flags=(
  "-I$std_root/include"
  "-I$std_root/src/net"
  -std=gnu11
  -Wall
  -Wextra
  -Werror
  -Wno-unused-parameter
  -Wno-unused-function
  -O1
  -fno-builtin-std
)

build_case() {
  local name=$1
  shift
  "$neverc" "${common_flags[@]}" -o "$work_dir/$name$exe_suffix" "$@" \
    "${platform_flags[@]}"
}

build_case thread \
  "$test_root/test_thread.c" \
  "$std_root/src/thread/thread.c" \
  "$std_root/src/context/context.c"
build_case net_internals \
  "$test_root/test_net_internals.c" \
  "$std_root/src/net/tcp/tcp.c"
build_case net_transport \
  "$test_root/test_net_transport.c" \
  "$std_root/src/net/tcp/tcp.c" \
  "$std_root/src/net/tcp/tcp_context.c" \
  "$std_root/src/net/udp/udp.c" \
  "$std_root/src/net/udp/udp_context.c" \
  "$std_root/src/thread/thread.c" \
  "$std_root/src/context/context.c"

cases=(thread net_internals net_transport)
if [[ -n $exe_suffix ]]; then
  build_case net_iocp "$test_root/test_net_iocp.c"
  cases+=(net_iocp)
fi

for ((round = 1; round <= rounds; ++round)); do
  echo "network-core round $round/$rounds"
  for name in "${cases[@]}"; do
    "$work_dir/$name$exe_suffix"
  done
done

echo "network-core gates passed: ${#cases[@]} cases x $rounds rounds"
