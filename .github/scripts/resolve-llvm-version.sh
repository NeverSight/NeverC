#!/usr/bin/env bash
# Resolve the latest stable llvmorg-X.Y.Z release and export LLVM_VER.
# Usage: source resolve-llvm-version.sh && resolve_llvm_version
#    or: ./resolve-llvm-version.sh
#
# Optional: LLVM_VERIFY_ASSET_SUFFIX — e.g. Linux-X64.tar.xz or win64.exe
#   When set, picks the newest stable release that actually ships that asset
#   (avoids 404 when /releases/latest points at a tag before binaries land).
#   GITHUB_TOKEN is strongly recommended (higher API quota).
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

# Newest stable release that includes a given prebuilt asset (by file suffix).
# Asset name must be LLVM-<ver>-<LLVM_VERIFY_ASSET_SUFFIX>, e.g. LLVM-22.1.5-Linux-X64.tar.xz
resolve_llvm_version_from_release_assets() {
  python3 - <<'PY'
import json
import os
import re
import urllib.request

suffix = os.environ.get("LLVM_VERIFY_ASSET_SUFFIX", "").strip()
if not suffix:
    raise SystemExit(2)

url = "https://api.github.com/repos/llvm/llvm-project/releases?per_page=80"
headers = {
    "Accept": "application/vnd.github+json",
    "User-Agent": "neverc-ci",
}
token = os.environ.get("GITHUB_TOKEN", "")
if token:
    headers["Authorization"] = f"Bearer {token}"

req = urllib.request.Request(url, headers=headers)
with urllib.request.urlopen(req, timeout=90) as resp:
    releases = json.load(resp)

pat = re.compile(r"^llvmorg-(\d+\.\d+\.\d+)$")
candidates = []
for rel in releases:
    if rel.get("prerelease"):
        continue
    m = pat.match(rel.get("tag_name") or "")
    if not m:
        continue
    ver = m.group(1)
    names = {a.get("name") for a in (rel.get("assets") or []) if a.get("name")}
    want = f"LLVM-{ver}-{suffix}"
    if want in names:
        candidates.append((tuple(map(int, ver.split("."))), ver))

if not candidates:
    raise SystemExit(
        f"no stable llvmorg release with asset LLVM-*-{suffix} "
        "(tag may exist before binaries are uploaded)"
    )

print(max(candidates)[1])
PY
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

  if [[ -n "${LLVM_VERIFY_ASSET_SUFFIX:-}" ]]; then
    if LLVM_VER="$(resolve_llvm_version_from_release_assets)"; then
      echo "Resolved LLVM_VER=${LLVM_VER} (newest release with asset …${LLVM_VERIFY_ASSET_SUFFIX})"
    fi
  elif LLVM_VER="$(resolve_llvm_version_from_redirect)"; then
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
