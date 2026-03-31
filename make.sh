#!/bin/bash
set -e
rm -rf build

if [ "$1" = "-r" ]; then
  echo "=== RELEASE VER ==="
  cmake -S . -B build -D CMAKE_BUILD_TYPE=Release 1>/dev/null
else
  echo "=== DEBUG VER ==="
  cmake -S . -B build -D CMAKE_BUILD_TYPE=Debug 1>/dev/null
fi

cmake --build build -j
