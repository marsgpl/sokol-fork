#!/bin/bash
# LLM maintained. Pinned Emdawn app in headless Chrome; no client build or audio.
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
playwright=${1:?usage: sulrim-app-webgpu.sh /absolute/path/to/playwright/index.js}
test_dir=$(mktemp -d)
trap 'rm -rf "$test_dir"' EXIT
"${EMCC:-emcc}" -std=c11 -O1 -Wall -Wextra -Werror -I "$root" --use-port=emdawnwebgpu \
    -sMODULARIZE=1 -sEXPORT_ES6=1 -sEXPORTED_RUNTIME_METHODS=WebGPU \
    -sINITIAL_MEMORY=33554432 -sALLOW_MEMORY_GROWTH=0 -sENVIRONMENT=web -sFILESYSTEM=0 \
    --no-entry "$root/tests/sulrim_app_webgpu.c" -o "$test_dir/probe.mjs"
bun "$root/tests/sulrim-app-webgpu.mjs" "$test_dir" "$playwright"
