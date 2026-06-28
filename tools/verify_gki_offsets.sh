#!/usr/bin/env bash
# verify_gki_offsets.sh -- verify (or discover) struct module offsets from a
# built GKI kernel, comparing against runtime/android/kernel/include/nvkmod_version.h.
#
# WHY: the module loader reads `init` and `exit` from the
# .gnu.linkonce.this_module blob at offsets fixed by *that kernel's* struct
# module (see runtime/android/kernel/tools/gen_struct_module_offsets.c). If the
# constants baked into nvkmod_version.h drift from the real kernel, the runtime
# loads garbage. This script re-derives them straight from a build artifact:
#
#   * section size of .gnu.linkonce.this_module   == sizeof(struct module)
#   * reloc to init_module    in that section     == offsetof(struct module, init)
#   * reloc to cleanup_module in that section      == offsetof(struct module, exit)
#
# ThinLTO `.mod.o` files are LLVM bitcode (readelf cannot parse them), so ELF
# `.mod.o` are preferred and the script transparently falls back to `.ko`
# (always ELF, even for ThinLTO builds) when every `.mod.o` is bitcode.
#
# Two modes:
#   verify_gki_offsets.sh <kernel-key> <dir...>   compare vs nvkmod_version.h
#   verify_gki_offsets.sh --print     <dir...>   just extract + print (no header),
#                                                useful to seed a NEW version
#                                                block (e.g. a fresh 6.18 build).
set -euo pipefail

PROG=${0##*/}
SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
DEFAULT_HEADER="$SCRIPT_DIR/../runtime/android/kernel/include/nvkmod_version.h"

usage() {
	cat <<EOF
Usage:
  $PROG [options] <kernel-key> <search-dir> [search-dir...]   # compare
  $PROG [options] --print       <search-dir> [search-dir...]   # discover

  <kernel-key>   one of: 510 515 601 606 612 618
  <search-dir>   directory tree(s) to scan for .mod.o / .ko objects

Options:
  --print         discover mode: extract offsets from the build and print them
                  as paste-ready #define lines; does not read nvkmod_version.h
  --header PATH   nvkmod_version.h to compare against
                  (default: <repo>/runtime/android/kernel/include/nvkmod_version.h)
  --readelf BIN   readelf to use (default: \$READELF, else auto-detect
                  llvm-readelf*/readelf)
  -h, --help      show this help

Exit status: 0 = match / printed, 1 = mismatch, 2 = bad usage / nothing found.
EOF
}

die() {
	printf '%s: error: %s\n' "$PROG" "$*" >&2
	exit 2
}

# ---- parse args --------------------------------------------------------------
HEADER=$DEFAULT_HEADER
READELF_OVERRIDE=${READELF:-}
PRINT_MODE=0
POS=()
while [ $# -gt 0 ]; do
	case "$1" in
	--print) PRINT_MODE=1; shift ;;
	--header) HEADER=${2:?--header needs a path}; shift 2 ;;
	--readelf) READELF_OVERRIDE=${2:?--readelf needs a binary}; shift 2 ;;
	-h | --help) usage; exit 0 ;;
	--) shift; break ;;
	-*) die "unknown option: $1" ;;
	*) POS+=("$1"); shift ;;
	esac
done
while [ $# -gt 0 ]; do POS+=("$1"); shift; done

KEY=
if [ "$PRINT_MODE" = 1 ]; then
	DIRS=("${POS[@]}")
else
	KEY=${POS[0]:-}
	DIRS=("${POS[@]:1}")
fi

[ "${#DIRS[@]}" -gt 0 ] || { usage >&2; die "missing <search-dir>"; }
if [ "$PRINT_MODE" != 1 ]; then
	[ -n "$KEY" ] || { usage >&2; die "missing <kernel-key>"; }
	case "$KEY" in
	510 | 515 | 601 | 606 | 612 | 618) ;;
	*) die "invalid kernel-key '$KEY' (want 510/515/601/606/612/618)" ;;
	esac
	[ -f "$HEADER" ] || die "header not found: $HEADER"
fi

# ---- pick a readelf ----------------------------------------------------------
pick_readelf() {
	local c
	if [ -n "$READELF_OVERRIDE" ]; then
		command -v "$READELF_OVERRIDE" >/dev/null 2>&1 ||
			die "readelf '$READELF_OVERRIDE' not found"
		printf '%s\n' "$READELF_OVERRIDE"
		return 0
	fi
	for c in llvm-readelf llvm-readelf-22 llvm-readelf-21 llvm-readelf-20 \
		llvm-readelf-19 llvm-readelf-18 llvm-readelf-17 readelf; do
		if command -v "$c" >/dev/null 2>&1; then
			printf '%s\n' "$c"
			return 0
		fi
	done
	return 1
}
READELF=$(pick_readelf) ||
	die "no readelf/llvm-readelf on PATH (apt-get install binutils llvm)"

# ---- expected values from nvkmod_version.h -----------------------------------
# Walk the #if NEVERC_KRT_KERNEL == <key> chain and grab the first define of
# each macro inside the matching block (first-wins also dodges the global
# MODULE_SIZE fallback that trails the last version block).
read_expected() {
	awk -v ver="$KEY" '
		index($0,"NEVERC_KRT_KERNEL")>0 && index($0,"==")>0 {
			inblk = ($NF==ver); next
		}
		inblk {
			for (i=1;i<=NF;i++) {
				if ($i=="NEVERC_KRT_OFF_INIT"    && e_init=="") e_init=$(i+1)
				if ($i=="NEVERC_KRT_OFF_EXIT"    && e_exit=="") e_exit=$(i+1)
				if ($i=="NEVERC_KRT_MODULE_SIZE" && e_size=="") e_size=$(i+1)
			}
		}
		END { print e_init "|" e_exit "|" e_size }
	' "$HEADER"
}

# ---- extraction helpers ------------------------------------------------------
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

is_elf() {
	# ELF magic: 7f 45 4c 46. Reading 4 bytes also cheaply skips bitcode.
	[ "$(head -c4 "$1" 2>/dev/null | od -An -v -tx1 2>/dev/null | tr -d ' \n')" = "7f454c46" ]
}

GOT_INIT= GOT_EXIT= GOT_SIZE=
extract() {
	local f=$1 sh ih eh
	"$READELF" -S -W "$f" >"$TMP/s" 2>/dev/null || return 1
	"$READELF" -r -W "$f" >"$TMP/r" 2>/dev/null || return 1
	sh=$(awk 'index($0,".gnu.linkonce.this_module")>0 {
			for (i=1;i<=NF;i++) if ($i=="PROGBITS") { print $(i+3); exit }
		}' "$TMP/s")
	ih=$(awk '/^Relocation section/ { b = (index($0,".rela.gnu.linkonce.this_module")>0) }
		b && $5=="init_module"    { print $1; exit }' "$TMP/r")
	eh=$(awk '/^Relocation section/ { b = (index($0,".rela.gnu.linkonce.this_module")>0) }
		b && $5=="cleanup_module" { print $1; exit }' "$TMP/r")
	{ [ -n "$sh" ] && [ -n "$ih" ] && [ -n "$eh" ]; } || return 1
	GOT_SIZE=$((16#$sh))
	GOT_INIT=$((16#$ih))
	GOT_EXIT=$((16#$eh))
	return 0
}

find_objs() {
	local d
	for d in "${DIRS[@]}"; do
		[ -d "$d" ] || continue
		# default find does NOT follow symlinks, so bazel-out/bazel-bin
		# symlinks are skipped while real out/cache + dist dirs are scanned.
		find "$d" -type f -name "$1" 2>/dev/null
	done
}

# ELF .mod.o first; fall back to .ko when every .mod.o is bitcode.
USED=
SAW_BITCODE=0
SCANNED=0
verify_one() {
	local f
	while IFS= read -r f; do
		[ -n "$f" ] || continue
		SCANNED=$((SCANNED + 1))
		if ! is_elf "$f"; then SAW_BITCODE=1; continue; fi
		if extract "$f"; then USED=$f; return 0; fi
	done < <(find_objs '*.mod.o')
	while IFS= read -r f; do
		[ -n "$f" ] || continue
		SCANNED=$((SCANNED + 1))
		is_elf "$f" || continue
		if extract "$f"; then USED=$f; return 0; fi
	done < <(find_objs '*.ko')
	return 1
}

if ! verify_one; then
	if [ "$SAW_BITCODE" = 1 ] && [ "$SCANNED" -gt 0 ]; then
		die "found only LLVM-bitcode .mod.o and no usable .ko under: ${DIRS[*]}"
	fi
	die "no .mod.o/.ko with .gnu.linkonce.this_module found under: ${DIRS[*]}"
fi

# ---- discover mode: print paste-ready defines and stop -----------------------
if [ "$PRINT_MODE" = 1 ]; then
	echo "[discover] readelf=$READELF"
	echo "[discover] object=$USED"
	echo "[discover] paste into the matching block of nvkmod_version.h:"
	printf '#    define NEVERC_KRT_OFF_INIT %s /* 0x%X */\n' "$GOT_INIT" "$GOT_INIT"
	printf '#    define NEVERC_KRT_OFF_EXIT %s /* 0x%X */\n' "$GOT_EXIT" "$GOT_EXIT"
	printf '#    define NEVERC_KRT_MODULE_SIZE %s /* 0x%X */\n' "$GOT_SIZE" "$GOT_SIZE"
	exit 0
fi

# ---- compare mode: read expected, compare, report ----------------------------
exp=$(read_expected)
EXP_INIT=${exp%%|*}
rest=${exp#*|}
EXP_EXIT=${rest%%|*}
EXP_SIZE=${rest#*|}
{ [ -n "$EXP_INIT" ] && [ -n "$EXP_EXIT" ] && [ -n "$EXP_SIZE" ]; } ||
	die "could not read expected offsets for kernel $KEY from $HEADER"

status=0
row() {
	local tag="OK"
	if [ "$2" != "$3" ]; then
		tag="MISMATCH"
		status=1
	fi
	printf '  %-12s expected=%-6s got=%-6s  %s\n' "$1" "$2" "$3" "$tag"
}

echo "[verify] kernel=$KEY  readelf=$READELF"
echo "[verify] header=$HEADER"
echo "[verify] object=$USED"
row "OFF_INIT" "$EXP_INIT" "$GOT_INIT"
row "OFF_EXIT" "$EXP_EXIT" "$GOT_EXIT"
row "MODULE_SIZE" "$EXP_SIZE" "$GOT_SIZE"

if [ "$status" -ne 0 ]; then
	echo "[verify] FAIL: nvkmod_version.h does not match the built kernel ($KEY)" >&2
	echo "[verify] hint: re-run with --print to get paste-ready values" >&2
	exit 1
fi
echo "[verify] PASS: struct module offsets match for kernel $KEY"
