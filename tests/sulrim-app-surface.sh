#!/bin/bash
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
test_dir=$(mktemp -d)
trap 'rm -rf "$test_dir"' EXIT
"${CC:-cc}" -x objective-c -std=gnu11 -fobjc-arc -I "$root" \
    "$root/tests/sulrim_app_surface.m" -framework Cocoa -framework Metal \
    -framework QuartzCore -o "$test_dir/surface"
"$test_dir/surface"
