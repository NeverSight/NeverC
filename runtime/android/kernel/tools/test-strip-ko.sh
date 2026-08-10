#!/usr/bin/env bash
# Verify the retired post-link strip entry point is mutation-free and actionable.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
STRIP_KO="$SCRIPT_DIR/strip-ko.sh"
TEST_ROOT="$(mktemp -d "${TMPDIR:-/tmp}/neverc-strip-ko-test.XXXXXX")"
trap 'rm -rf "$TEST_ROOT"' EXIT

fail()
{
	printf 'FAIL: %s\n' "$1" >&2
	exit 1
}

checksum()
{
	cksum "$1" | awk '{ print $1 ":" $2 }'
}

assert_migration_message()
{
	local diagnostics="$1"

	grep -Fq 'neverc make release' "$diagnostics" ||
		fail 'diagnostic does not mention neverc make release'
	grep -Fq -- '--strip' "$diagnostics" ||
		fail 'diagnostic does not mention --strip'
}

INPUT="$TEST_ROOT/input.ko"
OUTPUT="$TEST_ROOT/output.ko"
STDOUT="$TEST_ROOT/stdout"
STDERR="$TEST_ROOT/stderr"

printf '%s\n' 'NeverC strip-ko mutation sentinel' >"$INPUT"
BEFORE_CHECKSUM="$(checksum "$INPUT")"

set +e
"$STRIP_KO" "$INPUT" "$OUTPUT" >"$STDOUT" 2>"$STDERR"
STATUS=$?
set -e

[ "$STATUS" -ne 0 ] || fail 'post-link invocation unexpectedly succeeded'
[ "$(checksum "$INPUT")" = "$BEFORE_CHECKSUM" ] ||
	fail 'input checksum changed'
[ ! -e "$OUTPUT" ] || fail 'an output file was created'
[ ! -s "$STDOUT" ] || fail 'diagnostic was written to stdout instead of stderr'
assert_migration_message "$STDERR"

NO_ARG_STDOUT="$TEST_ROOT/no-arg-stdout"
NO_ARG_STDERR="$TEST_ROOT/no-arg-stderr"
set +e
"$STRIP_KO" >"$NO_ARG_STDOUT" 2>"$NO_ARG_STDERR"
NO_ARG_STATUS=$?
set -e

[ "$NO_ARG_STATUS" -ne 0 ] || fail 'no-argument invocation unexpectedly succeeded'
[ ! -s "$NO_ARG_STDOUT" ] ||
	fail 'no-argument diagnostic was written to stdout instead of stderr'
assert_migration_message "$NO_ARG_STDERR"

printf '%s\n' 'PASS: strip-ko.sh refuses post-link mutation and reports the source-build migration'
