#pragma once
#include <Arduino.h>

// USB drive mode: the microSD card shows up as a drive on the computer. The USB port stops being
// the serial/flash port while this is on, and SmartBoot must not touch the card meanwhile.
// Leaving drive mode restarts the device, which also gives the card back to SmartBoot cleanly.
namespace usbdrive {

void init();                             // at boot: take the USB port back if a drive session left it

bool start(String* err);                 // false when there is no card to share
void stop();                             // hands the port back and restarts the device; no return

bool hostConnected();                    // the computer has the drive open
bool ejected();                          // the computer ejected the drive (safe to leave)
uint32_t bytesRead();                    // by the computer, since start()
uint32_t bytesWritten();

}  // namespace usbdrive
