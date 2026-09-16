#!/bin/bash
# Native CPU-only WebGPU path. Pass the pinned emdawnwebgpu include directory.
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
"${CC:-cc}" -O2 -std=gnu11 -ffunction-sections -fdata-sections \
    -I "$root" -I "$webgpu_include" "$root/tests/slopa_wgpu_uniform_bench.c" \
    "${link_flags[@]}" -o "$test_dir/bench"
"$test_dir/bench"
