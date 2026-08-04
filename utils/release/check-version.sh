#!/bin/sh
set -eu

cmake_file=${1:-llvm/CMakeLists.txt}
release_tag=${2:-}

fail() {
  printf 'error: %s\n' "$*" >&2
  exit 1
}

read_component() {
  component_name=$1
  sed -n "s/^[[:space:]]*set(${component_name}[[:space:]][[:space:]]*\\([0-9][0-9]*\\))[[:space:]]*$/\\1/p" \
    "$cmake_file" | sed -n '1p'
}

[ -f "$cmake_file" ] || fail "CMake version file not found: $cmake_file"

major=$(read_component LLVM_VERSION_MAJOR)
minor=$(read_component LLVM_VERSION_MINOR)
patch=$(read_component LLVM_VERSION_PATCH)

[ -n "$major" ] || fail "LLVM_VERSION_MAJOR is missing or non-numeric"
[ -n "$minor" ] || fail "LLVM_VERSION_MINOR is missing or non-numeric"
[ -n "$patch" ] || fail "LLVM_VERSION_PATCH is missing or non-numeric"

version="${major}.${minor}.${patch}"
if [ -n "$release_tag" ] && [ "$release_tag" != "v${version}" ]; then
  fail "tag '${release_tag}' does not match CMake version 'v${version}'"
fi

printf '%s\n' "$version"
