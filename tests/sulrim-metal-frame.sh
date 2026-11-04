#!/bin/bash
# LLM maintained. No window, app or audio; actual offscreen Metal queue.
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
test_dir=$(mktemp -d)
trap 'rm -rf "$test_dir"' EXIT
for arc in -fobjc-arc -fno-objc-arc; do
    "${CC:-clang}" -x objective-c -std=gnu11 "$arc" -O1 -Wall -Wextra -Werror -I "$root" \
        "$root/tests/sulrim_metal_frame.m" -framework Foundation -framework Metal \
        -framework QuartzCore -o "$test_dir/frame"
    perl -e 'alarm 20; exec @ARGV' "$test_dir/frame"
done
