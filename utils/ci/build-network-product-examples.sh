#!/usr/bin/env bash

set -euo pipefail

if [[ $# -ne 3 ]]; then
  echo "usage: $0 <neverc> <repository-root> <target-triple>" >&2
  exit 2
fi

neverc=$1
repo_root=$2
target=$3
example_root="$repo_root/examples"
work_dir=$(mktemp -d "${TMPDIR:-/tmp}/neverc-network-products.XXXXXX")
trap 'rm -rf "$work_dir"' EXIT

case "$target" in
  *-windows-*)
    exe_suffix=.exe
    ;;
  *)
    exe_suffix=
    ;;
esac

common_flags=(
  "--target=$target"
  "-I$repo_root/std/include"
  -std=c11
  -Wall
  -Wextra
  -Werror
  -O2
)
if [[ $target == *-windows-* ]]; then
  common_flags+=(-Wl,--nodefaultlib=runtimeobject.lib)
fi

"$neverc" "${common_flags[@]}" \
  -o "$work_dir/authoritative-server$exe_suffix" \
  "$example_root/network-authoritative-server/main.c"

"$neverc" "${common_flags[@]}" \
  -o "$work_dir/anticheat-collector$exe_suffix" \
  "$example_root/network-anticheat-collector/main.c"

echo "network product examples built for $target"
