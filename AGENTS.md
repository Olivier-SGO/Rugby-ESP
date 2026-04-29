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
| `bblanchon/ArduinoJson@^7.0.0` | Parsing JSON (OTA version.json, config WiFi) |
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
| Core 0 | `DataFetcher` | 8KB | 1 | Fetch HTTP, parsing HTML/JSON, NTP, WiFi |
| Core 1 | `DisplayRenderer` | 6KB | 2 | Rendu 30fps DMA HUB75 |
| Core 1 | `SceneManager` | (même tâche) | — | Rotation des scènes, live-priority |

> **Validation (v1.3.0, 2026-04-29)** : high-water marks stables — DataFetcher 6632/8192, Renderer 4028/6144, BootFetch 8228/12288. Les valeurs originales (16K/10K/20K) étaient surdimensionnées et consommaient du heap SRAM précieux.

**Contrainte hardware** : le pilote DMA HUB75 est sensible aux interruptions. **Aucun appel réseau sur Core 1.** Le renderer ne doit jamais prendre de mutex plus que ~1ms.

### Flux de données

```
[HTTP stream] → [buffer 4KB] → [IdalgoParser] → [MatchDB] → [Scene] → [BackBuffer RGB332] → [DMA swap]
```

### Mémoire (320KB SRAM + 2MB PSRAM QSPI)

| Ressource | Taille | Zone | Détail |
|---|---|---|---|
| Back buffer | 16KB | SRAM | RGB332 (1 byte/pixel), 256×64 |
| Logos grands | 16KB max | SRAM | 2 slots × 64×64 RGB565 chargés depuis LittleFS à la demande |
| Logos mini | 512 bytes | SRAM | 16×16 RGB565 (Standings) |
| Logos compétition | ~5.6KB | **PSRAM** | Buffers `gCompLogoLg`/`gCompLogoSm` alloués via `heap_caps_malloc(MALLOC_CAP_SPIRAM)` (v1.3.0) |
| HTML page buffer | 512KB | PSRAM | Buffer de lecture stream (cap=524288) |
| CompetitionData | ~4.3KB × 3 | PSRAM | `top14`, `prod2`, `cc` alloués avec `MALLOC_CAP_SPIRAM` |
| ArduinoJson pool | variable | PSRAM | Allocateur custom `SpiRamAllocator` |
| HTML parsing chunks | 4KB | SRAM | Stream parsing, jamais de buffer complet en SRAM |

**Règle** : `heap_caps_malloc_extmem_enable()` est **intentionnellement désactivé** (Bug 13). Les gros buffers doivent être alloués explicitement en PSRAM via `heap_caps_malloc(..., MALLOC_CAP_SPIRAM)`.

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
  TeamData.h            # Table de mapping 45 clubs (Idalgo → canonique/abbrev/slug)
  DisplayPrefs.h        # Structs et helpers NVS pour les préférences d'affichage

  fonts/                # Headers GFX générés (Atkinson Hyperlegible)

src/
  main.cpp              # setup(), loop(), tâches FreeRTOS (renderer, boot fetch)
  data/
    DataFetcher.cpp/h   # Orchestration Core 0 : WiFi, NTP, fetch périodique, OTA
    IdalgoParser.cpp/h  # Parsing stream HTML ladepeche.fr (~300KB en chunks 4KB)
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
3. **Stack size des tâches** : `DataFetcher` 8192, `BootFetch` 12288, `Renderer` 6144. Le défaut FreeRTOS (2KB) crashe silencieusement. Les stacks originales (16K/20K/10K) étaient surdimensionnées ; les valeurs réduites ont libéré ~14KB de SRAM contiguë tout en restant sûres (high-water > 1500 bytes de marge).
4. **WDT** : utiliser `esp_task_wdt_init(timeout_s, true)` — `esp_task_wdt_config_t` / `esp_task_wdt_reconfigure` n'existent pas dans le framework Arduino ESP32 3.x.
5. **Accents** : `TeamData.h::stripAccents` utilise des `char` literals (`'e'`), pas des string literals (`"e"`) — sinon erreur de compilation `-fpermissive`.
6. **SVG** : Pillow ne supporte pas SVG nativement. Utiliser `cairosvg` + cairo (Homebrew) pour les logos comme Ulster.
7. **Logos .bin** sont gitignorés (`*.bin`, `data/logos/`) — régénérer avec `python3 tools/convert_logos.py` après un clone.
8. **Patch TLS obligatoire** : `tools/patch_mbedtls.py` (exécuté automatiquement via `extra_scripts` dans `platformio.ini`) force `max_frag_len=4096` dans `ssl_client.cpp` du framework Arduino-ESP32. Sans ce patch, les buffers TLS restent à 16KB×2 = 32KB et les handshakes HTTPS échouent par manque de heap (maxBlock < 45K). Le script est idempotent — il ne modifie le fichier que si le patch n'est pas déjà présent.

---

## Sources de données

### Priorité

1. **Idalgo (ladepeche.fr)** — résultats, live, fixtures. ~300KB HTML, parsing stream.
2. **WorldRugby PulseLive** — *code présent mais inactif*. Fallback fixtures potentiel.
3. **LNR (lnr.fr)** — *code présent mais inactif*. Classements Top 14 + Pro D2 potentiels.

### Gotchas par source

**Idalgo :**
- Headers obligatoires : `User-Agent: Mozilla/5.0 (compatible)` + `Accept-Language: fr-FR,fr;q=0.9` — sinon HTTP 403
- `data-status` : `"0"`=prévu, `"1"`/"3"`=terminé, `"7"`=live, `"2"`=traiter comme live
- Journées futures : détecter la transition décroissant→croissant dans les liens `journee-{n}`

> **Note** : les parsers WorldRugby et LNR existent dans le code historique mais ne sont pas appelés dans la version actuelle. Toutes les données proviennent d'Idalgo.

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

## WiFi

**Configuration** : entièrement via la Web UI (mode point d'accès `RugbyDisplay-Setup`). Aucun fichier `credentials.h` n'est nécessaire.

Au premier boot, si aucun réseau n'est enregistré, la carte démarre un AP avec portail captif. Les credentials saisis sont stockés en **NVS** (`Preferences`) — persistant aux redémarrages et aux mises à jour OTA. Modifiables via la Web UI à tout moment.

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
  1. Allocateur PSRAM custom (`JsonAllocator.h`) qui hérite de `ArduinoJson::Allocator`. Tous les `JsonDocument` volumineux utilisent `SpiRamJsonDocument` pour forcer l'intégralité du pool en PSRAM.
  2. **`heap_caps_malloc_extmem_enable()` est intentionnellement DÉSACTIVÉ** (`main.cpp`). Cette fonction force mbedtls dans la PSRAM lente et provoque des timeouts de handshake systématiques. Tous les gros buffers doivent être alloués explicitement en PSRAM via `heap_caps_malloc(..., MALLOC_CAP_SPIRAM)`.
- **Validation** : `_db->persist()` et `deserializeJson` fonctionnent sans crash. PSRAM free reste >1.5MB.

### Bug 14 — `DisplayManager::begin()` avec `if (_panel) end()`
- **Symptôme** : Corruption DMA potentielle si `begin()` est rappelé.
- **Cause** : `begin()` contenait `if (_panel) end();` qui libérait et ré-allouait les buffers DMA dans un heap déjà fragmenté (documenté comme Bug 11 dans les specs d'origine).
- **Fix** : Remplacé par un warning log + early return. `begin()` ne doit être appelé qu'une seule fois au boot.

### Bug 15 — Handshake TLS systématique en HTTP -1 après boot (2026-04-29)
- **Symptôme** : Après `Display.begin()` et création des tâches, tous les fetches HTTPS échouent avec `HTTP -1` (`HTTPC_ERROR_CONNECTION_REFUSED`). `maxAllocHeap` affichait ~47K pourtant supérieur au seuil théorique de 32K (buffers TLS 16K×2).
- **Cause** : le SDK ESP32 précompilé fixe `CONFIG_MBEDTLS_SSL_MAX_CONTENT_LEN=16384` et désactive `VARIABLE_BUFFER_LENGTH`. Les buffers TLS internes (input + output) font donc **16KB chacun = 32KB minimum**, et le handshake ajoute des structures de session + certificats qui portent l'empreinte totale à **~45–55KB contigus**. `max_frag_len=4096` négocie des records plus petits avec le serveur mais **ne réduit pas la taille des buffers I/O internes**. Avec un `maxBlock` de 47K et un heap déjà fragmenté par les stacks et les objets statiques, l'allocation TLS échoue.
- **Fix** : **combinaison cumulative** de toutes les optimisations suivantes — aucune seule ne suffisait :
  1. **Réserve TLS 48KB** (`malloc` placébo) : alloué au début de `setup()`, libéré juste avant chaque handshake, ré-alloué après. Protège un bloc contigu de 48K.
  2. **Stacks FreeRTOS réduites** : DataFetcher 8192 (was 16384), Renderer 6144 (was 10240), BootFetch 12288 (was 20480). Libère ~14KB de SRAM.
  3. **`CompLogos` en PSRAM** : `gCompLogoLg`/`gCompLogoSm` alloués via `heap_caps_malloc(MALLOC_CAP_SPIRAM)`. Libère ~11KB de SRAM.
  4. **`SceneManager` buffers en PSRAM** : `liveBuf`/`nonliveBuf`/`sortedBuf` déplacés de BSS statique à PSRAM. Libère ~5KB de SRAM.
  5. **`getMaxAllocHeap()` retiré du chemin fetch** : évite les crashs `LoadProhibited` pendant le parsing (Leçon 8).
  6. **3 fetches au boot** (pas 5) : suppression de `fetchNextJournee` du boot — les fixtures N+1 sont récupérées dans les cycles `fetchRotating()`.
  7. **`WiFiClientSecureSmall`** avec `max_frag_len=4096` — négocie des records 4K avec le serveur (évite la fermeture de connexion côté serveur).
  8. **`esp_sntp_stop()` avant fetch** — libère les sockets UDP et évite la corruption LWIP pendant le TLS.
- **Validation** : `maxBlock=83956` au moment du fetch. Trois handshakes successifs (CC calendar → Top14 → ProD2) réussissent. Pages de 340K–378K lues et parsées. 13 slots créés. High-water stacks stables (Renderer 4028/6144, DataFetcher 6632/8192, BootFetch 8228/12288).

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

- TLS/HTTPS utilisé pour Idalgo (ladepeche.fr). En production, utiliser `ESP_CERTS_TLS_BUNDLE`. En développement/test, le client est en mode insecure (`setInsecure()`).
- Credentials WiFi en NVS, pas en code source (après le premier boot).
- Web UI exposée sur le réseau local uniquement (pas d'authentification — usage domestique).

---

## ⚠️ Anti-patterns documentés

### Ne JAMAIS réutiliser `WiFiClientSecure` pour un NOUVEAU handshake
`WiFiClientSecure` ne supporte pas d'être réutilisé pour ouvrir une **nouvelle** connexion TLS après avoir fermé la précédente (`client->stop()` puis `client->connect()`). L'état interne mbedTLS reste corrompu et provoque systématiquement `HTTP -1 (connection refused)`.

**Approche actuelle** : client frais par requête. Chaque `fetch()` / `fetchCalendar()` crée localement un `WiFiClientSecureSmall` (ou `WiFiClientSecure`) sur la stack. Le destructeur libère proprement les buffers TLS après chaque requête. Pas de keep-alive — le serveur ladepeche.fr ferme les connexions inactives trop vite pour que le réusage soit fiable.

**Règle** : créer un `WiFiClientSecure` frais pour **chaque** requête. Ne jamais partager un client entre deux requêtes qui nécessiteraient chacune un handshake TLS.

---

## 🔥 Leçons critiques de debugging (2026-04-27)

### 1. Le handshake TLS consomme beaucoup de heap SRAM — les buffers internes restent à 16KB×2
Chaque `WiFiClientSecure` handshake alloue deux buffers TLS (input + output). Avec la configuration **précompilée du SDK ESP32**, `CONFIG_MBEDTLS_SSL_MAX_CONTENT_LEN=16384` et `VARIABLE_BUFFER_LENGTH` est désactivé. Les buffers font donc **16KB chacun = 32KB minimum**, plus les structures de session et de certificats → empreinte totale **~45–55KB contigus**.

**Le patch `max_frag_len=4096`** (via `tools/patch_mbedtls.py`) force `mbedtls_ssl_conf_max_frag_len()` dans `ssl_client.cpp`. Cela négocie avec le serveur des records TLS de 4KB max, ce qui évite que le serveur ferme la connexion. Mais **ce patch ne réduit PAS la taille des buffers I/O internes** alloués par `mbedtls_ssl_setup()`. Les buffers restent à 16KB chacun.

**Conséquence** : le seul moyen de réussir le handshake est d'avoir un bloc contigu de **≥ 50KB** dans le heap SRAM au moment du fetch. Sur un ESP32-S3 avec 320KB SRAM, après `Display.begin()` (16K DMA) + stacks + objets statiques, ce seuil est difficile à atteindre sans optimisation agressive.

### 2. Seuil critique de `maxAllocHeap` pour handshake TLS — valeurs corrigées
| `maxBlock` au moment du fetch | Résultat |
|---|---|
| ≥ 80K | Handshake quasi-certain (3 requêtes successives validées) |
| 60–75K | Probablement OK pour 1–2 requêtes |
| 45–55K | Échec systématique — insuffisant pour buffers 16K×2 + overhead session/cert |
| < 45K | Échec garanti |

**Expérience v1.3.0** : avec `maxBlock=83956`, 3 handshakes successifs + parsing de pages 340K–378K ont réussi. Avec `maxBlock=47092` (même firmware mais sans réserve 48K et sans stacks réduites), tous les fetches échouaient en `HTTP -1`.

**Action** : ne PAS logger `getMaxAllocHeap()` pendant le fetch (risque de crash, voir Leçon 8). Logger `getFreeHeap()` et s'assurer qu'il reste > 50K avant le premier handshake.

### 3. `MBEDTLS_SSL_MAX_CONTENT_LEN=8192` dans `platformio.ini` ne suffit pas — et le patch `max_frag_len` ne réduit pas les buffers internes
Cette définition est passée au compilateur, mais le SDK ESP32 la redéfinit à **16384** via `CONFIG_MBEDTLS_SSL_MAX_CONTENT_LEN` (warning "redefined" visible à la compilation). La librairie précompilée utilise donc **16KB par buffer** (input + output = **32KB total**).

**Le patch `max_frag_len=4096`** (via `tools/patch_mbedtls.py`) force `mbedtls_ssl_conf_max_frag_len(..., MBEDTLS_SSL_MAX_FRAG_LEN_4096)` dans `ssl_client.cpp`. Cela négocie avec le serveur des records de **4KB max**, mais **ne réduit PAS la taille des buffers I/O internes** alloués par `mbedtls_ssl_setup()` — ces buffers restent à 16KB chacun car le SDK n'a pas `CONFIG_MBEDTLS_SSL_VARIABLE_BUFFER_LENGTH` activé.

**Conséquence** : chaque handshake TLS nécessite un bloc contigu de **~45–55KB** dans le heap SRAM (32K buffers + overhead session + certificats). Sur un heap fragmenté après `Display.begin()`, c'est le seuil critique. Le *malloc placébo* seul de 32K ne suffit pas — il faut une **combinaison** de réserve 48K + stacks réduites + gros objets en PSRAM pour atteindre ≥80K de `maxBlock`.

**Risque du patch** : si le serveur envoie un record TLS > 4K, la connexion est fermée. À ce jour, `www.ladepeche.fr` n'en envoie pas.

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

> **Note (2026-04-29)** : cette règle est maintenant appliquée dans tout le codebase. `DataFetcher.cpp` et `OTAUpdater.cpp` n'utilisent plus `getMaxAllocHeap()` dans le chemin fetch. De plus, `fetchAll()` au boot ne fait plus de `fetchNextJournee` — les fixtures de la journée N+1 sont récupérées dans les cycles `fetchRotating()` ultérieurs, réduisant le nombre de handshakes TLS au boot de 5 à 3.

### 9. `malloc` placébo — réserver un bloc contigu au boot pour le TLS (v1.3.0)

**Problème** : après `Display.begin()` (buffers DMA) + `xTaskCreate` (stacks) + `LittleFS` + `Scenes.begin()`, le heap SRAM est fragmenté. Même avec 90–100 KB de `freeHeap`, le plus gros bloc contigu (`maxAllocHeap`) peut tomber sous les ~50K nécessaires au handshake TLS (16K input + 16K output buffers + overhead session/certificats), provoquant des échecs systématiques `HTTP -1`.

**Solution** : allouer un bloc de **48 KB** via `malloc()` au tout début de `setup()` (quand le heap est vierge), le garder alloué pendant tout le boot pour protéger ce bloc, puis le libérer juste avant les handshakes TLS. Cela crée instantanément un trou propre de 48K dans le heap, garantissant que `WiFiClientSecure` trouve un bloc contigu pour ses buffers.

```cpp
// main.cpp — au tout début de setup(), avant Display.begin()
static void* gTLSReserve = nullptr;
// ...
gTLSReserve = malloc(49168);  // 48KB + marge d'alignement
```

```cpp
// DataFetcher.cpp
void releaseTLSReserve() {
    extern void* gTLSReserve;
    if (gTLSReserve) { free(gTLSReserve); gTLSReserve = nullptr; }
}
void reclaimTLSReserve() {
    extern void* gTLSReserve;
    gTLSReserve = malloc(49168);
}

void DataFetcher::fetchAll() {
    releaseTLSReserve();
    // → handshake TLS ici, les buffers trouvent un bloc contigu
    // ... fetch CC → Top14 → ProD2 ...
    reclaimTLSReserve();
}
```

**Pourquoi 48K et pas 32K** : en pratique, 32K de réserve donnaient un `maxBlock=47K` au moment du fetch — insuffisant. 48K donnent un `maxBlock=84K`, ce qui laisse ~36K de marge pour l'overhead session/certificats au-delà des 32K de buffers fixes.

**⚠️ La réserve seule ne suffit pas.** Il faut la combiner avec :
- Stacks FreeRTOS réduites (libère ~14K)
- `CompLogos` en PSRAM (libère ~11K)
- `SceneManager` buffers en PSRAM (libère ~5K)
- Pas de `getMaxAllocHeap()` dans le fetch path
- 3 fetches au boot maximum

Sans ces optimisations cumulatives, même une réserve de 48K ne suffit pas à garantir `maxBlock ≥ 80K`.

---

