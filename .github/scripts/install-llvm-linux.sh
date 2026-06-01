#!/usr/bin/env bash
# Install latest LLVM release tarball on Linux CI.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=resolve-llvm-version.sh
source "${SCRIPT_DIR}/resolve-llvm-version.sh"
resolve_llvm_version

LLVM_ROOT="${LLVM_ROOT:-/tmp/llvm-pgo}"

case "$(uname -m)" in
  x86_64|amd64)  LLVM_HOST_ARCH="Linux-X64" ;;
  aarch64|arm64) LLVM_HOST_ARCH="Linux-ARM64" ;;
  *)
    echo "unsupported host arch: $(uname -m)" >&2
    exit 1
    ;;
esac

ASSET="LLVM-${LLVM_VER}-${LLVM_HOST_ARCH}.tar.xz"
URL="https://github.com/llvm/llvm-project/releases/download/llvmorg-${LLVM_VER}/${ASSET}"
ARCHIVE="/tmp/llvm-${LLVM_VER}-linux.tar.xz"

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

echo "${LLVM_ROOT}/bin" >> "${GITHUB_PATH:?GITHUB_PATH must be set}"
