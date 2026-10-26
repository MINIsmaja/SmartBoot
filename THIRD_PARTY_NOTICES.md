# Third-party notices

SmartBoot's own code is MIT-licensed (see [LICENSE](LICENSE)). It also contains, or is built with, the
following open-source software. Thank you to everyone who made it.

- `src/partitions.*` and parts of `src/installer.cpp` are ported from M5Launcher and keep its MIT notice.
- `bootloader/bootloader_components/main/bootloader_start.c` is ESP-IDF's, with SmartBoot's power-on
  rule added, and keeps its Apache-2.0 header.

| Component | Used for | License |
|---|---|---|
| [M5Launcher](https://github.com/bmorcelli/Launcher) by bmorcelli | Partition and install engine (ported) | MIT, full text below |
| [ESP-IDF](https://github.com/espressif/esp-idf) by Espressif | The bootloader is ESP-IDF's, with a small change; system libraries | Apache-2.0 |
| [Arduino core for ESP32](https://github.com/espressif/arduino-esp32) by Espressif | Wi-Fi, networking, storage | LGPL-2.1-or-later |
| [M5Unified](https://github.com/m5stack/M5Unified) and [M5GFX](https://github.com/m5stack/M5GFX), Copyright (c) 2021 M5Stack | Display, keyboard, power | MIT |
| [M5Cardputer](https://github.com/m5stack/M5Cardputer) by M5Stack | Cardputer support | MIT |
| [IRremote](https://github.com/Arduino-IRremote/Arduino-IRremote), (c) 2009 Ken Shirriff and contributors | Used by M5Cardputer | MIT |
| [ArduinoJson](https://arduinojson.org), Copyright 2014-2026 Benoit Blanchon | Reading the catalog | MIT |
| mbedTLS, lwIP, FreeRTOS, LittleFS, FatFs | Parts of ESP-IDF and the Arduino core | Apache-2.0, BSD-3-Clause, MIT, BSD-3-Clause, FatFs license |

The Windows package also contains **esptool.exe**, the unmodified standalone build of
[esptool](https://github.com/espressif/esptool) v5.6.0 by Espressif Systems (GPL-2.0). Its license
is included in the package as `ESPTOOL-LICENSE.txt`; its source code is at the link above.

The Arduino core for ESP32 is licensed under the LGPL. Its source code is available at
<https://github.com/espressif/arduino-esp32>, and SmartBoot's full source is in this repository, so
you can rebuild SmartBoot with a modified version of it.

Firmware installed through SmartBoot belongs to its authors and comes directly from their
download links; SmartBoot doesn't host or modify it.

## M5Launcher license

```
MIT License

Copyright (c) 2023 shikarunochi

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```
