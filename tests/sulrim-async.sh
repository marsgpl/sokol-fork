#!/bin/bash
# LLM maintained. Deterministic completion ownership; real compiles use client gpu_async.
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
test_dir=$(mktemp -d)
trap 'rm -rf "$test_dir"' EXIT
for mode in debug release; do
    flags=()
    if [ "$mode" = release ]; then flags=(-DNDEBUG); fi
    "${CC:-cc}" -std=c11 -O1 -Wall -Wextra -Werror "${flags[@]}" -I "$root" -I "$root/tests/mocks/wgpu" \
        "$root/tests/sulrim_wgpu_async.c" "$root/tests/mocks/wgpu/wgpu_mock.c" -o "$test_dir/async"
    "$test_dir/async"
    for arc in -fobjc-arc -fno-objc-arc; do
        "${CC:-clang}" -x objective-c -std=gnu11 -O1 "$arc" -Wall -Wextra -Werror "${flags[@]}" \
            -DSOKOL_METAL -DSULRIM_INVARIANCE_ONLY -I "$root" "$root/tests/sulrim_depth.c" \
            -framework Foundation -framework Metal -framework QuartzCore -o "$test_dir/invariance"
        MTL_DEBUG_LAYER=1 perl -e 'alarm 30; exec @ARGV' "$test_dir/invariance"
    done
done
