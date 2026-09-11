#!/usr/bin/env bash
# Build the Linux nest binary in this folder only.
# If ../engine exists, copy those sources first so logic stays 100% the same.
set -euo pipefail
cd "$(dirname "$0")"

ENGINE="../engine"
SRC_FILES="pf_nest.cpp pf_json.hpp pf_geom.hpp pf_dev.hpp pf_dev_trials.hpp"

if [ -d "$ENGINE" ]; then
  echo "Syncing identical sources from $ENGINE ..."
  for f in $SRC_FILES; do
    cp -f "$ENGINE/$f" "./$f"
  done
else
  echo "No ../engine — using sources already in this folder."
fi

if ! command -v g++ >/dev/null 2>&1; then
  echo "ERROR: g++ not found. On Ubuntu/Debian: sudo apt-get install -y g++ make"
  exit 1
fi

echo "Building Linux nest (same as Windows: -O2 -std=c++17 -DNDEBUG)..."
make -B
echo "OK: $(pwd)/nest"
file nest 2>/dev/null || true
echo "Run:  ./nest < input.json > result.json"
