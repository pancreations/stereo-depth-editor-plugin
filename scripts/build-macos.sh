#!/usr/bin/env bash
set -euo pipefail
project_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$project_root"
args=(-S . -B build-macos -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64
  -DCMAKE_OSX_DEPLOYMENT_TARGET=14.0 -DS3D_BUILD_CUDA=OFF -DS3D_BUILD_PREMIERE=OFF)
if [[ -n "${AE_SDK_ROOT:-}" || -n "${PREMIERE_SDK_ROOT:-}" ]]; then
  : "${AE_SDK_ROOT:?Set both Adobe SDK roots}"
  : "${PREMIERE_SDK_ROOT:?Set both Adobe SDK roots}"
  args+=(-DS3D_BUILD_PREMIERE=ON "-DAE_SDK_ROOT=$AE_SDK_ROOT" "-DPREMIERE_SDK_ROOT=$PREMIERE_SDK_ROOT")
fi
cmake "${args[@]}"
cmake --build build-macos --config Release --parallel
ctest --test-dir build-macos -C Release --output-on-failure
