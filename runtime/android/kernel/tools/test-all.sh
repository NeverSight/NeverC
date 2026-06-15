#!/bin/bash
# Full verification: compile all demos × all kernels, validate ELF structure.
# Usage: ./test-all.sh [path-to-neverc]

set -euo pipefail
NEVERC="${1:-../../build-neverc/bin/neverc}"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../../../.." && pwd)"
TMPDIR=$(mktemp -d)
trap "rm -rf $TMPDIR" EXIT

KERNELS=(510 515 601 606 612)
DEMOS=(
  "android-kernel-hello"
  "android-kernel-driver"
  "android-kernel-chardev"
  "android-kernel-inline-hook"
  "android-kernel-syscall-hook"
  "android-kernel-stealth"
)
EXTRA_MODES=(
  "android-kernel-inline-hook:-DNVK_CONTEXT_HOOK"
  "android-kernel-syscall-hook:-DNVK_SYSCALL_INLINE_HOOK"
  "android-kernel-stealth:-DNVK_STEALTH_HIDE"
  "android-kernel-stealth:-DNVK_STEALTH_ROOT"
  "android-kernel-stealth:-DNVK_STEALTH_SELINUX"
)

PASS=0
FAIL=0
TOTAL=0

OBJDUMP="${OBJDUMP:-objdump}"

check_elf() {
  local ko="$1"
  local name="$2"

  local filetype
  filetype=$(file "$ko" 2>/dev/null)
  if ! printf '%s' "$filetype" | grep -qi "elf.*64.*aarch64\|elf.*64.*arm"; then
    echo "  FAIL: $name — not AArch64 ELF"
    return 1
  fi

  local sections
  sections=$("$OBJDUMP" -h "$ko" 2>/dev/null || true)

  if ! printf '%s' "$sections" | grep -q "\.modinfo"; then
    echo "  FAIL: $name — missing .modinfo section"
    return 1
  fi

  if ! printf '%s' "$sections" | grep -q "gnu.linkonce.this_module"; then
    echo "  FAIL: $name — missing .gnu.linkonce.this_module"
    return 1
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
