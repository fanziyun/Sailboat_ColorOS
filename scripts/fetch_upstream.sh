#!/usr/bin/env bash
set -euo pipefail

BASE="${1:-$(cd "$(dirname "$0")/.." && pwd)/src_upstream}"
mkdir -p "$BASE"

clone_sparse() {
  local repo="$1" branch="$2" dest="$3"
  if [ -d "$dest/.git" ]; then
    echo "exists: $dest"
    return 0
  fi
  git clone --depth 1 --filter=blob:none --sparse -b "$branch" \
    "https://github.com/OnePlusOSS/$repo.git" "$dest"
  git -C "$dest" sparse-checkout set vendor/oplus/kernel
}

clone_sparse android_kernel_modules_and_devicetree_oneplus_sm8850 \
  oneplus/sm8850_b_16.0.0_oneplus_15 "$BASE/oplus_sm8850"
clone_sparse android_kernel_modules_and_devicetree_oneplus_sm8550 \
  oneplus/sm8550_v_15.0.0_oneplus11 "$BASE/oplus_sm8550"

echo "done: $BASE"
