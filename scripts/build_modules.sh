#!/usr/bin/env bash
set -euo pipefail

MOD="${1:?usage: build_modules.sh <module dir>}"
: "${KDIR:?set KDIR to the kernel build tree}"

CC="${CC:-clang}"
CLANG_TRIPLE="${CLANG_TRIPLE:-aarch64-linux-gnu-}"
CROSS_COMPILE="${CROSS_COMPILE:-aarch64-linux-gnu-}"
CROSS_COMPILE_ARM32="${CROSS_COMPILE_ARM32:-arm-linux-gnueabi-}"

make -C "$KDIR" M="$(cd "$MOD" && pwd)" \
  ARCH=arm64 LLVM=1 CC="$CC" \
  CLANG_TRIPLE="$CLANG_TRIPLE" \
  CROSS_COMPILE="$CROSS_COMPILE" \
  CROSS_COMPILE_ARM32="$CROSS_COMPILE_ARM32" \
  modules
