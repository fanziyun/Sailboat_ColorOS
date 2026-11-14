#!/usr/bin/env bash
set -euo pipefail

REPO=/mnt/h/Sailboat_ColorOS
SRC="$REPO/src_upstream/oplus_sm8550/vendor/oplus/kernel/cpu/sched"
OUT="$REPO/out/probe/sa"
KDIR=/home/fanziyun/ddk-android13-5.15-20260313/opt/ddk/kdir/android13-5.15
CLANG=/home/fanziyun/ddk-android13-5.15-20260313/opt/ddk/clang/clang-r450784e/bin

rm -rf "$OUT"
mkdir -p "$OUT"
cp "$SRC"/sched_assist/*.c "$SRC"/sched_assist/*.h "$OUT"/
cp "$SRC"/frame_boost/frame_boost_V1/frame_group.h "$OUT"/frame_group.h

sed -i 's|#include <../kernel/oplus_cpu/sched/frame_boost/frame_group.h>|#include "frame_group.h"|' \
	"$OUT"/sa_common.c "$OUT"/sa_fair.c
sed -i 's|^#define OTS_IDX\t\t\t0$|#define OTS_IDX\t\t\t2|' "$OUT"/sa_common.h

cat > "$OUT/Makefile" <<'EOF'
obj-m += sa_probe.o

sa_probe-y += sched_assist.o
sa_probe-y += sa_common.o
sa_probe-y += sa_oemdata.o
sa_probe-y += sa_sysfs.o
sa_probe-y += sa_priority.o
sa_probe-y += sa_fair.o
sa_probe-y += sa_exec.o
sa_probe-y += sa_jankinfo.o

ccflags-y += -I$(srctree)/ -I$(srctree)/kernel/sched -I$(src)
EOF

export PATH="$CLANG:$PATH"
make -C "$KDIR" M="$OUT" ARCH=arm64 LLVM=1 PAHOLE=true modules
