#!/bin/sh
# gen-offsets.sh — compute exact struct module offsets for a GKI kernel and
# print them as NVK_OFF_* values for nvkmod_version.h.
#
#   Usage: gen-offsets.sh <path-to-GKI-common> [generated-dir]
#
# <path-to-GKI-common> is the kernel source root (the directory that contains
# include/, arch/, ...).  [generated-dir] defaults to an out-of-tree build dir;
# pass the source root itself if the tree is already prepared.
#
# Easiest, fully-reliable path (Linux):
#   make -C <common> ARCH=arm64 LLVM=1 gki_defconfig
#   make -C <common> ARCH=arm64 LLVM=1 modules_prepare
#   gen-offsets.sh <common> <common>
#
# This script also attempts a best-effort preparation (config + synthesized
# asm-generic wrappers) so it can run without a full kernel build; that path is
# reliable on Linux and partial on macOS (some arch generated headers cascade).
set -u

HERE=$(cd "$(dirname "$0")" && pwd)
NEVERC="${NEVERC:-$HERE/../../../../build-neverc/bin/neverc}"
PROBE="$HERE/gen_struct_module_offsets.c"

KT="${1:?usage: gen-offsets.sh <GKI-common> [generated-dir]}"
GEN="${2:-/tmp/nvk-kbuild-$(basename "$KT")}"
OUT="$GEN/nvk-offsets.s"
LD="${LD:-$(command -v ld.lld || echo ld)}"

mkdir -p "$GEN"

if [ -f "$KT/include/generated/autoconf.h" ]; then
  # Tree is already prepared (Linux `make modules_prepare`, or a prebuilt kit).
  # Use it as-is; do NOT synthesize anything (would clash with real headers).
  GEN="$KT"
else
  # 1) Generate kernel config out-of-tree.
  echo "[*] preparing config in $GEN ..."
  make -C "$KT" ARCH=arm64 HOSTCC=cc CC=cc LD="$LD" O="$GEN" gki_defconfig
  make -C "$KT" ARCH=arm64 HOSTCC=cc CC=cc LD="$LD" O="$GEN" syncconfig

  # 2) Synthesize asm-generic wrappers + a few generated headers (best effort;
  #    reliable on Linux, partial on macOS).
  GA="$GEN/arch/arm64/include/generated/asm"; mkdir -p "$GA"
  UGA="$GEN/arch/arm64/include/generated/uapi/asm"; mkdir -p "$UGA"
  for h in "$KT"/include/asm-generic/*.h; do
    b=$(basename "$h"); [ "$b" = Kbuild ] && continue
    [ -f "$KT/arch/arm64/include/asm/$b" ] && continue
    [ -f "$GA/$b" ] || echo "#include <asm-generic/$b>" > "$GA/$b"
  done
  for h in "$KT"/include/uapi/asm-generic/*.h; do
    b=$(basename "$h"); [ "$b" = Kbuild ] && continue
    [ -f "$KT/arch/arm64/include/uapi/asm/$b" ] && continue
    [ -f "$KT/arch/arm64/include/asm/$b" ] && continue
    [ -f "$UGA/$b" ] || echo "#include <asm-generic/$b>" > "$UGA/$b"
  done
  [ -f "$KT/arch/arm64/tools/gen-cpucaps.awk" ] && \
    awk -f "$KT/arch/arm64/tools/gen-cpucaps.awk" "$KT/arch/arm64/tools/cpucaps" \
      > "$GA/cpucaps.h" 2>/dev/null
fi

# 3) Compile the probe and print results.  neverc presents as old GCC without
#    __clang__, so emulate a modern GCC and use gnu11 for the kernel headers.
"$NEVERC" --target=aarch64-linux-android -fno-lto -nostdlibinc -std=gnu11 \
  -D__KERNEL__ -DNVK_GEN_KSRC=1 \
  -U__GNUC__ -D__GNUC__=12 -U__GNUC_MINOR__ -D__GNUC_MINOR__=0 \
  -U__GNUC_PATCHLEVEL__ -D__GNUC_PATCHLEVEL__=0 -Wno-unknown-attributes -Wno-error \
  -I"$KT/arch/arm64/include" -I"$GEN/arch/arm64/include/generated" \
  -I"$KT/include" -I"$GEN/include" -I"$GEN/include/generated" \
  -I"$KT/arch/arm64/include/uapi" -I"$GEN/arch/arm64/include/generated/uapi" \
  -I"$KT/include/uapi" -I"$GEN/include/generated/uapi" \
  -include "$KT/include/linux/kconfig.h" \
  -include "$GEN/include/generated/autoconf.h" \
  -S -o "$OUT" "$PROBE" || { echo "probe compile failed; prepare the tree first"; exit 1; }

echo
echo "// paste into runtime/android/kernel/include/nvkmod_version.h:"
sed -n 's/.*==NVK== \([A-Z_]*\) \([0-9]*\) ==.*/#  define \1 \2/p' "$OUT"
