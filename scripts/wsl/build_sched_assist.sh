#!/usr/bin/env bash
set -euo pipefail

REPO=/mnt/h/Sailboat_ColorOS
SRC="$REPO/src_upstream/oplus_sm8550/vendor/oplus/kernel/cpu/sched"
OUT="$REPO/out/build/sched_assist"
KDIR=/home/fanziyun/ddk-android13-5.15-20260313/opt/ddk/kdir/android13-5.15
CLANG=/home/fanziyun/ddk-android13-5.15-20260313/opt/ddk/clang/clang-r450784e/bin

rm -rf "$OUT"
mkdir -p "$OUT"
cp "$SRC"/sched_assist/*.c "$SRC"/sched_assist/*.h "$OUT"/
cp "$SRC"/frame_boost/frame_boost_V1/frame_group.h "$OUT"/frame_group.h
cp "$REPO"/port/sched_assist/src/*.c "$REPO"/port/sched_assist/src/*.h "$OUT"/

python3 - "$OUT" <<'PY'
import pathlib
import sys

out = pathlib.Path(sys.argv[1])

def sub(path, old, new):
    p = out / path
    text = p.read_text()
    if old not in text:
        raise SystemExit(f"anchor not found in {path}: {old!r}")
    p.write_text(text.replace(old, new, 1))

sub("sa_common.c",
    "#include <../kernel/oplus_cpu/sched/frame_boost/frame_group.h>",
    "#include \"frame_group.h\"")
sub("sa_fair.c",
    "#include <../kernel/oplus_cpu/sched/frame_boost/frame_group.h>",
    "#include \"frame_group.h\"")
sub("sa_common.h", "#define OTS_IDX\t\t\t0", "#define OTS_IDX\t\t\t2")
sub("sa_sysfs.c",
    "#include \"sa_sysfs.h\"",
    "#include \"sa_sysfs.h\"\n#include \"sailboat_coloros_nodes.h\"")
sub("sa_sysfs.c",
    "\tgoto err_creat_d_sched_assist;\n\t}\n",
    "\tgoto err_creat_d_sched_assist;\n\t}\n\n"
    "\tsailboat_coloros_nodes_proc_init(d_sched_assist);\n")
PY

cat > "$OUT/Makefile" <<'EOF'
obj-m += sail_sched_assist.o

sail_sched_assist-y += sched_assist.o
sail_sched_assist-y += sa_common.o
sail_sched_assist-y += sa_oemdata.o
sail_sched_assist-y += sa_sysfs.o
sail_sched_assist-y += sa_priority.o
sail_sched_assist-y += sa_fair.o
sail_sched_assist-y += sa_exec.o
sail_sched_assist-y += sa_jankinfo.o
sail_sched_assist-y += sailboat_coloros_nodes.o

ccflags-y += -I$(srctree)/ -I$(srctree)/kernel/sched -I$(src)
EOF

export PATH="$CLANG:$PATH"
make -C "$KDIR" M="$OUT" ARCH=arm64 LLVM=1 PAHOLE=true modules
