#!/bin/sh
# Local development helper: add or remove build-neverc/bin on PATH so the
# in-tree neverc binary is available in the current shell. Not for production
# installs.
#
# Usage (zsh):
#   source ./tools/neverc-env.sh           # add to PATH
#   source ./tools/neverc-env.sh --remove  # remove from PATH
#   source ./tools/neverc-env.sh -r        # same as --remove
#
# Or add to ~/.zshrc (adjust the path to your repo):
#   source /path/to/NeverC-private/tools/neverc-env.sh

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
NEVERC_BIN="$REPO_ROOT/build-neverc/bin"

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

_show_help() {
  cat <<EOF
Usage: source $0 [OPTION]

Local development helper for the in-tree neverc binary.

  (none)        Add build-neverc/bin to PATH
  -r, --remove  Remove build-neverc/bin from PATH
  -h, --help    Show this help
EOF
}

case "${1:-}" in
  -h|--help)
    _show_help
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
