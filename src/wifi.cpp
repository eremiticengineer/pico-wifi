#include "wifi.hpp"

#include <cstdio>
#include <utility>

#include "lwip/dns.h"
#include "lwip/ip4_addr.h"
#include "lwip/ip_addr.h"
#include "lwip/netif.h"
#include "pico/stdlib.h"

namespace pico_wifi {

namespace {

uint64_t nowMs() {
    return to_ms_since_boot(get_absolute_time());
}

std::string ipv4ToString(const ip4_addr_t* address) {
    char buffer[IP4ADDR_STRLEN_MAX] {};

    if (address == nullptr || ip4addr_ntoa_r(address, buffer, sizeof(buffer)) == nullptr) {
        return {};
    }

    return buffer;
}

std::string ipToString(const ip_addr_t* address) {
    char buffer[IPADDR_STRLEN_MAX] {};

    if (address == nullptr || ipaddr_ntoa_r(address, buffer, sizeof(buffer)) == nullptr) {
        return {};
    }

    return buffer;
}

} // namespace

PicoWifi::PicoWifi(
    std::string ssid,
    std::string password,
    uint32_t auth,
    uint32_t connectionTimeoutMs,
    uint32_t reconnectIntervalMs
) :
    ssid_(std::move(ssid)),
    password_(std::move(password)),
    auth_(auth),
    connectionTimeoutMs_(connectionTimeoutMs),
    reconnectIntervalMs_(reconnectIntervalMs) {
}

bool PicoWifi::initialise() {
    errorMessage_.clear();

    if (initialised_) {
        return true;
    }

    const int result = cyw43_arch_init();

    if (result != 0) {
        setError("cyw43_arch_init() failed", result);
        return false;
    }

    cyw43_arch_enable_sta_mode();

    initialised_ = true;
    connecting_ = false;
    connectedReported_ = false;
    nextReconnectMs_ = 0;

    return true;
}

void PicoWifi::deinitialise() {
    if (!initialised_) {
        return;
    }

    cyw43_arch_disable_sta_mode();
    cyw43_arch_deinit();

    initialised_ = false;
    connecting_ = false;
    connectedReported_ = false;
}

bool PicoWifi::start() {
    if (!initialised_) {
        setError("WiFi has not been initialised");
        return false;
    }

    if (isConnected()) {
        handleConnected();
        return true;
    }

    nextReconnectMs_ = 0;
    return startConnectionAttempt();
}

void PicoWifi::process() {
    if (!initialised_) {
        return;
    }

    const int status = linkStatus();
    const uint64_t now = nowMs();

    if (status == CYW43_LINK_UP) {
        connecting_ = false;
        nextReconnectMs_ = 0;
        handleConnected();
        return;
    }

    if (connectedReported_) {
        printf("WiFi disconnected: %s\n", linkStatusString(status));
        connectedReported_ = false;
        connecting_ = false;
        scheduleReconnect();
    }

    if (connecting_) {
        const bool failed =
            status == CYW43_LINK_FAIL ||
            status == CYW43_LINK_NONET ||
            status == CYW43_LINK_BADAUTH;

        const bool timedOut = now - connectionStartedMs_ >= connectionTimeoutMs_;

        if (failed || timedOut) {
            if (timedOut) {
                setError("WiFi connection attempt timed out");
            } else {
                setError(std::string("WiFi connection failed: ") + linkStatusString(status));
            }

            printf("%s\n", errorMessage_.c_str());
            connecting_ = false;
            scheduleReconnect();
        }

        return;
    }

    if (now >= nextReconnectMs_) {
        startConnectionAttempt();
    }
}

bool PicoWifi::isInitialised() const {
    return initialised_;
}

bool PicoWifi::isConnected() const {
    return initialised_ && linkStatus() == CYW43_LINK_UP;
}

bool PicoWifi::isConnecting() const {
    return connecting_;
}

int PicoWifi::linkStatus() const {
    if (!initialised_) {
        return CYW43_LINK_DOWN;
    }

    return cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA);
}

const char* PicoWifi::linkStatusString() const {
    return linkStatusString(linkStatus());
}

NetworkInfo PicoWifi::networkInfo() const {
    NetworkInfo info {};
    info.ssid = ssid_;
    info.linkStatus = linkStatus();

    if (!initialised_) {
        return info;
    }

    uint8_t mac[6] {};
    if (cyw43_wifi_get_mac(&cyw43_state, CYW43_ITF_STA, mac) == 0) {
        info.macAddress = formatMac(mac);
    }

    if (info.linkStatus == CYW43_LINK_UP) {
        int32_t rssi = 0;
        if (cyw43_wifi_get_rssi(&cyw43_state, &rssi) == 0) {
            info.rssiDbm = rssi;
        }

        uint8_t bssid[6] {};
        if (cyw43_wifi_get_bssid(&cyw43_state, bssid) == 0) {
            info.bssid = formatMac(bssid);
        }
    }

    // Direct lwIP/netif access must be protected when using the FreeRTOS
    // CYW43 architecture because the lwIP RAW API is not thread-safe.
    cyw43_arch_lwip_begin();

    const netif* station = &cyw43_state.netif[CYW43_ITF_STA];

    info.ipAddress = ipv4ToString(netif_ip4_addr(station));
    info.netmask = ipv4ToString(netif_ip4_netmask(station));
    info.gateway = ipv4ToString(netif_ip4_gw(station));

#if LWIP_DNS
    info.dns1 = ipToString(dns_getserver(0));
#if DNS_MAX_SERVERS > 1
    info.dns2 = ipToString(dns_getserver(1));
#endif
#endif

#if LWIP_NETIF_HOSTNAME
    const char* hostname = netif_get_hostname(station);
    if (hostname != nullptr) {
        info.hostname = hostname;
    }
#endif

    cyw43_arch_lwip_end();

    return info;
}

void PicoWifi::printNetworkInfo() const {
    const NetworkInfo info = networkInfo();

    printf("WiFi information:\n");
    printf("  SSID:       %s\n", info.ssid.c_str());
    printf("  Status:     %s\n", linkStatusString(info.linkStatus));
    printf("  IP address: %s\n", info.ipAddress.empty() ? "-" : info.ipAddress.c_str());
    printf("  Netmask:    %s\n", info.netmask.empty() ? "-" : info.netmask.c_str());
    printf("  Gateway:    %s\n", info.gateway.empty() ? "-" : info.gateway.c_str());
    printf("  DNS 1:      %s\n", info.dns1.empty() ? "-" : info.dns1.c_str());
    printf("  DNS 2:      %s\n", info.dns2.empty() ? "-" : info.dns2.c_str());
    printf("  MAC:        %s\n", info.macAddress.empty() ? "-" : info.macAddress.c_str());
    printf("  BSSID:      %s\n", info.bssid.empty() ? "-" : info.bssid.c_str());
    printf("  Hostname:   %s\n", info.hostname.empty() ? "-" : info.hostname.c_str());

    if (info.linkStatus == CYW43_LINK_UP) {
        printf("  RSSI:       %ld dBm\n", static_cast<long>(info.rssiDbm));
    } else {
        printf("  RSSI:       -\n");
    }
}

const std::string& PicoWifi::errorMessage() const {
    return errorMessage_;
}

bool PicoWifi::startConnectionAttempt() {
    errorMessage_.clear();

    printf("Connecting to WiFi: %s\n", ssid_.c_str());

    const int result = cyw43_arch_wifi_connect_async(
        ssid_.c_str(),
        password_.c_str(),
        auth_
    );

    if (result != 0) {
        setError("cyw43_arch_wifi_connect_async() failed", result);
        printf("%s\n", errorMessage_.c_str());
        connecting_ = false;
        scheduleReconnect();
        return false;
    }

    connecting_ = true;
    connectionStartedMs_ = nowMs();
    return true;
}

void PicoWifi::scheduleReconnect() {
    nextReconnectMs_ = nowMs() + reconnectIntervalMs_;
    printf("WiFi reconnect scheduled in %lu ms\n", static_cast<unsigned long>(reconnectIntervalMs_));
}

void PicoWifi::handleConnected() {
    if (connectedReported_) {
        return;
    }

    connectedReported_ = true;
    errorMessage_.clear();

    printf("WiFi connected\n");
    printNetworkInfo();
}

void PicoWifi::setError(const std::string& message, int errorCode) {
    errorMessage_ = message;

    if (errorCode != 0) {
        errorMessage_ += " (" + std::to_string(errorCode) + ")";
    }
}

const char* PicoWifi::linkStatusString(int status) {
    switch (status) {
        case CYW43_LINK_DOWN:
            return "down";
        case CYW43_LINK_JOIN:
            return "joining";
        case CYW43_LINK_NOIP:
            return "connected, waiting for IP";
        case CYW43_LINK_UP:
            return "up";
        case CYW43_LINK_FAIL:
            return "connection failed";
        case CYW43_LINK_NONET:
            return "network not found";
        case CYW43_LINK_BADAUTH:
            return "authentication failed";
        default:
            return "unknown";
    }
}

std::string PicoWifi::formatMac(const uint8_t mac[6]) {
    char buffer[18] {};
    std::snprintf(
        buffer,
        sizeof(buffer),
        "%02X:%02X:%02X:%02X:%02X:%02X",
        mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]
    );
    return buffer;
}

} // namespace pico_wifi
