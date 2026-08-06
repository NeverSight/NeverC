#!/bin/sh
set -eu

repo_root=$(CDPATH='' cd -- "$(dirname "$0")/../.." && pwd)
test_name=${1:-all}

fail() {
  printf 'FAIL: %s\n' "$*" >&2
  exit 1
}

test_tag_matches_cmake_version() {
  output_file=$(mktemp "${TMPDIR:-/tmp}/neverc-version-test.XXXXXX")
  trap 'rm -f "$output_file"' EXIT HUP INT TERM

  if ! sh "$repo_root/utils/release/check-version.sh" \
    "$repo_root/llvm/CMakeLists.txt" v3389.1.5 > "$output_file" 2>&1; then
    sed 's/^/  checker: /' "$output_file" >&2
    fail "v3389.1.5 did not match the CMake version"
  fi

  [ "$(cat "$output_file")" = 3389.1.5 ] \
    || fail "checker did not print the canonical CMake version"
}

test_mismatched_tag_is_rejected() {
  output_file=$(mktemp "${TMPDIR:-/tmp}/neverc-version-test.XXXXXX")
  trap 'rm -f "$output_file"' EXIT HUP INT TERM

  if sh "$repo_root/utils/release/check-version.sh" \
    "$repo_root/llvm/CMakeLists.txt" v3389.1.4 > "$output_file" 2>&1; then
    fail "checker accepted a tag that does not match the CMake version"
  fi

  grep -F "does not match CMake version 'v3389.1.5'" "$output_file" > /dev/null \
    || fail "checker did not explain the tag/CMake mismatch"
}

run_all() {
  sh "$0" tag-matches-cmake-version
  sh "$0" mismatched-tag-is-rejected
}

case "$test_name" in
  all)
    run_all
    exit
    ;;
  tag-matches-cmake-version) test_tag_matches_cmake_version ;;
  mismatched-tag-is-rejected) test_mismatched_tag_is_rejected ;;
  *) fail "unknown test: $test_name" ;;
esac

printf 'PASS: %s\n' "$test_name"
