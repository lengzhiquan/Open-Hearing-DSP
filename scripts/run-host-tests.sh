#!/usr/bin/env bash
# Host DSP tests. Release is built with NDEBUG so the checks are real, not assert().
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
cxx="${CXX:-g++}"
cmake -S "$root/native/ohdsp" -B "$root/build/native-debug" -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER="$cxx"
cmake --build "$root/build/native-debug" -j"$(nproc)"
"$root/build/native-debug/ohdsp_tests"
cmake -S "$root/native/ohdsp" -B "$root/build/native-release" -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER="$cxx"
cmake --build "$root/build/native-release" -j"$(nproc)"
"$root/build/native-release/ohdsp_tests"
