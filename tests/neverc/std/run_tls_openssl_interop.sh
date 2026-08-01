#!/usr/bin/env bash
# NeverC TLS client/server ↔ OpenSSL interop gate.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
BUILD="${NEVERC_BUILD_DIR:-$ROOT/build-neverc}"
PEER="${1:-$BUILD/tls-interop-peer}"
CERT="${2:-$BUILD/tls-interop-cert.pem}"
KEY="${3:-$BUILD/tls-interop-key.pem}"
CLIENT_CERT="${CERT%.pem}-client.pem"
CLIENT_KEY="${KEY%.pem}-client.pem"
PORT="${NEVERC_TLS_INTEROP_PORT:-18443}"
ADDR="127.0.0.1:${PORT}"
REVERSE_PORT=$((PORT + 1))
REVERSE_ADDR="127.0.0.1:${REVERSE_PORT}"
CLIENT_RESUME_PORT=$((PORT + 2))
CLIENT_RESUME_ADDR="127.0.0.1:${CLIENT_RESUME_PORT}"
SERVER_RESUME_PORT=$((PORT + 3))
SERVER_RESUME_ADDR="127.0.0.1:${SERVER_RESUME_PORT}"

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
if [[ ! -f "$CLIENT_CERT" || ! -f "$CLIENT_KEY" ]]; then
  openssl req -x509 -newkey ec -pkeyopt ec_paramgen_curve:P-256 \
    -keyout "$CLIENT_KEY" -out "$CLIENT_CERT" -days 2 -nodes \
    -subj "/CN=neverc-interop-client" \
    -addext "basicConstraints=critical,CA:FALSE" \
    -addext "keyUsage=critical,digitalSignature" \
    -addext "extendedKeyUsage=clientAuth" >/dev/null 2>&1
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
       lsof -a -p "$pid" -nP -iTCP:"$port" \
         -sTCP:LISTEN >/dev/null 2>&1; then
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
  -ciphersuites TLS_AES_128_GCM_SHA256 \
  -Verify 1 -verify_return_error -CAfile "$CLIENT_CERT" -www \
  >"$LOG_DIR/openssl-server.log" 2>&1 &
SERVER_PID=$!
if ! wait_for_listener "$SERVER_PID" "$PORT" "openssl s_server"; then
  while IFS= read -r line; do
    echo "$line" >&2
  done <"$LOG_DIR/openssl-server.log"
  exit 1
fi

if "$PEER" client "$ADDR" "$CERT" - - \
    >"$LOG_DIR/neverc-client-without-cert.log" 2>&1; then
  echo "OpenSSL server accepted a NeverC client without a certificate" >&2
  exit 1
fi
echo "openssl interop missing client certificate: rejected"

"$PEER" client "$ADDR" "$CERT" "$CLIENT_CERT" "$CLIENT_KEY"
echo "openssl interop client: ok"
stop_server

"$PEER" server "$REVERSE_ADDR" "$CERT" "$KEY" "$CLIENT_CERT" \
  >"$LOG_DIR/neverc-server.log" 2>&1 &
SERVER_PID=$!
if ! wait_for_listener "$SERVER_PID" "$REVERSE_PORT" "NeverC TLS server"; then
  while IFS= read -r line; do
    echo "$line" >&2
  done <"$LOG_DIR/neverc-server.log"
  exit 1
fi

if printf 'ping' |
    openssl s_client -connect "$REVERSE_ADDR" -tls1_3 \
      -ciphersuites TLS_AES_128_GCM_SHA256 \
      -CAfile "$CERT" -verify_hostname localhost \
      -servername localhost -quiet \
      >"$LOG_DIR/openssl-client-without-cert.log" 2>&1; then
  :
fi
if wait "$SERVER_PID"; then
  SERVER_PID=
  echo "NeverC server accepted an OpenSSL client without a certificate" >&2
  exit 1
fi
SERVER_PID=
echo "openssl interop server missing client certificate: rejected"

"$PEER" server "$REVERSE_ADDR" "$CERT" "$KEY" "$CLIENT_CERT" \
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
      -servername localhost -cert "$CLIENT_CERT" -key "$CLIENT_KEY" \
      -quiet 2>"$LOG_DIR/openssl-client.log"
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

openssl s_server -accept "$CLIENT_RESUME_ADDR" -tls1_3 \
  -cert "$CERT" -key "$KEY" \
  -ciphersuites TLS_AES_128_GCM_SHA256 -www \
  >"$LOG_DIR/openssl-resumption-server.log" 2>&1 &
SERVER_PID=$!
if ! wait_for_listener \
    "$SERVER_PID" "$CLIENT_RESUME_PORT" "OpenSSL resumption server"; then
  while IFS= read -r line; do
    echo "$line" >&2
  done <"$LOG_DIR/openssl-resumption-server.log"
  exit 1
fi
if ! "$PEER" client-resume "$CLIENT_RESUME_ADDR" "$CERT" \
    >"$LOG_DIR/neverc-resumption-client.log" 2>&1; then
  while IFS= read -r line; do
    echo "$line" >&2
  done <"$LOG_DIR/neverc-resumption-client.log"
  exit 1
fi
echo "openssl interop client resumption: ok"
stop_server

"$PEER" server-resume "$SERVER_RESUME_ADDR" "$CERT" "$KEY" \
  >"$LOG_DIR/neverc-resumption-server.log" 2>&1 &
SERVER_PID=$!
if ! wait_for_listener \
    "$SERVER_PID" "$SERVER_RESUME_PORT" "NeverC resumption server"; then
  while IFS= read -r line; do
    echo "$line" >&2
  done <"$LOG_DIR/neverc-resumption-server.log"
  exit 1
fi

SESSION="$LOG_DIR/openssl-session.pem"
if ! first_response=$(
  printf 'ping' |
    openssl s_client -connect "$SERVER_RESUME_ADDR" -tls1_3 \
      -ciphersuites TLS_AES_128_GCM_SHA256 \
      -CAfile "$CERT" -verify_hostname localhost \
      -servername localhost -sess_out "$SESSION" -quiet \
      2>"$LOG_DIR/openssl-resumption-client-first.log"
); then
  echo "OpenSSL initial session failed against NeverC server" >&2
  exit 1
fi
if [[ "$first_response" != *pong* || ! -s "$SESSION" ]]; then
  echo "OpenSSL did not save the NeverC session ticket" >&2
  exit 1
fi
if ! second_response=$(
  printf 'ping' |
    openssl s_client -connect "$SERVER_RESUME_ADDR" -tls1_3 \
      -ciphersuites TLS_AES_128_GCM_SHA256 \
      -CAfile "$CERT" -verify_hostname localhost \
      -servername localhost -sess_in "$SESSION" -quiet \
      2>"$LOG_DIR/openssl-resumption-client-second.log"
); then
  echo "OpenSSL session resumption failed against NeverC server" >&2
  exit 1
fi
if [[ "$second_response" != *pong* ]]; then
  echo "OpenSSL resumed client did not receive NeverC response" >&2
  exit 1
fi
if ! wait "$SERVER_PID"; then
  SERVER_PID=
  while IFS= read -r line; do
    echo "$line" >&2
  done <"$LOG_DIR/neverc-resumption-server.log"
  exit 1
fi
SERVER_PID=
echo "openssl interop server resumption: ok"
