#!/usr/bin/env bash
# Integration tests for perfparse.
# Requires: redis-server and redis-cli on PATH.

set -euo pipefail

PORT=16379
BINARY=./perfparse
PASS=0
FAIL=0

# ── helpers ──────────────────────────────────────────────────────────────────

die()  { echo "FATAL: $*" >&2; exit 1; }
pass() { echo "  PASS: $*"; PASS=$((PASS + 1)); }
fail() { echo "  FAIL: $*"; FAIL=$((FAIL + 1)); }

assert_eq() {
    local desc="$1" got="$2" want="$3"
    if [ "$got" = "$want" ]; then
        pass "$desc"
    else
        fail "$desc (got='$got', want='$want')"
    fi
}

# Locate redis-server and redis-cli — check PATH first, then known source dirs
find_redis() {
    local name="$1"
    if command -v "$name" >/dev/null 2>&1; then
        command -v "$name"
        return
    fi
    # Search known source directories; ignore errors (missing dirs, permissions)
    local found
    found=$(find /usr/local/src /opt /usr /usr/local 2>/dev/null \
        -name "$name" -type f 2>/dev/null | head -1 || true)
    echo "$found"
}

REDIS_SERVER=$(find_redis redis-server)
REDIS_CLI=$(find_redis redis-cli)

[ -n "$REDIS_SERVER" ] || die "redis-server not found"
[ -n "$REDIS_CLI"    ] || die "redis-cli not found"

redis_cli() { "$REDIS_CLI" -p "$PORT" "$@"; }
[ -x "$BINARY" ] || die "$BINARY not found — run 'make' first"

# Start a throw-away Redis instance (pipe config via stdin for Redis 2.x compat)
printf 'port %s\ndaemonize yes\nlogfile /tmp/perfparse-test-redis.log\n' \
    "$PORT" | "$REDIS_SERVER" - >/dev/null
trap 'redis_cli shutdown 2>/dev/null || true' EXIT INT TERM

sleep 0.5  # give redis-server a moment to start

# ── test 1: happy path ────────────────────────────────────────────────────────

echo "Test 1: happy path (valid record)"
redis_cli flushall >/dev/null

echo "cpu|1700000000|42" | timeout 2 "$BINARY" 127.0.0.1 "$PORT" >/dev/null 2>&1 || true

VAL=$(redis_cli lindex cpu 0)
assert_eq "value stored in 'cpu' list"       "$VAL" "42"

TS=$(redis_cli lindex cpu.time 0)
assert_eq "timestamp stored in 'cpu.time' list" "$TS" "1700000000"

LEN=$(redis_cli llen cpu)
assert_eq "list length is 1" "$LEN" "1"

# ── test 2: multiple records and LTRIM ───────────────────────────────────────

echo "Test 2: multiple records pushed correctly"
redis_cli flushall >/dev/null

printf 'mem|1000|100\nmem|1001|200\nmem|1002|300\n' \
    | timeout 3 "$BINARY" 127.0.0.1 "$PORT" >/dev/null 2>&1 || true

LEN=$(redis_cli llen mem)
assert_eq "3 records in 'mem' list" "$LEN" "3"

FIRST=$(redis_cli lindex mem 0)
assert_eq "most recent value is head of list (LPUSH order)" "$FIRST" "300"

# ── test 3: malformed record — no crash, skipped ─────────────────────────────

echo "Test 3: malformed record is skipped without crashing"
redis_cli flushall >/dev/null

# Feed one bad line then one good line
printf 'bad_record_no_pipes\ndisk|9999|50\n' \
    | timeout 3 "$BINARY" 127.0.0.1 "$PORT" >/dev/null 2>&1 || true

# Bad record should not create any key
BAD=$(redis_cli exists bad_record_no_pipes)
assert_eq "malformed record creates no Redis key" "$BAD" "0"

# Good record after the bad one should still land
GOOD=$(redis_cli lindex disk 0)
assert_eq "valid record after bad one is stored" "$GOOD" "50"

# ── test 4: empty input ───────────────────────────────────────────────────────

echo "Test 4: empty input — clean exit"
redis_cli flushall >/dev/null

echo -n "" | timeout 2 "$BINARY" 127.0.0.1 "$PORT" >/dev/null 2>&1
STATUS=$?
# timeout exits 124 if the process was killed; 0 means it exited normally
assert_eq "clean exit on empty input (exit code 0)" "$STATUS" "0"

# ── summary ───────────────────────────────────────────────────────────────────

echo ""
echo "Results: $PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ] || exit 1
