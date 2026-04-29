#pragma once
#include <WiFiClientSecure.h>
#include "mbedtls/ssl.h"

/**
 * WiFiClientSecure with max fragment length = 4096 bytes.
 * This advertises smaller TLS records to the server (max_frag_len),
 * but does NOT reduce the internal I/O buffer sizes allocated by
 * mbedtls_ssl_setup() — those remain at 16KB each (input+output=32KB)
 * because the ESP32 SDK precompiles mbedTLS with
 * CONFIG_MBEDTLS_SSL_MAX_CONTENT_LEN=16384 and disables
 * VARIABLE_BUFFER_LENGTH.
 *
 * The real heap savings come from the server sending smaller records,
 * which avoids reallocation spikes during large transfers.
 * For the actual handshake to succeed, a contiguous ~48-55KB SRAM
 * block is still required (see AGENTS.md Bug 15).
 */
class WiFiClientSecureSmall : public WiFiClientSecure {
public:
    int connect(IPAddress ip, uint16_t port) override {
        setMaxFragLen();
        return WiFiClientSecure::connect(ip, port);
    }
    int connect(IPAddress ip, uint16_t port, int32_t timeout) override {
        setMaxFragLen();
        return WiFiClientSecure::connect(ip, port, timeout);
    }
    int connect(const char* host, uint16_t port) override {
        setMaxFragLen();
        return WiFiClientSecure::connect(host, port);
    }
    int connect(const char* host, uint16_t port, int32_t timeout) override {
        setMaxFragLen();
        return WiFiClientSecure::connect(host, port, timeout);
    }

private:
    void setMaxFragLen() {
        if (sslclient) {
            mbedtls_ssl_conf_max_frag_len(&sslclient->ssl_conf, MBEDTLS_SSL_MAX_FRAG_LEN_4096);
        }
    }
};
