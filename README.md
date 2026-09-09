# pico_wifi

A small C++ Pico SDK Wi-Fi connection manager intended for Raspberry Pi Pico 2 W applications using FreeRTOS and the full lwIP `NO_SYS=0` architecture.

The library deliberately does **not** create its own FreeRTOS task. The application owns the task and calls `process()` periodically. This keeps task lifetime, priority, stack sizing and shutdown policy under application control.

## Features

- Pico 2 W station-mode Wi-Fi
- asynchronous connection attempts
- automatic reconnection after link loss
- connection-attempt timeout
- configurable reconnect interval
- connection/link state queries
- IPv4 address, netmask and gateway
- DNS servers
- MAC address and access-point BSSID
- RSSI
- hostname (when supplied by lwIP)
- FreeRTOS-safe lwIP access using `cyw43_arch_lwip_begin()` / `cyw43_arch_lwip_end()`

## Important FreeRTOS architecture choice

The final application should use:

```cmake
pico_cyw43_arch_lwip_sys_freertos
```

This gives full lwIP `NO_SYS=0` integration and automatic servicing of the CYW43 driver/lwIP stack. You do not call `cyw43_arch_poll()` yourself.

The `pico_wifi` target links this architecture publicly, so a consuming FreeRTOS application only needs to provide the FreeRTOS kernel target and its normal `FreeRTOSConfig.h` / `lwipopts.h`.

## Using as a subdirectory

For example, with this repository at `lib/pico-wifi`:

```cmake
add_subdirectory(lib/pico-wifi)

target_link_libraries(your_app PRIVATE
    pico_wifi
    FreeRTOS-Kernel
    FreeRTOS-Kernel-Heap4
)
```

Your top-level application should already have imported and initialised the Pico SDK and FreeRTOS.

## Credentials

For the standalone example:

```bash
cp examples/secrets.example.h examples/secrets.h
```

Then edit `examples/secrets.h`.

Do not commit `secrets.h`.

## Build standalone example

Set `PICO_SDK_PATH` and `FREERTOS_KERNEL_PATH`, then:

```bash
cmake -S . -B build -DPICO_BOARD=pico2_w
cmake --build build -j
```

The resulting UF2 is `build/pico_wifi_example.uf2`.

## Typical FreeRTOS use

```cpp
pico_wifi::PicoWifi wifi(WIFI_SSID, WIFI_PASSWORD);

void wifi_task(void*) {
    if (!wifi.initialise()) {
        printf("WiFi init failed: %s\n", wifi.errorMessage().c_str());
        vTaskDelete(nullptr);
    }

    wifi.start();

    TickType_t last_wake = xTaskGetTickCount();

    while (true) {
        wifi.process();

        if (wifi.isConnected()) {
            // HTTPS / MQTT / socket work here, or signal another task.
        }

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(1000));
    }
}
```

With `pico_cyw43_arch_lwip_sys_freertos`, call `initialise()` after the FreeRTOS scheduler has started, from task context.
