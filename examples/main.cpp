#include <cstdio>

#include "FreeRTOS.h"
#include "task.h"
#include "pico/stdlib.h"

#include "wifi.hpp"

#include "secrets.h"

namespace {

constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 30000;
constexpr uint32_t WIFI_RECONNECT_INTERVAL_MS = 5000;
constexpr TickType_t WIFI_PROCESS_INTERVAL = pdMS_TO_TICKS(1000);

pico_wifi::PicoWifi wifi(
    WIFI_SSID,
    WIFI_PASSWORD,
    CYW43_AUTH_WPA2_AES_PSK,
    WIFI_CONNECT_TIMEOUT_MS,
    WIFI_RECONNECT_INTERVAL_MS
);

void wifiTask(void*) {
    // For pico_cyw43_arch_lwip_sys_freertos, initialise CYW43 from a task,
    // after the scheduler has started.
    if (!wifi.initialise()) {
        printf("WiFi initialisation failed: %s\n", wifi.errorMessage().c_str());
        vTaskDelete(nullptr);
        return;
    }

    wifi.start();

    TickType_t lastWake = xTaskGetTickCount();

    while (true) {
        wifi.process();

        // Other network work can safely use wifi.isConnected() as a gate:
        //
        // if (wifi.isConnected()) {
        //     httpsClient.post(...);
        // }

        vTaskDelayUntil(&lastWake, WIFI_PROCESS_INTERVAL);
    }
}

} // namespace

int main() {
    stdio_init_all();

    sleep_ms(2000);

    printf("Pico 2 W WiFi example\n");

    if (xTaskCreate(wifiTask, "WiFi", 2048, nullptr, tskIDLE_PRIORITY + 2, nullptr) != pdPASS) {
        printf("Failed to create WiFi task\n");
        return 1;
    }

    vTaskStartScheduler();

    while (true) { tight_loop_contents(); }
}
