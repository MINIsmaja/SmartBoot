#pragma once
#include <Arduino.h>

// Wi-Fi file sharing: SmartBoot makes its own hotspot and serves a small web page where a phone or
// computer can browse the SD card and add, rename, delete and download files. It works without
// any router or internet, and it isn't affected by a network that isolates its devices from each
// other. While it runs, SmartBoot's own Wi-Fi connection is paused.
namespace webshare {

struct Status {
    bool running = false;
    int devices = 0;          // phones/computers joined to the hotspot
    bool receiving = false;   // a file is arriving right now
    char file[48] = "";       // that file, or the last one received
    uint32_t fileBytes = 0;   // received so far of that file
    uint32_t totalBytes = 0;  // received since start()
    uint16_t files = 0;       // files received since start()
};

bool start(String* err);  // false when there is no SD card or the hotspot won't start
void stop();              // closes the hotspot and reconnects SmartBoot's Wi-Fi

const char* ssid();       // hotspot name, valid after start()
const char* password();   // random per start
const char* address();    // where to browse: "192.168.4.1"
Status status();

}  // namespace webshare
