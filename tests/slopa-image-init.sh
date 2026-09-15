#!/bin/bash
# Focused WebGPU ownership mocks and headless Metal texture checks.
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
webgpu_include=${1:?pass the pinned WebGPU include directory}
bash "$root/tests/slopa-p1.sh" "$webgpu_include"
if [ "$(uname)" = Darwin ]; then
    test_dir=$(mktemp -d)
    trap 'rm -rf "$test_dir"' EXIT
    "${CC:-cc}" -x objective-c -std=gnu11 -fno-objc-arc -I "$root" \
        "$root/tests/slopa_metal_image_init.m" -framework Foundation \
        -framework Metal -framework QuartzCore -o "$test_dir/image-init"
    "$test_dir/image-init"
fi
