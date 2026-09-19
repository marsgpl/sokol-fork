#!/bin/bash
# Native baseline + Wasm path. Pass pinned include dir; optionally EMCC and NODE.
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
webgpu_include=${1:?pass the pinned WebGPU include directory}
test_dir=$(mktemp -d)
trap 'rm -rf "$test_dir"' EXIT
if [ "$(uname)" = Darwin ]; then
    link_flags=(-Wl,-dead_strip)
else
    link_flags=(-Wl,--gc-sections)
fi
"${CC:-cc}" -std=c11 -O2 -ffunction-sections -fdata-sections -I "$root" -I "$webgpu_include" \
    "$root/tests/slopa_wgpu_initial.c" "${link_flags[@]}" -o "$test_dir/initial"
"$test_dir/initial"
if [ -n "${EMCC:-}" ]; then
    for mode in debug release; do
        flags=()
        if [ "$mode" = release ]; then flags=(-DNDEBUG -sASSERTIONS=0); fi
        "$EMCC" -std=c11 -O3 -flto "${flags[@]}" -I "$root" -I "$webgpu_include" \
            "$root/tests/slopa_wgpu_initial.c" -sENVIRONMENT=node -o "$test_dir/initial.js"
        "${NODE:-node}" "$test_dir/initial.js"
    done
fi
