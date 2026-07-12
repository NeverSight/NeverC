#!/bin/bash
# Full verification: compile all demos × all kernels, validate ELF structure.
# Usage: ./test-all.sh [path-to-neverc]

set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../../../.." && pwd)"
NEVERC="${1:-$REPO_ROOT/build-neverc/bin/neverc}"
USER_COPY_CHECKER="$SCRIPT_DIR/check-user-copy-backend.py"
TMPDIR=$(mktemp -d)
trap 'rm -rf "$TMPDIR"' EXIT

KERNELS=(510 515 601 606 612 618)
DEMO_SPECS=(
  "android-kernel-hello:main.c"
  "android-kernel-driver:main.c"
  "android-kernel-chardev:main.c"
  "android-kernel-inline-interpose:main.c"
  "android-kernel-syscall-interpose:main.c"
  "android-kernel-lowvis:main.c"
  "android-kernel-netlink:main.c"
  "android-kernel-full:main.c"
  "android-kernel-multifile:main.c interposes.c utils.c"
  "android-kernel-probe:main.c"
)
LINKAGE_MODES=("auto:" "full:-flto=full" "none:-fno-lto")
EXTRA_MODES=(
  "android-kernel-full:-DNEVERC_KRT_CONTEXT_INTERPOSE:main.c"
  "android-kernel-lowvis:-DNEVERC_KRT_CONTEXT_INTERPOSE:main.c"
  "android-kernel-syscall-interpose:-DNVK_SYSCALL_INLINE_INTERPOSE:main.c"
  "android-kernel-lowvis:-DNVK_LOWVIS_FILTER:main.c"
  "android-kernel-lowvis:-DNVK_LOWVIS_FILTER_FULL:main.c"
  "android-kernel-lowvis:-DNVK_LOWVIS_CRED:main.c"
  "android-kernel-lowvis:-DNVK_LOWVIS_SELINUX:main.c"
)

PASS=0
FAIL=0
TOTAL=0

OBJDUMP="${OBJDUMP:-objdump}"
NM="${NM:-nm}"

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

  local symbols
  local unresolved
  symbols=$("$NM" -a "$ko" 2>/dev/null || true)
  unresolved=$(printf '%s\n' "$symbols" | awk \
    '$NF ~ /^_?neverc_krt_/ && $(NF - 1) ~ /^[Uu]$/ { print }')
  if [ -n "$unresolved" ]; then
    echo "  FAIL: $name — unresolved embedded-runtime symbols"
    printf '%s\n' "$unresolved"
    return 1
  fi

  local state_symbol
  local state_count
  local -a runtime_state_symbols=(
    _neverc_krt_sym_resolver
    __neverc_nvk_local._neverc_krt_sym_cache
    __neverc_nvk_local._neverc_krt_cache_key
    __neverc_nvk_local._neverc_krt_cache_epoch
    __neverc_nvk_local._neverc_krt_log_level
  )
  for state_symbol in "${runtime_state_symbols[@]}"; do
    state_count=$(printf '%s\n' "$symbols" | awk -v symbol="$state_symbol" \
      '$NF == symbol && $(NF - 1) !~ /^[Uu]$/ { count++ }
       END { print count + 0 }')
    if [ "$state_count" -gt 1 ]; then
      echo "  FAIL: $name — duplicate runtime state $state_symbol ($state_count)"
      return 1
    fi
  done

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
  rtlibs=$(printf '%s\n' "$relocs" | awk \
    '/__(u?div|u?mod|mul|ashl|lshr)ti3/ { count++ }
     END { print count + 0 }')
  if [ "$rtlibs" -gt 0 ]; then
    echo "  FAIL: $name — references compiler-rt builtins ($rtlibs symbols)"
    return 1
  fi

  # Verify no outline atomics references (kernel doesn't export them)
  local oatom
  oatom=$(printf '%s\n' "$relocs" | awk \
    '/__aarch64_(cas|swp|ldadd|ldset|ldclr)/ { count++ }
     END { print count + 0 }')
  if [ "$oatom" -gt 0 ]; then
    echo "  FAIL: $name — references outline atomics ($oatom symbols)"
    return 1
  fi

  # Verify no leaked kernel symbol names (xorstr check)
  local leaked
  leaked=$(strings "$ko" 2>/dev/null | awk \
    '/^(kallsyms_lookup_name|module_alloc|flush_icache_range|_printk|sys_call_table|selinux_enforcing|prepare_creds|commit_creds|find_task_by_vpid|update_mapping_prot)$/ {
       count++
     } END { print count + 0 }')
  if [ "$leaked" -gt 0 ]; then
    echo "  FAIL: $name — $leaked unencrypted kernel symbol names found"
    return 1
  fi

  # Verify vermagic string exists in binary
  local vmcount
  vmcount=$(strings "$ko" 2>/dev/null | awk \
    '/^vermagic=/ { count++ } END { print count + 0 }')
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

  case "$name" in
    android-kernel-chardev@*)
      local user_copy_check
      if ! user_copy_check=$(python3 "$USER_COPY_CHECKER" \
          --artifact "$ko" --nm "$NM" 2>&1); then
        echo "  FAIL: $name — unsafe or stale user-copy runtime"
        printf '%s\n' "$user_copy_check" | awk '{ print "    " $0 }'
        return 1
      fi
      ;;
  esac

  return 0
}

run_case() {
  local name="$1"
  local out="$2"
  local kernel="$3"
  local linkage_flag="$4"
  local extra_flag="$5"
  shift 5

  local log="${out}.log"
  local -a compiler_args=(
    --target=aarch64-linux-android
    -fandroid-kernel-driver-mode
    -DNVK_KERNEL="$kernel"
  )
  if [ -n "$linkage_flag" ]; then
    compiler_args+=("$linkage_flag")
  fi
  if [ -n "$extra_flag" ]; then
    compiler_args+=("$extra_flag")
  fi
  compiler_args+=(-Wall -Wno-unused -r -nostdlib -o "$out")
  compiler_args+=("$@")

  if ! "$NEVERC" "${compiler_args[@]}" >"$log" 2>&1; then
    echo "  FAIL: $name — compilation error"
    awk '{ print "    " $0 }' "$log"
    return 1
  fi
  check_elf "$out" "$name"
}

echo "=== NeverC Android Kernel Module Test Suite ==="
echo "Compiler: $NEVERC"
echo "Kernels:  ${KERNELS[*]}"
echo ""

for spec in "${DEMO_SPECS[@]}"; do
  demo="${spec%%:*}"
  source_names="${spec#*:}"
  sources=()
  for source_name in $source_names; do
    sources+=("$REPO_ROOT/examples/$demo/$source_name")
  done

  for k in "${KERNELS[@]}"; do
    for linkage_spec in "${LINKAGE_MODES[@]}"; do
      linkage="${linkage_spec%%:*}"
      linkage_flag="${linkage_spec#*:}"
      TOTAL=$((TOTAL+1))
      name="${demo}@${k}[${linkage}]"
      out="$TMPDIR/${demo}_${k}_${linkage}.ko"

      if run_case "$name" "$out" "$k" "$linkage_flag" "" \
           "${sources[@]}"; then
        PASS=$((PASS+1))
      else
        FAIL=$((FAIL+1))
      fi
    done
  done
done

for entry in "${EXTRA_MODES[@]}"; do
  demo="${entry%%:*}"
  rest="${entry#*:}"
  extra="${rest%%:*}"
  source_names="${rest#*:}"
  sources=()
  for source_name in $source_names; do
    sources+=("$REPO_ROOT/examples/$demo/$source_name")
  done

  for k in "${KERNELS[@]}"; do
    TOTAL=$((TOTAL+1))
    name="${demo}[${extra}]@${k}"
    out="$TMPDIR/${demo}_${extra#-D}_${k}.ko"

    if run_case "$name" "$out" "$k" "" "$extra" "${sources[@]}"; then
      PASS=$((PASS+1))
    else
      FAIL=$((FAIL+1))
    fi
  done
done

echo ""
echo "=== Results: $PASS/$TOTAL passed, $FAIL failed ==="
[ "$FAIL" -eq 0 ] && echo "ALL GOOD" || exit 1
