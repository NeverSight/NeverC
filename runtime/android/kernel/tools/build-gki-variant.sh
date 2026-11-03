#!/usr/bin/env bash
# Build a pinned configured kernel; keep the original compile-family artifacts.
set -euo pipefail
TOOLS=$(cd "$(dirname "$0")" && pwd)
KERNEL=$(cd "$TOOLS/.." && pwd)
if [ "$#" -lt 4 ] || [ "$#" -gt 5 ]; then
 echo "Usage: $0 VARIANT SOURCE OUTPUT ARTIFACTS [JOBS]" >&2
 echo 'Put LLVM, the matching Rust toolchain/rust-src, and bindgen on PATH.' >&2
 exit 2
fi
KEY=$1
SOURCE=$(cd "$2" && pwd)
mkdir -p "$3" "$4"
OUTPUT=$(cd "$3" && pwd)
ARTIFACTS=$(cd "$4" && pwd)
JOBS=${5:-8}
mapfile -t identity < <(python3 - "$KERNEL/arm64/gki-variants.json" "$KEY" <<'PY'
import json,sys
records=[r for r in json.load(open(sys.argv[1]))['variants'] if r['key']==sys.argv[2]]
if len(records)!=1: raise SystemExit('unknown GKI variant')
r=records[0];p=r['profile']
for value in [r['source_commit'],p['android_release'],p['kmi_generation'],p['legacy_id'],p['kernel_name']]:print(value)
PY
)
[ "${#identity[@]}" -eq 5 ] || exit 2
[ "$(git -C "$SOURCE" rev-parse HEAD)" = "${identity[0]}" ] || {
 echo 'source commit differs from the measured variant' >&2; exit 1;
}
args=(-C "$SOURCE" O="$OUTPUT" ARCH=arm64 LLVM=1 LOCALVERSION= KCFLAGS=-fms-extensions)
make "${args[@]}" gki_defconfig
printf '%s\n' "-android${identity[1]}-${identity[2]}-maybe-dirty" > "$OUTPUT/localversion"
"$SOURCE/scripts/config" --file "$OUTPUT/.config" --disable LOCALVERSION_AUTO \
 --disable LTO_CLANG_FULL --disable LTO_CLANG_THIN --enable LTO_NONE \
 --disable DEBUG_INFO_COMPRESSED_ZSTD --enable DEBUG_INFO_COMPRESSED_NONE
if [ "$KEY" = 703-stable ]; then
 "$SOURCE/scripts/kconfig/merge_config.sh" -m -O "$OUTPUT" "$OUTPUT/.config" \
  "$KERNEL/arm64/android18-7.3-stable-production.fragment"
else
 "$SOURCE/scripts/config" --file "$OUTPUT/.config" --set-str LOCALVERSION -4k
fi
make "${args[@]}" olddefconfig
make "${args[@]}" -j"$JOBS" Image modules
mkdir -p "$ARTIFACTS/dist" "$OUTPUT/neverc-abi-probe"
cp "$TOOLS/gki-native-probe.c" "$OUTPUT/neverc-abi-probe/probe.c"
printf 'obj-m := probe.o\n' > "$OUTPUT/neverc-abi-probe/Kbuild"
make "${args[@]}" M="$OUTPUT/neverc-abi-probe" modules
cp "$OUTPUT/neverc-abi-probe/probe.ko" "$ARTIFACTS/probe.ko"
cp "$OUTPUT/arch/arm64/boot/Image" "$OUTPUT/vmlinux" "$OUTPUT/System.map" \
 "$OUTPUT/Module.symvers" "$ARTIFACTS/dist/"
cp "$OUTPUT/.config" "$ARTIFACTS/config"
printf '%s\n' "${identity[0]}" > "$ARTIFACTS/source-commit.txt"
python3 "$TOOLS/generate-gki-manifest.py" --runtime-variant \
 --profile "${identity[3]}" --kernel-name "${identity[4]}" \
 --config "$OUTPUT/.config" --vmlinux "$OUTPUT/vmlinux" \
 --symvers "$OUTPUT/Module.symvers" --output "$ARTIFACTS/manifest.json"
python3 "$TOOLS/verify-gki-variant.py" --variant "$KEY" --artifacts "$ARTIFACTS" \
 --module "$ARTIFACTS/probe.ko"
