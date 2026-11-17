#!/usr/bin/env bash
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
NDK="${NDK:-/h/android-ndk-r30}"
BIN="$NDK/toolchains/llvm/prebuilt/windows-x86_64/bin"
CC="$BIN/aarch64-linux-android21-clang"

if [ -x "$CC.cmd" ]; then
  CC="$CC.cmd"
fi

mkdir -p "$REPO/out"
"$CC" -static -O2 -o "$REPO/out/uinput_tap" "$REPO/tools/uinput_tap/uinput_tap.c"
echo "built: $REPO/out/uinput_tap"
