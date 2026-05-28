#!/usr/bin/env bash
# Install local git hooks for this repo.
#
# Usage:
#   bash scripts/install-git-hooks.sh
#
# Installs a pre-commit hook that checks Options.td.h ordering whenever
# the file is staged.  Skip with `git commit --no-verify`.

set -euo pipefail

REPO_ROOT="$(git rev-parse --show-toplevel)"
HOOK_PATH="$REPO_ROOT/.git/hooks/pre-commit"

cat > "$HOOK_PATH" <<'HOOK'
#!/usr/bin/env bash
set -e

REPO_ROOT="$(git rev-parse --show-toplevel)"
TARGET="neverc/include/neverc/Invoke/Options.td.h"

if git diff --cached --name-only | grep -qx "$TARGET"; then
    echo "[pre-commit] Checking $TARGET is sorted..."
    if ! python3 "$REPO_ROOT/scripts/sort-options-td.py" --check; then
        echo ""
        echo "[pre-commit] Options.td.h is not sorted. Run:"
        echo "    python3 scripts/sort-options-td.py --write"
        echo "and 'git add' the result, or use 'git commit --no-verify' to skip."
        exit 1
    fi
fi
HOOK

chmod +x "$HOOK_PATH"
echo "Installed pre-commit hook at $HOOK_PATH"
