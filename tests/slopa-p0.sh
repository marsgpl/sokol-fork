#!/bin/bash
# Usage: bash tests/slopa-p0.sh /path/to/emdawnwebgpu_pkg/webgpu/include
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
"${CC:-cc}" -std=c11 -ffunction-sections -fdata-sections -I "$root" -I "$webgpu_include" \
    "$root/tests/slopa_wgpu_stats.c" "${link_flags[@]}" -o "$test_dir/stats"
"$test_dir/stats"
