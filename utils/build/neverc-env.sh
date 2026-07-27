#!/bin/sh
# Local development helper: add or remove build-neverc/bin on PATH so the
# in-tree neverc binary is available in the current shell. Not for production
# installs.
#
# Usage (zsh):
#   source ./utils/build/neverc-env.sh              # add to PATH (current session)
#   source ./utils/build/neverc-env.sh --remove     # remove from PATH (current session)
#   source ./utils/build/neverc-env.sh --install    # write to ~/.zshrc (persistent)
#   source ./utils/build/neverc-env.sh --uninstall  # remove from ~/.zshrc

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
NEVERC_BIN="$REPO_ROOT/build-neverc/bin"
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

Local development helper for the in-tree neverc binary.

  (none)          Add build-neverc/bin to PATH (current session)
  -r, --remove    Remove build-neverc/bin from PATH (current session)
  -i, --install   Append source line to shell rc file (persistent)
  -u, --uninstall Remove source line from shell rc file
  -h, --help      Show this help
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
    case ":${PATH}:" in
      *":${NEVERC_BIN}:"*)
        _remove_from_path "$NEVERC_BIN"
        echo "removed $NEVERC_BIN from PATH"
        ;;
      *)
        echo "note: $NEVERC_BIN is not on PATH" >&2
        ;;
    esac
    ;;
  "")
    if [ ! -x "$NEVERC_BIN/neverc" ]; then
      echo "warning: neverc not found at $NEVERC_BIN/neverc" >&2
      echo "         build first, e.g. cmake --build build-neverc --target neverc" >&2
    fi

    case ":${PATH}:" in
      *":${NEVERC_BIN}:"*) ;;
      *) export PATH="${NEVERC_BIN}:${PATH}" ;;
    esac
    ;;
  *)
    echo "error: unknown option: $1" >&2
    echo "       try: source $0 --help" >&2
    return 1 2>/dev/null || exit 1
    ;;
esac
