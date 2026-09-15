#!/bin/bash
# Usage: bash tests/slopa-p1.sh /path/to/emdawnwebgpu_pkg/webgpu/include
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
    "$root/tests/slopa_wgpu_lifetime.c" "${link_flags[@]}" -o "$test_dir/lifetime"
"$test_dir/lifetime"

if [ "$(uname)" = Darwin ]; then
    "${CC:-cc}" -x objective-c -std=gnu11 -fno-objc-arc -ffunction-sections -fdata-sections \
        -I "$root" "$root/tests/slopa_metal_lifetime.m" -Wl,-dead_strip \
        -framework Foundation -framework Metal -framework QuartzCore -o "$test_dir/metal"
    "$test_dir/metal"
fi
