#!/bin/bash
# Full verification: compile all demos × all kernels, validate ELF structure.
# Usage: ./test-all.sh [path-to-neverc]

set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../../../.." && pwd)"
NEVERC="${1:-$REPO_ROOT/build-neverc/bin/neverc}"
TMPDIR=$(mktemp -d)
trap "rm -rf $TMPDIR" EXIT

KERNELS=(510 515 601 606 612 618)
DEMOS=(
  "android-kernel-hello"
  "android-kernel-driver"
  "android-kernel-chardev"
  "android-kernel-inline-interpose"
  "android-kernel-syscall-interpose"
  "android-kernel-lowvis"
  "android-kernel-netlink"
  "android-kernel-full"
)
EXTRA_MODES=(
  "android-kernel-inline-interpose:-DNVK_CONTEXT_INTERPOSE"
  "android-kernel-syscall-interpose:-DNVK_SYSCALL_INLINE_INTERPOSE"
  "android-kernel-lowvis:-DNVK_LOWVIS_HIDE"
  "android-kernel-lowvis:-DNVK_LOWVIS_FULL_HIDE"
  "android-kernel-lowvis:-DNVK_LOWVIS_ROOT"
  "android-kernel-lowvis:-DNVK_LOWVIS_SELINUX"
)

PASS=0
FAIL=0
TOTAL=0

OBJDUMP="${OBJDUMP:-objdump}"

check_elf() {
  local ko="$1"
  local name="$2"

  local filetype
  filetype=$(file "$ko" 2>/dev/null || true)
  case "$filetype" in
    *ELF*64*aarch64*|*ELF*64*ARM*|*ELF*64*arm*) ;;
    *)
      echo "  FAIL: $name — not AArch64 ELF"
      return 1
      ;;
  esac

  local sections
  sections=$("$OBJDUMP" -h "$ko" 2>/dev/null || true)

  case "$sections" in
    *".modinfo"*) ;;
    *)
      echo "  FAIL: $name — missing .modinfo section"
      return 1
      ;;
  esac

  case "$sections" in
    *"gnu.linkonce.this_module"*) ;;
    *)
      echo "  FAIL: $name — missing .gnu.linkonce.this_module"
      return 1
      ;;
  esac

  # Verify no GOT section (modules must use direct access)
  case "$sections" in
    *".got"*)
      echo "  FAIL: $name — has .got section (modules must use direct access)"
      return 1
      ;;
  esac

  # Verify init/exit relocations exist
  local relocs
  relocs=$("$OBJDUMP" -r "$ko" 2>/dev/null || true)
  case "$relocs" in
    *init_module*) ;;
    *)
      echo "  FAIL: $name — missing init_module relocation"
      return 1
      ;;
  esac

  # Verify no compiler-rt / libgcc symbol references
  local rtlibs
  rtlibs=$(echo "$relocs" | grep -c '__udivti3\|__umodti3\|__divti3\|__modti3\|__multi3\|__ashlti3\|__lshrti3' || true)
  if [ "$rtlibs" -gt 0 ]; then
    echo "  FAIL: $name — references compiler-rt builtins ($rtlibs symbols)"
    return 1
  fi

  # Verify no outline atomics references (kernel doesn't export them)
  local oatom
  oatom=$(echo "$relocs" | grep -c '__aarch64_cas\|__aarch64_swp\|__aarch64_ldadd\|__aarch64_ldset\|__aarch64_ldclr' || true)
  if [ "$oatom" -gt 0 ]; then
    echo "  FAIL: $name — references outline atomics ($oatom symbols)"
    return 1
  fi

  # Verify no leaked kernel symbol names (xorstr check)
  local leaked
  leaked=$(strings "$ko" 2>/dev/null | grep -xc "kallsyms_lookup_name\|module_alloc\|flush_icache_range\|_printk\|sys_call_table\|selinux_enforcing\|prepare_creds\|commit_creds\|find_task_by_vpid\|update_mapping_prot" || true)
  if [ "$leaked" -gt 0 ]; then
    echo "  FAIL: $name — $leaked unencrypted kernel symbol names found"
    return 1
  fi

  # Verify vermagic string exists in binary
  local vmcount
  vmcount=$(strings "$ko" 2>/dev/null | grep -c "^vermagic=" || true)
  if [ "$vmcount" -eq 0 ]; then
    echo "  FAIL: $name — missing vermagic string"
    return 1
  fi

  # Verify file size is reasonable (< 512KB for a kernel module)
  local kosize
  kosize=$(wc -c < "$ko" 2>/dev/null || echo 0)
  if [ "$kosize" -gt 524288 ]; then
    echo "  WARN: $name — unusually large ($kosize bytes)"
  fi

  return 0
}

echo "=== NeverC Android Kernel Module Test Suite ==="
echo "Compiler: $NEVERC"
echo "Kernels:  ${KERNELS[*]}"
echo ""

for demo in "${DEMOS[@]}"; do
  for k in "${KERNELS[@]}"; do
    TOTAL=$((TOTAL+1))
    name="${demo}@${k}"
    out="$TMPDIR/${demo}_${k}.ko"
    src="$REPO_ROOT/examples/$demo/main.c"

    if "$NEVERC" --target=aarch64-linux-android \
       -fandroid-kernel-driver-mode -DNVK_KERNEL=$k \
       -Wall -Wno-unused -r -nostdlib \
       -o "$out" "$src" 2>/dev/null; then
      if check_elf "$out" "$name"; then
        PASS=$((PASS+1))
      else
        FAIL=$((FAIL+1))
      fi
    else
      echo "  FAIL: $name — compilation error"
      FAIL=$((FAIL+1))
    fi
  done
done

for entry in "${EXTRA_MODES[@]}"; do
  demo="${entry%%:*}"
  extra="${entry##*:}"
  for k in "${KERNELS[@]}"; do
    TOTAL=$((TOTAL+1))
    name="${demo}[${extra}]@${k}"
    out="$TMPDIR/${demo}_${extra}_${k}.ko"
    src="$REPO_ROOT/examples/$demo/main.c"

    if "$NEVERC" --target=aarch64-linux-android \
       -fandroid-kernel-driver-mode -DNVK_KERNEL=$k \
       $extra -Wall -Wno-unused -r -nostdlib \
       -o "$out" "$src" 2>/dev/null; then
      if check_elf "$out" "$name"; then
        PASS=$((PASS+1))
      else
        FAIL=$((FAIL+1))
      fi
    else
      echo "  FAIL: $name — compilation error"
      FAIL=$((FAIL+1))
    fi
  done
done

echo ""
echo "=== Results: $PASS/$TOTAL passed, $FAIL failed ==="
[ "$FAIL" -eq 0 ] && echo "ALL GOOD" || exit 1
