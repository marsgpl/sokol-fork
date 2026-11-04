#!/bin/bash
# LLM maintained. Actual Metal and separate/shared Wasm resources; no window or audio.
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
playwright=${1:?pass the absolute Playwright module path; pinned emcc must be on PATH}
test_dir=$(mktemp -d)
trap 'rm -rf "$test_dir"' EXIT
for arc in -fobjc-arc -fno-objc-arc; do
    "${CC:-clang}" -x objective-c -std=gnu11 -O1 "$arc" -Wall -Wextra -Werror -DSOKOL_METAL -I "$root" \
        "$root/tests/sulrim_resources.c" -framework Foundation -framework Metal -framework QuartzCore -o "$test_dir/probe"
    perl -e 'alarm 30; exec @ARGV' "$test_dir/probe"
done
for memory in separate shared; do
    flags=()
    if [ "$memory" = shared ]; then flags=(-pthread -sPTHREAD_POOL_SIZE=0); fi
    emcc -std=c11 -O2 -Wall -Wextra -Werror -DSOKOL_WGPU -I "$root" --use-port=emdawnwebgpu "${flags[@]}" \
        "$root/tests/sulrim_resources.c" -sMODULARIZE=1 -sEXPORT_ES6=1 -sINITIAL_MEMORY=33554432 \
        -sALLOW_MEMORY_GROWTH=0 -sEXPORTED_RUNTIME_METHODS=WebGPU,HEAPU8 -sENVIRONMENT=web,worker -sASSERTIONS=1 -o "$test_dir/probe.mjs"
    echo "Resource APIs: $memory Wasm memory"
    bun "$root/tests/sulrim-resources.mjs" "$test_dir" "$playwright"
done
