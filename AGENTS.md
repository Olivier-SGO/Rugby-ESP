# AGENTS.md — Rugby ESP32 Display

Fichier de référence pour les agents de codage AI. Ce projet est un firmware embarqué C++ pour ESP32-S3. Le lecteur de ce fichier ne connaît rien du projet.

---

## Vue d'ensemble

**Rugby ESP32 Display** est le portage d'un afficheur LED rugby (originellement sur Raspberry Pi 4 / Python) vers une carte **Adafruit MatrixPortal S3** (ESP32-S3, 8MB Flash, 320KB SRAM, **2MB PSRAM QSPI**).

La carte pilote **2 panneaux HUB75 128×64 chaînés** pour une surface totale de **256×64 pixels**, affichant en temps réel :
- Scores live avec minute de jeu
- Résultats de la dernière journée
- Programme des prochains matchs (fixtures)
- Classements

Compétitions couvertes : **Top 14**, **Pro D2**, **Champions Cup**.

La documentation de conception complète se trouve dans `docs/superpowers/specs/2026-04-14-rugby-esp32-design.md` et `prompt_ESP.md`. Le projet Pi de référence est dans `rugby_display/` (même repo).

---

## Stack technique

- **Plateforme** : ESP32-S3 (dual-core Xtensa LX7)
- **Framework** : Arduino sur PlatformIO
- **Board** : `adafruit_matrixportal_esp32s3`
- **IDE/build** : PlatformIO (pas de CMake ni de Makefile natif)

### Bibliothèques principales (via `platformio.ini`)

| Bibliothèque | Usage |
|---|---|
| `mrfaptastic/ESP32 HUB75 LED MATRIX PANEL DMA Display` | Pilote DMA des panneaux LED |
| `adafruit/Adafruit GFX Library` | Primitives graphiques et fonts |
| `bblanchon/ArduinoJson@^7.0.0` | Parsing JSON (WorldRugby fallback) |
| `mathieucarbou/ESP Async WebServer` | Serveur web non-bloquant |
| `mathieucarbou/AsyncTCP` | TCP asynchrone pour le webserver |
| `paulstoffregen/Time` | Utilitaires temps |
| `adafruit/Adafruit NeoPixel` | LED RGB onboard (status boot) |

### Outils hôtes (PC de développement)

- **Python 3** + Pillow + requests — génération des logos (`tools/convert_logos.py`)
- **cairosvg** (optionnel) — conversion SVG→PNG pour certains logos
- **gcc + freetype2** — génération des fonts GFX (`tools/gen_fonts.sh`)

---

## Commandes de build

Le binaire PlatformIO sur cette machine est à `~/Library/Python/3.9/bin/pio`.

```bash
# Build
pio run -e matrixportal_s3              # firmware pour le hardware
pio run -e wokwi                        # firmware pour le simulateur Wokwi

# Flash
pio run -e matrixportal_s3 --target upload    # upload firmware (fermer le moniteur série avant)
pio run -e matrixportal_s3 --target uploadfs  # upload LittleFS (logos + index.html)

# Monitor
pio device monitor --baud 115200        # moniteur série avec decodeur d'exceptions ESP32

# Génération d'assets
python3 tools/convert_logos.py          # régénère data/logos/*.bin à partir des sources
bash tools/gen_fonts.sh                 # régénère les headers de fonts dans include/fonts/
```

### Simulateur Wokwi

1. Build avec `pio run -e wokwi`
2. Ouvrir `diagram.json` dans VS Code (extension Wokwi)
3. Web UI accessible sur `http://localhost:8888`
4. WiFi injecté automatiquement : `Wokwi-GUEST` / sans mot de passe

---

## Architecture runtime

### Séparation cœurs FreeRTOS (règle absolue)

| Cœur | Tâche | Stack | Priorité | Rôle |
|------|-------|-------|----------|------|
| Core 0 | `DataFetcher` | 16KB | 1 | Fetch HTTP, parsing HTML/JSON, NTP, WiFi |
| Core 1 | `DisplayRenderer` | 8KB | 2 | Rendu 30fps DMA HUB75 |
| Core 1 | `SceneManager` | (même tâche) | — | Rotation des scènes, live-priority |

**Contrainte hardware** : le pilote DMA HUB75 est sensible aux interruptions. **Aucun appel réseau sur Core 1.** Le renderer ne doit jamais prendre de mutex plus que ~1ms.

### Flux de données

```
[HTTP stream] → [buffer 4KB] → [IdalgoParser] → [MatchDB] → [Scene] → [BackBuffer RGB332] → [DMA swap]
```

### Mémoire (pas de PSRAM)

| Ressource | Taille | Détail |
|---|---|---|
| Back buffer | 16KB | RGB332 (1 byte/pixel), 256×64 |
| Logos grands | 16KB max | 2 slots × 64×64 RGB565 chargés depuis LittleFS à la demande |
| Logos mini | 512 bytes | 16×16 RGB565 (Standings) |
| Logos compétition | ~5.6KB | Buffers statiques `gCompLogoLg` (56×20) et `gCompLogoSm` (32×12) |
| HTML parsing | 4KB chunks | Stream parsing, jamais de buffer complet en RAM |

Le boot séquentiel libère la RAM avant `Display.begin()` :
1. Boot fetch WiFi+HTTP en tâche dédiée (16KB stack, Core 0)
2. WiFi OFF pour libérer ~134KB de SRAM contiguë
3. `Display.begin()` alloue les buffers DMA
4. Reconnexion WiFi par `DataFetcher` en tâche périodique

### Partitions flash (`partitions.csv`)

| Partition | Taille | Usage |
|---|---|---|
| nvs | 20KB | Préférences NVS (WiFi, brightness, config) |
| otadata | 8KB | Métadonnées OTA |
| app0 | 2MB | Firmware courant |
| app1 | 2MB | Firmware OTA |
| spiffs (LittleFS) | ~4MB | Logos RGB565, index.html, config.json |

---

## Organisation du code

```
include/
  config.h              # Constantes HW : pins HUB75, résolution, timings, couleurs RGB565
  MatchData.h           # Structs partagées : MatchData, StandingEntry, CompetitionData
  TeamData.h            # Table de mapping 45 clubs (Idalgo/WorldRugby/LNR → canonique/abbrev/slug)
  DisplayPrefs.h        # Structs et helpers NVS pour les préférences d'affichage
  credentials.h         # WIFI_SSID / WIFI_PASSWORD — gitignoré, créé manuellement
  fonts/                # Headers GFX générés (Atkinson Hyperlegible)

src/
  main.cpp              # setup(), loop(), tâches FreeRTOS (renderer, boot fetch)
  data/
    DataFetcher.cpp/h   # Orchestration Core 0 : WiFi, NTP, fetch périodique, OTA
    IdalgoParser.cpp/h  # Parsing stream HTML ladepeche.fr (~300KB en chunks 4KB)
    WorldRugbyAPI.cpp/h # Fallback JSON fixtures
    LNRParser.cpp/h     # Parsing classement lnr.fr (fix <template → <div)
    MatchDB.cpp/h       # Stockage RAM + persistance JSON LittleFS, protégé par mutex
  display/
    DisplayManager.cpp/h# Wrapper HUB75 DMA, double buffer RGB332→RGB565, text shadow
    Scene.h             # Classe abstraite Scene (onActivate, render, durationMs)
    SceneManager.cpp/h  # Rotation des scènes, live-priority, rebuild des slots
    ScoreboardScene.cpp/h
    FixturesScene.cpp/h
    StandingsScene.cpp/h
    LogoLoader.cpp/h    # Chargement .bin RGB565 depuis LittleFS
    CompLogos.cpp/h     # Buffers statiques pour logos de compétition
  web/
    WebServer.cpp/h     # ESPAsyncWebServer : /status /prefs /config /restart /next-scene

data/
  logos/*.bin           # Logos clubs et compétitions en RGB565 (générés par convert_logos.py)
  index.html            # Web UI de contrôle (uploadée en LittleFS)

tools/
  convert_logos.py      # Télécharge, crop, resize, encode RGB565 → .bin
  gen_fonts.sh          # Compile fontconvert Adafruit et génère les headers .h
```

---

## Conventions de code

### Langue

Les commentaires de code et la documentation technique sont principalement en **français**. Les noms de symboles (classes, fonctions, variables) sont en anglais.

### Style

- C++17 avec framework Arduino
- Headers avec `#pragma once`
- Classes singletons exposées comme variables globales (`extern DisplayManager Display;`, `extern SceneManager Scenes;`, etc.)
- Include paths cross-répertoires gérés par `-I src -I src/data -I src/display -I src/web` dans `platformio.ini`
- `build_flags` : `-DCORE_DEBUG_LEVEL=3` pour les logs Serial

### Points critiques de l'implémentation

1. **Ne jamais appeler `Display.end()/begin()` après le boot** — cela re-malloc les buffers DMA dans un heap fragmenté et provoque une corruption (Bug 11).
2. **`vTaskSuspend(rendererHandle)` requis** avant toute écriture directe sur l'affichage dans `loop()` — le renderer (Core 1, prio 2) préempte `loop()` (prio 1) en moins de 33ms (Bug 12).
3. **Stack size des tâches** : 16KB minimum pour tout ce qui fait du HTTP/JSON/TLS. Le défaut FreeRTOS (2KB) crashe silencieusement.
4. **WDT** : utiliser `esp_task_wdt_init(timeout_s, true)` — `esp_task_wdt_config_t` / `esp_task_wdt_reconfigure` n'existent pas dans le framework Arduino ESP32 3.x.
5. **Accents** : `TeamData.h::stripAccents` utilise des `char` literals (`'e'`), pas des string literals (`"e"`) — sinon erreur de compilation `-fpermissive`.
6. **SVG** : Pillow ne supporte pas SVG nativement. Utiliser `cairosvg` + cairo (Homebrew) pour les logos comme Ulster.
7. **Logos .bin** sont gitignorés (`*.bin`, `data/logos/`) — régénérer avec `python3 tools/convert_logos.py` après un clone.

---

## Sources de données

### Priorité

1. **Idalgo (ladepeche.fr)** — résultats, live, fixtures. ~300KB HTML, parsing stream.
2. **WorldRugby PulseLive** — fixtures fallback uniquement. ~12 min de délai live.
3. **LNR (lnr.fr)** — classements Top 14 + Pro D2 uniquement.

### Gotchas par source

**Idalgo :**
- Headers obligatoires : `User-Agent: Mozilla/5.0 (compatible)` + `Accept-Language: fr-FR,fr;q=0.9` — sinon HTTP 403
- `data-status` : `"0"`=prévu, `"1"`/"3"`=terminé, `"7"`=live, `"2"`=traiter comme live
- Journées futures : détecter la transition décroissant→croissant dans les liens `journee-{n}`

**WorldRugby :**
- `startDate == endDate` → HTTP 400. Toujours utiliser `today-1` à `today+1`.
- Kickoff TBD : `00:00:00 UTC` → en heure Paris ≤ 02:00 → ne pas afficher l'heure

**LNR :**
- Le tableau est dans un slot Vue `<template #first-tab>` — remplacer `<template` par `<div` avant parsing
- La SPA envoie ~200KB de JS avant les données. Stall timeout **25s** (pas 12s), max bytes **600KB**.

---

## Scènes d'affichage

Trois types de scènes, toutes en 256×64 pixels :

### ScoreboardScene
- Logos 64×64 à gauche et à droite
- Header centré avec logo compétition 56×20 + numéro de journée
- Scores splittés : domicile centré dans x=64–128, extérieur dans x=128–192
- Text shadow sur abréviations et scores
- Live : affiche les minutes écoulées au lieu de "Final"

### FixturesScene
- Même layout que Scoreboard mais avec date/heure au centre
- Kickoff TBD : date seule, pas d'heure
- Text shadow sur tout le texte

### StandingsScene
- Header fixe 16px avec logo compétition 32×12 + "CLASSEMENT"
- 3 lignes × 16px = 48px de zone scrollable
- Scroll vertical continu à ~1px/2 frames
- Zones couleur : playoffs→or, relégation→rouge

### Style visuel
- **Police** : Atkinson Hyperlegible (Google Fonts), convertie en GFX via `fontconvert`
- **Texte sur logos** : dessiné directement par-dessus, sans fond opaque. Ordre : noir → logo → texte
- **Shadow** : `Display.drawTextShadow()` dessine à (+1,+1) en gris foncé (0x2104), puis au coordonnée cible
- **Couleurs** : gagnant=or(255,200,30), perdant=rouge(220,60,60), nul=blanc, live=vert(0,210,80)
- **Headers compétition** : Top14=bleu, ProD2=orange, CC=violet
- Accents strippés avant affichage (`é→e`, `è→e`, `à→a`, `ç→c`, etc.)

---

## Web UI

Servie par `ESPAsyncWebServer` sur le port 80. Accessible via `http://rugby-display.local` (mDNS).

Routes API :
- `GET /status` — JSON : WiFi, IP, heap, uptime
- `GET /prefs` — JSON : préférences compétitions et scènes
- `POST /prefs` — met à jour les préférences (persisté en NVS)
- `POST /config` — `{brightness: N}`
- `POST /restart` — redémarrage ESP
- `GET /next-scene` — avance manuellement la scène
- `GET /preview` — framebuffer courant en BMP raw

Page HTML servie depuis LittleFS (`data/index.html`).

---

## WiFi et credentials

Créer `include/credentials.h` (gitigné) :
```cpp
#pragma once
#define WIFI_SSID     "MonReseau"
#define WIFI_PASSWORD "MonMotDePasse"
```

Au premier boot, les credentials sont stockés en NVS (`Preferences`) et la carte se reconnecte automatiquement par la suite. Modifiables via la Web UI.

Resilience WiFi : event handler sur `WIFI_EVENT_STA_DISCONNECTED` → `WiFi.reconnect()` avec backoff exponentiel.

---

## Timezone

```cpp
configTime(0, 0, "pool.ntp.org");
setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
tzset();
```

Tous les kickoffs sont stockés en epoch UTC. Conversion en heure locale Paris uniquement à l'affichage avec `localtime()`.

---

## Bugs mémoire corrigés (à ne pas réintroduire)

### Bug 12-bis — Stack overflow `renderTask` (2026-04-27)
- **Symptôme** : Crash aléatoire pendant `rebuildSlots()` ou après quelques minutes d'affichage.
- **Cause** : `renderTask` avait une stack de 4096 bytes. `SceneManager::rebuildSlots()` allouait ~3600 bytes de tableaux locaux (`MatchData live[12]`, `nonlive[12]`, `sorted[12]`) sur la stack, laissant moins de 500 bytes de marge pour la chaîne d'appel GFX.
- **Fix** : Stack passée à **10240 bytes**. Tableaux locaux déplacés en **BSS statique** (`static MatchData liveBuf[]`).
- **Validation** : `uxTaskGetStackHighWaterMark` affiche ~7948 bytes libres (stable).

### Bug 13 — ArduinoJson 7 + `heap_caps_malloc_extmem_enable` (2026-04-27)
- **Symptôme** : Crash systématique après le 3e fetch HTTP (ProD2), exactement au moment de `_db->persist()`. Le log s'arrêtait juste avant `[FETCH] done persist`.
- **Cause** : `heap_caps_malloc_extmem_enable(4096)` redirige les `malloc` >4KB vers PSRAM. ArduinoJson 7 alloue son pool initial avec un petit `malloc` (SRAM), puis fait des `realloc` pour agrandir. **`realloc` sur un bloc SRAM ne peut pas le déplacer vers PSRAM**. Quand le document JSON dépasse le plus gros bloc SRAM contigu (~42KB), `realloc` échoue et corrompt la mémoire.
- **Fix** : 
  1. Allocateur PSRAM custom (`JsonAllocator.h`) qui hérite de `ArduinoJson::Allocator`. Tous les `JsonDocument` volumineux utilisent `JsonDocument doc(&spiRamAlloc);` pour forcer l'intégralité du pool en PSRAM.
  2. `heap_caps_malloc_extmem_enable` remis avec un seuil de **32KB** (au lieu de 4KB). Cela laisse les buffers TLS de `WiFiClientSecure` (~16KB) en SRAM rapide (évite les timeouts handshake / HTTP -1), tout en redirigeant les buffers `readEntireStream` (~512KB) et les gros JSON en PSRAM.
- **Validation** : `_db->persist()` et `deserializeJson` fonctionnent sans crash. PSRAM free reste >1.5MB. Les 3 compétitions fetchées sans erreur TLS.

### Bug 14 — `DisplayManager::begin()` avec `if (_panel) end()`
- **Symptôme** : Corruption DMA potentielle si `begin()` est rappelé.
- **Cause** : `begin()` contenait `if (_panel) end();` qui libérait et ré-allouait les buffers DMA dans un heap déjà fragmenté (documenté comme Bug 11 dans les specs d'origine).
- **Fix** : Remplacé par un warning log + early return. `begin()` ne doit être appelé qu'une seule fois au boot.

---

## Tests et validation

- **Environnement matériel** : build `matrixportal_s3`, flash firmware + LittleFS
- **Simulateur** : build `wokwi`, ouvrir `diagram.json` dans VS Code avec l'extension Wokwi
- **Test hardware** : `hwTest()` dans `main.cpp` affiche R/G/B/W plein écran au boot
- **Pas de tests unitaires natifs** : l'environnement `native` de PlatformIO est commenté dans `platformio.ini` car les headers Arduino/ESP32 ne sont pas disponibles sur l'hôte

### Checklist avant flash hardware

1. Fermer le moniteur série (libère le port USB)
2. `pio run -e matrixportal_s3 --target upload`
3. `pio run -e matrixportal_s3 --target uploadfs`
4. Vérifier la LED onboard : bleu→cyan→vert (boot→fetch→display OK)

---

## OTA

`ArduinoOTA` est initialisé dans `loop()` une fois le WiFi connecté. Hostname : `rugby-display`. La partition OTA est configurée dans `partitions.csv`.

---

## Sécurité

- TLS/HTTPS utilisé pour toutes les sources de données (ladepeche.fr, lnr.fr, WorldRugby). En production, utiliser `ESP_CERTS_TLS_BUNDLE`. En développement/test, le client peut être en mode insecure.
- Credentials WiFi en NVS, pas en code source (après le premier boot).
- Web UI exposée sur le réseau local uniquement (pas d'authentification — usage domestique).

---

## ⚠️ Anti-patterns documentés

### Ne JAMAIS réutiliser `WiFiClientSecure` pour un NOUVEAU handshake
`WiFiClientSecure` ne supporte pas d'être réutilisé pour ouvrir une **nouvelle** connexion TLS après avoir fermé la précédente (`client->stop()` puis `client->connect()`). L'état interne mbedTLS reste corrompu et provoque systématiquement `HTTP -1 (connection refused)`.

**Exception** : `HTTPClient::setReuse(true)` permet de garder la connexion TCP+TLS ouverte entre plusieurs requêtes HTTP/1.1 vers le même hôte. Dans ce cas, le même `WiFiClientSecure` est utilisé, mais il n'y a qu'**un seul handshake TLS** au début de la session.

**Règles** :
- Si le serveur ne supporte pas keep-alive → créer un `WiFiClientSecure` frais (via `new`) pour **chaque** requête, et le `delete` immédiatement après `http.end()`.
- Si le serveur supporte keep-alive → créer **un seul** `WiFiClientSecure` frais au début de la session, le partager entre les requêtes via `HTTPClient::setReuse(true)`, et le `delete` à la fin de la session.

---

## 🔥 Leçons critiques de debugging (2026-04-27)

### 1. Le handshake TLS fragmente irréversiblement le heap SRAM
Chaque `WiFiClientSecure` handshake consomme ~10-15KB de bloc contigu dans le heap standard et le fragmente. Le `heap` total remonte après `delete client`, mais **`maxAllocHeap` ne se reconsolide pas**. Après un handshake, `maxAllocHeap` chute de ~47K → ~36K. Après deux handshakes, il tombe sous le seuil critique.

**Conséquence** : on ne peut faire qu'**UN seul handshake TLS** par cycle de fetch sans risquer l'échec des suivants.

**Solution** : utiliser `HTTPClient::setReuse(true)` + un seul `WiFiClientSecure` frais pour négocier **une seule connexion TLS**, puis envoyer les 3 requêtes HTTP/1.1 keep-alive dessus. Le client WiFiClientSecure n'est pas « réutilisé » au sens mbedtls (pas de nouveau handshake) — la connexion TCP+TLS reste ouverte.

### 2. Seuil critique de `maxAllocHeap` pour handshake TLS
| `maxAllocHeap` | Résultat |
|---|---|
| ≥ 45K | Handshake quasi-certain |
| 35-40K | Aléatoire / dépend de la fragmentation exacte |
| < 35K | Échec systématique (`HTTP -1`) |

**Action** : logger `maxAllocHeap` avant chaque fetch. Si < 40K, ne pas tenter de nouveau handshake.

### 3. `MBEDTLS_SSL_MAX_CONTENT_LEN=8192` est obligatoire
Sans cette définition dans `platformio.ini`, mbedTLS alloue 2 buffers de 16K = **32K contigus** pour les records TLS. Sur un heap déjà fragmenté (post-boot, post-WiFi), ces 32K ne trouvent pas de bloc contigu et le handshake échoue silencieusement.

Avec `8192`, les buffers font 8K chacun = 16K total, ce qui passe dans un bloc de 35-40K.

**Risque** : si le serveur envoie un record TLS > 8K, la connexion est fermée. À ce jour, `www.ladepeche.fr` n'en envoie pas.

### 4. `WiFiClientSecure` local (stack) peut échouer alors que `HTTPClient` réussit
Le test `WiFiClientSecure::connect(host, port)` direct peut retourner `false` dans des conditions où `HTTPClient::begin(client, url) + GET()` réussit (même `maxBlock`, même heap). `HTTPClient` configure probablement des paramètres additionnels (SNI, timeout socket, etc.).

**Règle** : ne pas se fier aux tests TLS bruts pour diagnostiquer un problème de connexion. Toujours tester via `HTTPClient`.

### 5. Cap `readEntireStream` : 512K minimum
Les pages HTML de ladepeche.fr (Top14/ProD2/CC) font entre 200K et 380K. Un cap de 64K causait `cap exceeded`. 256K était encore insuffisant (ProD2 a dépassé 262K). **512K** (`524288`) est la valeur de sécurité.

Le buffer est alloué en PSRAM via `heap_caps_malloc(MALLOC_CAP_SPIRAM)` — la taille n'a pas d'impact sur le heap SRAM.

### 6. Timeout `readEntireStream` : 120s pour les gros fichiers
Le TLS stream peut "staller" plusieurs dizaines de secondes sans livrer de données (probablement buffering côté serveur ou congestion LWIP). Un timeout de 30s causait des échecs systématiques à ~266K/378K. **120s** est la valeur de sécurité.

Dans la boucle de lecture, utiliser `vTaskDelay(pdMS_TO_TICKS(10))` quand `stream->available() == 0` pour céder le CPU au stack TCP.

### 7. Les gros objets doivent être en PSRAM, pas en BSS
Chaque `CompetitionData` fait ~4.3K. Empiler 4 instances en `static` (BSS) réduisait le heap initial de **~17K**, ce qui faisait passer `maxAllocHeap` sous le seuil critique au boot.

**Règle** : les buffers de travail > 1K (surtout ceux utilisés pendant le fetch) doivent être alloués en PSRAM :
```cpp
CompetitionData* d = (CompetitionData*)heap_caps_malloc(sizeof(CompetitionData), MALLOC_CAP_SPIRAM);
```

### 8. `ESP.getMaxAllocHeap()` peut crasher après fragmentation extrême
Après un handshake TLS + parsing d'une page de 378K, le heap est tellement fragmenté que `getMaxAllocHeap()` (qui parcourt les métadonnées du heap) peut lire des structures corrompues et causer un `LoadProhibited` / reboot.

**Règle** : ne jamais appeler `ESP.getMaxAllocHeap()` dans les logs des fonctions de fetch/parsing. Utiliser `ESP.getFreeHeap()` uniquement, ou ne logger le heap qu'au niveau `fetchAll()` (entre les requêtes, pas à l'intérieur).

---

