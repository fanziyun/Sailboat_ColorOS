#!/system/bin/sh
MODDIR=${0%/*}
LOG="$MODDIR/load.log"

insmod_one() {
  local ko="$1"
  local name
  name=$(basename "$ko")
  if insmod "$ko" 2>>"$LOG"; then
    echo "ok $name" >>"$LOG"
  else
    echo "fail $name" >>"$LOG"
  fi
}

insmod_dir() {
  local dir="$1"
  local ko
  for ko in "$dir"/*.ko; do
    [ -f "$ko" ] || continue
    insmod_one "$ko"
  done
}

mkdir -p "$MODDIR/lib/modules"
insmod_dir "$MODDIR/lib/modules"
