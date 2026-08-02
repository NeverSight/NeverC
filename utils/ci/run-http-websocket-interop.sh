#!/usr/bin/env bash

set -euo pipefail

if [[ $# -lt 1 || $# -gt 3 ]]; then
  echo "usage: $0 <repository-root> [c-compiler] [port]" >&2
  exit 2
fi

repo_root=$1
compiler=${2:-clang}
port=${3:-19090}
if [[ ! $port =~ ^[1-9][0-9]*$ ]] || ((port > 65535)); then
  echo "invalid port: $port" >&2
  exit 2
fi

work_dir=$(mktemp -d "${TMPDIR:-/tmp}/neverc-http-ws-interop.XXXXXX")
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
  "$repo_root/tests/neverc/std/network_interop_server.c" \
  "${std_sources[@]}" "${platform_libraries[@]}" \
  -o "$work_dir/network-interop-server"

"$work_dir/network-interop-server" "$port" \
  >"$work_dir/server.log" 2>&1 &
server_pid=$!
base_url="http://127.0.0.1:$port"
for ((attempt = 0; attempt < 100; ++attempt)); do
  if curl --fail --silent "$base_url/echo" >/dev/null; then
    break
  fi
  sleep 0.05
done
curl --fail --silent --show-error "$base_url/echo" |
  rg --fixed-strings 'NeverC interop GET' >/dev/null
curl --fail --silent --show-error --data-binary 'curl-interop' \
  "$base_url/echo" | rg --fixed-strings 'curl-interop' >/dev/null
go run "$repo_root/tests/neverc/interop/go_http_client.go" "$base_url"

if [[ ${NEVERC_RUN_AUTOBAHN:-0} == 1 ]]; then
  reports="$work_dir/autobahn-reports"
  mkdir -p "$reports"
  docker run --rm --network host \
    -v "$repo_root/tests/neverc/interop/autobahn-fuzzingclient.json:/config/fuzzingclient.json:ro" \
    -v "$reports:/reports" \
    crossbario/autobahn-testsuite:25.10.1 \
    wstest --mode fuzzingclient --spec /config/fuzzingclient.json
  if rg -q '"behavior"[[:space:]]*:[[:space:]]*"(FAILED|NON-STRICT|UNIMPLEMENTED)"' \
      "$reports"; then
    echo "Autobahn reported non-conforming WebSocket cases" >&2
    exit 1
  fi
  test -n "$(find "$reports" -type f -print -quit)"
  echo "Autobahn WebSocket interop passed"
fi

echo "curl and Go net/http interop passed"
