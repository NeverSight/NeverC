#!/usr/bin/env bash
# Resolve the latest stable llvmorg-X.Y.Z release and export LLVM_VER.
# Usage: source resolve-llvm-version.sh && resolve_llvm_version
#    or: ./resolve-llvm-version.sh
set -euo pipefail

resolve_llvm_version() {
  if [[ -n "${LLVM_VER:-}" ]]; then
    echo "LLVM_VER already set: ${LLVM_VER}"
    return 0
  fi

  LLVM_VER="$(python3 - <<'PY'
import json
import os
import re
import urllib.request

url = "https://api.github.com/repos/llvm/llvm-project/releases?per_page=40"
headers = {
    "Accept": "application/vnd.github+json",
    "User-Agent": "neverc-ci",
}
token = os.environ.get("GITHUB_TOKEN", "")
if token:
    headers["Authorization"] = f"Bearer {token}"

req = urllib.request.Request(url, headers=headers)
with urllib.request.urlopen(req, timeout=60) as resp:
    releases = json.load(resp)

pat = re.compile(r"^llvmorg-(\d+\.\d+\.\d+)$")
candidates = []
for release in releases:
    if release.get("prerelease"):
        continue
    match = pat.match(release.get("tag_name", ""))
    if not match:
        continue
    version = match.group(1)
    candidates.append((tuple(map(int, version.split("."))), version))

if not candidates:
    raise SystemExit("no stable llvmorg release found")

print(max(candidates)[1])
PY
)"

  if [[ -z "${LLVM_VER}" || "${LLVM_VER}" == "null" ]]; then
    echo "failed to resolve latest LLVM version" >&2
    return 1
  fi

  echo "Resolved LLVM_VER=${LLVM_VER}"
  export LLVM_VER
  if [[ -n "${GITHUB_ENV:-}" ]]; then
    echo "LLVM_VER=${LLVM_VER}" >> "${GITHUB_ENV}"
  fi
}

if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
  resolve_llvm_version
fi
