# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Goal

Port a Raspberry Pi rugby score display (Python/PIL) to an **Adafruit MatrixPortal S3** (YHC 5778) driving **2 chained HUB75 128×64 panels → 256×64 pixels total**. Displays live scores, results, standings, and fixtures for Top 14, Pro D2, and Champions Cup.

Full spec: `prompt_ESP.md`. Design doc: `docs/superpowers/specs/2026-04-14-rugby-esp32-design.md`. Pi reference implementation: `rugby_display/`.

---

## Hardware

| | |
|--|--|
| Board | Adafruit MatrixPortal S3 (ESP32-S3, 8MB Flash, 2MB SRAM, **no PSRAM**) |
| Display | 2× HUB75 128×64 chained → **256×64 total** |
| Framework | Arduino on PlatformIO |

**Critical:** No PSRAM — the 300KB Idalgo HTML cannot be buffered in RAM. Always stream-parse in 4KB chunks.

---

## Build System

```bash
pio run                          # build
pio run --target upload          # upload firmware
pio run --target uploadfs        # upload LittleFS (logos, HTML, config)
pio device monitor --baud 115200 # serial monitor
```

Key `platformio.ini` flags:
- Stack size for HTTP/JSON tasks: **16KB** (FreeRTOS default 2KB crashes silently)
- No `BOARD_HAS_PSRAM` / no `qio_opi` — this board has no PSRAM

---

## Architecture

### FreeRTOS task split (never mix)

| Core | Task | Stack |
|------|------|-------|
| Core 0 | `DataFetcher` (fetch + parse + NTP) | 16KB |
| Core 1 | `DisplayRenderer` (30fps DMA) | 8KB |
| Core 1 | `SceneManager` (rotation, live-priority) | 4KB |

HUB75 DMA driver is interrupt-sensitive — rendering must stay on Core 1. No network calls on Core 1.

### Memory strategy (no PSRAM)

- **Back buffer:** RGB332 (1 byte/pixel) — 256×64 = **16KB**. Convert to RGB565 on DMA swap.
- **Logos (large):** 64×64 RGB565 = 8KB each — loaded from LittleFS on demand, 2 slots in RAM (16KB total)
- **Logos (mini):** 16×16 RGB565 = 512 bytes each — Standings scene
- **HTML parsing:** stream in 4KB chunks, never hold full page in RAM
- Shared `MatchDB` protected by FreeRTOS mutex; renderer takes it briefly for snapshot only

### Filesystem (LittleFS)

Partition: 1MB app / 1MB OTA / 2MB LittleFS.

```
data/
  logos/
    toulouse.bin      ← RGB565 64×64 (Scoreboard/Fixtures)
    toulouse_sm.bin   ← RGB565 16×16 (Standings)
    ...
  config.json
  index.html          ← web UI
```

Generate logos with `tools/convert_logos.py`, upload with `pio run --target uploadfs`.

---

## Visual Style

- **Font:** Atkinson Hyperlegible (https://github.com/googlefonts/atkinson-hyperlegible), converted to Adafruit GFX `.h` files via `fontconvert`. Headers in `include/fonts/`.
- **Text over logos:** abbreviations and scores are drawn directly on top of logos — no opaque background behind text. Draw order: black fill → logos → text.
- **Colors:** winner=gold(255,200,30), loser=red(220,60,60), draw=white, live=green(0,210,80). Headers: Top14=blue, ProD2=orange, CC=purple.
- Accents stripped before display: `é→e, è→e, à→a, ç→c`, etc.

---

## Scene Layouts (256×64)

### ScoreboardScene
```
[LOGO 64×64]  TOP14 · J26           [LOGO 64×64]
              USM        RCT
                  23 - 17
                  Final      5/7
```
- Inline vs stacked: if abbrev+score text > 128px center zone → stacked mode
- Live: show minute instead of "Final"

### FixturesScene
```
[LOGO 64×64]       TOP14            [LOGO 64×64]
              USM         RCT
                Sam 18 avr
                  16:35      5/7
```
- TBD kickoff: show date only, no time

### StandingsScene (no logos)
```
TOP14  CLASSEMENT                    ← 16px header, fixed
[logo16] 1. Stade Toulousain    99   ← 16px row
[logo16] 2. ASM Clermont        88   ← 16px row
[logo16] 3. RC Toulon           71   ← 16px row (3 visible, scroll)
```
- Header 16px fixed, 3 rows × 16px = 48px scrolling area
- Continuous vertical scroll through all 14/16 teams
- Zone colors: playoffs→gold, relegation→red

---

## Data Sources

### Priority
1. **Idalgo (ladepeche.fr)** — results, live, fixtures. Primary. ~300KB HTML, stream-parsed.
2. **WorldRugby PulseLive** — fixtures fallback only. ~12 min live delay.
3. **LNR (lnr.fr)** — standings only.

### Critical gotchas

**Idalgo:**
- Headers required: `User-Agent: Mozilla/5.0 (compatible)` + `Accept-Language: fr-FR,fr;q=0.9` — else HTTP 403
- `data-status`: `"0"`=scheduled, `"1"`/`"3"`=finished, `"7"`=live, `"2"`=treat as live
- Future matchdays: detect decreasing→increasing transition in `journee-{n}` links

**WorldRugby:**
- `startDate == endDate` → HTTP 400 — always use `today-1` to `today+1`
- TBD kickoff: `00:00:00 UTC` → Paris ≤ 02:00 → don't display time

**LNR:**
- Standings table is inside Vue `<template #first-tab>` — replace `<template` with `<div` before parsing

---

## WiFi Credentials

Create `include/credentials.h` (gitignored):
```cpp
#pragma once
#define WIFI_SSID     "MyNetwork"
#define WIFI_PASSWORD "MyPassword"
```
Stored to NVS on first boot. Editable via web UI after that.

---

## Key Implementation Notes

### Timezone
```cpp
configTime(0, 0, "pool.ntp.org");
setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
tzset();
```
Store all kickoff times as UTC epoch. Convert to Paris local only at display with `localtime()`.

### WiFi resilience
Event handler on `WIFI_EVENT_STA_DISCONNECTED` → `WiFi.reconnect()` with exponential backoff.

### Team name normalization
Full mapping in `include/TeamData.h` (ported from `rugby_display/data/team_data.py`). WorldRugby abbreviations are unreliable — always override from local table.

---

## Pi Reference

- `rugby_display/data/idalgo.py` — Idalgo parsing logic (source of truth)
- `rugby_display/data/worldrugby.py` — WorldRugby fallback
- `rugby_display/data/team_data.py` — all 45 clubs: names, abbreviations, logo slugs
- `rugby_display/display/scenes/scoreboard.py` — inline/stacked layout
- `rugby_display/display/scenes/fixtures.py` — fixtures with Paris dates
- `rugby_display/display/scenes/standings.py` — standings layout
