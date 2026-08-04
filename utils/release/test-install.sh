#!/bin/sh
set -eu

repo_root=$(CDPATH='' cd -- "$(dirname "$0")/../.." && pwd)
test_name=${1:-all}
test_root=$(mktemp -d "${TMPDIR:-/tmp}/neverc-install-test.XXXXXX")

cleanup() {
  rm -rf "$test_root"
}
trap cleanup EXIT HUP INT TERM

fail() {
  printf 'FAIL: %s\n' "$*" >&2
  exit 1
}

write_fake_commands() {
  mkdir -p "$test_root/bin" "$test_root/home"

  cat > "$test_root/bin/uname" <<'EOF'
#!/bin/sh
case "${1:-}" in
  -s) printf '%s\n' "${TEST_UNAME_S:-Linux}" ;;
  -m) printf '%s\n' "${TEST_UNAME_M:-x86_64}" ;;
  *) exit 1 ;;
esac
EOF

  cat > "$test_root/bin/curl" <<'EOF'
#!/bin/sh
set -eu
url=
destination=
while [ "$#" -gt 0 ]; do
  case "$1" in
    http://*|https://*) url=$1 ;;
    -o)
      shift
      destination=$1
      ;;
  esac
  shift
done

printf '%s\n' "$url" >> "$TEST_CURL_LOG"
case "$url" in
  */releases/latest)
    printf '%s\n' '{"tag_name":"gki-build-20260701"}'
    ;;
  *'/releases?per_page=100')
    cat <<'JSON'
[
  {
    "tag_name": "gki-build-20260701",
    "draft": false,
    "prerelease": false,
    "assets": [{"name": "gki-android17-6.18-build.tar.gz"}]
  },
  {
    "tag_name": "v3389.1.2",
    "draft": false,
    "prerelease": false,
    "assets": [{"name": "neverc-linux-x64.zip"}]
  }
]
JSON
    ;;
  */releases/download/*/neverc-linux-x64.zip)
    printf '%s' 'fake archive' > "$destination"
    ;;
  */releases/download/*/SHA256SUMS)
    if [ "${TEST_CHECKSUM_MODE:-valid}" = mismatch ]; then
      digest=$(printf '%064d' 0)
    else
      digest=8d57abb57a0dae3ff23c8f0df1f51951b7772822e0d560e860d6f68c24ef6d3d
    fi
    printf '%s  neverc-linux-x64.zip\n' "$digest" > "$destination"
    ;;
  *)
    printf 'unexpected curl URL: %s\n' "$url" >&2
    exit 1
    ;;
esac
EOF

  cat > "$test_root/bin/unzip" <<'EOF'
#!/bin/sh
set -eu
destination=
while [ "$#" -gt 0 ]; do
  case "$1" in
    -d)
      shift
      destination=$1
      ;;
  esac
  shift
done
mkdir -p "$destination/bin"
printf '%s\n' '#!/bin/sh' 'exit 0' > "$destination/bin/neverc"
EOF

  chmod +x "$test_root/bin/uname" "$test_root/bin/curl" "$test_root/bin/unzip"
}

run_installer() {
  TEST_CURL_LOG="$test_root/curl.log"
  export TEST_CURL_LOG
  HOME="$test_root/home" \
  PATH="$test_root/bin:$PATH" \
  NEVERC_INSTALL_DIR="$test_root/install" \
  NEVERC_NO_MODIFY_PATH=1 \
    sh "$repo_root/install.sh" > "$test_root/output" 2>&1
}

test_latest_selects_platform_asset() {
  write_fake_commands
  installer_status=0
  run_installer || installer_status=$?

  grep -F '/releases/download/v3389.1.2/neverc-linux-x64.zip' \
    "$test_root/curl.log" > /dev/null \
    || fail "latest install did not select the release containing the platform asset"

  if [ "$installer_status" -ne 0 ]; then
    sed 's/^/  installer: /' "$test_root/output" >&2
    fail "installer exited with status $installer_status"
  fi
}

test_checksum_mismatch_is_rejected() {
  write_fake_commands
  TEST_CHECKSUM_MODE=mismatch
  export TEST_CHECKSUM_MODE
  installer_status=0
  run_installer || installer_status=$?

  if [ "$installer_status" -eq 0 ]; then
    fail "installer accepted an archive without a matching published checksum"
  fi

  if [ -e "$test_root/install/bin/neverc" ]; then
    fail "installer changed the install prefix before checksum verification"
  fi
}

test_unsupported_platform_fails_before_network() {
  write_fake_commands
  TEST_UNAME_S=Darwin
  TEST_UNAME_M=x86_64
  export TEST_UNAME_S TEST_UNAME_M
  installer_status=0
  run_installer || installer_status=$?

  if [ "$installer_status" -eq 0 ]; then
    fail "installer accepted macOS x64 without a matching release asset"
  fi

  if [ -s "$test_root/curl.log" ]; then
    fail "installer accessed the network before rejecting macOS x64"
  fi

  grep -F 'unsupported platform: macos-x64' "$test_root/output" > /dev/null \
    || fail "installer did not explain the unsupported platform combination"
}

test_explicit_version_is_normalized() {
  write_fake_commands
  NEVERC_VERSION=3389.1.2
  export NEVERC_VERSION
  run_installer

  grep -F '/releases/download/v3389.1.2/neverc-linux-x64.zip' \
    "$test_root/curl.log" > /dev/null \
    || fail "bare explicit version was not normalized to its release tag"

  if grep -F '/releases?per_page=' "$test_root/curl.log" > /dev/null; then
    fail "explicit version unexpectedly queried the Releases API"
  fi
}

test_invalid_version_fails_before_network() {
  write_fake_commands
  NEVERC_VERSION='../gki-build-20260701'
  export NEVERC_VERSION
  installer_status=0
  run_installer || installer_status=$?

  if [ "$installer_status" -eq 0 ]; then
    fail "installer accepted a malformed explicit release version"
  fi

  if [ -s "$test_root/curl.log" ]; then
    fail "installer accessed the network before rejecting a malformed version"
  fi

  grep -F 'expected vMAJOR.MINOR.PATCH' "$test_root/output" > /dev/null \
    || fail "installer did not explain the required version format"
}

run_all() {
  sh "$0" latest-selects-platform-asset
  sh "$0" checksum-mismatch-is-rejected
  sh "$0" unsupported-platform-fails-before-network
  sh "$0" explicit-version-is-normalized
  sh "$0" invalid-version-fails-before-network
}

case "$test_name" in
  all)
    run_all
    exit
    ;;
  latest-selects-platform-asset) test_latest_selects_platform_asset ;;
  checksum-mismatch-is-rejected) test_checksum_mismatch_is_rejected ;;
  unsupported-platform-fails-before-network) test_unsupported_platform_fails_before_network ;;
  explicit-version-is-normalized) test_explicit_version_is_normalized ;;
  invalid-version-fails-before-network) test_invalid_version_fails_before_network ;;
  *) fail "unknown test: $test_name" ;;
esac

printf 'PASS: %s\n' "$test_name"
