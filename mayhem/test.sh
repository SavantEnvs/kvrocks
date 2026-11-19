#!/usr/bin/env bash
#
# mayhem/test.sh — RUN kvrocks' own C++ unit tests (gtest `unittest`, built by mayhem/build.sh with
# upstream's normal flags). exit 0 = pass. PATCH-grade oracle: after an agent patches the source, the grader
# rebuilds (build.sh) then runs this. DELETE this file if the repo has no meaningful tests.
#
# IMPORTANT:
#  * Must assert BEHAVIOR/OUTPUT, not just exit status. The oracle has to check asserted values /
#    golden-output diffs / known-answer results — so a PATCH that "fixes" a bug by making the program
#    exit(0) (or any no-op) FAILS here. Running inputs and checking only "exit 0 / didn't crash" is
#    NOT a functional test (it's trivially reward-hackable) — use the project's real assertion suite.
#  * Do NOT build here — mayhem/build.sh already compiled the test suite (with the project's normal
#    flags). This script only RUNS the pre-built tests and reports counts. If the test runner is
#    missing, that's a build.sh bug — fail loudly rather than silently rebuilding.
#  * REQUIRED OUTPUT — a CTRF (https://ctrf.io) summary so Mayhem/the PATCH grader reads the counts:
#      - writes a CTRF JSON report to ${CTRF_REPORT:-$SRC/ctrf-report.json}, and
#      - prints a one-line `CTRF {...}` marker to stdout (same JSON, compact).
#    Only `results.summary` (with tests/passed/failed/pending/skipped/other) is required.
#    Use the emit_ctrf helper below; it computes tests = passed+failed+skipped and sets the exit
#    code (0 iff failed==0). Map your framework's output to passed/failed/skipped.
set -uo pipefail
[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH
: "${MAYHEM_JOBS:=$(nproc)}"   # build parallelism; env-overridable, falls back to nproc (use -j"$MAYHEM_JOBS")
cd "$SRC"

# emit_ctrf <tool> <passed> <failed> [skipped] [pending] [other]
# Writes a CTRF report (file + stdout `CTRF {...}` marker) and returns non-zero iff failed>0.
emit_ctrf() {
  local tool="$1" passed="$2" failed="$3" skipped="${4:-0}" pending="${5:-0}" other="${6:-0}"
  local tests=$(( passed + failed + skipped + pending + other ))
  cat > "${CTRF_REPORT:-$SRC/ctrf-report.json}" <<JSON
{
  "results": {
    "tool": { "name": "$tool" },
    "summary": {
      "tests": $tests,
      "passed": $passed,
      "failed": $failed,
      "pending": $pending,
      "skipped": $skipped,
      "other": $other
    }
  }
}
JSON
  printf 'CTRF {"results":{"tool":{"name":"%s"},"summary":{"tests":%d,"passed":%d,"failed":%d,"pending":%d,"skipped":%d,"other":%d}}}\n' \
    "$tool" "$tests" "$passed" "$failed" "$pending" "$skipped" "$other"
  [ "$failed" -eq 0 ]
}

# The suites below cover the code the fuzz targets reach: the RDB loader, JSON, streams, bitfield,
# cluster topology, config parsing, the search SQL/query parsers, IR passes and plan executor, and
# the core data types. They are self-contained (each opens RocksDB in the working directory).
UNITTEST="$SRC/build/mayhem/kvrocks/unittest"
if [ ! -x "$UNITTEST" ]; then
  echo "test.sh: $UNITTEST is missing — run mayhem/build.sh first" >&2
  emit_ctrf "gtest" 0 1 0
  exit 1
fi
FILTER="BitfieldUtil.*:ClusterTest.*:CommandParser.*:Config.*:CronTest*:IntervalSet.*:IRPassTest.*"
FILTER+=":SemaCheckerTest.*:ParseUtil.*:PlanExecutorTest*:RDBTest.*:RdbFileStream*:RedisQueryParserTest.*"
FILTER+=":SQLParserTest.*:StringUtil.*:StringReplyTest.*:RedisJsonTest.*:RedisStreamTest.*:RedisStringTest.*"
FILTER+=":RedisHashTest.*:RedisZSetTest.*:RedisListTest.*:RedisSetTest.*:RedisGeoTest.*"
FILTER+=":RedisHyperLogLogTest.*:RedisTDigestTest.*:Util.*:StatusOr.*"

work="$(mktemp -d "${TMPDIR:-/tmp}/kvrocks-test.XXXXXX")"
trap 'rm -rf "$work"' EXIT
json="$work/gtest.json"
( cd "$work" && "$UNITTEST" --gtest_filter="$FILTER" --gtest_output="json:$json" ) > "$work/log" 2>&1
rc=$?
grep -E '^\[  (PASSED|FAILED) |^\[==========\]' "$work/log" || true

# Counts come from gtest's own JSON report; a run that produced none counts as one failure.
read -r passed failed skipped < <(python3 -I - "$json" <<'PY' 2>/dev/null || echo "0 1 0"
import json, sys
r = json.load(open(sys.argv[1]))
p = f = s = 0
for suite in r.get("testsuites", []):
    for t in suite.get("testsuite", []):
        if t.get("result") == "SKIPPED" or t.get("status") == "NOTRUN":
            s += 1
        elif t.get("failures"):
            f += 1
        else:
            p += 1
print(p, f, s)
PY
)
if [ "$rc" -ne 0 ] && [ "$failed" -eq 0 ]; then
  echo "test.sh: unittest exited $rc with no failed test recorded" >&2
  tail -20 "$work/log" >&2
  failed=1
fi
if [ "$passed" -eq 0 ]; then failed=$(( failed > 0 ? failed : 1 )); fi
[ "$failed" -eq 0 ] || grep -E '^\[  FAILED  \]' "$work/log" | head -50 >&2
emit_ctrf "gtest" "$passed" "$failed" "$skipped"
