#!/bin/sh
# gen-offsets.sh — compute exact struct module offsets for a GKI kernel and
# print them with stable NEVERC_KRT_* evidence names for a profile manifest.
#
#   Usage: gen-offsets.sh <path-to-GKI-common> [generated-dir-or-headers.tar.gz]
#
# <path-to-GKI-common> is the kernel source root (the directory that contains
# include/, arch/, ...).  The optional second argument may be a prepared output
# directory or Kleaf's out-dir-kernel-headers.tar.gz.  It defaults to an
# out-of-tree build directory; pass the source root if it is already prepared.
#
# Easiest, fully-reliable path (Linux):
#   make -C <common> ARCH=arm64 LLVM=1 gki_defconfig
#   make -C <common> ARCH=arm64 LLVM=1 modules_prepare
#   gen-offsets.sh <common> <common>
#
# This script also attempts a best-effort preparation (config + synthesized
# asm-generic wrappers) so it can run without a full kernel build; that path is
# reliable on Linux and partial on macOS (some arch generated headers cascade).
set -eu

HERE=$(cd "$(dirname "$0")" && pwd)
NEVERC="${NEVERC:-$HERE/../../../../build-neverc/bin/neverc}"
PROBE="${NVK_OFFSET_PROBE:-$HERE/gen_struct_module_offsets.c}"

KT="${1:?usage: gen-offsets.sh <GKI-common> [generated-dir]}"
GEN="${2:-}"
LD="${LD:-$(command -v ld.lld || echo ld)}"

if [ -z "$GEN" ]; then
  if [ -f "$KT/include/generated/autoconf.h" ]; then
    GEN="$KT"
  else
    GEN="/tmp/nvk-kbuild-$(basename "$KT")"
  fi
fi

OUT=$(mktemp "${TMPDIR:-/tmp}/nvk-offsets.XXXXXX")
OVERLAY=$(mktemp -d "${TMPDIR:-/tmp}/nvk-offsets-headers.XXXXXX")
trap 'rm -f "$OUT"; rm -rf "$OVERLAY"' EXIT HUP INT TERM

GEN_IS_ARCHIVE=0
case "$GEN" in
  *.tar.gz|*.tgz)
    [ -f "$GEN" ] || { echo "generated-header archive not found: $GEN"; exit 1; }
    tar -xzf "$GEN" -C "$OVERLAY"
    GEN="$OVERLAY"
    GEN_IS_ARCHIVE=1
    ;;
  *.tar)
    [ -f "$GEN" ] || { echo "generated-header archive not found: $GEN"; exit 1; }
    tar -xf "$GEN" -C "$OVERLAY"
    GEN="$OVERLAY"
    GEN_IS_ARCHIVE=1
    ;;
esac

if [ -f "$GEN/include/generated/autoconf.h" ]; then
  # A prepared out-of-tree GKI output was supplied.  Keep source and generated
  # headers separate so offsets reflect the exact official build config.
  :
elif [ "$GEN_IS_ARCHIVE" -eq 1 ]; then
  echo "generated-header archive has no include/generated/autoconf.h"
  exit 1
else
  # 1) Generate kernel config out-of-tree.
  mkdir -p "$GEN"
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
    [ -f "$KT/arch/arm64/include/uapi/asm/$b" ] && continue
    [ -f "$GA/$b" ] || echo "#include <asm-generic/$b>" > "$GA/$b"
  done
  for h in "$KT"/include/uapi/asm-generic/*.h; do
    b=$(basename "$h"); [ "$b" = Kbuild ] && continue
    [ -f "$KT/arch/arm64/include/uapi/asm/$b" ] && continue
    [ -f "$UGA/$b" ] || echo "#include <asm-generic/$b>" > "$UGA/$b"
  done
fi

# Some saved Kleaf outputs retain the official .config/autoconf.h but omit
# generated asm wrappers.  Recreate only the deterministic generated headers
# in a temporary overlay; never modify the supplied GKI output directory.
OGA="$OVERLAY/arch/arm64/include/generated/asm"; mkdir -p "$OGA"
OUGA="$OVERLAY/arch/arm64/include/generated/uapi/asm"; mkdir -p "$OUGA"
for h in "$KT"/include/asm-generic/*.h; do
  b=$(basename "$h"); [ "$b" = Kbuild ] && continue
  [ -f "$KT/arch/arm64/include/asm/$b" ] && continue
  [ -f "$KT/arch/arm64/include/uapi/asm/$b" ] && continue
  [ -f "$GEN/arch/arm64/include/generated/asm/$b" ] && continue
  echo "#include <asm-generic/$b>" > "$OGA/$b"
done
for h in "$KT"/include/uapi/asm-generic/*.h; do
  b=$(basename "$h"); [ "$b" = Kbuild ] && continue
  [ -f "$KT/arch/arm64/include/uapi/asm/$b" ] && continue
  [ -f "$GEN/arch/arm64/include/generated/uapi/asm/$b" ] && continue
  echo "#include <asm-generic/$b>" > "$OUGA/$b"
done
if [ -f "$KT/arch/arm64/tools/gen-cpucaps.awk" ]; then
  CPUCAP_HEADER=cpucaps.h
  if [ -f "$KT/arch/arm64/include/asm/cpucaps.h" ]; then
    CPUCAP_HEADER=cpucap-defs.h
  fi
  awk -f "$KT/arch/arm64/tools/gen-cpucaps.awk" \
    "$KT/arch/arm64/tools/cpucaps" > "$OGA/$CPUCAP_HEADER"
fi
if [ ! -f "$GEN/arch/arm64/include/generated/asm/sysreg-defs.h" ] &&
   [ -f "$KT/arch/arm64/tools/gen-sysreg.awk" ] &&
   [ -f "$KT/arch/arm64/tools/sysreg" ]; then
  awk -f "$KT/arch/arm64/tools/gen-sysreg.awk" \
    "$KT/arch/arm64/tools/sysreg" > "$OGA/sysreg-defs.h"
fi
if [ ! -f "$GEN/include/generated/autoksyms.h" ] &&
   [ -f "$GEN/include/config/auto.conf" ] &&
   grep -q '^CONFIG_TRIM_UNUSED_KSYMS=y$' "$GEN/include/config/auto.conf" &&
   [ -f "$KT/scripts/gen_autoksyms.sh" ]; then
  mkdir -p "$OVERLAY/include/generated"
  (
    cd "$GEN"
    abs_srctree="$KT" sh "$KT/scripts/gen_autoksyms.sh" \
      "$OVERLAY/include/generated/autoksyms.h"
  )
elif [ ! -f "$GEN/include/generated/autoksyms.h" ] &&
     { grep -q '^CONFIG_TRIM_UNUSED_KSYMS=y$' \
         "$GEN/include/config/auto.conf" 2>/dev/null ||
       grep -q '^#define CONFIG_TRIM_UNUSED_KSYMS 1$' \
         "$GEN/include/generated/autoconf.h"; }; then
  # Kleaf's exported header archive omits the symbol allowlist.  The layout
  # probe never emits or links exported symbols, so an empty temporary list is
  # sufficient and cannot affect any measured type size or member offset.
  mkdir -p "$OVERLAY/include/generated"
  : > "$OVERLAY/include/generated/autoksyms.h"
fi
if [ ! -f "$GEN/include/generated/timeconst.h" ] &&
   [ -f "$GEN/include/config/auto.conf" ] &&
   [ -f "$KT/kernel/time/timeconst.bc" ]; then
  HZ=$(sed -n 's/^CONFIG_HZ=//p' "$GEN/include/config/auto.conf")
  if [ -n "$HZ" ]; then
    mkdir -p "$OVERLAY/include/generated"
    echo "$HZ" | bc -q "$KT/kernel/time/timeconst.bc" \
      > "$OVERLAY/include/generated/timeconst.h"
  fi
fi

# 3) Compile the probe and print results.  neverc presents as old GCC without
#    __clang__, so emulate a modern GCC and use gnu11 for the kernel headers.
"$NEVERC" --target=aarch64-linux-android -fno-lto -nostdlibinc -std=gnu11 \
  -D__KERNEL__ -DNVK_GEN_KSRC=1 '-DKBUILD_MODNAME="nvk_offsets"' \
  -U__GNUC__ -D__GNUC__=12 -U__GNUC_MINOR__ -D__GNUC_MINOR__=0 \
  -U__GNUC_PATCHLEVEL__ -D__GNUC_PATCHLEVEL__=0 -Wno-unknown-attributes -Wno-error \
  -I"$KT/arch/arm64/include" -I"$OVERLAY/arch/arm64/include/generated" \
  -I"$GEN/arch/arm64/include/generated" \
  -I"$KT/include" -I"$OVERLAY/include" -I"$GEN/include" \
  -I"$GEN/include/generated" \
  -I"$KT/arch/arm64/include/uapi" \
  -I"$OVERLAY/arch/arm64/include/generated/uapi" \
  -I"$GEN/arch/arm64/include/generated/uapi" \
  -I"$KT/include/uapi" -I"$GEN/include/generated/uapi" \
  -include "$KT/include/linux/kconfig.h" \
  -include "$GEN/include/generated/autoconf.h" \
  -S -o "$OUT" "$PROBE" || { echo "probe compile failed; prepare the tree first"; exit 1; }

echo
echo "// record in the profile layout manifest, then regenerate the contract:"
sed -n 's/.*==NVK== \([A-Z_]*\) \([0-9]*\) ==.*/#  define \1 \2/p' "$OUT"
