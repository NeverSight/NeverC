#!/usr/bin/env bash
# Focused integration checks for embedded NVK runtime linkage.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../../../.." && pwd)"
NEVERC="${1:-$REPO_ROOT/build-neverc/bin/neverc}"
SCOPE="${2:---smoke}"
NM="${NM:-nm}"
LTO_MODES=(auto full none)
SHARED_STATE_SYMBOLS=(
	_neverc_krt_sym_resolver
	__neverc_nvk_local._neverc_krt_sym_cache
	__neverc_nvk_local._neverc_krt_cache_key
	__neverc_nvk_local._neverc_krt_cache_epoch
	__neverc_nvk_local._neverc_krt_log_level
)
READELF="${READELF:-}"
if [ -z "$READELF" ]; then
	if command -v llvm-readelf >/dev/null 2>&1; then
		READELF="llvm-readelf"
	elif [ -x /opt/homebrew/opt/llvm/bin/llvm-readelf ]; then
		READELF="/opt/homebrew/opt/llvm/bin/llvm-readelf"
	fi
fi

if [ ! -x "$NEVERC" ]; then
	echo "error: NeverC compiler is not executable: $NEVERC" >&2
	exit 2
fi

case "$SCOPE" in
--smoke)
	KERNELS=(612)
	;;
--full)
	KERNELS=(510 515 601 606 612 618)
	;;
*)
	echo "usage: $0 [neverc] [--smoke|--full]" >&2
	exit 2
	;;
esac

TMP_ROOT="$(mktemp -d "${TMPDIR:-/tmp}/neverc-krt-linkage.XXXXXX")"
cleanup_tmp_root()
{
	if [ "${NVK_TEST_KEEP_TMP:-0}" = 1 ]; then
		echo "NVK linkage artifacts kept at $TMP_ROOT" >&2
		return
	fi
	rm -rf "$TMP_ROOT"
}
trap cleanup_tmp_root EXIT

write_markerless_sources()
{
	local dir="$1"

	cat >"$dir/owner.c" <<'EOF'
#include <nvkmod.h>

int crypto_consumer(void);

int init_module(void)
{
	return crypto_consumer();
}

void cleanup_module(void)
{
}

NEVERC_KRT_DEFINE_MODULE("nvk_rt_test");
EOF

	cat >"$dir/addr_owner.c" <<'EOF'
#include <nvkmod.h>

int addr_consumer(void);

int init_module(void)
{
	return addr_consumer();
}

void cleanup_module(void)
{
}

NEVERC_KRT_DEFINE_MODULE("nvk_rt_addr_test");
EOF

	cat >"$dir/inline_owner.c" <<'EOF'
#include <nvkmod.h>

int inline_consumer(void);

int init_module(void)
{
	return inline_consumer();
}

void cleanup_module(void)
{
}

NEVERC_KRT_DEFINE_MODULE("nvk_rt_inline_test");
EOF

	cat >"$dir/crypto_consumer.c" <<'EOF'
#include <nvk_crypto.h>

int crypto_consumer(void)
{
	unsigned char digest[32];

	neverc_krt_sha256("x", 1, digest);
	return digest[0] == 0xff;
}
EOF

	cat >"$dir/addr_consumer.c" <<'EOF'
#include <linux/io.h>
#include <nvk_addr.h>

int addr_consumer(void)
{
	unsigned long bits = neverc_krt_va_bits();
	unsigned long page_size = neverc_krt_page_size();

	return bits < 39 || page_size < 4096 ||
	       (bits == ~0UL && ioremap((phys_addr_t)0, page_size) != (void *)0);
}
EOF

	cat >"$dir/inline_consumer.c" <<'EOF'
#include <nvk_cpu.h>
#include <nvk_interpose.h>
#include <nvk_mem.h>
#include <nvk_pmu.h>
#include <nvk_timer.h>

int inline_consumer(void)
{
	struct neverc_krt_interpose interpose = { 0 };
	volatile u64 atomic_value = 1;
	u64 midr = neverc_krt_cpu_midr();
	u64 counter = neverc_krt_arch_counter();
	u64 cycles = neverc_krt_pmu_cycle_count();
	u64 old_value;

	neverc_krt_interpose_enable(&interpose);
	neverc_krt_interpose_reset_stats(&interpose);
	old_value = neverc_krt_mem_xchg64(&atomic_value, 2);
	neverc_krt_mem_atomic_write64(&atomic_value, old_value);

	return midr == ~0ULL &&
	       (neverc_krt_has_pac() || counter == cycles) &&
	       neverc_krt_interpose_is_enabled(&interpose) &&
	       neverc_krt_interpose_hits(&interpose) == 0 &&
	       neverc_krt_mem_atomic_read64(&atomic_value) == 1;
}
EOF
}

write_shared_state_sources()
{
	local dir="$1"

	cat >"$dir/owner.c" <<'EOF'
#include <nvkmod.h>

int log_level_from_a(void);
int log_level_from_b(void);

int init_module(void)
{
	int ret = NEVERC_KRT_BOOTSTRAP();

	if (ret)
		return ret;
	if (log_level_from_a() != 4)
		return -1;
	if (log_level_from_b() != 4)
		return -2;
	return 0;
}

void cleanup_module(void)
{
}

NEVERC_KRT_DEFINE_MODULE("nvk_rt_state_test");
EOF

	cat >"$dir/consumer_a.c" <<'EOF'
#include <nvk_log.h>
#include <nvk_ksyms.h>

int log_level_from_a(void)
{
	neverc_krt_sym_cache_clear();
	neverc_krt_log_set_level(4);
	return neverc_krt_log_get_level();
}
EOF

	cat >"$dir/consumer_b.c" <<'EOF'
#include <nvk_log.h>
#include <nvk_ksyms.h>

int log_level_from_b(void)
{
	unsigned long addr;

	if (neverc_krt_ksyms_resolve("printk", &addr) == 1)
		return -3;
	return neverc_krt_log_get_level();
}
EOF
}

write_collision_source()
{
	local dir="$1"

	cat >"$dir/collision.c" <<'EOF'
#include <nvk_crypto.h>
#include <nvkmod.h>

__attribute__((used))
static const char _neverc_krt_sha256_k[] = "NVK_USER_LOCAL_GLOBAL";

__attribute__((used))
static int _neverc_krt_sha256_transform(void)
{
	static const char value[] = "NVK_USER_LOCAL_FUNCTION";
	return value[0];
}

int init_module(void)
{
	unsigned char digest[32];

	neverc_krt_sha256("collision", 9, digest);
	return digest[0] == 0xff;
}

void cleanup_module(void)
{
}

NEVERC_KRT_DEFINE_MODULE("nvk_rt_collision_test");
EOF
}

read_symbols()
{
	local ko="$1"
	local table

	if ! table="$("$NM" -a "$ko" 2>&1)"; then
		echo "FAIL: cannot inspect ELF symbols in $ko" >&2
		echo "$table" >&2
		return 1
	fi
	printf '%s\n' "$table"
}

check_no_undefined_runtime()
{
	local ko="$1"
	local table
	local bad

	table="$(read_symbols "$ko")"
	bad="$(printf '%s\n' "$table" | awk \
		'$NF ~ /^_?neverc_krt_/ && $(NF - 1) ~ /^[Uu]$/ { print }')"
	if [ -n "$bad" ]; then
		# Baseline on 2026-07-11: markerless crypto left
		# "U neverc_krt_sha256" in the final relocatable ELF.
		echo "FAIL: unresolved embedded-runtime symbols in $ko" >&2
		echo "$bad" >&2
		return 1
	fi
}

check_single_runtime_definition()
{
	local ko="$1"
	local symbol="$2"
	local table
	local count

	table="$(read_symbols "$ko")"
	count="$(printf '%s\n' "$table" | awk -v symbol="$symbol" \
		'$NF == symbol && $(NF - 1) !~ /^[Uu]$/ { count++ }
		 END { print count + 0 }')"
	if [ "$count" -ne 1 ]; then
		echo "FAIL: expected one definition of $symbol in $ko, found $count" >&2
		printf '%s\n' "$table" | awk -v symbol="$symbol" \
			'$NF == symbol { print }' >&2
		return 1
	fi
}

check_no_runtime_definition()
{
	local ko="$1"
	local symbol="$2"
	local table

	table="$(read_symbols "$ko")"
	if printf '%s\n' "$table" | awk -v symbol="$symbol" \
		'$NF == symbol && $(NF - 1) !~ /^[Uu]$/ { found = 1 }
		 END { exit !found }'; then
		echo "FAIL: expected $symbol to inline completely in $ko" >&2
		printf '%s\n' "$table" | awk -v symbol="$symbol" \
			'$NF == symbol { print }' >&2
		return 1
	fi
}

check_user_local_symbol()
{
	local ko="$1"
	local symbol="$2"
	local table
	local matches

	table="$(read_symbols "$ko")"
	matches="$(printf '%s\n' "$table" | awk -v symbol="$symbol" \
		'$NF == symbol { print }')"
	if [ "$(printf '%s\n' "$matches" | awk 'NF { count++ } END {
		print count + 0
	}')" -ne 1 ] ||
	    ! printf '%s\n' "$matches" | awk '$(NF - 1) ~ /^[a-z]$/ {
		found = 1
	    } END { exit !found }'; then
		echo "FAIL: user-local collision symbol was lost or made public: $symbol" >&2
		printf '%s\n' "$matches" >&2
		return 1
	fi
}

check_no_duplicate_comdat()
{
	local ko="$1"
	local symbol="$2"
	local groups
	local count

	if [ -z "$READELF" ]; then
		return
	fi
	groups="$("$READELF" --section-groups "$ko")"
	count="$(printf '%s\n' "$groups" | awk -v symbol="$symbol" \
		'index($0, "COMDAT group") && index($0, "[" symbol "]") {
			count++
		 } END { print count + 0 }')"
	if [ "$count" -gt 1 ]; then
		echo "FAIL: duplicate COMDAT groups for $symbol in $ko" >&2
		return 1
	fi
}

run_markerless_test()
{
	local kernel="$1"
	local dir="$TMP_ROOT/markerless-$kernel"
	local ko="$dir/markerless.ko"

	mkdir -p "$dir"
	write_markerless_sources "$dir"

	"$NEVERC" --target=aarch64-linux-android \
		-fandroid-kernel-driver-mode \
		-DNVK_KERNEL="$kernel" \
		-Wall -Wno-unused \
		-r -nostdlib \
		"$dir/owner.c" "$dir/crypto_consumer.c" \
		-o "$ko"

	check_no_undefined_runtime "$ko"
	echo "PASS: markerless runtime consumer @ $kernel"
}

run_markerless_addr_test()
{
	local kernel="$1"
	local mode="$2"
	local dir="$TMP_ROOT/address-$kernel-$mode"
	local ko="$dir/address.ko"
	local lto_flag=""
	local -a compiler_args

	case "$mode" in
	auto)
		;;
	full)
		lto_flag="-flto=full"
		;;
	none)
		lto_flag="-fno-lto"
		;;
	esac

	mkdir -p "$dir"
	write_markerless_sources "$dir"

	compiler_args=(
		--target=aarch64-linux-android
		-fandroid-kernel-driver-mode
		-DNVK_KERNEL="$kernel"
		-Wall -Wno-unused
	)
	if [ -n "$lto_flag" ]; then
		compiler_args+=("$lto_flag")
	fi
	compiler_args+=(
		-r -nostdlib
		"$dir/addr_owner.c" "$dir/addr_consumer.c"
		-o "$ko"
	)

	"$NEVERC" "${compiler_args[@]}"

	check_no_undefined_runtime "$ko"
	check_single_runtime_definition "$ko" neverc_krt_ioremap
	echo "PASS: markerless address runtime consumer @ $kernel ($mode)"
}

run_markerless_inline_test()
{
	local kernel="$1"
	local mode="$2"
	local dir="$TMP_ROOT/inline-$kernel-$mode"
	local ko="$dir/inline.ko"
	local lto_flag=""
	local -a compiler_args

	case "$mode" in
	auto)
		;;
	full)
		lto_flag="-flto=full"
		;;
	none)
		lto_flag="-fno-lto"
		;;
	esac

	mkdir -p "$dir"
	write_markerless_sources "$dir"

	compiler_args=(
		--target=aarch64-linux-android
		-fandroid-kernel-driver-mode
		-DNVK_KERNEL="$kernel"
		-Wall -Wno-unused
	)
	if [ -n "$lto_flag" ]; then
		compiler_args+=("$lto_flag")
	fi
	compiler_args+=(
		-r -nostdlib
		"$dir/inline_owner.c" "$dir/inline_consumer.c"
		-o "$ko"
	)

	"$NEVERC" "${compiler_args[@]}"

	check_no_undefined_runtime "$ko"
	check_no_runtime_definition "$ko" neverc_krt_cpu_midr
	check_no_runtime_definition "$ko" neverc_krt_has_pac
	check_no_runtime_definition "$ko" neverc_krt_arch_counter
	check_no_runtime_definition "$ko" neverc_krt_pmu_cycle_count
	check_no_runtime_definition "$ko" neverc_krt_interpose_enable
	check_no_runtime_definition "$ko" neverc_krt_interpose_hits
	check_no_runtime_definition "$ko" neverc_krt_interpose_is_enabled
	check_no_runtime_definition "$ko" neverc_krt_interpose_reset_stats
	check_no_runtime_definition "$ko" neverc_krt_mem_atomic_read64
	check_no_runtime_definition "$ko" neverc_krt_mem_atomic_write64
	check_no_runtime_definition "$ko" neverc_krt_mem_xchg64
	echo "PASS: markerless forced-inline runtime consumer @ $kernel ($mode)"
}

run_shared_state_test()
{
	local kernel="$1"
	local mode="$2"
	local dir="$TMP_ROOT/shared-$kernel-$mode"
	local ko="$dir/shared-state.ko"
	local lto_flag=""
	local -a compiler_args

	case "$mode" in
	auto)
		;;
	full)
		lto_flag="-flto=full"
		;;
	none)
		# Baseline on 2026-07-11: duplicate weak __cfi_check definitions
		# caused a false offset-shift rejection in the relocatable verifier.
		lto_flag="-fno-lto"
		;;
	esac

	mkdir -p "$dir"
	write_shared_state_sources "$dir"

	compiler_args=(
		--target=aarch64-linux-android
		-fandroid-kernel-driver-mode
		-DNVK_KERNEL="$kernel"
		-Wall -Wno-unused
	)
	if [ -n "$lto_flag" ]; then
		compiler_args+=("$lto_flag")
	fi
	compiler_args+=(
		-r -nostdlib
		"$dir/owner.c" "$dir/consumer_a.c" "$dir/consumer_b.c"
		-o "$ko"
	)

	"$NEVERC" "${compiler_args[@]}"

	check_no_undefined_runtime "$ko"
	for symbol in "${SHARED_STATE_SYMBOLS[@]}"; do
		check_single_runtime_definition "$ko" "$symbol"
		check_no_duplicate_comdat "$ko" "$symbol"
	done
	echo "PASS: shared runtime state @ $kernel ($mode)"
}

run_collision_test()
{
	local kernel="$1"
	local dir="$TMP_ROOT/collision-$kernel"
	local ko="$dir/collision.ko"

	mkdir -p "$dir"
	write_collision_source "$dir"

	"$NEVERC" --target=aarch64-linux-android \
		-fandroid-kernel-driver-mode \
		-DNVK_KERNEL="$kernel" \
		-Wall -Wno-unused \
		-fno-lto -r -nostdlib \
		"$dir/collision.c" -o "$ko"

	check_no_undefined_runtime "$ko"
	check_user_local_symbol "$ko" _neverc_krt_sha256_k
	check_user_local_symbol "$ko" _neverc_krt_sha256_transform
	if ! strings "$ko" | awk '
		/NVK_USER_LOCAL_GLOBAL/ { global = 1 }
		/NVK_USER_LOCAL_FUNCTION/ { function_seen = 1 }
		END { exit !(global && function_seen) }
	'; then
		echo "FAIL: user-local collision sentinel was removed from $ko" >&2
		return 1
	fi
	echo "PASS: runtime/user-local provenance collision @ $kernel"
}

for kernel in "${KERNELS[@]}"; do
	run_markerless_test "$kernel"
	for mode in "${LTO_MODES[@]}"; do
		run_markerless_addr_test "$kernel" "$mode"
		run_markerless_inline_test "$kernel" "$mode"
		run_shared_state_test "$kernel" "$mode"
	done
	run_collision_test "$kernel"
done

echo "All focused NVK runtime linkage checks passed."
