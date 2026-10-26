#!/usr/bin/env bash
# Flash SmartBoot into the FACTORY partition (0x10000) with SmartBoot's own bootloader
# (bootloader/: power-on always starts SmartBoot) and clear otadata, so the device
# boots SmartBoot.
#
#   tools/flash.sh [port]           update SmartBoot, keep the partition table (installed apps)
#   tools/flash.sh --table [port]   also write the starting partition table from custom_8mb.csv:
#                                   first flash, or to forget all installed apps
#
# PlatformIO's own `upload` targets the wrong offset for this layout, so use this instead.
set -e
cd "$(dirname "$0")/.."
TABLE=0
if [ "$1" = "--table" ]; then TABLE=1; shift; fi
PORT="${1:-$(command ls /dev/ttyACM* 2>/dev/null | head -1)}"  # the Cardputer's USB serial
B=.pio/build/cardputer-adv
BL=bootloader/.pio/build/bootloader

pio run
(cd bootloader && pio run)

PARTS=()
if [ "$TABLE" = 1 ]; then PARTS=(0x8000 "$B/partitions.bin"); fi
esptool --port "$PORT" --after no-reset write-flash \
  0x0      "$BL/bootloader.bin" \
  "${PARTS[@]}" \
  0x10000  "$B/firmware.bin"

# Clear otadata so the bootloader runs the factory app (SmartBoot).
esptool --port "$PORT" --after hard-reset erase-region 0xe000 0x2000

echo "Flashed SmartBoot to factory$([ "$TABLE" = 1 ] && echo " with the starting partition table"); otadata cleared."
