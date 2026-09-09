#pragma once

#include <cstdint>
#include <string>

#include "pico/cyw43_arch.h"

namespace pico_wifi {

struct NetworkInfo {
    std::string ssid;
    std::string ipAddress;
    std::string netmask;
    std::string gateway;
    std::string dns1;
    std::string dns2;
    std::string macAddress;
    std::string bssid;
    std::string hostname;
    int32_t rssiDbm = 0;
    int linkStatus = CYW43_LINK_DOWN;
};

class PicoWifi {
public:
    PicoWifi(
        std::string ssid,
        std::string password,
        uint32_t auth = CYW43_AUTH_WPA2_AES_PSK,
        uint32_t connectionTimeoutMs = 30000,
        uint32_t reconnectIntervalMs = 5000
    );

    ~PicoWifi() = default;

    PicoWifi(const PicoWifi&) = delete;
    PicoWifi& operator=(const PicoWifi&) = delete;

    // With pico_cyw43_arch_lwip_sys_freertos, call initialise() from a
    // FreeRTOS task after the scheduler has started.
    bool initialise();
    void deinitialise();

    // Starts an asynchronous connection attempt. process() completes the
    // state machine and automatically retries after failures/disconnections.
    bool start();
    void process();

    bool isInitialised() const;
    bool isConnected() const;
    bool isConnecting() const;

    int linkStatus() const;
    const char* linkStatusString() const;

    NetworkInfo networkInfo() const;
    void printNetworkInfo() const;

    const std::string& errorMessage() const;

private:
    bool startConnectionAttempt();
    void scheduleReconnect();
    void handleConnected();
    void setError(const std::string& message, int errorCode = 0);

    static const char* linkStatusString(int status);
    static std::string formatMac(const uint8_t mac[6]);

    std::string ssid_;
    std::string password_;
    uint32_t auth_;
    uint32_t connectionTimeoutMs_;
    uint32_t reconnectIntervalMs_;

    bool initialised_ = false;
    bool connecting_ = false;
    bool connectedReported_ = false;

    uint64_t connectionStartedMs_ = 0;
    uint64_t nextReconnectMs_ = 0;

    std::string errorMessage_;
};

} // namespace pico_wifi
