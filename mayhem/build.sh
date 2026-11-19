#!/usr/bin/env bash
#
# mayhem/build.sh — build the kvrocks fuzz harnesses and upstream's unit tests.
#
# One CMake tree (build/mayhem, gitignored by upstream's `build` pattern) built from
# mayhem/fuzz/CMakeLists.txt, which add_subdirectory()s upstream's own CMake project:
#   * the dependencies (rocksdb, libevent, luajit, jsoncons, ...) and upstream's `unittest` are
#     built with upstream's normal, uninstrumented flags;
#   * kvrocks' own sources are compiled a second time with $SANITIZER_FLAGS $DEBUG_FLAGS and
#     SanitizerCoverage (kvrocks_fuzz_objs) and linked into each harness, once with
#     $LIB_FUZZING_ENGINE and once with $STANDALONE_FUZZ_MAIN (the -standalone reproducer).
# Upstream's CMake fetches every dependency as a GitHub zip; the Dockerfile caches those zips in
# /opt/toolchains/deps/kvrocks-deps and DEPS_FETCH_DIR (upstream's own option) makes the build use
# them, so this script runs offline and can be re-run on a built tree.
set -euo pipefail

[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH

: "${SANITIZER_FLAGS=-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer}"
: "${DEBUG_FLAGS:=-g -gdwarf-3}"
: "${CC:=clang}" ; : "${CXX:=clang++}" ; : "${LIB_FUZZING_ENGINE:=-fsanitize=fuzzer}"
: "${MAYHEM_JOBS:=$(nproc)}"
: "${COVERAGE_FLAGS=}"
: "${SRC:=/mayhem}"
: "${KVROCKS_DEPS_DIR:=/opt/toolchains/deps/kvrocks-deps}"
export SANITIZER_FLAGS DEBUG_FLAGS CC CXX LIB_FUZZING_ENGINE MAYHEM_JOBS COVERAGE_FLAGS
# CMake's zip extraction fails on non-ASCII member names without a UTF-8 locale (pegtl).
export LC_ALL=C.UTF-8

cd "$SRC"

# Flags for everything that is not a harness or kvrocks itself: the dependencies and the unit
# tests. DWARF 3 line tables, so dependency frames in a harness backtrace resolve to source lines
# (SPEC 6.2 item 10) without the full debug info of rocksdb in each of the 16 binaries.
BASE_FLAGS="-O1 -gline-tables-only -gdwarf-3"
# UBSan's `alignment` check is relaxed for kvrocks: it reads packed on-disk records in place
# (e.g. TSSample in src/types/redis_timeseries.cc) and fires on every TS.ADD. Upstream's own
# ENABLE_UBSAN build disables the same check (CMakeLists.txt). ASan and the rest of UBSan halt.
KVFUZZ_FLAGS="$SANITIZER_FLAGS $DEBUG_FLAGS${SANITIZER_FLAGS:+ -fno-sanitize=alignment}"
# clang lowers the 16-byte std::atomic in src/cluster/slot_migrate.cc to __atomic_* libcalls.

cmake -S mayhem/fuzz -B build/mayhem -G Ninja \
  -DCMAKE_BUILD_TYPE= \
  -DCMAKE_CXX_SCAN_FOR_MODULES=OFF \
  -DCMAKE_C_FLAGS="$BASE_FLAGS" \
  -DCMAKE_CXX_FLAGS="$BASE_FLAGS" \
  -DCMAKE_EXE_LINKER_FLAGS="-latomic" \
  -DDEPS_FETCH_DIR="$KVROCKS_DEPS_DIR" \
  -DDISABLE_JEMALLOC=ON \
  -DPORTABLE=1 \
  -DENABLE_STATIC_LIBSTDCXX=OFF \
  -DNINJA_MAKE_JOBS="$MAYHEM_JOBS" \
  -DKVFUZZ_FLAGS="$KVFUZZ_FLAGS" \
  -DKVFUZZ_ENGINE="$LIB_FUZZING_ENGINE" \
  -DKVFUZZ_STANDALONE_MAIN="$STANDALONE_FUZZ_MAIN" \
  -DKVFUZZ_LSAN_OFF="$SRC/mayhem/lsan_off.cc"

cmake --build build/mayhem -j"$MAYHEM_JOBS" --target kvrocks_fuzzers unittest

# Harnesses at the top of /mayhem (hard links: no copy of the large binaries).
for t in resp rdb_load lua json ft_sql ft_query cluster_nodes config; do
  ln -f "build/mayhem/kvrocks_fuzz_$t" "$SRC/kvrocks_fuzz_$t"
  ln -f "build/mayhem/kvrocks_fuzz_$t-standalone" "$SRC/kvrocks_fuzz_$t-standalone"
done
