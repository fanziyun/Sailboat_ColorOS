#!/usr/bin/env bash
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
KO="${1:-$REPO/out/build/sched_assist/sail_sched_assist.ko}"
STAGE="$REPO/out/ksu/sailboat_oppo_perf"

[ -f "$KO" ] || { echo "missing $KO" >&2; exit 1; }

rm -rf "$STAGE"
mkdir -p "$STAGE/lib/modules"
cp "$REPO"/ksu/sailboat_oppo_perf/* "$STAGE"/
cp "$KO" "$STAGE/lib/modules/"

FB="$REPO/frameboost/dist/Sail_FrameBoost"
if [ -f "$FB" ]; then
  mkdir -p "$STAGE/bin"
  cp "$FB" "$STAGE/bin/Sail_FrameBoost"
  chmod 0755 "$STAGE/bin/Sail_FrameBoost"
  echo "staged frameboost: $FB"
fi

chmod 0755 "$STAGE"/*.sh
chmod 0644 "$STAGE/module.prop" "$STAGE/sepolicy.rule" "$STAGE"/lib/modules/*.ko

echo "staged: $STAGE"
