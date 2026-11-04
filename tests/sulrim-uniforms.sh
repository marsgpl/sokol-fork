#!/bin/bash
# LLM maintained. Focused cache contracts and actual Metal/WebGPU output.
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
playwright=${1:?pass the absolute Playwright module path; pinned emcc must be on PATH}
test_dir=$(mktemp -d)
trap 'rm -rf "$test_dir"' EXIT
for mode in debug release; do
    flags=()
    if [ "$mode" = release ]; then flags=(-DNDEBUG); fi
    "${CC:-cc}" -std=c11 -O1 -Wall -Wextra -Werror "${flags[@]}" -I "$root" -I "$root/tests/mocks/wgpu" \
        "$root/tests/sulrim_wgpu_caches.c" "$root/tests/mocks/wgpu/wgpu_mock.c" -o "$test_dir/mock"
    "$test_dir/mock"
    for arc in -fobjc-arc -fno-objc-arc; do
        "${CC:-clang}" -x objective-c -std=gnu11 -O1 "$arc" -Wall -Wextra -Werror "${flags[@]}" -DSOKOL_METAL -I "$root" \
            "$root/tests/sulrim_uniforms.c" -framework Foundation -framework Metal -framework QuartzCore -o "$test_dir/probe"
        MTL_DEBUG_LAYER=1 perl -e 'alarm 30; exec @ARGV' "$test_dir/probe"
    done
done
for memory in separate shared; do
    flags=()
    if [ "$memory" = shared ]; then flags=(-pthread -sPTHREAD_POOL_SIZE=0); fi
    emcc -std=c11 -O1 -Wall -Wextra -Werror -DSOKOL_WGPU -I "$root" --use-port=emdawnwebgpu "${flags[@]}" \
        "$root/tests/sulrim_uniforms.c" -sMODULARIZE=1 -sEXPORT_ES6=1 -sINITIAL_MEMORY=33554432 \
        -sALLOW_MEMORY_GROWTH=0 -sEXPORTED_RUNTIME_METHODS=WebGPU -sENVIRONMENT=web,worker -sASSERTIONS=1 -o "$test_dir/probe.mjs"
    echo "Uniforms: $memory Wasm memory"
    bun "$root/tests/sulrim-uniforms.mjs" "$test_dir" "$playwright"
done
