#!/usr/bin/env bash
# NeverC TLS client ↔ OpenSSL s_server interop gate.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
BUILD="${NEVERC_BUILD_DIR:-$ROOT/build-neverc}"
CLIENT="${1:-$BUILD/tls-interop-client}"
CERT="${2:-$BUILD/tls-interop-cert.pem}"
KEY="${3:-$BUILD/tls-interop-key.pem}"
PORT="${NEVERC_TLS_INTEROP_PORT:-18443}"
ADDR="127.0.0.1:${PORT}"

if ! command -v openssl >/dev/null 2>&1; then
  echo "skip: openssl not available"
  exit 0
fi
if [[ ! -x "$CLIENT" ]]; then
  echo "missing client binary: $CLIENT" >&2
  exit 1
fi
mkdir -p "$BUILD"
if [[ ! -f "$CERT" || ! -f "$KEY" ]]; then
  openssl req -x509 -newkey ec -pkeyopt ec_paramgen_curve:P-256 \
    -keyout "$KEY" -out "$CERT" -days 2 -nodes \
    -subj "/CN=localhost" \
    -addext "subjectAltName=DNS:localhost" >/dev/null 2>&1
fi

cleanup() {
  if [[ -n "${SERVER_PID:-}" ]]; then
    kill "$SERVER_PID" 2>/dev/null || true
    wait "$SERVER_PID" 2>/dev/null || true
  fi
}
trap cleanup EXIT

# Keep the listener alive across readiness checks; close after one NeverC
# handshake by killing the process in cleanup.
openssl s_server -accept "$ADDR" -tls1_3 -cert "$CERT" -key "$KEY" \
  -www -no_ticket >/tmp/neverc-tls-ossl-server.log 2>&1 &
SERVER_PID=$!

ready=0
for _ in $(seq 1 100); do
  if kill -0 "$SERVER_PID" 2>/dev/null && \
     lsof -nP -iTCP:"$PORT" -sTCP:LISTEN >/dev/null 2>&1; then
    ready=1
    break
  fi
  if ! kill -0 "$SERVER_PID" 2>/dev/null; then
    break
  fi
  sleep 0.05
done
if [[ "$ready" != 1 ]]; then
  echo "openssl s_server failed to listen on $ADDR" >&2
  cat /tmp/neverc-tls-ossl-server.log >&2 || true
  exit 1
fi

"$CLIENT" client "$ADDR" "$CERT"
echo "openssl interop client: ok"
