#!/usr/bin/env bash
#
# mayhem/test.sh — METIS functional oracle. Upstream ships no runnable test suite (test/mtest.c
# targets the retired METIS 4 API), so this runs upstream's own CLI tools — built by mayhem/build.sh
# with upstream's normal flags into build/test/programs — on upstream's sample inputs in graphs/ and
# asserts known answers: reported edgecuts/volumes, fill-in counts, golden output files, and that
# every partition/permutation file is well-formed.
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

B="$SRC/build/test/programs"
for p in gpmetis ndmetis mpmetis m2gmetis graphchk; do
  [ -x "$B/$p" ] || { echo "missing $B/$p — mayhem/build.sh did not build it" >&2; emit_ctrf metis-cli 0 1; exit 1; }
done
T="$(mktemp -d "${TMPDIR:-/tmp}/metis-test.XXXXXX")"
trap 'rm -rf "$T"' EXIT
cp "$SRC/graphs/4elt.graph" "$SRC/graphs/test.mgraph" "$SRC/graphs/metis.mesh" "$T/"
cd "$T"

PASS=0 FAIL=0
ok()   { PASS=$((PASS+1)); echo "PASS: $1"; }
bad()  { FAIL=$((FAIL+1)); echo "FAIL: $1"; }
# expect_out <name> <expected substring> <cmd...>: the command's stdout must contain the substring.
expect_out() {
  local name="$1" want="$2"; shift 2
  local out; out="$("$@" 2>&1)" || true
  if grep -qF -- "$want" <<<"$out"; then ok "$name"; else bad "$name (wanted '$want')"; printf '%s\n' "$out" | tail -5; fi
}
expect_md5() {
  local name="$1" want="$2" f="$3"
  if [ -f "$f" ] && [ "$(md5sum < "$f" | cut -d' ' -f1)" = "$want" ]; then ok "$name"; else bad "$name ($f)"; fi
}
# check_part <name> <file> <n> <nparts>: n lines, every value in [0,nparts), every part used.
check_part() {
  local name="$1" f="$2" n="$3" k="$4"
  if [ -f "$f" ] && awk -v n="$n" -v k="$k" '{ if ($1 !~ /^[0-9]+$/ || $1+0 >= k) bad=1; used[$1]=1 }
       END { c=0; for (p in used) c++; exit (bad || NR != n || c != k) }' "$f"; then ok "$name"; else bad "$name"; fi
}
# check_perm <name> <file> <n>: a permutation of 0..n-1.
check_perm() {
  local name="$1" f="$2" n="$3"
  if [ -f "$f" ] && awk -v n="$n" '{ if ($1 !~ /^[0-9]+$/ || $1+0 >= n || seen[$1]++) bad=1 } END { exit (bad || NR != n) }' "$f"
  then ok "$name"; else bad "$name"; fi
}
# cut_of <graph> <part>: the edgecut recomputed from the graph file and a partition file.
cut_of() {
  awk 'NR==FNR { p[FNR]=$1; next } /^%/ { next } !h { h=1; next } { v++; for (i=1;i<=NF;i++) if (p[v] != p[$i]) c++ } END { print c/2 }' "$2" "$1"
}

# graphchk
expect_out "graphchk accepts 4elt"        "The format of the graph is correct!" "$B/graphchk" 4elt.graph
printf '4 3 1\n2 1 2 1 1 1\n3 1\n4 1\n1 1\n' > bad.graph
expect_out "graphchk rejects a malformed graph" "The format of the graph is incorrect!" "$B/graphchk" bad.graph fixed.graph
expect_md5 "graphchk FixGraph output"     785c07d9caa75739b37711a2476f0f72 fixed.graph   # symmetrized: 4 4 / 2 4 / 1 3 / 2 4 / 1 3

# gpmetis (k-way, recursive bisection, volume objective, multi-constraint)
expect_out "gpmetis kway 4 edgecut"       "Edgecut: 401, communication volume: 409." "$B/gpmetis" 4elt.graph 4
check_part "gpmetis kway 4 partition"     4elt.graph.part.4 15606 4
if [ "$(cut_of 4elt.graph 4elt.graph.part.4)" = 401 ]; then ok "gpmetis kway 4 partition has cut 401"; else bad "gpmetis kway 4 partition has cut 401"; fi
expect_md5 "gpmetis kway 4 golden"        9ecbc3f15e913909788ea21fe60908b4 4elt.graph.part.4
expect_out "gpmetis rb 8 edgecut"         "Edgecut: 661, communication volume: 681." "$B/gpmetis" -ptype=rb 4elt.graph 8
check_part "gpmetis rb 8 partition"       4elt.graph.part.8 15606 8
if [ "$(cut_of 4elt.graph 4elt.graph.part.8)" = 661 ]; then ok "gpmetis rb 8 partition has cut 661"; else bad "gpmetis rb 8 partition has cut 661"; fi
expect_out "gpmetis vol 3"                "Edgecut: 298, communication volume: 303." "$B/gpmetis" -objtype=vol 4elt.graph 3
check_part "gpmetis vol 3 partition"      4elt.graph.part.3 15606 3
expect_out "gpmetis multi-constraint 2"   "Edgecut: 23, communication volume: 42." "$B/gpmetis" test.mgraph 2
check_part "gpmetis multi-constraint partition" test.mgraph.part.2 "$(awk '!/^%/{print $1; exit}' test.mgraph)" 2

# ndmetis (nested dissection + symbolic factorization)
expect_out "ndmetis fill-in"              "Nonzeros: 3.292e+05 	Operation Count: 1.227e+07" "$B/ndmetis" 4elt.graph
check_perm "ndmetis iperm is a permutation" 4elt.graph.iperm 15606
expect_md5 "ndmetis iperm golden"         ec91bb098eb48ac67e2eb831b851a952 4elt.graph.iperm

# mpmetis (dual and nodal mesh partitioning)
expect_out "mpmetis dual 4 edgecut"       "Edgecut: 545." "$B/mpmetis" metis.mesh 4
check_part "mpmetis dual 4 element partition" metis.mesh.epart.4 7434 4
expect_md5 "mpmetis dual 4 epart golden"  f0e60113e64705b5ad7a76be19101e3c metis.mesh.epart.4
expect_md5 "mpmetis dual 4 npart golden"  d050eaa5e9e5d853a3ffcf63775f4881 metis.mesh.npart.4
expect_out "mpmetis nodal 3 edgecut"      "Edgecut: 104." "$B/mpmetis" -gtype=nodal metis.mesh 3

# m2gmetis (mesh -> dual graph), then graphchk on its output
"$B/m2gmetis" metis.mesh m.graph >/dev/null 2>&1 || true
expect_md5 "m2gmetis dual graph golden"   9b860e426119b6fa11c1b0cc906679a6 m.graph
expect_out "m2gmetis output is a valid graph" "The format of the graph is correct!" "$B/graphchk" m.graph

cd "$SRC"
emit_ctrf metis-cli "$PASS" "$FAIL" 0
