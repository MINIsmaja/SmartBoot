#include "usbdrive.h"
#include <SD.h>
#include <USB.h>
#include <USBMSC.h>
#include <tusb.h>
#include <ff.h>
#include <diskio.h>
#include <soc/rtc_cntl_reg.h>
#include <soc/usb_serial_jtag_reg.h>
#include "sdcard.h"

extern "C" esp_err_t deinit_usb_hal();  // Arduino core: releases the USB-OTG PHY

namespace {
constexpr uint8_t SD_DRIVE = 0;  // the only FatFS drive SmartBoot registers
uint32_t sectorSize = 512;
uint32_t readTotal = 0, writeTotal = 0;
volatile bool wasEjected = false;
USBMSC msc;

// The host asks for whole sectors, several per call (offset 0, size a multiple of the sector size).
int32_t onRead(uint32_t lba, uint32_t offset, void* buffer, uint32_t size) {
    if (offset || size % sectorSize) return -1;
    if (ff_disk_read(SD_DRIVE, (uint8_t*)buffer, lba, size / sectorSize) != RES_OK) return -1;
    readTotal += size;
    return size;
}

int32_t onWrite(uint32_t lba, uint32_t offset, uint8_t* buffer, uint32_t size) {
    if (offset || size % sectorSize) return -1;
    if (ff_disk_write(SD_DRIVE, buffer, lba, size / sectorSize) != RES_OK) return -1;
    writeTotal += size;
    return size;
}

bool onStartStop(uint8_t, bool start, bool loadEject) {
    if (loadEject && !start) wasEjected = true;
    return true;
}

// Points the USB pins back at the built-in serial/JTAG controller (what flashing and the dev
// console use). Same steps as the Arduino core's usb_switch_to_cdc_jtag(), which is private.
void giveBackToSerial() {
    CLEAR_PERI_REG_MASK(RTC_CNTL_USB_CONF_REG,
                        RTC_CNTL_SW_HW_USB_PHY_SEL | RTC_CNTL_SW_USB_PHY_SEL | RTC_CNTL_USB_PAD_ENABLE);
    CLEAR_PERI_REG_MASK(USB_SERIAL_JTAG_CONF0_REG, USB_SERIAL_JTAG_PHY_SEL | USB_SERIAL_JTAG_USB_PAD_ENABLE);
    SET_PERI_REG_MASK(USB_SERIAL_JTAG_CONF0_REG, USB_SERIAL_JTAG_USB_PAD_ENABLE);
}
}  // namespace

namespace usbdrive {

void init() {
    // The pin selection survives a software restart, so a drive session that ended in a restart
    // (or a crash) would leave the port dead until the battery was disconnected.
    if (READ_PERI_REG(RTC_CNTL_USB_CONF_REG) & RTC_CNTL_SW_HW_USB_PHY_SEL) giveBackToSerial();
}

bool start(String* err) {
    if (!sdcard::mount()) {
        if (err) *err = "No SD card found";
        return false;
    }
    sectorSize = SD.sectorSize();
    if (sectorSize != 512 || !SD.numSectors()) {
        if (err) *err = "This SD card can't be shared";
        return false;
    }
    readTotal = writeTotal = 0;
    wasEjected = false;

    msc.vendorID("SmartBoo");
    msc.productID("SD Card");
    msc.productRevision("1.0");
    msc.onRead(onRead);
    msc.onWrite(onWrite);
    msc.onStartStop(onStartStop);
    msc.isWritable(true);
    msc.mediaPresent(true);
    if (!msc.begin(SD.numSectors(), sectorSize)) {
        if (err) *err = "USB drive could not start";
        return false;
    }
    USB.productName("SmartBoot SD Card");
    USB.begin();
    return true;
}

void stop() {
    tud_disconnect();  // the computer sees the drive go away
    delay(100);
    deinit_usb_hal();
    giveBackToSerial();
    esp_restart();
}

bool hostConnected() { return tud_ready(); }
bool ejected() { return wasEjected; }
uint32_t bytesRead() { return readTotal; }
uint32_t bytesWritten() { return writeTotal; }

}  // namespace usbdrive
