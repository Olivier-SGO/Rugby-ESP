#pragma once
#include <WiFiClientSecure.h>
#include "mbedtls/ssl.h"

/**
 * WiFiClientSecure with max fragment length = 4096 bytes.
 * This reduces the TLS input/output buffers from 2×16KB to 2×4KB,
 * saving ~24KB of heap during the handshake.
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
