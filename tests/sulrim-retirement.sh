#!/bin/bash
# LLM maintained. Upstream mocks; separate real-backend resource probe is required.
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
test_dir=$(mktemp -d)
trap 'rm -rf "$test_dir"' EXIT
for mode in debug release; do
    flags=()
    if [ "$mode" = release ]; then flags=(-DNDEBUG); fi
    "${CC:-cc}" -std=c11 -O1 -Wall -Wextra -Werror "${flags[@]}" -I "$root" -I "$root/tests/mocks/wgpu" \
        "$root/tests/sulrim_wgpu_retirement.c" "$root/tests/mocks/wgpu/wgpu_mock.c" -o "$test_dir/retirement"
    "$test_dir/retirement"
done
