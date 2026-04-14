# Design Spec — Rugby ESP32 Display

**Date:** 2026-04-14  
**Statut:** Approuvé

---

## Contexte

Port du projet Rugby Display (Raspberry Pi / Python) vers un **Adafruit MatrixPortal S3** (YHC 5778) pilotant **2 panneaux HUB75 128×64 chaînés → 256×64 pixels**.

Référence de logique : `rugby_display/` (projet Pi dans ce repo).  
Spec complète : `prompt_ESP.md`.

---

## Hardware

| Composant | Détail |
|-----------|--------|
| MCU | ESP32-S3, 8MB Flash, 2MB SRAM, **pas de PSRAM** |
| Board | Adafruit MatrixPortal S3 (connecteur HUB75 intégré) |
| Panneaux | 2× HUB75 128×64, chaînés → **256×64 total** |
| Framework | Arduino sur PlatformIO |

**Contrainte mémoire critique :** pas de PSRAM → le HTML Idalgo (~300KB) ne peut pas être chargé en entier. Parsing en streaming par chunks de 4KB.

---

## Structure des fichiers

```
Rugby-ESP32/
├── platformio.ini
├── include/
│   ├── credentials.h        ← gitignored — WIFI_SSID, WIFI_PASSWORD
│   ├── config.h             ← constantes HW (pins MatrixPortal S3, résolution, timings)
│   ├── MatchData.h          ← structs partagées (MatchData, StandingEntry, CompetitionData)
│   └── TeamData.h           ← table noms/abréviations/slugs logos (45 clubs)
├── src/
│   ├── main.cpp             ← setup(), déclaration tâches FreeRTOS uniquement
│   ├── data/
│   │   ├── IdalgoParser.cpp/h   ← parsing HTML ladepeche.fr (stream 4KB chunks)
│   │   ├── WorldRugbyAPI.cpp/h  ← fallback JSON fixtures
│   │   ├── LNRParser.cpp/h     ← parsing classement lnr.fr
│   │   ├── DataFetcher.cpp/h   ← task Core 0 : orchestration fetches + NTP
│   │   └── MatchDB.cpp/h       ← stockage RAM + persistance LittleFS JSON
│   ├── display/
│   │   ├── DisplayManager.cpp/h    ← init HUB75, double buffer, swap DMA
│   │   ├── Scene.h                 ← classe abstraite Scene
│   │   ├── ScoreboardScene.cpp/h   ← résultats + live
│   │   ├── FixturesScene.cpp/h     ← prochains matchs
│   │   ├── StandingsScene.cpp/h    ← classement scroll vertical
│   │   └── SceneManager.cpp/h      ← rotation, live-priority
│   └── web/
│       ├── WebServer.cpp/h     ← ESPAsyncWebServer : /status /config /restart /next-scene
│       └── data/index.html     ← page de contrôle (uploadée en LittleFS)
├── data/                    ← LittleFS : logos RGB565 + config.json + index.html
│   └── logos/
│       ├── toulouse.bin     ← RGB565 64×64 (Scoreboard/Fixtures)
│       ├── toulouse_sm.bin  ← RGB565 12×12 (Standings)
│       └── ...
└── tools/
    └── convert_logos.py     ← script PC : PNG/URL → RGB565 .bin (64×64 et 12×12)
```

---

## Architecture FreeRTOS

```
Core 0                              Core 1
──────────────────────────────       ──────────────────────────────
DataFetcher (16KB stack)             DisplayRenderer (8KB stack)
  ├─ NTP sync (toutes les heures)    SceneManager (4KB stack)
  ├─ Idalgo fetch (toutes les 10min)
  ├─ LNR fetch (toutes les 30min)
  └─ WorldRugby fetch (fallback)
       ↓ mutex FreeRTOS ↓                  ↑ mutex FreeRTOS ↑
              MatchDB (RAM)
```

**Règle absolue :** le driver DMA HUB75 tourne sur Core 1. Aucun appel réseau sur Core 1.

### Flux de données

```
[HTTP stream] → [buffer 4KB] → [IdalgoParser] → [MatchDB] → [Scene] → [FrameBuffer DMA]
```

### Mutex et double buffer

- `MatchDB` protégé par `SemaphoreHandle_t mutex`
- `DisplayManager` maintient un double buffer : backbuffer rendu sur Core 1, swap atomique vers DMA
- Le renderer ne prend jamais le mutex plus de ~1ms (lecture snapshot rapide)

---

## Sources de données

### Priorité

1. **Idalgo** (ladepeche.fr) — résultats, live, fixtures — principal
2. **WorldRugby PulseLive** — fixtures uniquement — fallback
3. **LNR** (lnr.fr) — classements Top14 + ProD2 uniquement

### Idalgo — points critiques

- Headers HTTP obligatoires : `User-Agent: Mozilla/5.0 (compatible)` + `Accept-Language: fr-FR,fr;q=0.9`
- `data-status` : `"0"`=prévu, `"1"`/`"3"`=terminé, `"7"`=live, `"2"`=traiter comme live
- Parsing stream : `HTTPClient` + buffer circulaire 4KB + `strstr` sur attributs-clés
- Journées futures : détecter transition décroissant→croissant dans les liens `journee-{n}`

### WorldRugby — points critiques

- `startDate == endDate` → HTTP 400 : utiliser `today-1` à `today+1`
- Fixtures sans horaire : `00:00:00 UTC` → en heure Paris ≤ 02:00 → TBD, ne pas afficher l'heure
- Délai live ~12 min → jamais utilisé pour scores live

### LNR classement — point critique

- Contenu dans `<template #first-tab>` (Vue.js) : remplacer `<template` par `<div` avant parsing

---

## Scènes d'affichage

### Dimensions de référence (256×64)

- Logo grand : 64×64 px (Scoreboard, Fixtures)
- Logo mini : 16×16 px (Standings)
- Zone centrale Scoreboard/Fixtures : x=64 à x=191 (128px), pleine hauteur

### Police — Atkinson Hyperlegible

Source : https://github.com/googlefonts/atkinson-hyperlegible  
Conversion via `fontconvert` (Adafruit GFX) depuis les fichiers TTF.

Tailles générées (headers `.h` dans `include/fonts/`) :
- `AtkinsonHyperlegible8pt7b.h` — noms d'équipes, labels
- `AtkinsonHyperlegible10pt7b.h` — date/heure fixtures
- `AtkinsonHyperlegible16pt7b.h` — scores (grand)
- `AtkinsonHyperlegibleBold16pt7b.h` — scores en gras

### Style visuel — texte superposé aux logos

Les abréviations/scores sont dessinés **directement par-dessus le logo**, sans fond opaque. Ordre de rendu par couche :
1. Fond noir
2. Logos RGB565
3. Textes (scores, abréviations, dates) — aucun `fillRect` derrière

Ce chevauchement intentionnel reproduit exactement le style des captures Pi.

### Back buffer 8 bits (optimisation mémoire)

Le driver DMA HUB75 travaille en RGB565 — format imposé par le hardware.  
Le back buffer de rendu est maintenu en **RGB332** (1 byte/pixel) :
- Back buffer : 256 × 64 × 1 = **16KB**
- Conversion RGB332 → RGB565 lors du swap (macro inline, ~1ms)
- Économie : 16KB vs 32KB pour un back buffer RGB565

```cpp
inline uint16_t rgb332_to_rgb565(uint8_t c) {
    uint8_t r = (c & 0xE0);         // 3 bits → 8 bits
    uint8_t g = (c & 0x1C) << 3;
    uint8_t b = (c & 0x03) << 6;
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}
```

### ScoreboardScene

```
┌──────────────────┬──────────────────────────────────────────┬──────────────────┐
│                  │         TOP14 · J26   (header, centré)   │                  │
│   LOGO HOME      │  USM              RCT                     │   LOGO AWAY      │
│   64×64          │        23   -   17                        │   64×64          │
│                  │          Final (ou 45')          5/7      │                  │
└──────────────────┴──────────────────────────────────────────┴──────────────────┘
```

**Logique inline/stacked :** si `text_w(abbrev_home) + score_w + text_w(abbrev_away) > 128px` → mode stacked (abréviations au-dessus de leur score).

**Couleurs :**
- Gagnant : or `(255, 200, 30)`
- Perdant : rouge `(220, 60, 60)`
- Nul : blanc
- Live : vert `(0, 210, 80)`
- Headers : Top14=bleu, ProD2=orange, Champions Cup=violet

**Compteur `n/total`** : bas droite absolu, toujours visible.

### FixturesScene

```
┌──────────────────┬──────────────────────────────────────────┬──────────────────┐
│                  │              TOP14   (header)            │                  │
│   LOGO HOME      │  USM        Sam 18 avr        RCT        │   LOGO AWAY      │
│   64×64          │                  16:35                    │   64×64          │
│                  │                                   5/7     │                  │
└──────────────────┴──────────────────────────────────────────┴──────────────────┘
```

- Si kickoff TBD : afficher la date seule, pas l'heure
- Heure toujours en heure locale Paris (après `setenv TZ`)

### StandingsScene

```
┌──────────────────────────────────────────────────────────────────────────────────┐
│  TOP14  CLASSEMENT                                              (header fixe 16px)│
│ [logo]  1. Stade Toulousain                                              99      │ ← 16px
│ [logo]  2. ASM Clermont                                                  88      │ ← 16px
│ [logo]  3. RC Toulon                                                     71      │ ← 16px
│                         ↑ scroll continu vers le haut ↑                          │
└──────────────────────────────────────────────────────────────────────────────────┘
```

- Header 16px fixe pendant le scroll
- 3 lignes visibles à la fois (3 × 16px = 48px)
- Logo mini 16×16 par ligne, rang, nom complet, points alignés à droite
- Couleurs zones : top playoffs=or, relégation=rouge, reste=blanc
- Vitesse scroll : ~1px/2 frames

### Mode live-priority

Quand un match est en cours : SceneManager reste sur ScoreboardScene, poll 30s. Grace period 5 min après dernière détection live.

---

## Logos

### Stockage

Deux tailles par club dans LittleFS `/logos/` :
- `{slug}.bin` : RGB565, 64×64, ~8KB — Scoreboard et Fixtures
- `{slug}_sm.bin` : RGB565, 16×16, ~512 bytes — Standings

Logos chargés **à la demande** depuis LittleFS (2 slots grands en RAM = 2 × 8KB = 16KB max simultanément pour Scoreboard/Fixtures).

### Génération (script PC)

`tools/convert_logos.py` : télécharge depuis LNR CDN / EPCR CDN / Wikipedia, auto-crop transparence, resize LANCZOS, composite fond noir, encode RGB565 big-endian.

Upload : `pio run --target uploadfs`

---

## Credentials WiFi

Fichier `include/credentials.h` (gitignored) :
```cpp
#pragma once
#define WIFI_SSID     "MonReseau"
#define WIFI_PASSWORD "MonMotDePasse"
```

Au 1er boot : stockés en NVS Preferences. Modifiables via web UI sans recompilation.

---

## Config persistée (NVS Preferences)

```
wifi_ssid, wifi_password
brightness          (0–255)
comp_top14          (bool, défaut: true)
comp_prod2          (bool, défaut: true)
comp_cc             (bool, défaut: true)
scene_scores        (bool, défaut: true)
scene_fixtures      (bool, défaut: true)
scene_standings     (bool, défaut: true)
```

---

## Web UI (Phase 4)

Routes ESPAsyncWebServer :
- `GET /status` → JSON état complet
- `GET/POST /config` → luminosité, compétitions, scènes
- `POST /restart` → `ESP.restart()`
- `GET /next-scene` → avance manuellement la scène
- `GET /preview` → framebuffer courant en BMP raw

Page HTML servie depuis LittleFS, contrôles via fetch JS.

---

## Robustesse

- Task watchdog sur DataFetcher (redémarre si pas de fetch en 5 min)
- Données persistées en JSON dans LittleFS après chaque fetch réussi → fonctionne hors ligne
- WiFi : event handler `WIFI_EVENT_STA_DISCONNECTED` → reconnect avec backoff exponentiel
- TLS : `ESP_CERTS_TLS_BUNDLE` en production
- OTA : `ArduinoOTA` + upload HTTP via web UI

---

## Normalisation noms d'équipes

Table hardcodée dans `TeamData.h` — même mapping que `rugby_display/data/team_data.py`.  
Les abréviations WorldRugby sont ignorées (ex: "TOL" → override "RCT").  
Accents strippés avant affichage : `é→e`, `è→e`, `à→a`, `ç→c`, etc.

---

## Phases d'implémentation

| Phase | Contenu |
|-------|---------|
| 1 | PlatformIO + HUB75 + WiFi + NTP + LittleFS + double buffer |
| 2 | Stream-parsing Idalgo + WorldRugby fallback + LNR classement |
| 3 | ScoreboardScene + FixturesScene + StandingsScene + live-priority |
| 4 | Web UI ESPAsyncWebServer |
| 5 | Watchdog + OTA + persistance hors-ligne |
