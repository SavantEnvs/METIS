#!/usr/bin/env bash
#
# mayhem/build.sh — build METIS's fuzz targets and the CLI tools mayhem/test.sh runs.
#
# Runs inside the commit image (mayhem/Dockerfile) as `mayhem` in /mayhem; re-runnable and offline:
# the only external dependency, GKlib, is prebuilt in the image at /opt/toolchains/deps/gklib.
#
#   1. build/xinclude/metis.h — include/metis.h with the 32-bit IDXTYPEWIDTH/REALTYPEWIDTH defines
#      prepended, exactly what upstream's `make config` writes (the CMake build reads it from there).
#   2. build/afl  — libmetis with afl-clang-fast + $SANITIZER_FLAGS $DEBUG_FLAGS (AFL edge coverage,
#      ASan+UBSan halting, DWARF 3), then the five file-input harnesses (mayhem/harnesses/fuzz_*.c)
#      linked against it -> /mayhem/fuzz_<target>. They are AFL targets (Mayhemfile `afl: true`),
#      their own reproducers, so there is no -standalone build (SPEC §6.3).
#   3. build/test — upstream's own CMake build with its normal flags (plus optional $COVERAGE_FLAGS):
#      the gpmetis/ndmetis/mpmetis/m2gmetis/graphchk CLIs that mayhem/test.sh runs.
# All build trees live under build/, which upstream's .gitignore already ignores.
set -euo pipefail

[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH

: "${SANITIZER_FLAGS=-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer}"
: "${DEBUG_FLAGS:=-g -gdwarf-3}"
: "${CC:=clang}" ; : "${CXX:=clang++}"
: "${MAYHEM_JOBS:=$(nproc)}"
: "${COVERAGE_FLAGS=}"
: "${AFL_CC:=afl-clang-fast}"
: "${GKLIB_PATH:=/opt/toolchains/deps/gklib}"
export SANITIZER_FLAGS DEBUG_FLAGS CC CXX MAYHEM_JOBS COVERAGE_FLAGS
export AFL_QUIET=1

cd "$SRC"

# 1) build/xinclude/metis.h, as upstream's `make config` (Makefile run-config) generates it.
mkdir -p build/xinclude
{ echo '#define IDXTYPEWIDTH 32'; echo '#define REALTYPEWIDTH 32'; cat include/metis.h; } > build/xinclude/metis.h.new
if cmp -s build/xinclude/metis.h.new build/xinclude/metis.h; then rm -f build/xinclude/metis.h.new
else mv build/xinclude/metis.h.new build/xinclude/metis.h; fi
cp -p include/CMakeLists.txt build/xinclude/CMakeLists.txt

# 2) Fuzz build: instrumented libmetis + the harnesses.
# ASSERT/ASSERT2=ON keep libmetis's GKlib ASSERT/ASSERT2 checks live (upstream's `make config assert=1
# assert2=1`); without them conf/gkbuild.cmake adds -DNDEBUG -DNDEBUG2 and compiles them out.
CC="$AFL_CC" cmake -S . -B build/afl -DGKLIB_PATH="$GKLIB_PATH" -DASSERT=ON -DASSERT2=ON \
    -DCMAKE_C_FLAGS="$SANITIZER_FLAGS $DEBUG_FLAGS" >/dev/null
cmake --build build/afl --target metis -j"$MAYHEM_JOBS"

# The same defines and optimisation upstream's conf/gkbuild.cmake gives libmetis and the programs with
# assert=1 assert2=1: no -DNDEBUG/-DNDEBUG2, so assert() in programs/ (e.g. smbfactor.c) stays live.
HFLAGS="$SANITIZER_FLAGS $DEBUG_FLAGS -O3 -std=c99 -DLINUX -D_FILE_OFFSET_BITS=64
        -DHAVE_EXECINFO_H -DHAVE_GETLINE -Ibuild/xinclude -I$GKLIB_PATH/include -Ilibmetis -Iprograms
        -Imayhem/harnesses"
OBJ=build/afl/harness
mkdir -p "$OBJ"
# shellcheck disable=SC2086
"$AFL_CC" $HFLAGS -c mayhem/lsan_off.c -o "$OBJ/lsan_off.o"

# target -> the upstream programs/ sources its CLI is built from (besides io.c). The CLI's own
# main.c is compiled with -Dmain=<cli>_cli_main so the harness can reuse its report functions.
declare -A EXTRA=(
  [gpmetis]="cmdline_gpmetis.c stat.c"
  [ndmetis]="cmdline_ndmetis.c smbfactor.c"
  [mpmetis]="cmdline_mpmetis.c stat.c"
  [m2gmetis]="cmdline_m2gmetis.c"
  [graphchk]=""
)
for t in gpmetis ndmetis mpmetis m2gmetis graphchk; do
  objs=("$OBJ/lsan_off.o")
  for s in io.c ${EXTRA[$t]}; do
    o="$OBJ/$t-${s%.c}.o"
    # shellcheck disable=SC2086
    "$AFL_CC" $HFLAGS -c "programs/$s" -o "$o"
    objs+=("$o")
  done
  if [ "$t" != graphchk ]; then
    # shellcheck disable=SC2086
    "$AFL_CC" $HFLAGS -Dmain="${t}_cli_main" -c "programs/$t.c" -o "$OBJ/$t-cli.o"
    objs+=("$OBJ/$t-cli.o")
  fi
  # shellcheck disable=SC2086
  "$AFL_CC" $HFLAGS "mayhem/harnesses/fuzz_$t.c" "${objs[@]}" \
      build/afl/libmetis/libmetis.a "$GKLIB_PATH/lib/libGKlib.a" -lm -o "$SRC/fuzz_$t"
done

# 3) Test build: upstream's CMake build of the CLI tools with its normal flags.
cmake -S . -B build/test -DGKLIB_PATH="$GKLIB_PATH" \
    -DCMAKE_C_FLAGS="$COVERAGE_FLAGS" -DCMAKE_EXE_LINKER_FLAGS="$COVERAGE_FLAGS" >/dev/null
cmake --build build/test -j"$MAYHEM_JOBS" --target gpmetis ndmetis mpmetis m2gmetis graphchk

echo "build.sh: built $(ls "$SRC"/fuzz_* | tr '\n' ' ')"
