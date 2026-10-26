// Boot-rule test app (the bootloader project needs an app to build anyway). Flash it into an
// OTA app partition, select that partition in otadata, and read the USB log. It goes through
// the resets an app does on its own and checks that each one comes back to the app, not to
// SmartBoot: it restarts itself, then deep-sleeps for 3 s, then prints PASS with the reset
// reason of every run. Turning the device off and on must land in SmartBoot instead.
#include <stdio.h>
#include "esp_attr.h"
#include "esp_ota_ops.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define MAGIC 0x5B007E57u
#define MAX_RUNS 8

// RTC memory survives software restarts and deep sleep, but not power-on. Each run is
// recorded before anything else, so runs cut short by a USB reset still show up.
RTC_NOINIT_ATTR static uint32_t magic;
RTC_NOINIT_ATTR static uint32_t runs;
RTC_NOINIT_ATTR static uint8_t reasons[MAX_RUNS];

static const char *reasonName(esp_reset_reason_t r) {
    switch (r) {
        case ESP_RST_POWERON: return "power-on";
        case ESP_RST_SW: return "software";
        case ESP_RST_DEEPSLEEP: return "deep-sleep";
        case ESP_RST_PANIC: return "panic";
        case ESP_RST_INT_WDT: case ESP_RST_TASK_WDT: case ESP_RST_WDT: return "watchdog";
        case ESP_RST_BROWNOUT: return "brownout";
        case ESP_RST_USB: return "usb";
        default: return "other";
    }
}

static bool seen(esp_reset_reason_t r) {
    for (uint32_t i = 0; i < runs && i < MAX_RUNS; i++)
        if (reasons[i] == r) return true;
    return false;
}

void app_main(void) {
    if (magic != MAGIC) {
        magic = MAGIC;
        runs = 0;
    }
    if (runs < MAX_RUNS) reasons[runs] = esp_reset_reason();
    runs++;

    vTaskDelay(pdMS_TO_TICKS(2000));  // give the USB console time to reconnect

    const char *part = esp_ota_get_running_partition()->label;
    printf("BOOTTEST in %s, runs so far:", part);
    for (uint32_t i = 0; i < runs && i < MAX_RUNS; i++) printf(" %s", reasonName(reasons[i]));
    printf("\n");

    if (runs >= MAX_RUNS) {
        printf("BOOTTEST FAIL too many runs\n");
    } else if (!seen(ESP_RST_SW)) {
        printf("BOOTTEST software restart\n");
        fflush(stdout);
        vTaskDelay(pdMS_TO_TICKS(300));
        esp_restart();
    } else if (!seen(ESP_RST_DEEPSLEEP)) {
        printf("BOOTTEST deep sleep 3 s\n");
        fflush(stdout);
        vTaskDelay(pdMS_TO_TICKS(300));
        esp_sleep_enable_timer_wakeup(3 * 1000 * 1000);
        esp_deep_sleep_start();
    } else {
        printf("BOOTTEST PASS still in %s after a software restart and a deep-sleep wake\n", part);
    }
    for (;;) {
        printf("BOOTTEST alive in %s\n", part);
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}
