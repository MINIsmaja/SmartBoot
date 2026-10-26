# Developing SmartBoot

SmartBoot is an Arduino (ESP-IDF 5.5.5 based) PlatformIO project for the M5Stack Cardputer ADV,
plus a small ESP-IDF project for its bootloader.

## Layout

```
platformio.ini        SmartBoot firmware (env cardputer-adv; env release = without the dev console)
custom_8mb.csv        the starting partition table
src/                  the firmware
bootloader/           SmartBoot's bootloader (ESP-IDF project)
tools/                flash.sh, release.sh, flash.bat (Windows), screenshot.py
docs/                 this file, screenshots
licenses/             licenses of code SmartBoot builds on
```

Main parts of `src/`:

| File | What it does |
|---|---|
| `main.cpp`, `nav.cpp`, `ui.*`, `theme.h`, `input.*` | Screen stack, drawing helpers, keyboard |
| `screen_*.cpp` | The screens: Home, store, details/install, SD browser, Wi-Fi, settings |
| `catalog.*`, `recommended.h` | Firmware list from LauncherHub (cached on flash) and the Recommended picks |
| `net.*` | Wi-Fi, HTTPS JSON and binary downloads |
| `partitions.*` | Read, change and write the partition table; set the boot app (ported from M5Launcher) |
| `apps.*` | Installed-app registry, launch, remove |
| `installer.*` | Install manifest, space planning, download/copy, verify, table write |
| `sdcard.*` | SD card listing and `.bin` inspection |
| `settings.*` | Settings in SmartBoot's own NVS partition |
| `devtools.*` | Serial dev console (not in release builds) |

## How it works

**Boot rule.** `bootloader/` is ESP-IDF's bootloader with one change (in
`bootloader_components/main/bootloader_start.c`): a power-on reset always starts the factory app
(SmartBoot); any other reset (an app's own restart, deep-sleep wake, crash, USB) starts whatever
otadata selects. SmartBoot points otadata back at itself when it starts, and at an app to launch it.
The bootloader must be built for QIO flash, like the stock Arduino one: apps built for QIO expect it.

**Partition layout.** `custom_8mb.csv` is only the starting table: nvs (shared with apps), otadata,
factory (SmartBoot), and at the end `sbnvs` and `sbdata`, SmartBoot's private settings and files.
The 6.2 MB in between belongs to installed apps. Each app is an OTA app partition; its data
partitions (`spiffs`, `vfs`, ...) are added next to it. OTA subtypes are kept contiguous because the
bootloader picks slot `(seq - 1) % count`.

**Installing.** `installer::plan()` tries layouts in order: no changes, shrinking growable storage,
then removing as few apps as possible. `installer::install()` writes the app into free flash, verifies
the image, prepares data partitions and writes the new partition table last. If apps have to be
removed, they leave the table right before their space is first overwritten, so a pulled plug never
leaves a half-overwritten app listed. The table at 0x8000 is written through a copy of the main flash
chip whose OS hooks skip IDF's protected-region check (never `esp_flash_app_disable_protect()`, which
switches to the panic handler's no-OS hooks).

**Storage sizes.** Data without content gets min(declared, 448 KB). Data declared larger than 5 MB
(Game Station's ROM cache) gets all remaining space, is never shared, shrinks (not below 1 MB) when
another app needs room, and grows back when it's the only app left.

## Building and flashing

```sh
tools/flash.sh --table   # bootloader + starting partition table + SmartBoot (forgets installed apps)
tools/flash.sh           # bootloader + SmartBoot only; installed apps stay
tools/release.sh         # dist/: merged .bin for 0x0, checksum, Windows zip
```

`flash.sh` finds the Cardputer's port by itself (`/dev/ttyACM*`); pass a port to override.

## Dev console

Development builds answer commands on the USB serial port (115200 baud). Opening the port usually
resets the ESP32-S3, so keep one session open.

```
shot              dump the screen (used by tools/screenshot.py)
key <name>        press a key: up down left right ok back tab, or a single character
net / info        Wi-Fi state; reset reasons, uptime, lowest free heap and stack
refresh           download the firmware list now
details <q>       LauncherHub details of the first All-list match for <q>
plan <q>          show how that firmware would be installed (writes nothing)
install <q>       install it; <q> may be id:<LauncherHub fid> [name]
parts / apps      partition table / installed apps
launch <n>        start installed app n;   remove <n>   uninstall it
sd [path]         list a folder on the SD card;   sdcheck <file>   plan installing it
home              back to the Home screen
```

Screenshots: `python3 tools/screenshot.py out.png --keys ok,down` presses keys, then saves the screen.

## Gotchas

- While an installed app runs, the serial console is the app's. To get back without a power-cycle,
  erase otadata: `esptool erase-region 0xe000 0x2000`.
- Fonts are ASCII only (0x20-0x7E).
- TLS needs about 45 KB of contiguous heap; downloads run on the loop task (12 KB stack).
- `NetworkClientSecure::read()` returns -1 while no data is buffered; JSON is parsed through
  `PatientReader` in `net.cpp` for that reason.
