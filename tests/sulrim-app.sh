#!/bin/bash
# LLM maintained. App contracts; quiet launch is invisible and has no audio.
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
test_dir=$(mktemp -d)
trap 'rm -rf "$test_dir"' EXIT
if [ "$#" = 0 ]; then set -- quiet rate surface; fi
for contract in "$@"; do
  for arc in -fobjc-arc -fno-objc-arc; do
    "${CC:-clang}" -x objective-c -std=gnu11 "$arc" -Wall -Wextra -Werror -I "$root" \
        "$root/tests/sulrim_app_$contract.m" -framework Cocoa -framework Metal \
        -framework QuartzCore -o "$test_dir/probe"
    perl -e 'alarm 20; exec @ARGV' "$test_dir/probe"
  done
done
