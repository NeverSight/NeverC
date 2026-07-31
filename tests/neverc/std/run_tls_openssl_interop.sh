#!/usr/bin/env bash
# NeverC TLS client/server ↔ OpenSSL interop gate.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
BUILD="${NEVERC_BUILD_DIR:-$ROOT/build-neverc}"
PEER="${1:-$BUILD/tls-interop-peer}"
CERT="${2:-$BUILD/tls-interop-cert.pem}"
KEY="${3:-$BUILD/tls-interop-key.pem}"
PORT="${NEVERC_TLS_INTEROP_PORT:-18443}"
ADDR="127.0.0.1:${PORT}"
REVERSE_PORT=$((PORT + 1))
REVERSE_ADDR="127.0.0.1:${REVERSE_PORT}"

if ! command -v openssl >/dev/null 2>&1; then
  echo "skip: openssl not available"
  exit 0
fi
if [[ ! -x "$PEER" ]]; then
  echo "missing interop binary: $PEER" >&2
  exit 1
fi
mkdir -p "$BUILD"
if [[ ! -f "$CERT" || ! -f "$KEY" ]]; then
  openssl req -x509 -newkey ec -pkeyopt ec_paramgen_curve:P-256 \
    -keyout "$KEY" -out "$CERT" -days 2 -nodes \
    -subj "/CN=localhost" \
    -addext "subjectAltName=DNS:localhost" >/dev/null 2>&1
fi

LOG_DIR=$(mktemp -d "${TMPDIR:-/tmp}/neverc-tls-interop.XXXXXX")

stop_server() {
  if [[ -n "${SERVER_PID:-}" ]]; then
    kill "$SERVER_PID" 2>/dev/null || true
    wait "$SERVER_PID" 2>/dev/null || true
    SERVER_PID=
  fi
}

cleanup() {
  stop_server
  rm -rf "$LOG_DIR"
}
trap cleanup EXIT

wait_for_listener() {
  local pid=$1
  local port=$2
  local name=$3
  for _ in $(seq 1 100); do
    if kill -0 "$pid" 2>/dev/null && \
       lsof -nP -iTCP:"$port" -sTCP:LISTEN >/dev/null 2>&1; then
      return 0
    fi
    if ! kill -0 "$pid" 2>/dev/null; then
      break
    fi
    sleep 0.05
  done
  echo "$name failed to listen on 127.0.0.1:$port" >&2
  return 1
}

# Keep the listener alive across readiness checks; close after one NeverC
# handshake by killing the process in cleanup.
openssl s_server -accept "$ADDR" -tls1_3 -cert "$CERT" -key "$KEY" \
  -ciphersuites TLS_AES_128_GCM_SHA256 -www -no_ticket \
  >"$LOG_DIR/openssl-server.log" 2>&1 &
SERVER_PID=$!
if ! wait_for_listener "$SERVER_PID" "$PORT" "openssl s_server"; then
  while IFS= read -r line; do
    echo "$line" >&2
  done <"$LOG_DIR/openssl-server.log"
  exit 1
fi

"$PEER" client "$ADDR" "$CERT"
echo "openssl interop client: ok"
stop_server

"$PEER" server "$REVERSE_ADDR" "$CERT" "$KEY" \
  >"$LOG_DIR/neverc-server.log" 2>&1 &
SERVER_PID=$!
if ! wait_for_listener "$SERVER_PID" "$REVERSE_PORT" "NeverC TLS server"; then
  while IFS= read -r line; do
    echo "$line" >&2
  done <"$LOG_DIR/neverc-server.log"
  exit 1
fi

if ! response=$(
  printf 'ping' |
    openssl s_client -connect "$REVERSE_ADDR" -tls1_3 \
      -ciphersuites TLS_AES_128_GCM_SHA256 \
      -CAfile "$CERT" -verify_hostname localhost \
      -servername localhost -quiet 2>"$LOG_DIR/openssl-client.log"
); then
  echo "OpenSSL client failed to connect to NeverC TLS server" >&2
  while IFS= read -r line; do
    echo "$line" >&2
  done <"$LOG_DIR/openssl-client.log"
  exit 1
fi
if [[ "$response" != *pong* ]]; then
  echo "OpenSSL client did not receive NeverC server response" >&2
  while IFS= read -r line; do
    echo "$line" >&2
  done <"$LOG_DIR/openssl-client.log"
  exit 1
fi
if ! wait "$SERVER_PID"; then
  SERVER_PID=
  while IFS= read -r line; do
    echo "$line" >&2
  done <"$LOG_DIR/neverc-server.log"
  exit 1
fi
SERVER_PID=
echo "openssl interop server: ok"
