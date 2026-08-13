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
  -fsanitize=undefined \
  -DNCI_HAS_BUILTINS=0 \
  "-I$std_root/include" \
  "$test_root/test_bits.c" \
  "$std_root/src/math/bits/bits.c" \
  -o "$work_dir/bits-fallback-ubsan"

UBSAN_OPTIONS=${UBSAN_OPTIONS:-halt_on_error=1:print_stacktrace=1} \
  "$work_dir/bits-fallback-ubsan"
