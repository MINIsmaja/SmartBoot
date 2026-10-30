#pragma once

// Rename the whole firmware here.
#define APP_NAME    "SmartBoot"
#define APP_VERSION "1.0.0"

// Screen layout (landscape, 240x135)
constexpr int SCREEN_W   = 240;
constexpr int SCREEN_H   = 135;
constexpr int STATUS_H   = 16;
constexpr int FOOTER_H   = 14;
constexpr int CONTENT_Y  = STATUS_H;
constexpr int CONTENT_H  = SCREEN_H - STATUS_H - FOOTER_H;
constexpr int FOOTER_Y   = SCREEN_H - FOOTER_H;

// SmartBoot's private storage partitions (custom_8mb.csv). Installed apps share the
// default "nvs" partition; these labels are never handed to them.
#define SB_NVS "sbnvs"   // settings, Wi-Fi, installed-app registry
#define SB_FS  "sbdata"  // LittleFS: catalog cache
