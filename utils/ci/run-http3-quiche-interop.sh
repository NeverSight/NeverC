#!/usr/bin/env bash

set -euo pipefail

if [[ $# -lt 2 || $# -gt 4 ]]; then
  echo "usage: $0 <repository-root> <quiche-client> [c-compiler] [port]" >&2
  exit 2
fi

repo_root=$1
quiche_client=$2
compiler=${3:-clang}
port=${4:-19092}
if [[ ! -x $quiche_client ]]; then
  echo "quiche-client is not executable: $quiche_client" >&2
  exit 2
fi
if [[ ! $port =~ ^[1-9][0-9]*$ ]] || ((port > 65535)); then
  echo "invalid port: $port" >&2
  exit 2
fi

work_dir=$(mktemp -d "${TMPDIR:-/tmp}/neverc-http3-quiche.XXXXXX")
server_pid=
cleanup() {
  local status=$?
  if [[ -n $server_pid ]]; then
    kill "$server_pid" 2>/dev/null || true
    wait "$server_pid" 2>/dev/null || true
  fi
  if ((status != 0)); then
    echo "NeverC HTTP/3 server log:" >&2
    test ! -f "$work_dir/server.log" || cat "$work_dir/server.log" >&2
    echo "quiche client log:" >&2
    test ! -f "$work_dir/quiche.log" || cat "$work_dir/quiche.log" >&2
    echo "ngtcp2 client log:" >&2
    test ! -f "$work_dir/ngtcp2.log" || cat "$work_dir/ngtcp2.log" >&2
  fi
  rm -rf "$work_dir"
  return "$status"
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
  "$repo_root/tests/neverc/std/http3_interop_server.c" \
  "${std_sources[@]}" "${platform_libraries[@]}" \
  -o "$work_dir/http3-interop-server"

(cd "$work_dir" && ./http3-interop-server "$port") \
  >"$work_dir/server.log" 2>&1 &
server_pid=$!
for ((attempt = 0; attempt < 100; ++attempt)); do
  if rg --fixed-strings 'server listening' "$work_dir/server.log" >/dev/null; then
    break
  fi
  if ! kill -0 "$server_pid" 2>/dev/null; then
    cat "$work_dir/server.log" >&2
    exit 1
  fi
  sleep 0.05
done

mkdir -p "$work_dir/responses"
"$quiche_client" --no-verify --http-version HTTP/3 --dump-json \
  --dump-responses "$work_dir/responses" \
  --connect-to "127.0.0.1:$port" \
  "https://localhost:$port/interop" >"$work_dir/quiche.log" 2>&1
rg --fixed-strings 'NeverC HTTP/3 interop' \
  "$work_dir/responses" >/dev/null
echo "official quiche HTTP/3 client interop passed"

if [[ ${NEVERC_RUN_NGTCP2:-0} == 1 ]]; then
  mkdir -p "$work_dir/ngtcp2-responses"
  docker run --rm --network host \
    --entrypoint /usr/local/bin/wsslclient \
    -v "$work_dir/ngtcp2-responses:/downloads" \
    ghcr.io/ngtcp2/ngtcp2-interop:latest \
    127.0.0.1 "$port" --download /downloads --no-quic-dump \
    --no-http-dump --exit-on-all-streams-close -v v1 \
    "https://localhost:$port/interop" >"$work_dir/ngtcp2.log" 2>&1
  rg --fixed-strings 'NeverC HTTP/3 interop' \
    "$work_dir/ngtcp2-responses" >/dev/null
  echo "official ngtcp2/nghttp3 HTTP/3 client interop passed"
fi
