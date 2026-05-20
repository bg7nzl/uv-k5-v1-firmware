#!/usr/bin/env bash
# Re-fetch DP32G030 external reference files into this directory.
set -euo pipefail
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$DIR"

fetch() {
  local url="$1" out="$2"
  echo "GET $out"
  curl -fsSL -o "$out" "$url"
}

fetch "https://raw.githubusercontent.com/Xpl0itR/dp32g030-rs/master/src/DP32G030.svd" \
  "DP32G030-extended.svd"
fetch "https://raw.githubusercontent.com/amnemonic/Quansheng_UV-K5_Firmware/0255bca35f0f4d95bd67c3c4406af798e8a8a2df/hardware/DP32G030/DP32G030.svd" \
  "DP32G030-amnemonic-original.svd"
fetch "https://raw.githubusercontent.com/Xpl0itR/dp32g030-rs/master/README.md" \
  "dp32g030-rs-README.md"
fetch "https://raw.githubusercontent.com/egzumer/uv-k5-firmware-custom/main/dp32g030.cfg" \
  "dp32g030.cfg"

echo "Done. See docs/external/dp32g030/README.md"
