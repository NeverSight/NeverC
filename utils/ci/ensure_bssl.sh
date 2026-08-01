#!/usr/bin/env bash
# Locate or build the BoringSSL "bssl" command-line tool for TLS interop gates.
set -euo pipefail

if [[ $# -gt 1 ]]; then
  echo "usage: $0 [repository-root]" >&2
  exit 2
fi

repo_root="$(cd "$(dirname "$0")/../.." && pwd)"
if [[ $# -eq 1 ]]; then
  repo_root=$1
fi

if [[ -n "${NEVERC_BSSL:-}" && -x "$NEVERC_BSSL" ]]; then
  echo "$NEVERC_BSSL"
  exit 0
fi

if command -v bssl >/dev/null 2>&1; then
  command -v bssl
  exit 0
fi

cache_root="${NEVERC_BSSL_CACHE:-$repo_root/build-neverc/boringssl}"
src_dir="$cache_root/src"
build_dir="$cache_root/build"
bssl_bin="$build_dir/bssl"

if [[ -x "$bssl_bin" ]]; then
  echo "$bssl_bin"
  exit 0
fi

if ! command -v cmake >/dev/null 2>&1; then
  echo "ensure_bssl: cmake is required to build BoringSSL" >&2
  exit 1
fi
if ! command -v git >/dev/null 2>&1; then
  echo "ensure_bssl: git is required to fetch BoringSSL" >&2
  exit 1
fi

mkdir -p "$cache_root"
if [[ ! -d "$src_dir/.git" ]]; then
  git clone --depth=1 https://boringssl.googlesource.com/boringssl "$src_dir"
fi

cmake -S "$src_dir" -B "$build_dir" -DCMAKE_BUILD_TYPE=Release >&2
if command -v nproc >/dev/null 2>&1; then
  jobs=$(nproc)
elif command -v sysctl >/dev/null 2>&1; then
  jobs=$(sysctl -n hw.ncpu)
else
  jobs=4
fi
cmake --build "$build_dir" --target bssl -j"$jobs" >&2

if [[ ! -x "$bssl_bin" ]]; then
  echo "ensure_bssl: failed to build $bssl_bin" >&2
  exit 1
fi

echo "$bssl_bin"
