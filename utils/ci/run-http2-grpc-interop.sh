#!/usr/bin/env bash

set -euo pipefail

if [[ $# -lt 1 || $# -gt 3 ]]; then
  echo "usage: $0 <repository-root> [c-compiler] [port]" >&2
  exit 2
fi

repo_root=$1
compiler=${2:-clang}
port=${3:-19091}
if [[ ! $port =~ ^[1-9][0-9]*$ ]] || ((port > 65535)); then
  echo "invalid port: $port" >&2
  exit 2
fi

work_dir=$(mktemp -d "${TMPDIR:-/tmp}/neverc-http2-grpc-interop.XXXXXX")
server_pid=
cleanup() {
  if [[ -n $server_pid ]]; then
    kill "$server_pid" 2>/dev/null || true
    wait "$server_pid" 2>/dev/null || true
  fi
  rm -rf "$work_dir"
}
trap cleanup EXIT

std_sources=()
while IFS= read -r source; do
  std_sources+=("$repo_root/$source")
done < <(cd "$repo_root" && rg --files std/src | rg '\.c$')
platform_libraries=(-pthread -lm -lresolv)
if [[ $(uname -s) == Linux ]]; then
  platform_libraries+=(-ldl)
fi
"$compiler" -std=gnu11 -O1 -g -Wall -Wextra -Werror \
  -Wno-unused-parameter -Wno-unused-function \
  "-I$repo_root/std/include" \
  "$repo_root/tests/neverc/std/h2_grpc_interop_server.c" \
  "${std_sources[@]}" "${platform_libraries[@]}" \
  -o "$work_dir/h2-grpc-interop-server"

"$work_dir/h2-grpc-interop-server" "$port" \
  >"$work_dir/server.log" 2>&1 &
server_pid=$!
for ((attempt = 0; attempt < 100; ++attempt)); do
  if nc -z 127.0.0.1 "$port" 2>/dev/null; then
    break
  fi
  sleep 0.05
done

if command -v nghttp >/dev/null 2>&1; then
  nghttp --verbose "http://127.0.0.1:$port/" \
    >"$work_dir/nghttp.log" 2>&1
  rg --fixed-strings 'NeverC HTTP/2 interop' "$work_dir/nghttp.log" >/dev/null
  echo "nghttp2 prior-knowledge interop passed"
else
  curl --fail --silent --show-error --http2-prior-knowledge \
    "http://127.0.0.1:$port/" |
    rg --fixed-strings 'NeverC HTTP/2 interop' >/dev/null
  echo "curl HTTP/2 prior-knowledge interop passed"
fi

if [[ ${NEVERC_RUN_H2SPEC:-0} == 1 ]]; then
  if command -v h2spec >/dev/null 2>&1; then
    h2spec --host 127.0.0.1 --port "$port" --strict generic http2 hpack
  else
    docker run --rm --network host summerwind/h2spec:2.6.0 \
      --host 127.0.0.1 --port "$port" --strict generic http2 hpack
  fi
  echo "h2spec strict interop passed"
fi

(cd "$repo_root/tests/neverc/interop/go-grpc" && go run . "127.0.0.1:$port")
