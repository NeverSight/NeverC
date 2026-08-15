#!/bin/sh
# Compile public layout-sensitive SDK headers for every supported GKI profile.

set -eu

HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
REPO_ROOT=$(CDPATH= cd -- "$HERE/../../../.." && pwd)
KERNEL_ROOT=$(CDPATH= cd -- "$HERE/.." && pwd)
NEVERC="${1:-$REPO_ROOT/build-neverc/bin/neverc}"

for kernel in 510 51013 515 51514 601 606 612 618; do
	"$NEVERC" \
		--target=aarch64-linux-android \
		-fandroid-kernel-driver-mode \
		-DNVK_KERNEL="$kernel" \
		-I"$KERNEL_ROOT/arm64/include" \
		-I"$KERNEL_ROOT/include" \
		-std=gnu11 \
		-Wall \
		-Werror \
		-fsyntax-only \
		"$HERE/test-sdk-layouts.c"
done

python3 "$HERE/verify-sdk-layouts.py" --compiler "$NEVERC"

echo "All GKI SDK layout checks passed."
