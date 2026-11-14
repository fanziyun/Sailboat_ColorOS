#!/usr/bin/env bash
set -euo pipefail

KDIR=/home/fanziyun/ddk-android13-5.15-20260313/opt/ddk/kdir/android13-5.15
CLANG=/home/fanziyun/ddk-android13-5.15-20260313/opt/ddk/clang/clang-r450784e/bin
export PATH="$CLANG:$PATH"

make -C "$KDIR" M=/mnt/h/Sailboat_ColorOS/tools/slotprobe \
  ARCH=arm64 LLVM=1 PAHOLE=true modules
