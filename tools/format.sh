#!/usr/bin/env bash
# Format all .cpp / .h under this repo using .clang-format files.
# Excludes: .git/, build-neverc/
#
# Usage:
#   ./format.sh              # format in place
#   ./format.sh --check      # fail if any file would change (CI)
#   JOBS=16 ./format.sh      # parallel jobs (default: 8)
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"

: "${JOBS:=8}"

if ! command -v clang-format &>/dev/null; then
  echo "error: clang-format not found in PATH (e.g. brew install clang-format)" >&2
  exit 1
fi

CHECK=0
case "${1:-}" in
  --check | -n) CHECK=1 ;;
  --help | -h)
    sed -n '2,8p' "$0" | sed 's/^# \{0,1\}//'
    exit 0
    ;;
  "") ;;
  *)
    echo "usage: $0 [--check|-n] [--help|-h]" >&2
    exit 1
    ;;
esac

if [[ "$CHECK" -eq 1 ]]; then
  FMT_ARGS=(--dry-run --Werror)
else
  FMT_ARGS=(-i)
fi

find . -type f \( -name '*.cpp' -o -name '*.h' \) \
  ! -path './.git/*' \
  ! -path './build-neverc/*' \
  -print0 |
  xargs -0 -P "$JOBS" clang-format "${FMT_ARGS[@]}"
