#!/usr/bin/env bash
# Install latest LLVM.org release tarball on macOS (optional / local use).
# CI workflows use Xcode Apple Clang instead — see build-macos.yml / release-macos-pgo.yml.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=resolve-llvm-version.sh
source "${SCRIPT_DIR}/resolve-llvm-version.sh"
resolve_llvm_version

case "$(uname -m)" in
  arm64) LLVM_PLATFORM_SUFFIX="macOS-ARM64" ;;
  x86_64) LLVM_PLATFORM_SUFFIX="macOS-X64" ;;
  *)
    echo "unsupported macOS architecture: $(uname -m)" >&2
    exit 1
    ;;
esac

LLVM_ROOT="${LLVM_ROOT:-/tmp/llvm-pgo}"
ASSET="LLVM-${LLVM_VER}-${LLVM_PLATFORM_SUFFIX}.tar.xz"
URL="https://github.com/llvm/llvm-project/releases/download/llvmorg-${LLVM_VER}/${ASSET}"
ARCHIVE="/tmp/llvm-${LLVM_VER}-macos.tar.xz"

if [[ -x "${LLVM_ROOT}/bin/clang" ]]; then
  echo "LLVM already present at ${LLVM_ROOT}"
else
  echo "Downloading ${ASSET}..."
  curl -fsSL --retry 5 --retry-delay 10 --connect-timeout 30 --max-time 1800 \
    -o "${ARCHIVE}" "${URL}"
  rm -rf "${LLVM_ROOT}"
  mkdir -p "${LLVM_ROOT}"
  tar xf "${ARCHIVE}" -C "${LLVM_ROOT}" --strip-components=1
  rm -f "${ARCHIVE}"
fi

"${LLVM_ROOT}/bin/clang" --version

{
  echo "PGO_CLANG=${LLVM_ROOT}/bin/clang"
  echo "PGO_CLANGXX=${LLVM_ROOT}/bin/clang++"
  echo "CC=${LLVM_ROOT}/bin/clang"
  echo "CXX=${LLVM_ROOT}/bin/clang++"
  echo "LLVM_ROOT=${LLVM_ROOT}"
} >> "${GITHUB_ENV:?GITHUB_ENV must be set}"
