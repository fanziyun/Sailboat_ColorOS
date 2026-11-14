#!/usr/bin/env bash
set -euo pipefail

D=/home/fanziyun/ddk-android13-5.15-20260313
KDIR="$D/opt/ddk/kdir/android13-5.15"
CLANG="$D/opt/ddk/clang/clang-r450784e/bin"

echo "== kdir =="
ls "$KDIR" | head -20
echo "== generated =="
ls "$KDIR/include/generated" 2>&1 | head -10
echo "== config =="
ls "$KDIR/.config" 2>&1

if [ -f "$KDIR/.config" ] && [ -f "$KDIR/include/generated/autoconf.h" ]; then
  export PATH="$CLANG:$PATH"
  make -C "$KDIR" M=/mnt/h/Sailboat_ColorOS/tools/hello ARCH=arm64 LLVM=1 PAHOLE=true modules
  ls -l /mnt/h/Sailboat_ColorOS/tools/hello/hello.ko
else
  echo "DDK kdir is not prepared; skip build"
fi
