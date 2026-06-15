#!/bin/sh
# NeverC installer
# Usage: curl -fsSL https://raw.githubusercontent.com/NeverSight/NeverC/main/install.sh | sh
#
# Environment variables:
#   NEVERC_INSTALL_DIR  Override install directory (default: ~/.neverc)
#   NEVERC_VERSION      Install a specific version tag (default: latest)
#   NEVERC_NO_MODIFY_PATH  Set to 1 to skip shell profile modification
set -eu

REPO="NeverSight/NeverC"
DEFAULT_INSTALL_DIR="$HOME/.neverc"

# ── Helpers ──────────────────────────────────────────────────────────

say() { printf '%s\n' "$*"; }
err() { say "error: $*" >&2; exit 1; }

need_cmd() {
  if ! command -v "$1" > /dev/null 2>&1; then
    err "need '$1' (command not found)"
  fi
}

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

# ── Resolve latest release tag via GitHub API ────────────────────────

resolve_version() {
  local version="${NEVERC_VERSION:-}"
  if [ -n "$version" ]; then
    echo "$version"
    return
  fi

  need_cmd curl
  local url="https://api.github.com/repos/${REPO}/releases/latest"
  local tag
  tag=$(curl -fsSL "$url" 2>/dev/null \
    | grep '"tag_name"' \
    | head -1 \
    | sed 's/.*"tag_name"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/')

  if [ -z "$tag" ]; then
    err "failed to determine latest release — set NEVERC_VERSION=vX.Y.Z manually"
  fi
  echo "$tag"
}

# ── Download & extract ───────────────────────────────────────────────

download_and_install() {
  local os="$1"
  local arch="$2"
  local version="$3"
  local install_dir="$4"

  local asset="neverc-${os}-${arch}.zip"
  local url="https://github.com/${REPO}/releases/download/${version}/${asset}"

  say ""
  say "  NeverC installer"
  say "  ────────────────"
  say "  Version:   ${version}"
  say "  Platform:  ${os}-${arch}"
  say "  Install:   ${install_dir}"
  say ""

  need_cmd curl
  need_cmd unzip
  need_cmd mkdir

  mkdir -p "$install_dir"

  say "Downloading ${asset}..."
  local tmp
  tmp=$(mktemp -d)
  trap 'rm -rf "$tmp"' EXIT

  if ! curl -fSL --progress-bar "$url" -o "${tmp}/${asset}"; then
    err "download failed — check that version '${version}' exists and has asset '${asset}'"
  fi

  say "Extracting to ${install_dir}..."
  unzip -o "${tmp}/${asset}" -d "$install_dir"

  if [ ! -f "${install_dir}/bin/neverc" ]; then
    err "extraction succeeded but ${install_dir}/bin/neverc not found"
  fi

  chmod +x "${install_dir}/bin/neverc"
  say "Installed neverc to ${install_dir}/bin/neverc"
}

# ── PATH setup ───────────────────────────────────────────────────────

add_to_path() {
  local bin_dir="$1"

  if [ "${NEVERC_NO_MODIFY_PATH:-0}" = "1" ]; then
    return
  fi

  # Already on PATH?
  case ":${PATH}:" in
    *":${bin_dir}:"*) return ;;
  esac

  local profile=""
  local shell_name
  shell_name=$(basename "${SHELL:-/bin/sh}")

  case "$shell_name" in
    zsh)
      if [ -f "$HOME/.zshrc" ]; then
        profile="$HOME/.zshrc"
      elif [ -f "$HOME/.zprofile" ]; then
        profile="$HOME/.zprofile"
      else
        profile="$HOME/.zshrc"
      fi
      ;;
    bash)
      if [ -f "$HOME/.bashrc" ]; then
        profile="$HOME/.bashrc"
      elif [ -f "$HOME/.bash_profile" ]; then
        profile="$HOME/.bash_profile"
      else
        profile="$HOME/.bashrc"
      fi
      ;;
    fish)
      profile="$HOME/.config/fish/conf.d/neverc.fish"
      ;;
    *)
      profile="$HOME/.profile"
      ;;
  esac

  if [ -z "$profile" ]; then
    return
  fi

  # Don't add duplicate entries
  if [ -f "$profile" ] && grep -q "neverc" "$profile" 2>/dev/null; then
    return
  fi

  say "Adding ${bin_dir} to PATH in ${profile}"

  if [ "$shell_name" = "fish" ]; then
    mkdir -p "$(dirname "$profile")"
    printf '\n# NeverC\nfish_add_path %s\n' "$bin_dir" >> "$profile"
  else
    printf '\n# NeverC\nexport PATH="%s:$PATH"\n' "$bin_dir" >> "$profile"
  fi
}

# ── Main ─────────────────────────────────────────────────────────────

main() {
  local os arch version install_dir

  os=$(detect_os)
  arch=$(detect_arch)
  version=$(resolve_version)
  install_dir="${NEVERC_INSTALL_DIR:-${DEFAULT_INSTALL_DIR}}"

  download_and_install "$os" "$arch" "$version" "$install_dir"
  add_to_path "${install_dir}/bin"

  say ""
  say "Done! NeverC ${version} is installed."
  say ""

  local needs_source=0
  case ":${PATH}:" in
    *":${install_dir}/bin:"*) ;;
    *) needs_source=1 ;;
  esac

  if [ "$needs_source" = "1" ]; then
    say "Restart your shell or run:"
    say "  export PATH=\"${install_dir}/bin:\$PATH\""
    say ""
  fi

  say "Quick start:"
  say "  neverc --version"
  say "  neverc hello.c -o hello"
  say ""
  say "Cross-compilation runtimes (optional):"
  say "  neverc runtime install windows-x64"
  say "  neverc runtime install linux-arm64"
  say "  neverc runtime install macos-arm64"
  say "  neverc runtime list"
}

main "$@"
