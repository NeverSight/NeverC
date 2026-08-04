#!/bin/sh
# NeverC installer
# Usage: curl -fsSL https://raw.githubusercontent.com/NeverSight/NeverC/HEAD/install.sh | sh
#
# Environment variables:
#   NEVERC_INSTALL_DIR  Override install directory (default: ~/.neverc)
#   NEVERC_VERSION      Install a specific version tag (default: latest)
#   NEVERC_NO_MODIFY_PATH  Set to 1 to skip shell profile modification
set -eu

REPO="NeverSight/NeverC"
DEFAULT_INSTALL_DIR="$HOME/.neverc"
TMP_DIR=""

# ── Helpers ──────────────────────────────────────────────────────────

say() { printf '%s\n' "$*"; }
err() { say "error: $*" >&2; exit 1; }

need_cmd() {
  if ! command -v "$1" > /dev/null 2>&1; then
    err "need '$1' (command not found)"
  fi
}

cleanup() {
  if [ -n "$TMP_DIR" ] && [ -d "$TMP_DIR" ]; then
    rm -rf "$TMP_DIR"
  fi
}
trap cleanup EXIT
trap 'exit 1' HUP INT TERM

# ── Detect platform ─────────────────────────────────────────────────

detect_os() {
  case "$(uname -s)" in
    Darwin) echo "macos" ;;
    Linux)  echo "linux" ;;
    *)      err "unsupported OS: $(uname -s) — use manual download for Windows" ;;
  esac
}

detect_arch() {
  case "$(uname -m)" in
    x86_64|amd64)       echo "x64" ;;
    aarch64|arm64)      echo "arm64" ;;
    *)                  err "unsupported architecture: $(uname -m)" ;;
  esac
}

validate_platform() {
  platform_os=$1
  platform_arch=$2

  case "${platform_os}-${platform_arch}" in
    linux-x64|linux-arm64|macos-arm64) ;;
    *) err "unsupported platform: ${platform_os}-${platform_arch} — use a manual release archive if available" ;;
  esac
}

# ── Resolve latest release tag via GitHub API ────────────────────────

normalize_version() {
  normalized_version=$1

  case "$normalized_version" in
    v*) ;;
    *) normalized_version="v${normalized_version}" ;;
  esac

  if ! printf '%s\n' "$normalized_version" \
    | awk '/^v[0-9]+\.[0-9]+\.[0-9]+$/ { valid = 1 } END { exit !valid }'; then
    err "invalid version '${normalized_version}' — expected vMAJOR.MINOR.PATCH"
  fi
  echo "$normalized_version"
}

resolve_version() {
  resolve_asset=$1
  resolve_requested=${NEVERC_VERSION:-}
  if [ -n "$resolve_requested" ]; then
    normalize_version "$resolve_requested"
    return
  fi

  need_cmd curl
  resolve_url="https://api.github.com/repos/${REPO}/releases?per_page=100"
  resolved_tag=$(curl -fsSL \
    -H 'Accept: application/vnd.github+json' \
    -H 'X-GitHub-Api-Version: 2022-11-28' \
    "$resolve_url" 2>/dev/null | awk -v target="$resolve_asset" '
    /"tag_name"[[:space:]]*:/ {
      line = $0
      sub(/^.*"tag_name"[[:space:]]*:[[:space:]]*"/, "", line)
      sub(/".*$/, "", line)
      tag = line
    }
    /"draft"[[:space:]]*:/ {
      draft = ($0 ~ /"draft"[[:space:]]*:[[:space:]]*true/)
    }
    /"prerelease"[[:space:]]*:/ {
      prerelease = ($0 ~ /"prerelease"[[:space:]]*:[[:space:]]*true/)
    }
    /"name"[[:space:]]*:/ {
      line = $0
      sub(/^.*"name"[[:space:]]*:[[:space:]]*"/, "", line)
      sub(/".*$/, "", line)
      if (line == target && !draft && !prerelease) {
        print tag
        exit
      }
    }
  ')

  if [ -z "$resolved_tag" ]; then
    err "failed to determine latest release — set NEVERC_VERSION=vX.Y.Z manually"
  fi
  echo "$resolved_tag"
}

# ── Download & extract ───────────────────────────────────────────────

verify_checksum() {
  checksum_archive=$1
  checksum_manifest=$2
  checksum_asset=$3

  checksum_expected=$(awk -v target="$checksum_asset" '
    $2 == target || $2 == "*" target { print $1; exit }
  ' "$checksum_manifest")
  if [ -z "$checksum_expected" ]; then
    err "checksum manifest has no entry for '${checksum_asset}'"
  fi

  if command -v sha256sum > /dev/null 2>&1; then
    checksum_actual=$(sha256sum "$checksum_archive" | awk '{ print $1 }')
  elif command -v shasum > /dev/null 2>&1; then
    checksum_actual=$(shasum -a 256 "$checksum_archive" | awk '{ print $1 }')
  else
    err "need 'sha256sum' or 'shasum' to verify the release archive"
  fi

  if [ "$checksum_actual" != "$checksum_expected" ]; then
    err "checksum verification failed for '${checksum_asset}'"
  fi
  say "Checksum verified."
}

download_and_install() {
  download_os=$1
  download_arch=$2
  download_version=$3
  download_install_dir=$4

  download_asset="neverc-${download_os}-${download_arch}.zip"
  download_url="https://github.com/${REPO}/releases/download/${download_version}/${download_asset}"
  download_checksum_url="https://github.com/${REPO}/releases/download/${download_version}/SHA256SUMS"

  say ""
  say "  NeverC installer"
  say "  ────────────────"
  say "  Version:   ${download_version}"
  say "  Platform:  ${download_os}-${download_arch}"
  say "  Install:   ${download_install_dir}"
  say ""

  need_cmd curl
  need_cmd unzip
  need_cmd mkdir
  need_cmd cp
  need_cmd mktemp

  say "Downloading ${download_asset}..."
  TMP_DIR=$(mktemp -d)
  download_archive="${TMP_DIR}/${download_asset}"
  download_manifest="${TMP_DIR}/SHA256SUMS"
  download_extract_dir="${TMP_DIR}/extract"

  if ! curl -fSL --progress-bar "$download_url" -o "$download_archive"; then
    err "download failed — check that version '${download_version}' exists and has asset '${download_asset}'"
  fi

  say "Downloading SHA256SUMS..."
  if ! curl -fsSL "$download_checksum_url" -o "$download_manifest"; then
    err "failed to download checksum manifest for version '${download_version}'"
  fi
  verify_checksum "$download_archive" "$download_manifest" "$download_asset"

  say "Extracting and validating archive..."
  mkdir -p "$download_extract_dir"
  unzip -q -o "$download_archive" -d "$download_extract_dir"

  if [ ! -f "${download_extract_dir}/bin/neverc" ]; then
    err "archive is valid but bin/neverc was not found"
  fi

  chmod +x "${download_extract_dir}/bin/neverc"
  mkdir -p "$download_install_dir"
  cp -R "${download_extract_dir}/." "$download_install_dir/"
  say "Installed neverc to ${download_install_dir}/bin/neverc"
}

# ── PATH setup ───────────────────────────────────────────────────────

add_to_path() {
  path_bin_dir=$1

  if [ "${NEVERC_NO_MODIFY_PATH:-0}" = "1" ]; then
    return
  fi

  # Already on PATH?
  case ":${PATH}:" in
    *":${path_bin_dir}:"*) return ;;
  esac

  path_profile=""
  path_shell_name=$(basename "${SHELL:-/bin/sh}")

  case "$path_shell_name" in
    zsh)
      if [ -f "$HOME/.zshrc" ]; then
        path_profile="$HOME/.zshrc"
      elif [ -f "$HOME/.zprofile" ]; then
        path_profile="$HOME/.zprofile"
      else
        path_profile="$HOME/.zshrc"
      fi
      ;;
    bash)
      if [ -f "$HOME/.bashrc" ]; then
        path_profile="$HOME/.bashrc"
      elif [ -f "$HOME/.bash_profile" ]; then
        path_profile="$HOME/.bash_profile"
      else
        path_profile="$HOME/.bashrc"
      fi
      ;;
    fish)
      path_profile="$HOME/.config/fish/conf.d/neverc.fish"
      ;;
    *)
      path_profile="$HOME/.profile"
      ;;
  esac

  if [ -z "$path_profile" ]; then
    return
  fi

  # Don't add duplicate entries
  if [ -f "$path_profile" ] && grep -F "$path_bin_dir" "$path_profile" > /dev/null 2>&1; then
    return
  fi

  say "Adding ${path_bin_dir} to PATH in ${path_profile}"

  if [ "$path_shell_name" = "fish" ]; then
    mkdir -p "$(dirname "$path_profile")"
    printf '\n# NeverC\nfish_add_path -- "%s"\n' "$path_bin_dir" >> "$path_profile"
  else
    # Keep $PATH literal so the profile expands it when the shell starts.
    # shellcheck disable=SC2016
    printf '\n# NeverC\nexport PATH="%s:$PATH"\n' "$path_bin_dir" >> "$path_profile"
  fi
}

# ── Main ─────────────────────────────────────────────────────────────

main() {
  main_os=$(detect_os)
  main_arch=$(detect_arch)
  validate_platform "$main_os" "$main_arch"
  main_asset="neverc-${main_os}-${main_arch}.zip"
  main_version=$(resolve_version "$main_asset")
  main_install_dir=${NEVERC_INSTALL_DIR:-${DEFAULT_INSTALL_DIR}}

  download_and_install "$main_os" "$main_arch" "$main_version" "$main_install_dir"
  add_to_path "${main_install_dir}/bin"

  say ""
  say "Done! NeverC ${main_version} is installed."
  say ""

  main_needs_source=0
  case ":${PATH}:" in
    *":${main_install_dir}/bin:"*) ;;
    *) main_needs_source=1 ;;
  esac

  if [ "$main_needs_source" = "1" ]; then
    say "Restart your shell or run:"
    say "  export PATH=\"${main_install_dir}/bin:\$PATH\""
    say ""
  fi

  say "Quick start:"
  say "  neverc --version"
  say "  neverc hello.c -o hello -fbuiltin-string"
  say ""
  say "Cross-compilation runtimes (optional):"
  say "  neverc runtime install all"
  say "  neverc runtime install windows-x64"
  say "  neverc runtime install windows-arm64"
  say "  neverc runtime install linux-x64"
  say "  neverc runtime install linux-arm64"
  say "  neverc runtime install macos-arm64"
  say "  neverc runtime install android-arm64"
  say "  neverc runtime install android-kernel-arm64"
  say "  neverc runtime list"
}

main "$@"
