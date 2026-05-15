#!/usr/bin/env bash
set -euo pipefail

rm -rf build

JOBS="$(nproc)"

COMMON_WARNINGS=(
  -Wall
  -Wextra
  -Wpedantic
  -Wshadow
  -Wconversion
  -Wsign-conversion
  -Wnull-dereference
  -Wdouble-promotion
  -Wformat=2
  -Wimplicit-fallthrough
)

DEBUG_CXX_FLAGS=(
  -g
  -O0
  "${COMMON_WARNINGS[@]}"
  -fsanitize=address,undefined
  -fno-omit-frame-pointer
  -D_GLIBCXX_ASSERTIONS
)

DEBUG_LINK_FLAGS=(
  -fsanitize=address,undefined
)

RELEASE_CXX_FLAGS=(
  -O3
  -DNDEBUG
  "${COMMON_WARNINGS[@]}"
)

join_flags() {
  local IFS=' '
  echo "$*"
}

if [[ "${1:-}" == "-r" ]]; then
  echo "=== RELEASE VER ==="

  cmake -S . -B build \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_FLAGS="$(join_flags "${RELEASE_CXX_FLAGS[@]}")"
else
  echo "=== DEBUG VER ==="

  cmake -S . -B build \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_CXX_FLAGS="$(join_flags "${DEBUG_CXX_FLAGS[@]}")" \
    -DCMAKE_EXE_LINKER_FLAGS="$(join_flags "${DEBUG_LINK_FLAGS[@]}")"
fi

cmake --build build --parallel "$JOBS"
