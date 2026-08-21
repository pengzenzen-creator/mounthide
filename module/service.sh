#!/system/bin/sh
MODPATH=/data/adb/modules/mounthide
insmod "$MODPATH/mounthide.ko" 2>/dev/null || true
