#!/usr/bin/env bash
# mayhem/test.sh — known-answer tests for DawDreamer's .asd warp-file parser.
# Runs the asd_dump tool built by build.sh on each upstream .asd sample (tests/assets) and diffs its output
# (loop/start/end markers, loop_on/warp_on, warp markers, beat->seconds/sample mapping) against
# golden files; also checks that a non-.asd file is rejected with the parser's error.
set -uo pipefail
[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH
: "${SRC:=/mayhem}"
cd "$SRC"

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

TOOL="$SRC/asd_dump"
if [ ! -x "$TOOL" ]; then
  echo "test.sh: $TOOL missing — run mayhem/build.sh first" >&2
  emit_ctrf asd-golden 0 1
  exit 1
fi

pass=0; fail=0
for g in "$SRC"/mayhem/tests/golden/*.txt; do
  name="$(basename "$g" .txt)"
  in="$SRC/tests/assets/Music Delta - Disco/$name.wav.asd"
  if out="$("$TOOL" "$in" 2>&1)" && [ "$out" = "$(cat "$g")" ]; then
    echo "PASS $name"; pass=$((pass+1))
  else
    echo "FAIL $name"; diff <(printf '%s\n' "$out") "$g" | head -20; fail=$((fail+1))
  fi
done

# Negative case: a file without the .asd structure must be rejected.
out="$("$TOOL" "$SRC/LICENSE" 2>&1)"; rc=$?
if [ $rc -eq 1 ] && [[ "$out" == "error: Error: Couldn't find loop info in warp file:"* ]]; then
  echo "PASS reject-non-asd"; pass=$((pass+1))
else
  echo "FAIL reject-non-asd (rc=$rc): $out"; fail=$((fail+1))
fi

emit_ctrf asd-golden "$pass" "$fail"
