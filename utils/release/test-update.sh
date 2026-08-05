#!/bin/sh

set -eu

fail() {
  printf 'test-update: %s\n' "$*" >&2
  exit 1
}

if [ "$#" -lt 1 ] || [ "$#" -gt 2 ]; then
  fail "usage: $0 /path/to/neverc [explicit|latest|failure|same|all]"
fi

neverc_input=$1
mode=${2:-all}
case "$mode" in
  explicit|latest|failure|same|all) ;;
  *) fail "unknown mode '$mode'" ;;
esac

case "$neverc_input" in
  /*) neverc_binary=$neverc_input ;;
  *) neverc_binary=$(cd "$(dirname "$neverc_input")" && pwd)/$(basename "$neverc_input") ;;
esac
[ -x "$neverc_binary" ] || fail "NeverC binary is not executable: $neverc_binary"

for tool in cp grep mktemp unzip zip; do
  command -v "$tool" >/dev/null 2>&1 || fail "required tool '$tool' not found"
done

case "$(uname -s):$(uname -m)" in
  Darwin:arm64) compiler_asset=neverc-macos-arm64.zip ;;
  Linux:x86_64) compiler_asset=neverc-linux-x64.zip ;;
  Linux:aarch64|Linux:arm64) compiler_asset=neverc-linux-arm64.zip ;;
  *) fail "unsupported integration-test host: $(uname -s) $(uname -m)" ;;
esac

if command -v sha256sum >/dev/null 2>&1; then
  sha256_file() {
    sha256sum "$1" | awk '{print $1}'
  }
else
  command -v shasum >/dev/null 2>&1 || fail "sha256sum or shasum is required"
  sha256_file() {
    shasum -a 256 "$1" | awk '{print $1}'
  }
fi

fixture_root=$(mktemp -d "${TMPDIR:-/tmp}/neverc-update-e2e.XXXXXX")
cleanup() {
  rm -rf -- "$fixture_root"
}
trap cleanup EXIT HUP INT TERM

current_body=$("$neverc_binary" -dumpversion | tr -d '\r\n')
case "$current_body" in
  *[!0-9.]*|*.*.*.*|.*|*.) fail "unexpected compiler version '$current_body'" ;;
esac
current_tag=v$current_body

write_fake_curl() {
  destination=$1
  cat > "$destination" <<'EOF'
#!/bin/sh
set -eu
output=
url=
while [ "$#" -gt 0 ]; do
  case "$1" in
    -o) output=$2; shift 2 ;;
    -H) shift 2 ;;
    -fSL|--progress-bar|--silent) shift ;;
    *) url=$1; shift ;;
  esac
done
[ -n "$output" ] && [ -n "$url" ]
printf "%s\n" "$url" >> "$FAKE_CURL_LOG"
case "$url" in
  https://api.github.com/*) source_file=$FAKE_RELEASE_DIR/releases.json ;;
  https://github.com/*) source_file=$FAKE_RELEASE_DIR/${url##*/} ;;
  *) exit 22 ;;
esac
cp "$source_file" "$output"
EOF
  chmod +x "$destination"
}

write_fake_compiler() {
  destination=$1
  version_body=$2
  cat > "$destination" <<EOF
#!/bin/sh
if [ "\${1-}" = "-dumpversion" ]; then
  printf '%s\\n' '$version_body'
  exit 0
fi
printf "fake NeverC compiler fixture\\n"
EOF
  chmod +x "$destination"
}

make_runtime_archive() {
  work=$1
  release_dir=$2
  target_name=$3
  runtime_path=$4
  target_tag=$5

  tree=$work/runtime-package-$target_name
  mkdir -p "$tree/$runtime_path"
  printf 'runtime:%s:%s\n' "$target_tag" "$target_name" > "$tree/$runtime_path/marker"
  (
    cd "$tree"
    zip -q -r "$release_dir/neverc-runtime-$target_name.zip" .
  )
}

make_release() {
  work=$1
  target_tag=$2
  corrupt=${3:-no}
  release_dir=$work/release
  package=$work/compiler-package
  target_body=${target_tag#v}

  mkdir -p "$release_dir" "$package/bin" "$package/lib" \
    "$package/pluginsdk/include" "$package/runtime/embedded-from-compiler"
  write_fake_compiler "$package/bin/neverc" "$target_body"
  printf 'compiler-library:%s\n' "$target_tag" > "$package/lib/release.txt"
  printf 'plugin-sdk:%s\n' "$target_tag" > "$package/pluginsdk/include/release.txt"
  printf 'must-not-be-installed\n' \
    > "$package/runtime/embedded-from-compiler/marker"
  (
    cd "$package"
    zip -q -r "$release_dir/$compiler_asset" bin lib pluginsdk runtime
  )

  make_runtime_archive "$work" "$release_dir" linux-x64 linux/x64 "$target_tag"
  make_runtime_archive "$work" "$release_dir" android-arm64 android/arm64 "$target_tag"

  : > "$release_dir/SHA256SUMS"
  for asset in "$compiler_asset" \
    neverc-runtime-linux-x64.zip neverc-runtime-android-arm64.zip; do
    hash=$(sha256_file "$release_dir/$asset")
    printf '%s  %s\n' "$hash" "$asset" >> "$release_dir/SHA256SUMS"
  done

  if [ "$corrupt" = yes ]; then
    printf 'corrupt-after-checksum\n' \
      >> "$release_dir/neverc-runtime-android-arm64.zip"
  fi

  printf '%s\n' \
    "[{\"tag_name\":\"v999999.1.0\",\"draft\":true,\"prerelease\":false,\"assets\":[{\"name\":\"$compiler_asset\"}]},{\"tag_name\":\"v999999.0.1\",\"draft\":false,\"prerelease\":true,\"assets\":[{\"name\":\"$compiler_asset\"}]},{\"tag_name\":\"$target_tag\",\"draft\":false,\"prerelease\":false,\"assets\":[{\"name\":\"$compiler_asset\"}]}]" \
    > "$release_dir/releases.json"
}

seed_installation() {
  work=$1
  linux_manifest_tag=$2
  prefix=$work/prefix

  mkdir -p "$prefix/bin" "$prefix/lib" "$prefix/pluginsdk/include" \
    "$prefix/runtime/linux/x64" "$prefix/runtime/android/arm64" \
    "$prefix/runtime/custom"
  cp "$neverc_binary" "$prefix/bin/neverc"
  chmod +x "$prefix/bin/neverc"
  printf 'custom-tool\n' > "$prefix/bin/custom-tool"
  printf 'old-library\n' > "$prefix/lib/release.txt"
  printf 'old-sdk\n' > "$prefix/pluginsdk/include/release.txt"
  printf 'old-linux\n' > "$prefix/runtime/linux/x64/marker"
  printf 'old-android\n' > "$prefix/runtime/android/arm64/marker"
  printf 'preserve-me\n' > "$prefix/runtime/custom/keep"
  printf '%s\n' \
    "{\"schema\":1,\"targets\":{\"linux-x64\":{\"release_tag\":\"$linux_manifest_tag\"}}}" \
    > "$prefix/runtime/manifest.json"
}

assert_common_success() {
  work=$1
  target_tag=$2
  prefix=$work/prefix

  actual_body=$("$prefix/bin/neverc" -dumpversion | tr -d '\r\n')
  [ "v$actual_body" = "$target_tag" ] || \
    fail "compiler did not move to $target_tag"
  grep -q "runtime:$target_tag:linux-x64" \
    "$prefix/runtime/linux/x64/marker" || fail "linux runtime did not synchronize"
  grep -q "runtime:$target_tag:android-arm64" \
    "$prefix/runtime/android/arm64/marker" || fail "android runtime did not synchronize"
  [ "$(grep -o "$target_tag" "$prefix/runtime/manifest.json" | wc -l | tr -d ' ')" = 2 ] || \
    fail "manifest does not record both runtimes at $target_tag"
  grep -q custom-tool "$prefix/bin/custom-tool" || fail "custom compiler sibling changed"
  grep -q preserve-me "$prefix/runtime/custom/keep" || fail "custom runtime sibling changed"
  [ ! -e "$prefix/runtime/embedded-from-compiler" ] || \
    fail "runtime embedded in compiler archive was installed"
  [ ! -e "$prefix/.neverc-update.lock" ] || fail "update lock was not removed"
}

run_explicit() {
  work=$fixture_root/explicit
  target_tag=v1.2.3
  mkdir -p "$work/fake-bin"
  seed_installation "$work" v0.0.1
  make_release "$work" "$target_tag"
  write_fake_curl "$work/fake-bin/curl"
  : > "$work/curl.log"

  FAKE_RELEASE_DIR=$work/release
  FAKE_CURL_LOG=$work/curl.log
  PATH=$work/fake-bin:$PATH
  export FAKE_RELEASE_DIR FAKE_CURL_LOG PATH
  "$work/prefix/bin/neverc" upgrade --help > "$work/help-output" 2>&1 || \
    fail "upgrade alias did not route to update help"
  if "$work/prefix/bin/neverc" update --version= > "$work/empty-version-output" 2>&1; then
    fail "empty explicit version unexpectedly selected latest"
  fi
  if "$work/prefix/bin/neverc" update 1.2.3 v1.2.3 > "$work/duplicate-version-output" 2>&1; then
    fail "duplicate update versions were accepted"
  fi
  "$work/prefix/bin/neverc" update 1.2.3 > "$work/output" 2>&1 || \
    fail "explicit downgrade failed; see $work/output"
  assert_common_success "$work" "$target_tag"
  if grep -q api.github.com "$work/curl.log"; then
    fail "explicit version unexpectedly queried latest releases"
  fi
}

run_latest() {
  work=$fixture_root/latest
  target_tag=v999999.0.0
  mkdir -p "$work/fake-bin"
  seed_installation "$work" v0.0.1
  make_release "$work" "$target_tag"
  write_fake_curl "$work/fake-bin/curl"
  : > "$work/curl.log"

  FAKE_RELEASE_DIR=$work/release
  FAKE_CURL_LOG=$work/curl.log
  PATH=$work/fake-bin:$PATH
  export FAKE_RELEASE_DIR FAKE_CURL_LOG PATH
  "$work/prefix/bin/neverc" update > "$work/output" 2>&1 || \
    fail "latest update failed; see $work/output"
  assert_common_success "$work" "$target_tag"
  grep -q api.github.com "$work/curl.log" || fail "latest release API was not queried"
  if grep -q 'latest' "$work/prefix/runtime/manifest.json"; then
    fail "manifest persisted the symbolic latest version"
  fi
}

run_failure() {
  work=$fixture_root/failure
  target_tag=v1.2.3
  mkdir -p "$work/fake-bin"
  seed_installation "$work" v0.0.1
  make_release "$work" "$target_tag" yes
  write_fake_curl "$work/fake-bin/curl"
  : > "$work/curl.log"
  prefix=$work/prefix
  compiler_before=$(sha256_file "$prefix/bin/neverc")
  linux_before=$(sha256_file "$prefix/runtime/linux/x64/marker")
  android_before=$(sha256_file "$prefix/runtime/android/arm64/marker")
  manifest_before=$(sha256_file "$prefix/runtime/manifest.json")

  FAKE_RELEASE_DIR=$work/release
  FAKE_CURL_LOG=$work/curl.log
  PATH=$work/fake-bin:$PATH
  export FAKE_RELEASE_DIR FAKE_CURL_LOG PATH
  if "$prefix/bin/neverc" update "$target_tag" > "$work/output" 2>&1; then
    fail "corrupt runtime archive unexpectedly succeeded"
  fi
  [ "$(sha256_file "$prefix/bin/neverc")" = "$compiler_before" ] || fail "compiler changed on checksum failure"
  [ "$(sha256_file "$prefix/runtime/linux/x64/marker")" = "$linux_before" ] || fail "linux runtime changed on checksum failure"
  [ "$(sha256_file "$prefix/runtime/android/arm64/marker")" = "$android_before" ] || fail "android runtime changed on checksum failure"
  [ "$(sha256_file "$prefix/runtime/manifest.json")" = "$manifest_before" ] || fail "manifest changed on checksum failure"
  [ ! -e "$prefix/.neverc-update.lock" ] || fail "failure left the update lock behind"
}

run_same() {
  work=$fixture_root/same
  mkdir -p "$work/fake-bin"
  seed_installation "$work" "$current_tag"
  make_release "$work" "$current_tag"
  write_fake_curl "$work/fake-bin/curl"
  : > "$work/curl.log"
  prefix=$work/prefix
  compiler_before=$(sha256_file "$prefix/bin/neverc")
  linux_before=$(sha256_file "$prefix/runtime/linux/x64/marker")

  FAKE_RELEASE_DIR=$work/release
  FAKE_CURL_LOG=$work/curl.log
  PATH=$work/fake-bin:$PATH
  export FAKE_RELEASE_DIR FAKE_CURL_LOG PATH
  "$prefix/bin/neverc" update "$current_tag" > "$work/output-first" 2>&1 || \
    fail "same-version runtime repair failed; see $work/output-first"
  [ "$(sha256_file "$prefix/bin/neverc")" = "$compiler_before" ] || fail "same-version repair replaced compiler"
  [ "$(sha256_file "$prefix/runtime/linux/x64/marker")" = "$linux_before" ] || fail "matching runtime was unnecessarily replaced"
  grep -q "runtime:$current_tag:android-arm64" \
    "$prefix/runtime/android/arm64/marker" || fail "unknown runtime was not repaired"
  if grep -q "$compiler_asset\|neverc-runtime-linux-x64.zip" "$work/curl.log"; then
    fail "same-version repair downloaded an unneeded archive"
  fi
  grep -q neverc-runtime-android-arm64.zip "$work/curl.log" || \
    fail "same-version repair did not request the mismatched runtime"
  [ "$(grep -o "$current_tag" "$prefix/runtime/manifest.json" | wc -l | tr -d ' ')" = 2 ] || \
    fail "repair manifest did not reconcile all installed runtimes"

  requests_before=$(wc -l < "$work/curl.log" | tr -d ' ')
  android_before=$(sha256_file "$prefix/runtime/android/arm64/marker")
  "$prefix/bin/neverc" update "$current_tag" > "$work/output-second" 2>&1 || \
    fail "same-version no-op failed; see $work/output-second"
  requests_after=$(wc -l < "$work/curl.log" | tr -d ' ')
  [ "$requests_after" = "$requests_before" ] || fail "no-op update performed network requests"
  [ "$(sha256_file "$prefix/runtime/android/arm64/marker")" = "$android_before" ] || fail "no-op update changed runtime content"
  grep -q 'already at' "$work/output-second" || fail "no-op status was not reported"
}

case "$mode" in
  explicit) run_explicit ;;
  latest) run_latest ;;
  failure) run_failure ;;
  same) run_same ;;
  all)
    run_explicit
    run_latest
    run_failure
    run_same
    ;;
esac

printf 'test-update: %s passed\n' "$mode"
