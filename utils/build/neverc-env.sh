#!/bin/sh
# Local development helper: switch PATH between the in-tree neverc binary and a
# release install (~/.neverc by default). Not for production installs.
#
# Usage (zsh):
#   source ./utils/build/neverc-env.sh              # local dev (current session)
#   source ./utils/build/neverc-env.sh --local      # same as above
#   source ./utils/build/neverc-env.sh --release    # release install (current session)
#   source ./utils/build/neverc-env.sh --status     # show active neverc
#   source ./utils/build/neverc-env.sh --remove     # remove both from PATH (current session)
#   source ./utils/build/neverc-env.sh --install    # write local dev source line to shell rc
#   source ./utils/build/neverc-env.sh --uninstall  # remove local dev source line from shell rc

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
NEVERC_BIN="$REPO_ROOT/build-neverc/bin"
RELEASE_ROOT="${NEVERC_INSTALL_DIR:-$HOME/.neverc}"
RELEASE_BIN="$RELEASE_ROOT/bin"
_SELF="$REPO_ROOT/utils/build/neverc-env.sh"

_remove_from_path() {
  _target="$1"
  _result=""
  _path="${PATH}"

  while [ -n "$_path" ]; do
    case "$_path" in
      *:*) _entry="${_path%%:*}"; _path="${_path#*:}" ;;
      *)   _entry="$_path"; _path="" ;;
    esac
    [ -z "$_entry" ] && continue
    [ "$_entry" = "$_target" ] && continue
    if [ -z "$_result" ]; then
      _result="$_entry"
    else
      _result="$_result:$_entry"
    fi
  done

  export PATH="${_result}"
}

_prepend_to_path() {
  _target="$1"
  case ":${PATH}:" in
    *":${_target}:"*) ;;
    *) export PATH="${_target}:${PATH}" ;;
  esac
}

_activate_local() {
  _remove_from_path "$RELEASE_BIN"
  if [ ! -x "$NEVERC_BIN/neverc" ]; then
    echo "warning: neverc not found at $NEVERC_BIN/neverc" >&2
    echo "         build first, e.g. cmake --build build-neverc --target neverc" >&2
  fi
  _prepend_to_path "$NEVERC_BIN"
  export NEVERC_ENV=local
  echo "NeverC PATH: local dev ($NEVERC_BIN)"
}

_activate_release() {
  _remove_from_path "$NEVERC_BIN"
  if [ ! -x "$RELEASE_BIN/neverc" ]; then
    echo "warning: release neverc not found at $RELEASE_BIN/neverc" >&2
    echo "         install first, e.g. curl -fsSL https://raw.githubusercontent.com/NeverSight/NeverC/HEAD/install.sh | sh" >&2
    echo "         or set NEVERC_INSTALL_DIR if you used a custom prefix" >&2
  fi
  _prepend_to_path "$RELEASE_BIN"
  export NEVERC_ENV=release
  echo "NeverC PATH: release ($RELEASE_BIN)"
}

_show_status() {
  _active=""
  if command -v neverc > /dev/null 2>&1; then
    _active="$(command -v neverc)"
  fi

  echo "NEVERC_ENV=${NEVERC_ENV:-unset}"
  echo "Active neverc: ${_active:-not found on PATH}"
  if [ -n "$_active" ] && [ -x "$_active" ]; then
    "$_active" --version 2>/dev/null || true
  fi
  echo "Local dev bin:  $NEVERC_BIN"
  echo "Release bin:    $RELEASE_BIN"
}

_rc_file() {
  case "${SHELL:-}" in
    */zsh)  echo "$HOME/.zshrc" ;;
    */bash) echo "$HOME/.bashrc" ;;
    *)      echo "$HOME/.profile" ;;
  esac
}

_source_line() {
  echo "source \"$_SELF\""
}

_show_help() {
  cat <<EOF
Usage: source $0 [OPTION]

Switch PATH between the in-tree neverc binary and a release install.

  (none)              Use local dev build (build-neverc/bin)
  -d, --local, --dev  Same as above
  --release, --rel    Use release install (~/.neverc/bin by default)
  -s, --status        Show which neverc is active
  -r, --remove        Remove both local and release bins from PATH (current session)
  -i, --install       Append local dev source line to shell rc file (persistent)
  -u, --uninstall     Remove local dev source line from shell rc file
  -h, --help          Show this help

Environment:
  NEVERC_INSTALL_DIR  Release install root (default: ~/.neverc)
  NEVERC_ENV          Set to "local" or "release" after switching
EOF
}

case "${1:-}" in
  -h|--help)
    _show_help
    return 0 2>/dev/null || exit 0
    ;;
  -i|--install)
    _rc="$(_rc_file)"
    _line="$(_source_line)"
    if [ -f "$_rc" ] && grep -qF "$_SELF" "$_rc" 2>/dev/null; then
      echo "already installed in $_rc"
    else
      echo "" >> "$_rc"
      echo "# NeverC local development PATH" >> "$_rc"
      echo "$_line" >> "$_rc"
      echo "installed to $_rc"
      echo "  restart your shell or run: $_line"
    fi
    return 0 2>/dev/null || exit 0
    ;;
  -u|--uninstall)
    _rc="$(_rc_file)"
    if [ ! -f "$_rc" ]; then
      echo "note: $_rc does not exist" >&2
      return 0 2>/dev/null || exit 0
    fi
    if grep -qF "$_SELF" "$_rc" 2>/dev/null; then
      _tmp="$_rc.neverc-env-tmp"
      grep -v "$_SELF" "$_rc" | grep -v "# NeverC local development PATH" > "$_tmp"
      mv "$_tmp" "$_rc"
      echo "uninstalled from $_rc"
    else
      echo "note: not found in $_rc" >&2
    fi
    return 0 2>/dev/null || exit 0
    ;;
  -r|--remove)
    _removed=0
    case ":${PATH}:" in
      *":${NEVERC_BIN}:"*)
        _remove_from_path "$NEVERC_BIN"
        _removed=1
        ;;
    esac
    case ":${PATH}:" in
      *":${RELEASE_BIN}:"*)
        _remove_from_path "$RELEASE_BIN"
        _removed=1
        ;;
    esac
    unset NEVERC_ENV 2>/dev/null || true
    if [ "$_removed" = "1" ]; then
      echo "removed NeverC bins from PATH"
    else
      echo "note: neither $NEVERC_BIN nor $RELEASE_BIN is on PATH" >&2
    fi
    ;;
  -d|--local|--dev)
    _activate_local
    ;;
  --release|--rel)
    _activate_release
    ;;
  -s|--status)
    _show_status
    return 0 2>/dev/null || exit 0
    ;;
  "")
    _activate_local
    ;;
  *)
    echo "error: unknown option: $1" >&2
    echo "       try: source $0 --help" >&2
    return 1 2>/dev/null || exit 1
    ;;
esac
