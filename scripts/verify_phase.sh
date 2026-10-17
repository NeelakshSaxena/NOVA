#!/usr/bin/env bash
# Clean configure + build + full CTest, logging to state/evidence/<phase>/.
# Usage: scripts/verify_phase.sh V0.4
# Environment: needs cmake and a C++17 compiler on PATH. On the Windows/MinGW dev machine this script
# adds the default install locations; set NOVA_GENERATOR to override the CMake generator.
set -u
phase="${1:?usage: verify_phase.sh <phase-id>}"
cd "$(dirname "$0")/.."
export PATH="/c/Program Files/CMake/bin:/c/MinGW/bin:$PATH"
gen="${NOVA_GENERATOR:-}"
if [ -z "$gen" ] && command -v mingw32-make >/dev/null 2>&1 && ! command -v ninja >/dev/null 2>&1; then
  gen="MinGW Makefiles"
fi
ev="state/evidence/$phase"
mkdir -p "$ev"
rm -rf build
{
  date -Iseconds
  cmake --version | head -1
  ctest --version | head -1
  g++ --version | head -1
} > "$ev/00_toolchain.log" 2>&1
{
  if [ -n "$gen" ]; then cmake -S . -B build -G "$gen"; else cmake -S . -B build; fi
  echo "configure exit $?"
  cmake --build build
  echo "build exit $?"
} > "$ev/01_clean_build.log" 2>&1
grep -E "warning|error|exit" "$ev/01_clean_build.log"
{
  ctest --test-dir build --output-on-failure
  echo "ctest exit $?"
} > "$ev/02_ctest.log" 2>&1
grep -E "tests passed|tests failed|ctest exit|Failed" "$ev/02_ctest.log"
