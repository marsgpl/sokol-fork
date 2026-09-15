#!/bin/bash
# Focused direct-draw validation and real Metal singleton readback.
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
if [ "$(uname)" = Darwin ]; then
    test_dir=$(mktemp -d)
    trap 'rm -rf "$test_dir"' EXIT
    "${CC:-cc}" -x objective-c -std=gnu11 -fno-objc-arc -I "$root" \
        "$root/tests/slopa_metal_draw_offsets.m" -framework Foundation \
        -framework Metal -framework QuartzCore -o "$test_dir/metal"
    MTL_DEBUG_LAYER=1 "$test_dir/metal"
fi
