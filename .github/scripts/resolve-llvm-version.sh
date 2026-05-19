#!/usr/bin/env bash
# Resolve the latest stable llvmorg-X.Y.Z release and export LLVM_VER.
# Usage: source resolve-llvm-version.sh && resolve_llvm_version
#    or: ./resolve-llvm-version.sh
set -euo pipefail

# Prefer the releases/latest redirect (no GitHub REST API quota).
resolve_llvm_version_from_redirect() {
  local url tag
  url="$(curl -fsSL -o /dev/null -w '%{url_effective}' \
    "https://github.com/llvm/llvm-project/releases/latest")"
  tag="${url##*/}"
  if [[ "${tag}" =~ ^llvmorg-([0-9]+\.[0-9]+\.[0-9]+)$ ]]; then
    echo "${BASH_REMATCH[1]}"
    return 0
  fi
  echo "unexpected latest release URL: ${url}" >&2
  return 1
}

# Fallback: GitHub REST API (requires GITHUB_TOKEN on CI to avoid 403).
resolve_llvm_version_from_api() {
  local api_url="https://api.github.com/repos/llvm/llvm-project/releases?per_page=40"
  local -a curl_args=(
    -fsSL
    -H "Accept: application/vnd.github+json"
    -H "User-Agent: neverc-ci"
  )
  if [[ -n "${GITHUB_TOKEN:-}" ]]; then
    curl_args+=(-H "Authorization: Bearer ${GITHUB_TOKEN}")
  fi

  if ! command -v jq >/dev/null 2>&1; then
    echo "jq is required for API fallback" >&2
    return 1
  fi

  curl "${curl_args[@]}" "${api_url}" | jq -r '
    [.[] | select(.prerelease == false) | .tag_name
     | select(test("^llvmorg-[0-9]+\\.[0-9]+\\.[0-9]+$"))
     | sub("^llvmorg-"; "")]
    | sort_by(split(".") | map(tonumber))
    | last // empty
  '
}

resolve_llvm_version() {
  if [[ -n "${LLVM_VER:-}" ]]; then
    echo "LLVM_VER already set: ${LLVM_VER}"
    return 0
  fi

  LLVM_VER=""
  if LLVM_VER="$(resolve_llvm_version_from_redirect)"; then
    echo "Resolved LLVM_VER=${LLVM_VER} (from releases/latest redirect)"
  elif LLVM_VER="$(resolve_llvm_version_from_api)"; then
    echo "Resolved LLVM_VER=${LLVM_VER} (from GitHub API)"
  fi

  if [[ -z "${LLVM_VER}" || "${LLVM_VER}" == "null" ]]; then
    echo "failed to resolve latest LLVM version" >&2
    return 1
  fi

  export LLVM_VER
  if [[ -n "${GITHUB_ENV:-}" ]]; then
    echo "LLVM_VER=${LLVM_VER}" >> "${GITHUB_ENV}"
  fi
}

if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
  resolve_llvm_version
fi
