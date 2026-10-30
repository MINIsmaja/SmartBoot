#!/usr/bin/env bash
# Build the release: one merged image (bootloader + partition table + SmartBoot) to
# flash at 0x0, plus the Windows package (flash.bat + esptool.exe). Output in dist/.
#   tools/release.sh            -> dist/SmartBoot-v<version>.bin, .sha256, -windows.zip
set -e
cd "$(dirname "$0")/.."
VERSION=$(grep -oP 'APP_VERSION "\K[^"]+' src/config.h)
NAME="SmartBoot-v$VERSION"
B=.pio/build/release
BL=bootloader/.pio/build/bootloader

pio run -e release
(cd bootloader && pio run)
mkdir -p dist
esptool --chip esp32s3 merge-bin -o "dist/$NAME.bin" \
  0x0 "$BL/bootloader.bin" 0x8000 "$B/partitions.bin" 0x10000 "$B/firmware.bin"
(cd dist && sha256sum "$NAME.bin" > "$NAME.bin.sha256")

# Windows: double-click flash.bat; esptool.exe is Espressif's standalone build (GPL-2.0),
# downloaded once and checked against its published SHA-256.
ESPTOOL_VER=v5.6.0
ESPTOOL_SHA=d55a96fbeb97fd4cb05bd93f201e5c8f6518b7bfcde2fc6966fd0190fd891629
if [ ! -f tools/esptool-windows/esptool.exe ]; then
  mkdir -p tools/esptool-windows
  ZIP=tools/esptool-windows/esptool-windows.zip
  curl -sL -o "$ZIP" "https://github.com/espressif/esptool/releases/download/$ESPTOOL_VER/esptool-$ESPTOOL_VER-windows-amd64.zip"
  echo "$ESPTOOL_SHA  $ZIP" | sha256sum -c -
  python3 -I - "$ZIP" "$ESPTOOL_VER" <<'PY'
import sys, zipfile
z = zipfile.ZipFile(sys.argv[1])
open("tools/esptool-windows/esptool.exe", "wb").write(z.read("esptool-windows-amd64/esptool.exe"))
note = (f"esptool.exe is the standalone Windows build of esptool {sys.argv[2]} by Espressif Systems,\n"
        f"unmodified, from https://github.com/espressif/esptool/releases/tag/{sys.argv[2]}\n"
        "Source code: https://github.com/espressif/esptool (GPL-2.0, full text below)\n\n").encode()
open("tools/esptool-windows/ESPTOOL-LICENSE.txt", "wb").write(note + z.read("esptool-windows-amd64/LICENSE"))
PY
  rm "$ZIP"
fi
WIN=dist/windows && rm -rf "$WIN" && mkdir -p "$WIN"
cp "dist/$NAME.bin" "$WIN/"
sed "s/@IMAGE@/$NAME.bin/" tools/flash.bat > "$WIN/flash.bat"
cp tools/esptool-windows/esptool.exe tools/esptool-windows/ESPTOOL-LICENSE.txt "$WIN/"
(cd "$WIN" && rm -f "../$NAME-windows.zip" && python3 -I -m zipfile -c "../$NAME-windows.zip" flash.bat "$NAME.bin" esptool.exe ESPTOOL-LICENSE.txt)
echo "dist/$NAME.bin  dist/$NAME-windows.zip"
