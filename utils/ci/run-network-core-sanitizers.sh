#!/usr/bin/env bash

set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "usage: $0 <repository-root>" >&2
  exit 2
fi

repo_root=$1
compiler=${CC:-clang}
cxx_compiler=${CXX:-clang++}
std_root="$repo_root/std"
test_root="$repo_root/tests/neverc/std"
work_dir=$(mktemp -d "${TMPDIR:-/tmp}/neverc-network-sanitizers.XXXXXX")
trap 'rm -rf "$work_dir"' EXIT

common_flags=(
  -std=gnu11
  -O1
  -g
  -fno-omit-frame-pointer
  -Wall
  -Wextra
  -Werror
  -Wno-unused-parameter
  -Wno-unused-function
  "-I$std_root/include"
  "-I$std_root/src/net"
)

build_cases() {
  local label=$1
  shift
  "$compiler" "${common_flags[@]}" "$@" \
    "$test_root/test_thread.c" \
    "$std_root/src/thread/thread.c" \
    "$std_root/src/context/context.c" \
    -pthread -lm -o "$work_dir/thread-$label"
  "$compiler" "${common_flags[@]}" "$@" \
    "$test_root/test_net_internals.c" \
    "$std_root/src/net/tcp/tcp.c" \
    -pthread -lm -o "$work_dir/net-internals-$label"
  "$compiler" "${common_flags[@]}" "$@" \
    "$test_root/test_net_transport.c" \
    "$std_root/src/net/tcp/tcp.c" \
    "$std_root/src/net/tcp/tcp_context.c" \
    "$std_root/src/net/udp/udp.c" \
    "$std_root/src/net/udp/udp_context.c" \
    "$std_root/src/thread/thread.c" \
    "$std_root/src/context/context.c" \
    -pthread -lm -o "$work_dir/net-transport-$label"
  "$compiler" "${common_flags[@]}" "$@" \
    "$test_root/test_udp.c" \
    "$std_root/src/net/udp/udp.c" \
    "$std_root/src/net/udp/udp_context.c" \
    "$std_root/src/context/context.c" \
    -pthread -lm -o "$work_dir/udp-$label"
  "$compiler" "${common_flags[@]}" "$@" \
    "$test_root/test_quic_frame.c" \
    -pthread -lm -o "$work_dir/quic-frame-$label"
  "$compiler" "${common_flags[@]}" "$@" \
    -DNEVERC_NET_PIPE_TESTING=1 \
    "$test_root/test_resolve.c" \
    "$std_root/src/net/resolve/resolve.c" \
    "$std_root/src/net/netip/netip.c" \
    "$std_root/src/unicode/unicode.c" \
    -pthread -lm -lresolv -o "$work_dir/resolve-$label"
}

build_cases asan-ubsan -fsanitize=address,undefined
default_asan_options=detect_leaks=1
if [[ $(uname -s) == Darwin ]]; then
  default_asan_options=detect_leaks=0
fi
asan_options=${ASAN_OPTIONS:-$default_asan_options}
ASAN_OPTIONS=$asan_options \
UBSAN_OPTIONS=${UBSAN_OPTIONS:-halt_on_error=1:print_stacktrace=1} \
  "$work_dir/thread-asan-ubsan"
ASAN_OPTIONS=$asan_options \
UBSAN_OPTIONS=${UBSAN_OPTIONS:-halt_on_error=1:print_stacktrace=1} \
  "$work_dir/net-internals-asan-ubsan"
ASAN_OPTIONS=$asan_options \
UBSAN_OPTIONS=${UBSAN_OPTIONS:-halt_on_error=1:print_stacktrace=1} \
  "$work_dir/net-transport-asan-ubsan"
ASAN_OPTIONS=$asan_options \
UBSAN_OPTIONS=${UBSAN_OPTIONS:-halt_on_error=1:print_stacktrace=1} \
  "$work_dir/udp-asan-ubsan"
ASAN_OPTIONS=$asan_options \
UBSAN_OPTIONS=${UBSAN_OPTIONS:-halt_on_error=1:print_stacktrace=1} \
  "$work_dir/quic-frame-asan-ubsan"
ASAN_OPTIONS=$asan_options \
UBSAN_OPTIONS=${UBSAN_OPTIONS:-halt_on_error=1:print_stacktrace=1} \
  "$work_dir/resolve-asan-ubsan"

build_cases tsan -fsanitize=thread
TSAN_OPTIONS=${TSAN_OPTIONS:-halt_on_error=1:history_size=7} \
  "$work_dir/thread-tsan"
TSAN_OPTIONS=${TSAN_OPTIONS:-halt_on_error=1:history_size=7} \
  "$work_dir/net-internals-tsan"
TSAN_OPTIONS=${TSAN_OPTIONS:-halt_on_error=1:history_size=7} \
  "$work_dir/net-transport-tsan"
TSAN_OPTIONS=${TSAN_OPTIONS:-halt_on_error=1:history_size=7} \
  "$work_dir/udp-tsan"
TSAN_OPTIONS=${TSAN_OPTIONS:-halt_on_error=1:history_size=7} \
  "$work_dir/quic-frame-tsan"
TSAN_OPTIONS=${TSAN_OPTIONS:-halt_on_error=1:history_size=7} \
  "$work_dir/resolve-tsan"

protocol_flags=(
  -O1
  -g
  -fno-omit-frame-pointer
  -ffunction-sections
  -fdata-sections
  -Wall
  -Wextra
  -Werror
  -Wno-unused-parameter
  -Wno-unused-function
  -DNEVERC_NETWORK_PROTOCOL_FUZZING=1
  -fsanitize=address,undefined
  "-I$std_root/include"
  "-I$std_root/src/net"
  "-I$std_root/src/net/quic"
)
protocol_sources=(
  "$repo_root/tests/neverc/NetworkProtocolFuzzAdapters.c"
  "$std_root/src/net/http/http.c"
  "$std_root/src/net/websocket/websocket.c"
  "$std_root/src/net/url/url.c"
  "$std_root/src/net/netip/netip.c"
  "$std_root/src/unicode/unicode.c"
  "$std_root/src/net/http/http2/http2.c"
  "$std_root/src/net/http/http2/http2_server.c"
  "$std_root/src/net/http3/http3_frame.c"
  "$std_root/src/net/quic/quic_varint.c"
  "$std_root/src/net/quic/quic_packet.c"
  "$std_root/src/net/quic/quic_frame.c"
  "$std_root/src/net/quic/quic_transport_params.c"
  "$std_root/src/net/rpc/rpc_frame.c"
)
protocol_objects=()
for source in "${protocol_sources[@]}"; do
  object="$work_dir/protocol-${#protocol_objects[@]}.o"
  "$compiler" -std=gnu11 "${protocol_flags[@]}" -c "$source" -o "$object"
  protocol_objects+=("$object")
done
for source in \
  "$repo_root/tests/neverc/NetworkProtocolFuzzer.cpp" \
  "$repo_root/tests/neverc/NetworkProtocolCorpusRunner.cpp"; do
  object="$work_dir/protocol-${#protocol_objects[@]}.o"
  "$cxx_compiler" -std=c++17 "${protocol_flags[@]}" -c "$source" -o "$object"
  protocol_objects+=("$object")
done
dead_strip_flag=-Wl,--gc-sections
if [[ $(uname -s) == Darwin ]]; then
  dead_strip_flag=-Wl,-dead_strip
fi
"$cxx_compiler" -fsanitize=address,undefined "$dead_strip_flag" \
  "${protocol_objects[@]}" -pthread -lm -o "$work_dir/network-protocol-corpus"
ASAN_OPTIONS=$asan_options \
UBSAN_OPTIONS=${UBSAN_OPTIONS:-halt_on_error=1:print_stacktrace=1} \
  "$work_dir/network-protocol-corpus" \
  "$repo_root/tests/neverc/network-protocol-corpus"

full_std_sources=()
while IFS= read -r source; do
  full_std_sources+=("$source")
done < <(find "$repo_root/std/src" -name '*.c' | sort)
if ((${#full_std_sources[@]} == 0)); then
  echo "failed to enumerate std/src/*.c sources" >&2
  exit 1
fi
full_platform_libraries=(-pthread -lm -lresolv)
if [[ $(uname -s) == Linux ]]; then
  full_platform_libraries+=(-ldl)
fi
full_stack_tests=(
  http_stage5
  rpc
  grpc
  quic_e2e
  quic_network_sim
  http3_e2e
)
for name in "${full_stack_tests[@]}"; do
  "$compiler" -std=gnu11 "${protocol_flags[@]}" \
    "$test_root/test_$name.c" "${full_std_sources[@]}" \
    "${full_platform_libraries[@]}" -o "$work_dir/$name-asan-ubsan"
  (cd "$work_dir" && \
    ASAN_OPTIONS=$asan_options \
    UBSAN_OPTIONS=${UBSAN_OPTIONS:-halt_on_error=1:print_stacktrace=1} \
      "./$name-asan-ubsan")
done

tls_config_race_flags=(
  -DNEVERC_TLS_ENABLE_EXPERIMENTAL_TRANSPORT=1
  -DNEVERC_TLS_TESTING=1
  -DNEVERC_TLS_CONFIG_RACE_ONLY=1
)
"$compiler" -std=gnu11 "${protocol_flags[@]}" \
  "${tls_config_race_flags[@]}" \
  "$test_root/test_tls.c" "${full_std_sources[@]}" \
  "${full_platform_libraries[@]}" \
  -o "$work_dir/tls-config-race-asan-ubsan"
(cd "$work_dir" && \
  ASAN_OPTIONS=$asan_options \
  UBSAN_OPTIONS=${UBSAN_OPTIONS:-halt_on_error=1:print_stacktrace=1} \
    ./tls-config-race-asan-ubsan)

full_tsan_flags=(
  -O1
  -g
  -fno-omit-frame-pointer
  -Wall
  -Wextra
  -Werror
  -Wno-unused-parameter
  -Wno-unused-function
  -fsanitize=thread
  "-I$std_root/include"
  "-I$std_root/src/net"
  "-I$std_root/src/net/quic"
)
for name in "${full_stack_tests[@]}"; do
  "$compiler" -std=gnu11 "${full_tsan_flags[@]}" \
    "$test_root/test_$name.c" "${full_std_sources[@]}" \
    "${full_platform_libraries[@]}" -o "$work_dir/$name-tsan"
  (cd "$work_dir" && \
    TSAN_OPTIONS=${TSAN_OPTIONS:-halt_on_error=1:history_size=7} \
      "./$name-tsan")
done
"$compiler" -std=gnu11 "${full_tsan_flags[@]}" \
  "${tls_config_race_flags[@]}" \
  "$test_root/test_tls.c" "${full_std_sources[@]}" \
  "${full_platform_libraries[@]}" \
  -o "$work_dir/tls-config-race-tsan"
(cd "$work_dir" && \
  TSAN_OPTIONS=${TSAN_OPTIONS:-halt_on_error=1:history_size=7} \
    ./tls-config-race-tsan)

echo "network-core sanitizer gates passed"
