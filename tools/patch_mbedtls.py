"""
Extra script for PlatformIO — patches mbedtls ssl_client.cpp to force
max fragment length = 4096 bytes. This reduces TLS input/output buffers
from 2×16KB to 2×4KB, saving ~24KB of heap per handshake.

Registered in platformio.ini via:
    extra_scripts = tools/patch_mbedtls.py

Runs automatically before every build. Idempotent (safe to run multiple times).
"""

import os
import glob


def patch_ssl_client(source, target, env):
    # PATCH RE-ENABLED: esp_sntp_stop() fixes the fatal alert root cause
    # (SNTP thread was corrupting lwIP state, not the max_frag_len extension)
    project_dir = env.subst("$PROJECT_DIR")

    # Search for ssl_client.cpp inside platformio packages (project-local or global)
    home_dir = os.path.expanduser("~")
    search_patterns = [
        os.path.join(
            project_dir, ".pio", "packages", "framework-arduinoespressif32",
            "*", "libraries", "WiFiClientSecure", "src", "ssl_client.cpp"
        ),
        os.path.join(
            project_dir, ".pio", "packages", "framework-arduinoespressif32",
            "libraries", "WiFiClientSecure", "src", "ssl_client.cpp"
        ),
        os.path.join(
            home_dir, ".platformio", "packages", "framework-arduinoespressif32",
            "*", "libraries", "WiFiClientSecure", "src", "ssl_client.cpp"
        ),
        os.path.join(
            home_dir, ".platformio", "packages", "framework-arduinoespressif32",
            "libraries", "WiFiClientSecure", "src", "ssl_client.cpp"
        ),
    ]

    ssl_client_path = None
    for pattern in search_patterns:
        matches = glob.glob(pattern)
        if matches:
            ssl_client_path = matches[0]
            break

    if not ssl_client_path or not os.path.isfile(ssl_client_path):
        print("WARNING: ssl_client.cpp not found — TLS patch NOT applied")
        return

    with open(ssl_client_path, "r") as f:
        content = f.read()

    # Already patched?
    if "MBEDTLS_SSL_MAX_FRAG_LEN_4096" in content:
        print("TLS patch already applied to ssl_client.cpp")
        return

    old_block = (
        "    if ((ret = mbedtls_ssl_config_defaults(&ssl_client->ssl_conf,\n"
        "                                           MBEDTLS_SSL_IS_CLIENT,\n"
        "                                           MBEDTLS_SSL_TRANSPORT_STREAM,\n"
        "                                           MBEDTLS_SSL_PRESET_DEFAULT)) != 0) {\n"
        "        return handle_error(ret);\n"
        "    }"
    )

    new_block = (
        "    if ((ret = mbedtls_ssl_config_defaults(&ssl_client->ssl_conf,\n"
        "                                           MBEDTLS_SSL_IS_CLIENT,\n"
        "                                           MBEDTLS_SSL_TRANSPORT_STREAM,\n"
        "                                           MBEDTLS_SSL_PRESET_DEFAULT)) != 0) {\n"
        "        return handle_error(ret);\n"
        "    }\n"
        "    mbedtls_ssl_conf_max_frag_len(&ssl_client->ssl_conf, MBEDTLS_SSL_MAX_FRAG_LEN_4096);"
    )

    if old_block not in content:
        print("WARNING: TLS patch pattern not found in ssl_client.cpp — patch NOT applied")
        return

    content = content.replace(old_block, new_block)

    with open(ssl_client_path, "w") as f:
        f.write(content)

    print("TLS patch applied to ssl_client.cpp: max_frag_len=4096")


# Hook: run before the program build starts
Import("env")
env.AddPreAction("buildprog", patch_ssl_client)
