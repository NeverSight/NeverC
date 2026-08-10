#!/usr/bin/env bash
# Retired compatibility entry point. Release hardening belongs in the source build.

set -euo pipefail

cat >&2 <<'EOF'
error: strip-ko.sh is deprecated and intentionally does not modify files.

NeverC cannot safely apply release hardening as a second pass over an
already-linked .ko. Rebuild the module from source so stripping and symbol
policy are applied while NeverC still owns the final link:

  neverc make release

or add the integrated option to the original source compile/link command:

  neverc -O2 --strip <source-and-link-options> -o module.ko

No input or output file was changed.
EOF

exit 1
