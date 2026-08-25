#!/usr/bin/env bash

set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "usage: $0 <repository-root>" >&2
  exit 2
fi

repo_root=$1
compiler=${CC:-clang}
std_root="$repo_root/std"
test_root="$repo_root/tests/neverc/std"
work_dir=$(mktemp -d "${TMPDIR:-/tmp}/neverc-std-sanitizers.XXXXXX")
trap 'rm -rf "$work_dir"' EXIT

"$compiler" \
  -std=gnu11 \
  -O1 \
  -g \
  -fno-omit-frame-pointer \
  -Wall \
  -Wextra \
  -Werror \
  -fsanitize=undefined \
  "-I$std_root/include" \
  "$test_root/test_rand_ubsan.c" \
  "$std_root/src/math/rand/rand.c" \
  "$std_root/src/math/log.c" \
  "$std_root/src/math/exp.c" \
  "$std_root/src/math/frexp.c" \
  "$std_root/src/math/ldexp.c" \
  "$std_root/src/math/floor.c" \
  "$std_root/src/math/modf.c" \
  "$std_root/src/math/trunc.c" \
  "$std_root/src/math/sqrt.c" \
  "$std_root/src/math/isinf.c" \
  -lm \
  -o "$work_dir/rand-ubsan"

UBSAN_OPTIONS=${UBSAN_OPTIONS:-halt_on_error=1:print_stacktrace=1} \
  "$work_dir/rand-ubsan"

"$compiler" \
  -std=gnu11 \
  -O1 \
  -g \
  -fno-omit-frame-pointer \
  -Wall \
  -Wextra \
  -Werror \
  -fsanitize=address,undefined \
  "-I$std_root/include" \
  "$test_root/test_crypto_ctx_abi.c" \
  "$std_root/src/crypto/des/des.c" \
  "$std_root/src/crypto/md5/md5.c" \
  "$std_root/src/crypto/sha1/sha1.c" \
  "$std_root/src/crypto/sha224/sha224.c" \
  "$std_root/src/crypto/sha256/sha256.c" \
  "$std_root/src/crypto/sha3/sha3.c" \
  -o "$work_dir/crypto-ctx-abi-asan-ubsan"

ASAN_OPTIONS=${ASAN_OPTIONS:-halt_on_error=1:detect_leaks=1} \
UBSAN_OPTIONS=${UBSAN_OPTIONS:-halt_on_error=1:print_stacktrace=1} \
  "$work_dir/crypto-ctx-abi-asan-ubsan"

"$compiler" \
  -std=gnu11 \
  -O1 \
  -g \
  -fno-omit-frame-pointer \
  -Wall \
  -Wextra \
  -Werror \
  -fsanitize=address,undefined \
  "-I$std_root/include" \
  "$test_root/test_zip_writer_abi.c" \
  "$std_root/src/archive/zip/zip.c" \
  "$std_root/src/hash/crc32/crc32.c" \
  "$std_root/src/io/fs/fs.c" \
  "$std_root/src/path/match.c" \
  "$std_root/src/unicode/utf8/utf8.c" \
  -o "$work_dir/zip-writer-abi-asan-ubsan"

ASAN_OPTIONS=${ASAN_OPTIONS:-halt_on_error=1:detect_leaks=1} \
UBSAN_OPTIONS=${UBSAN_OPTIONS:-halt_on_error=1:print_stacktrace=1} \
  "$work_dir/zip-writer-abi-asan-ubsan"

"$compiler" \
  -std=gnu11 \
  -O1 \
  -g \
  -fno-omit-frame-pointer \
  -Wall \
  -Wextra \
  -Werror \
  -fsanitize=address,undefined \
  "-I$std_root/include" \
  "$test_root/test_dwarf_data_abi.c" \
  -o "$work_dir/dwarf-data-abi-asan-ubsan"

ASAN_OPTIONS=${ASAN_OPTIONS:-halt_on_error=1:detect_leaks=1} \
UBSAN_OPTIONS=${UBSAN_OPTIONS:-halt_on_error=1:print_stacktrace=1} \
  "$work_dir/dwarf-data-abi-asan-ubsan"

"$compiler" \
  -std=gnu11 \
  -O1 \
  -g \
  -fno-omit-frame-pointer \
  -Wall \
  -Wextra \
  -Werror \
  -fsanitize=thread \
  "-I$std_root/include" \
  "$test_root/test_rand_concurrency.c" \
  "$std_root/src/math/rand/rand.c" \
  "$std_root/src/math/log.c" \
  "$std_root/src/math/exp.c" \
  "$std_root/src/math/frexp.c" \
  "$std_root/src/math/ldexp.c" \
  "$std_root/src/math/floor.c" \
  "$std_root/src/math/modf.c" \
  "$std_root/src/math/trunc.c" \
  "$std_root/src/math/sqrt.c" \
  "$std_root/src/math/isinf.c" \
  -lm \
  -pthread \
  -o "$work_dir/rand-tsan"

TSAN_OPTIONS=${TSAN_OPTIONS:-halt_on_error=1} \
  "$work_dir/rand-tsan"

"$compiler" \
  -std=gnu11 \
  -O1 \
  -g \
  -fno-omit-frame-pointer \
  -Wall \
  -Wextra \
  -Werror \
  -fsanitize=thread \
  "-I$std_root/include" \
  "$test_root/test_mlkem_concurrency.c" \
  "$std_root/src/crypto/mlkem/mlkem.c" \
  "$std_root/src/crypto/sha3/sha3.c" \
  "$std_root/src/crypto/rand/rand.c" \
  -pthread \
  -o "$work_dir/mlkem-tsan"

TSAN_OPTIONS=${TSAN_OPTIONS:-halt_on_error=1} \
  "$work_dir/mlkem-tsan"

"$compiler" \
  -std=gnu11 \
  -O1 \
  -g \
  -fno-omit-frame-pointer \
  -Wall \
  -Wextra \
  -Werror \
  -fsanitize=thread \
  "-I$std_root/include" \
  "$test_root/test_mldsa_concurrency.c" \
  "$std_root/src/crypto/mldsa/mldsa.c" \
  "$std_root/src/crypto/sha3/sha3.c" \
  "$std_root/src/crypto/rand/rand.c" \
  "$std_root/src/crypto/subtle/subtle.c" \
  -pthread \
  -o "$work_dir/mldsa-tsan"

TSAN_OPTIONS=${TSAN_OPTIONS:-halt_on_error=1} \
  "$work_dir/mldsa-tsan"

"$compiler" \
  -std=gnu11 \
  -O1 \
  -g \
  -fno-omit-frame-pointer \
  -Wall \
  -Wextra \
  -Werror \
  -fsanitize=thread \
  "-I$std_root/include" \
  "$test_root/test_os_concurrency.c" \
  "$std_root/src/os/os.c" \
  -pthread \
  -o "$work_dir/os-tsan"

TSAN_OPTIONS=${TSAN_OPTIONS:-halt_on_error=1} \
  "$work_dir/os-tsan"

"$compiler" \
  -std=gnu11 \
  -O1 \
  -g \
  -fno-omit-frame-pointer \
  -Wall \
  -Wextra \
  -Werror \
  -fsanitize=thread \
  -DNEVERC_SIGNAL_SKIP_FORK=1 \
  "-I$std_root/include" \
  "$test_root/test_signal_concurrency.c" \
  "$std_root/src/os/signal/signal.c" \
  -pthread \
  -o "$work_dir/signal-tsan"

TSAN_OPTIONS=${TSAN_OPTIONS:-halt_on_error=1} \
  "$work_dir/signal-tsan"

"$compiler" \
  -std=gnu11 \
  -O1 \
  -g \
  -fno-omit-frame-pointer \
  -Wall \
  -Wextra \
  -Werror \
  -fsanitize=undefined \
  -DNCI_HAS_BUILTINS=0 \
  "-I$std_root/include" \
  "$test_root/test_bits.c" \
  "$std_root/src/math/bits/bits.c" \
  -o "$work_dir/bits-fallback-ubsan"

UBSAN_OPTIONS=${UBSAN_OPTIONS:-halt_on_error=1:print_stacktrace=1} \
  "$work_dir/bits-fallback-ubsan"
