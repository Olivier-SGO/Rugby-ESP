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

PlatformIO path on this machine: `~/Library/Python/3.9/bin/pio`

```bash
pio run -e matrixportal_s3              # build for hardware
pio run -e wokwi                        # build for Wokwi simulator
pio run --target upload                 # upload firmware to board
pio run --target uploadfs               # upload LittleFS (logos, index.html)
pio device monitor --baud 115200        # serial monitor
python3 tools/convert_logos.py          # regenerate logos → data/logos/*.bin
```

**Simulateur Wokwi** : `pio run -e wokwi` puis ouvrir `diagram.json` dans VS Code (extension Wokwi). Web UI sur `http://localhost:8888`. WiFi = `Wokwi-GUEST` (injecté automatiquement via build flags).

Key `platformio.ini` flags:
- Stack size for HTTP/JSON tasks: **16KB** (FreeRTOS default 2KB crashes silently)
- Include paths: `-I src -I src/data -I src/display -I src/web` (requis pour includes cross-répertoires)
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
- **Competition logos:** 56×20 × 3 + 32×12 × 3 = **5.6KB** in static arrays (`gCompLogoLg`, `gCompLogoSm`), loaded once at boot
- **HTML parsing:** stream in 4KB chunks, never hold full page in RAM
- Shared `MatchDB` protected by FreeRTOS mutex; renderer takes it briefly for snapshot only

### Filesystem (LittleFS)

Partition: 1MB app / 1MB OTA / 2MB LittleFS.

```
data/
  logos/
    toulouse.bin        ← RGB565 64×64 (Scoreboard/Fixtures)
    toulouse_sm.bin     ← RGB565 16×16 (Standings)
    comp_top14.bin      ← RGB565 56×20 (competition logo, large)
    comp_top14_sm.bin   ← RGB565 32×12 (competition logo, small)
    comp_prod2.bin / comp_prod2_sm.bin
    comp_cc.bin / comp_cc_sm.bin
    ...
  config.json
  index.html            ← web UI
```

Generate logos with `tools/convert_logos.py`, upload with `pio run --target uploadfs`.

---

## Visual Style

- **Font:** Atkinson Hyperlegible (https://github.com/googlefonts/atkinson-hyperlegible), converted to Adafruit GFX `.h` files via `fontconvert`. Headers in `include/fonts/`.
- **Text over logos:** abbreviations and scores are drawn directly on top of logos — no opaque background behind text. Draw order: black fill → logos → text.
- **Text shadow:** `Display.drawTextShadow()` — draw at (+1,+1) in 0x2104 (dark grey), then at (x,y) in main color. Used for all scores and abbreviations.
- **Competition logos:** All scene headers use real competition logo images (not text). Two sizes: 56×20 (`gCompLogoLg`) for scoreboard/fixtures, 32×12 (`gCompLogoSm`) for standings. Loaded once at boot into static buffers in `CompLogos.cpp`.
- **Colors:** winner=gold(255,200,30), loser=red(220,60,60), draw=white, live=green(0,210,80). Headers: Top14=blue, ProD2=orange, CC=purple.
- Accents stripped before display: `é→e, è→e, à→a, ç→c`, etc.

---

## Scene Layouts (256×64)

### ScoreboardScene
```
[LOGO 64×64]  [comp logo 56×20]  J26  [LOGO 64×64]
              ──────────────────────
              USM              RCT
               23      -      17
                    Final   5/7
```
- Competition logo centered at x=100, round label right of it
- Scores split: home score centered in x=64–128, away in x=128–192
- Live: show elapsed minutes instead of "Final"
- Text shadow on abbrevs and scores

### FixturesScene
```
[LOGO 64×64]  [comp logo 56×20]  J26  [LOGO 64×64]
              ──────────────────────
              USM              RCT
                   Sam 18 avr
                    16:35   5/7
```
- TBD kickoff: show date only, no time
- Text shadow on all text

### StandingsScene
```
[comp logo 32×12]  CLASSEMENT           ← 16px header, fixed
[logo16] 1. Stade Toulousain    99      ← 16px row
[logo16] 2. ASM Clermont        88      ← 16px row
[logo16] 3. RC Toulon           71      ← 16px row (3 visible, scroll)
```
- Competition logo at (2,2) in header, "CLASSEMENT" right of it
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
- Vue SPA: server sends ~200KB of JS before the standings data. Stall timeout must be **25s** (not 12s), max bytes **600KB**.

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

---

## État d'implémentation (2026-04-19)

**Firmware complet, hardware en test continu.** RAM 32.6%, Flash 53.7%. Builds clean (`matrixportal_s3` + `wokwi`).

### Nouveaux fichiers (depuis 2026-04-15)

| Fichier | Description |
|---|---|
| `src/display/CompLogos.h/cpp` | Buffers statiques partagés pour logos de compétition (56×20 et 32×12). `loadCompLogos()` appelé au boot dans `SceneManager::begin()`. `compIndex()` : "14"→0, "D2"→1, sinon→2 |
| `include/config.h` | Ajout `LOGO_COMP_W/H` (56×20) et `LOGO_COMP_SM_W/H` (32×12) |

### Scenes redesignées

- **ScoreboardScene** : header avec logo compétition 56×20, scores splitté gauche/droite avec shadow
- **FixturesScene** : même header logo compétition, abréviations + date/heure avec shadow
- **StandingsScene** : header avec logo compétition 32×12 + "CLASSEMENT"

### Bugs résolus en hardware

- **Bug 11 — Brouillage après ~10 min** : `Display.end()/begin()` dans fetch périodique → re-malloc DMA dans heap fragmenté → corruption. **Fix :** supprimé ces appels de `DataFetcher::loop()`, renderer juste suspendu/repris.
- **Bug 12 — Web UI inaccessible** : renderer (Core 1, prio 2) réécrit l'écran en <33ms pendant que `loop()` est en `delay()`. **Fix :** `vTaskSuspend(rendererHandle)` avant l'affichage IP, `vTaskResume` après 15s.
- **Bug 13 — LNR 0 standings** : timeout 12s insuffisant pour SPA Vue qui envoie ~200KB de JS avant les données. **Fix :** stall timeout→25s, max bytes→600KB.

### Flash à faire (firmware compilé, pas encore flashé au 2026-04-19)

```bash
# Fermer le moniteur série avant upload (port /dev/cu.usbmodem1101)
~/Library/Python/3.9/bin/pio run -e matrixportal_s3 --target upload
~/Library/Python/3.9/bin/pio run -e matrixportal_s3 --target uploadfs
```

### Gotchas découverts à l'implémentation

- `TeamData.h::stripAccents` : le champ `char to` doit être initialisé avec `'e'` (char literal), pas `"e"` (string literal) — erreur de compilation `-fpermissive`
- WDT : utiliser `esp_task_wdt_init(timeout_s, true)` — `esp_task_wdt_config_t` / `esp_task_wdt_reconfigure` n'existent pas dans Arduino ESP32 framework 3.x
- Logo SVG (ex: Ulster) : Pillow ne supporte pas SVG nativement → utiliser `cairosvg` + cairo via Homebrew (`/opt/homebrew/lib/`)
- Logos gitignorés (`*.bin`, `data/logos/`) — régénérer avec `python3 tools/convert_logos.py`
- `Display.end()/begin()` pendant le fetch périodique → corruption DMA — ne jamais appeler ces méthodes après le boot
- `vTaskSuspend(rendererHandle)` requis avant toute écriture display dans `loop()` (renderer Core 1 prio 2 préempte loop() prio 1 en <33ms)
