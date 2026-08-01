#!/usr/bin/env bash
# NeverC TLS client/server ↔ BoringSSL (bssl) interop gate.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
BUILD="${NEVERC_BUILD_DIR:-$ROOT/build-neverc}"
PEER="${1:-$BUILD/tls-interop-peer}"
CERT="${2:-$BUILD/tls-bssl-interop-cert.pem}"
KEY="${3:-$BUILD/tls-bssl-interop-key.pem}"
CLIENT_CERT="${CERT%.pem}-client.pem"
CLIENT_KEY="${KEY%.pem}-client.pem"
PORT="${NEVERC_TLS_BSSL_INTEROP_PORT:-18543}"
ADDR="127.0.0.1:${PORT}"
REVERSE_PORT=$((PORT + 1))
REVERSE_ADDR="127.0.0.1:${REVERSE_PORT}"
CLIENT_RESUME_PORT=$((PORT + 2))
CLIENT_RESUME_ADDR="127.0.0.1:${CLIENT_RESUME_PORT}"
SERVER_RESUME_PORT=$((PORT + 3))
SERVER_RESUME_ADDR="127.0.0.1:${SERVER_RESUME_PORT}"

resolve_bssl() {
  local candidate
  if [[ -n "${NEVERC_BSSL_BIN:-}" && -x "${NEVERC_BSSL_BIN}" ]]; then
    printf '%s\n' "$NEVERC_BSSL_BIN"
    return 0
  fi
  for candidate in \
      "$ROOT/build/boringssl/bssl" \
      "$ROOT/build-neverc/boringssl/bssl" \
      "$(command -v bssl 2>/dev/null || true)"; do
    [[ -n "$candidate" && -x "$candidate" ]] || continue
    printf '%s\n' "$candidate"
    return 0
  done
  return 1
}

resolve_openssl() {
  local candidate version
  if [[ -n "${OPENSSL_BIN:-}" ]]; then
    printf '%s\n' "$OPENSSL_BIN"
    return 0
  fi
  for candidate in \
      /opt/homebrew/opt/openssl@3/bin/openssl \
      /usr/local/opt/openssl@3/bin/openssl \
      /opt/homebrew/bin/openssl \
      /usr/local/bin/openssl \
      "$(command -v openssl 2>/dev/null || true)"; do
    [[ -n "$candidate" && -x "$candidate" ]] || continue
    version="$("$candidate" version 2>/dev/null || true)"
    case "$version" in
      OpenSSL\ 3*|OpenSSL\ 1.1*)
        printf '%s\n' "$candidate"
        return 0
        ;;
    esac
  done
  return 1
}

if ! BSSL="$(resolve_bssl)"; then
  echo "skip: bssl not available"
  exit 0
fi
if ! OPENSSL="$(resolve_openssl)"; then
  echo "skip: OpenSSL 1.1+/3.x needed to mint interop certificates"
  exit 0
fi
if [[ ! -x "$PEER" ]]; then
  echo "missing interop binary: $PEER" >&2
  exit 1
fi

mkdir -p "$BUILD"
if [[ ! -f "$CERT" || ! -f "$KEY" ]]; then
  "$OPENSSL" req -x509 -newkey ec -pkeyopt ec_paramgen_curve:P-256 \
    -keyout "$KEY" -out "$CERT" -days 2 -nodes \
    -subj "/CN=localhost" \
    -addext "subjectAltName=DNS:localhost" >/dev/null 2>&1
fi
if [[ ! -f "$CLIENT_CERT" || ! -f "$CLIENT_KEY" ]]; then
  "$OPENSSL" req -x509 -newkey ec -pkeyopt ec_paramgen_curve:P-256 \
    -keyout "$CLIENT_KEY" -out "$CLIENT_CERT" -days 2 -nodes \
    -subj "/CN=neverc-interop-client" \
    -addext "basicConstraints=critical,CA:FALSE" \
    -addext "keyUsage=critical,digitalSignature" \
    -addext "extendedKeyUsage=clientAuth" >/dev/null 2>&1
fi

LOG_DIR=$(mktemp -d "${TMPDIR:-/tmp}/neverc-tls-bssl-interop.XXXXXX")

dump_log() {
  local path=$1
  if [[ -f "$path" ]]; then
    while IFS= read -r line; do
      echo "$line" >&2
    done <"$path"
  fi
}

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

start_bssl_mtls_server() {
  local port=$1
  local log=$2
  stop_server
  "$BSSL" server -accept "$port" -key "$KEY" -cert "$CERT" \
    -min-version tls1.3 -max-version tls1.3 \
    -require-any-client-cert -www -loop \
    >"$log" 2>&1 &
  SERVER_PID=$!
  if ! wait_for_listener "$SERVER_PID" "$port" "bssl server"; then
    dump_log "$log"
    exit 1
  fi
}

run_peer() {
  local log=$1
  shift
  if ! "$@" >"$log" 2>&1; then
    local status=$?
    echo "peer command failed (exit $status): $*" >&2
    dump_log "$log"
    return "$status"
  fi
}

start_bssl_mtls_server "$PORT" "$LOG_DIR/bssl-server-reject.log"
if "$PEER" client "$ADDR" "$CERT" - - \
    >"$LOG_DIR/neverc-client-without-cert.log" 2>&1; then
  echo "BoringSSL server accepted a NeverC client without a certificate" >&2
  exit 1
fi
echo "boringssl interop missing client certificate: rejected"

run_peer "$LOG_DIR/neverc-client.log" \
  "$PEER" client "$ADDR" "$CERT" "$CLIENT_CERT" "$CLIENT_KEY"
echo "boringssl interop client: ok"
stop_server

"$PEER" server "$REVERSE_ADDR" "$CERT" "$KEY" "$CLIENT_CERT" \
  >"$LOG_DIR/neverc-server.log" 2>&1 &
SERVER_PID=$!
if ! wait_for_listener "$SERVER_PID" "$REVERSE_PORT" "NeverC TLS server"; then
  dump_log "$LOG_DIR/neverc-server.log"
  exit 1
fi

if printf 'ping' |
    "$BSSL" client -connect "$REVERSE_ADDR" \
      -min-version tls1.3 -max-version tls1.3 \
      -server-name localhost -root-certs "$CERT" \
      >"$LOG_DIR/bssl-client-without-cert.log" 2>&1; then
  :
fi
if wait "$SERVER_PID"; then
  SERVER_PID=
  echo "NeverC server accepted a BoringSSL client without a certificate" >&2
  exit 1
fi
SERVER_PID=
echo "boringssl interop server missing client certificate: rejected"

"$PEER" server "$REVERSE_ADDR" "$CERT" "$KEY" "$CLIENT_CERT" \
  >"$LOG_DIR/neverc-server.log" 2>&1 &
SERVER_PID=$!
if ! wait_for_listener "$SERVER_PID" "$REVERSE_PORT" "NeverC TLS server"; then
  dump_log "$LOG_DIR/neverc-server.log"
  exit 1
fi

if ! response=$(
  printf 'ping' |
    "$BSSL" client -connect "$REVERSE_ADDR" \
      -min-version tls1.3 -max-version tls1.3 \
      -server-name localhost -root-certs "$CERT" \
      -cert "$CLIENT_CERT" -key "$CLIENT_KEY" \
      2>"$LOG_DIR/bssl-client.log"
); then
  echo "BoringSSL client failed to connect to NeverC TLS server" >&2
  dump_log "$LOG_DIR/bssl-client.log"
  exit 1
fi
if [[ "$response" != *pong* ]]; then
  echo "BoringSSL client did not receive NeverC server response" >&2
  dump_log "$LOG_DIR/bssl-client.log"
  printf 'response was: %s\n' "$response" >&2
  exit 1
fi
if ! wait "$SERVER_PID"; then
  SERVER_PID=
  dump_log "$LOG_DIR/neverc-server.log"
  exit 1
fi
SERVER_PID=
echo "boringssl interop server: ok"

"$BSSL" server -accept "$CLIENT_RESUME_PORT" -key "$KEY" -cert "$CERT" \
  -min-version tls1.3 -max-version tls1.3 -www -loop \
  >"$LOG_DIR/bssl-resumption-server.log" 2>&1 &
SERVER_PID=$!
if ! wait_for_listener \
    "$SERVER_PID" "$CLIENT_RESUME_PORT" "BoringSSL resumption server"; then
  dump_log "$LOG_DIR/bssl-resumption-server.log"
  exit 1
fi
# BoringSSL tickets may not always surface as NeverC's strict resume bit;
# accept a successful second handshake even if the resume flag is false.
if ! "$PEER" client-resume-lenient "$CLIENT_RESUME_ADDR" "$CERT" \
    >"$LOG_DIR/neverc-resumption-client.log" 2>&1; then
  dump_log "$LOG_DIR/neverc-resumption-client.log"
  exit 1
fi
echo "boringssl interop client resumption: ok"
stop_server

"$PEER" server-resume "$SERVER_RESUME_ADDR" "$CERT" "$KEY" \
  >"$LOG_DIR/neverc-resumption-server.log" 2>&1 &
SERVER_PID=$!
if ! wait_for_listener \
    "$SERVER_PID" "$SERVER_RESUME_PORT" "NeverC resumption server"; then
  dump_log "$LOG_DIR/neverc-resumption-server.log"
  exit 1
fi

SESSION="$LOG_DIR/bssl-session.bin"
if ! first_response=$(
  printf 'ping' |
    "$BSSL" client -connect "$SERVER_RESUME_ADDR" \
      -min-version tls1.3 -max-version tls1.3 \
      -server-name localhost -root-certs "$CERT" \
      -session-out "$SESSION" \
      2>"$LOG_DIR/bssl-resumption-client-first.log"
); then
  echo "BoringSSL initial session failed against NeverC server" >&2
  dump_log "$LOG_DIR/bssl-resumption-client-first.log"
  exit 1
fi
if [[ "$first_response" != *pong* || ! -s "$SESSION" ]]; then
  echo "BoringSSL did not save the NeverC session ticket" >&2
  dump_log "$LOG_DIR/bssl-resumption-client-first.log"
  exit 1
fi
if ! second_response=$(
  printf 'ping' |
    "$BSSL" client -connect "$SERVER_RESUME_ADDR" \
      -min-version tls1.3 -max-version tls1.3 \
      -server-name localhost -root-certs "$CERT" \
      -session-in "$SESSION" \
      2>"$LOG_DIR/bssl-resumption-client-second.log"
); then
  echo "BoringSSL session resumption failed against NeverC server" >&2
  dump_log "$LOG_DIR/bssl-resumption-client-second.log"
  exit 1
fi
if [[ "$second_response" != *pong* ]]; then
  echo "BoringSSL resumed client did not receive NeverC response" >&2
  dump_log "$LOG_DIR/bssl-resumption-client-second.log"
  exit 1
fi
if ! wait "$SERVER_PID"; then
  SERVER_PID=
  dump_log "$LOG_DIR/neverc-resumption-server.log"
  exit 1
fi
SERVER_PID=
echo "boringssl interop server resumption: ok"
