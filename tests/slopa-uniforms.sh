#!/bin/bash
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
if [ "$(uname)" = Darwin ]; then
    test_dir=$(mktemp -d)
    trap 'rm -rf "$test_dir"' EXIT
    "${CC:-cc}" -O2 -x objective-c -std=gnu11 -fno-objc-arc -I "$root" \
        "$root/tests/slopa_metal_uniforms.m" -framework Foundation \
        -framework Metal -framework QuartzCore -o "$test_dir/metal"
    if [ "${1:-}" = --bench ]; then
        SLOPA_UNIFORM_BENCH=1 MTL_DEBUG_LAYER=0 "$test_dir/metal"
    else
        MTL_DEBUG_LAYER=1 "$test_dir/metal"
    fi
fi
