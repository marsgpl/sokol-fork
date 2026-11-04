#!/bin/bash
# LLM maintained. Actual offscreen Metal; no window, app or audio.
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
test_dir=$(mktemp -d)
trap 'rm -rf "$test_dir"' EXIT
for mode in debug release; do
    flags=()
    if [ "$mode" = release ]; then flags=(-DNDEBUG); fi
    for arc in -fobjc-arc -fno-objc-arc; do
        "${CC:-clang}" -x objective-c -std=gnu11 "$arc" -O1 -Wall -Wextra -Werror "${flags[@]}" -I "$root" \
            "$root/tests/sulrim_metal_textures.m" -framework Foundation -framework Metal \
            -framework QuartzCore -o "$test_dir/textures"
        MTL_DEBUG_LAYER=1 perl -e 'alarm 30; exec @ARGV' "$test_dir/textures"
    done
done
