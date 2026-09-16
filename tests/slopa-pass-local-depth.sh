#!/bin/bash
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
if [ "$(uname)" = Darwin ]; then
    test_dir=$(mktemp -d)
    trap 'rm -rf "$test_dir"' EXIT
    for mode in debug release; do
        flags=()
        if [ "$mode" = release ]; then flags=(-DNDEBUG); fi
        "${CC:-cc}" -O1 -x objective-c -std=gnu11 -fno-objc-arc -I "$root" "${flags[@]}" \
            "$root/tests/slopa_metal_pass_local_depth.m" -framework Foundation \
            -framework Metal -framework QuartzCore -o "$test_dir/$mode"
        MTL_DEBUG_LAYER=1 "$test_dir/$mode"
    done
fi
