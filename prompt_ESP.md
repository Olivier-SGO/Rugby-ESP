# Prompt de développement — Rugby Display sur ESP32-S3

Ce document est un brief complet pour implémenter sur **ESP32-S3** les fonctionnalités du projet `Display` (actuellement sur Raspberry Pi 4). Il est destiné à être transmis à un agent de développement (Claude Code ou autre) comme contexte de départ.

---

## Contexte du projet existant

Le projet `Display` est un afficheur LED de résultats et scores rugby, actuellement fonctionnel sur Raspberry Pi 4 avec des panneaux LED P2.5 RGB (HUB75). Il affiche en temps réel :
- **Scores live** avec minute de jeu
- **Résultats** de la dernière journée
- **Classement** Top 14 et Pro D2
- **Programme** des prochains matchs avec horaires

Les compétitions couvertes : Top 14, Pro D2, Champions Cup.

Architecture Python sur Pi :
- Scraping HTTP → `DataManager` → `DisplayEngine` (30fps PIL) → panneaux via `rpi-rgb-led-matrix`
- Flask web UI sur port 8080 (preview, config, contrôles)

---

## Cible matérielle : ESP32-S3

### Hardware recommandé
- **MCU** : ESP32-S3 (double cœur Xtensa LX7, 512KB SRAM, 8MB+ PSRAM recommandé)
- **Panneaux LED** : même technologie HUB75, P2.5 ou P3, résolution cible 128×32 ou 64×32
- **Connexion** : WiFi 2.4GHz intégré
- **Mémoire** : flash 8MB minimum pour OTA + assets

### Bibliothèques ESP32 recommandées
- **Affichage HUB75** : [`ESP32-HUB75-MatrixPanel-DMA`](https://github.com/mrfaptastic/ESP32-HUB75-MatrixPanel-DMA) — pilote mature, double buffer DMA, supporte chaînage de panneaux
- **HTTP** : `HTTPClient` (Arduino) ou `esp_http_client` (ESP-IDF) — préférer ESP-IDF pour le contrôle fin des timeouts
- **JSON parsing** : `ArduinoJson` v7
- **HTML parsing** : **pas de BeautifulSoup** disponible sur ESP32 — utiliser des parsers légers : `regex` sur strings ou extraction manuelle avec `indexOf` / `substring`
- **Timezone** : bibliothèque `Timezone` de Jack Christensen ou `configTime` + `setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1)` pour NTP + Paris
- **Fonts** : fonts GFX intégrées ou fonts custom `.h` générées avec `fontconvert`

### Framework recommandé
**Arduino framework sur PlatformIO** — plus de bibliothèques disponibles que ESP-IDF pur pour ce cas. Fichier `platformio.ini` :

```ini
[env:esp32s3]
platform = espressif32
board = esp32-s3-devkitc-1
framework = arduino
board_build.arduino.memory_type = qio_opi   ; active PSRAM OPI
build_flags =
  -DBOARD_HAS_PSRAM
  -DARDUINO_USB_CDC_ON_BOOT=1
lib_deps =
  mrfaptastic/ESP32 HUB75 LED MATRIX PANEL DMA Display
  bblanchon/ArduinoJson@^7
  paulstoffregen/Time
  JChristensen/Timezone
monitor_speed = 115200
```

---

## Leçons apprises des sources de données

Ces observations sont critiques — les APIs rugby sont peu documentées et changeantes.

### Source 1 : Idalgo (ladepeche.fr) — PRIORITAIRE

**Avantages** : temps réel, pas de clé API, couvre Top 14 + Pro D2 + Champions Cup.

**URLs :**
```
Top 14:         https://www.ladepeche.fr/sports/resultats-sportifs/rugby/top-14/phase-reguliere/resultats
Pro D2:         https://www.ladepeche.fr/sports/resultats-sportifs/rugby/pro-d2/phase-reguliere/resultats
Champions Cup:  https://www.ladepeche.fr/sports/resultats-sportifs/rugby/champions-cup/resultats
```

**Structure HTML (SSR, pas de JS requis) :**
- `div[data-match]` → un match
- `data-status` : `"0"` = à venir, `"1"/"3"` = terminé, `"7"` = en cours (live), `"2"` = peut apparaître (traiter comme live)
- Home : `a.localteam_txt` → texte
- Away : `a.visitorteam_txt` → texte
- Scores : `span.span_idalgo_score_square_score_txt` (index 0 = domicile, 1 = extérieur)
- Minute live : `span.span_idalgo_score_square_score_status` → `"45'"` ou `"MT"` (mi-temps)
- **Heure des fixtures** : `span.span_idalgo_score_square_time` avec classe `idalgo_date_timezone` → `"21:05"` en heure locale Paris
- **Date** : dans le `li` parent `li_idalgo_content_result_date_list` → `span` → `"Samedi 18 avril 2026"`

**Navigation des journées futures :**
La page principale montre la dernière journée terminée. Les journées futures sont dans la navigation sous forme de liens `<a href="/sports/.../resultats/{id}/journee-{n}">`. Dans l'ordre HTML, les journées passées apparaissent en ordre décroissant puis les futures en ordre croissant — la transition décroissant→croissant identifie le début des journées à venir.

**Parsing sur ESP32 :**
Le HTML Idalgo fait ~300KB. Stratégie recommandée :
1. Fetch en streaming avec `HTTPClient` en morceaux
2. Extraction par `strstr` / `indexOf` sur les attributs-clés
3. Alternativement, faire une requête HTTP conditionnelle (ETag/If-Modified-Since) pour économiser la bande passante

**Headers HTTP obligatoires :**
```
User-Agent: Mozilla/5.0 (compatible)
Accept-Language: fr-FR,fr;q=0.9
```
Sans User-Agent correct, certaines requêtes retournent 403.

### Source 2 : WorldRugby PulseLive — FALLBACK

**URL :** `https://api.wr-rims-prod.pulselive.com/rugby/v3/match`

**Paramètres :**
```
startDate=YYYY-MM-DD
endDate=YYYY-MM-DD
sort=asc|desc
page_size=100
competition={id}
```

**IDs de compétition :**
- Top 14 : vérifier dans les réponses API (peut changer entre saisons)
- Champions Cup : idem

**Problèmes observés :**
- `startDate == endDate` → HTTP 400 : toujours utiliser `today-1 → today+1`
- Délai live ~12 minutes (observé QF Champions Cup 2026-04-11) → ne pas utiliser pour live si Idalgo disponible
- Fixtures sans horaire : retourne `00:00:00 UTC` (minuit UTC) = placeholder. En heure Paris (CEST/UTC+2) = 02:00 ou (CET/UTC+1) = 01:00. Détecter : si minute == 0 et heure ≤ 2, l'heure est inconnue → ne pas afficher
- Réponse JSON : `time.millis` = timestamp Unix ms pour le kickoff

**Structure JSON (match) :**
```json
{
  "matchId": "...",
  "time": { "millis": 1744930800000, "label": "..." },
  "teams": [
    { "name": "...", "abbreviation": "RCT", "logo": { "url": "..." } },
    { "name": "...", "abbreviation": "ST",  "logo": { "url": "..." } }
  ],
  "scores": [{ "score": 20 }, { "score": 17 }],
  "status": { "type": "C" }   // C=terminé, U=prévu, L/L1/L2/LHT=live
}
```

### Source 3 : LNR Classement — STANDINGS SEULEMENT

**URLs :**
- Top 14 : `https://top14.lnr.fr/classement`
- Pro D2 : `https://prod2.lnr.fr/classement`

**Piège critique :** le tableau est dans un slot Vue `<template #first-tab>` — les parsers HTML standard ignorent le contenu des `<template>`. Sur ESP32, remplacer `<template` par `<div` dans la réponse brute avant de parser.

**Structure HTML après fix :**
- `div.ranking__fixed-block` → `div.table-line--ranking-fixed` → rang + logo
- `div.ranking__scrollable-cells` → `div.table-line--ranking-scrollable` → cellules : [0]=club, [1]=pts, [2]=M joués, [3]=G, [4]=N, [5]=P, [6]=bonus, [7]=pts_marqués, [8]=pts_encaissés, [9]=diff

---

## Logos des clubs

### LNR CDN (clubs français Top 14 + Pro D2)
```
https://cdn.lnr.fr/club/{slug}/photo/logo-thumbnail-2x.5ab42ba7610b284f40f993f6a452bec5362fa8b6
```
Slugs : `toulouse`, `paris`, `clermont`, `bordeaux-begles`, `la-rochelle`, `montpellier`, `lyon`, `toulon`, `racing-92`, `castres`, `bayonne`, `pau`, `perpignan`, `montauban`, etc.

**Attention :** certains logos LNR ont de larges marges transparentes ou des étoiles de championnat qui réduisent la taille utile. Overrides recommandés :
- RCT (Toulon) : `https://upload.wikimedia.org/wikipedia/fr/thumb/5/5a/Logo_Rugby_club_toulonnais.svg/1920px-Logo_Rugby_club_toulonnais.svg.png` (sans les 3 étoiles)
- Racing 92 : `https://www.racing92.fr/wp-content/uploads/2025/05/racing92.png`

### EPCR CDN (clubs européens Champions Cup)
Source : `https://www.epcrugby.com/champions-cup/clubs/` (liste avec URLs)

Clubs principaux (Champions Cup 2025-26) :
```
Bath Rugby:        https://media-cdn.incrowdsports.com/f4d9a293-9086-41bf-aa1b-c98d1c62fe3b.png
Glasgow Warriors:  https://media-cdn.incrowdsports.com/f0e4ca1a-3001-42d4-a134-befe8348540c.png
Leinster Rugby:    https://media-cdn.incrowdsports.com/02ec4396-a5c2-49b2-bd5d-4056277b1278.png
Stade Toulousain:  https://media-cdn.incrowdsports.com/9b827a29-a5b1-4752-b312-477492f2819a.png
Sale Sharks:       https://www.rezorugby.com/images/photos_fiches/club_20141215063439.png
(voir team_data.py du projet Pi pour la liste complète)
```

### Stratégie logos sur ESP32
Les logos PNG/JPEG doivent être convertis en tableaux de pixels pour l'affichage. Options :
1. **SPIFFS/LittleFS** : stocker les logos pré-convertis en RGB565 au format binaire (format natif ESP32-HUB75)
2. **Fetch HTTP + decode** : utiliser `TJpgDec` ou `PNGdec` pour décoder à la volée — lourd en RAM
3. **Compromis recommandé** : stocker les logos en flash (LittleFS) en RGB565 16-bit à la taille cible (32×32 ou 64×64). Régénérer les fichiers `.bin` avec un script Python sur le PC lors des mises à jour de saison.

Script de conversion Python (à exécuter sur PC) :
```python
from PIL import Image
import struct, hashlib

def logo_to_rgb565(url_or_path, target_w, target_h, output_path):
    img = Image.open(url_or_path).convert("RGBA")
    # Auto-crop transparent padding
    bbox = img.split()[3].getbbox()
    if bbox:
        img = img.crop(bbox)
    img = img.resize((target_w, target_h), Image.LANCZOS)
    # Composite on black background
    bg = Image.new("RGB", (target_w, target_h), (0, 0, 0))
    bg.paste(img, mask=img.split()[3])
    with open(output_path, "wb") as f:
        for y in range(target_h):
            for x in range(target_w):
                r, g, b = bg.getpixel((x, y))
                rgb565 = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
                f.write(struct.pack(">H", rgb565))
```

---

## Noms d'équipes — table de correspondance

Le même club a des noms différents selon la source. Table de mapping essentielle (source → nom canonique → abréviation) :

| Idalgo | WorldRugby | LNR Classement | Abbrev |
|--------|-----------|----------------|--------|
| Lyon | Lyon Olympique Universitaire | Lyon OU | LOU |
| Montpellier | Montpellier Herault Rugby Club | Montpellier Hérault | MHR |
| Toulon | RC Toulonnais | Toulon | RCT |
| Stade Français | Stade Francais Paris | Stade Français Paris | SF |
| Bordeaux-Bègles | Union Bordeaux Begles | UBB | UBB |
| Clermont | ASM Clermont Auvergne | Clermont | ASM |
| La Rochelle | Stade Rochelais | La Rochelle | SR |
| Castres | Castres Olympique | Castres Olympique | CO |
| Bayonne | Aviron Bayonnais | Bayonne | AB |

Ne jamais se fier aux abréviations retournées par WorldRugby (ex. "TOL" pour Toulon → toujours overrider avec "RCT").

---

## Architecture ESP32 recommandée

### Structure des tâches FreeRTOS

```
Core 0 (protocole/réseau) :
  Task: DataFetcher         — HTTP fetch, parsing JSON/HTML, mise à jour MatchDB
  Task: NTPSync             — synchronisation horloge toutes les heures

Core 1 (temps réel display) :
  Task: DisplayRenderer     — rendu 30fps dans double buffer DMA
  Task: SceneRotator        — timer de rotation des scènes (watchdog 6s/8s/20s)
```

La séparation cœurs est critique : le pilote DMA HUB75 est sensible aux interruptions et doit tourner sur un cœur dédié.

### Structure de données en mémoire

```c
// En PSRAM (déclaré avec IRAM_ATTR ou heap_caps_malloc(MALLOC_CAP_SPIRAM))
typedef struct {
  char home_name[32];
  char away_name[32];
  char home_abbrev[8];
  char away_abbrev[8];
  int16_t home_score;      // -1 = pas encore joué
  int16_t away_score;
  uint8_t status;          // 0=scheduled, 1=live, 2=finished
  int8_t  minute;          // -1 = MT, 0 = inconnu
  uint32_t kickoff_epoch;  // Unix timestamp UTC
  uint8_t  home_logo[LOGO_W * LOGO_H * 2];  // RGB565, en PSRAM
  uint8_t  away_logo[LOGO_W * LOGO_H * 2];
} MatchData;

typedef struct {
  MatchData results[10];
  MatchData fixtures[8];
  StandingEntry standings[16];
  uint8_t result_count;
  uint8_t fixture_count;
  uint8_t standings_count;
  uint32_t last_updated;
} CompetitionData;
```

### Gestion de la mémoire

L'ESP32-S3 avec 8MB PSRAM peut stocker les logos en RAM. Sans PSRAM, les logos doivent rester en flash.

Estimation mémoire pour logos 32×32 RGB565 :
- 32 × 32 × 2 bytes = 2048 bytes par logo
- 45 clubs × 2048 = ~90KB → tient en PSRAM

### Fonts recommandées pour HUB75

La bibliothèque ESP32-HUB75 supporte les fonts GFX d'Adafruit. Pour imiter le look Hyperlegible :
- `FreeSansBold9pt7b.h` — scores (équivalent size 22 PIL → ~9pt GFX)
- `FreeSans7pt7b.h` — abréviations (≈ size 13 PIL → ~7pt GFX)
- Font bitmap 5×7 intégrée — labels/header (≈ size 10 PIL)

Caractères accentués : les fonts GFX ne couvrent pas le charset étendu par défaut. Solution : strip accents avant affichage (même convention que le projet Pi).

---

## Étapes de développement — Planification

### Phase 1 : Infrastructure de base (1-2 semaines)

**1.1 — Setup PlatformIO + pilote HUB75**
- Configurer `platformio.ini` avec les bibliothèques
- Valider l'affichage HUB75 avec le sketch exemple de la lib
- Mesurer la fréquence de refresh réelle et le taux de CPU utilisé
- Tester le chaînage de panneaux (2 panneaux 64×32 → 128×32)

**1.2 — WiFi + NTP**
- Connexion WiFi avec credentials en flash (Preferences ou SPIFFS)
- Synchronisation NTP avec timezone Europe/Paris
- `configTime(0, 0, "pool.ntp.org")` + `setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1)`
- Vérifier que `localtime()` retourne bien l'heure Paris

**1.3 — LittleFS pour logos et config**
- Partitionner la flash : 1MB app, 1MB OTA, 2MB LittleFS (logo + config)
- Script Python sur PC pour générer les logos RGB565 → upload via `platformio run --target uploadfs`
- Lire un logo RGB565 depuis LittleFS et l'afficher sur la matrice

**1.4 — Double buffer et scene manager**
- Implémenter un `FrameBuffer` double buffer en PSRAM (2 × W × H × 2 bytes)
- Thread rendu sur Core 1 écrit dans le buffer "back", swap atomique
- Interface `Scene` : classe abstraite avec `render(FrameBuffer&)`

---

### Phase 2 : Sources de données (2-3 semaines)

**2.1 — Fetch HTTP Idalgo**
- Fetch de la page ladepeche.fr (300KB HTML) avec `HTTPClient` ou `esp_http_client`
- Gérer la compression gzip (header `Accept-Encoding: gzip`) si supporté
- Parser les attributs `data-match`, `data-status` par `strstr` / extraction manuelle

**Stratégie de parsing HTML sur ESP32 :**
```cpp
// Extraction légère — pas de DOM complet
const char* findAttr(const char* html, const char* attr) {
  const char* pos = strstr(html, attr);
  if (!pos) return nullptr;
  pos += strlen(attr);
  if (*pos != '=') return nullptr;
  pos++; // skip '='
  if (*pos == '"') pos++; // skip '"'
  return pos;
}
```
Pour des pages de 300KB, allouer en PSRAM : `char* html = (char*)heap_caps_malloc(350000, MALLOC_CAP_SPIRAM)`.

**2.2 — Parser les résultats et le live**
- Itérer sur les `div data-match=` 
- Extraire `data-status`, noms d'équipes, scores, minute
- Condition live : status != "0" && status != "1" && status != "3"
- Remplir la structure `MatchData`

**2.3 — Parser les fixtures avec horaires**
- Détecter `data-status="0"` → match à venir
- Extraire `span_idalgo_score_square_time` → "21:05" → convertir en epoch UTC avec timezone Paris
- Pour les journées futures : extraire les liens `<a href=...journee-{n}...>`, détecter la transition décroissant→croissant, fetcher la page de la prochaine journée

**2.4 — Fallback WorldRugby**
- Implémenter `get_fixtures` sur l'API JSON WorldRugby
- Filtrer `status.type == "U"` pour les fixtures
- Détecter horaires TBD : minute == 0 && heure <= 2 (local Paris) → ne pas afficher l'heure

**2.5 — Classement LNR**
- Fetch `top14.lnr.fr/classement`
- Remplacer `<template` par `<div` dans le HTML brut
- Parser `table-line--ranking-scrollable` → rank, club, pts

---

### Phase 3 : Scènes d'affichage (2 semaines)

**3.1 — ScoreboardScene**

Layout (référence depuis le projet Pi) :
```
[LOGO h×h] TOP14 · J22          [LOGO h×h]
           SF    64 - 20   ASM
                  Final              1/7
```

Règle inline vs stacked :
- Calculer la largeur totale : `text_w(abbrev_home) + gap + text_w(score) + gap + text_w(abbrev_away)`
- Si > zone centrale (`canvas_w - 2*logo_w`) : mode stacked (abréviations centrées sous leur chiffre)
- Compteur match toujours en bas à droite absolu

Couleurs :
- Gagnant : or (255, 200, 30) | Perdant : rouge (220, 60, 60) | Nul : blanc | Live : vert (0, 210, 80)
- En-tête compétition : Top14=bleu, ProD2=orange, CC=violet

**3.2 — FixturesScene**
```
[LOGO h×h] TOP14 · PROCHAIN     [LOGO h×h]
           SR      UBB
         Sam 18 avr  14:30           1/7
```
- Date et heure en heure locale Paris
- Si heure == 00:00 (TBD) : afficher seulement la date

**3.3 — StandingsScene**
```
PRO D2  CLASSEMENT
 1 RC2V  99   2 CU   81   3 VRDR  71   4 PR   71
 5 CAB   69   6 OCR  67   7 SUA   57   8 SAU  52
...
```
- 4 colonnes × N lignes (14 pour Top14, 16 pour ProD2)
- Zones couleur : top playoffs→or, relégation→rouge, reste→blanc

**3.4 — Mode live-priority**
- Quand un match est en cours : figer sur ScoreboardScene, afficher seulement les données live
- Grace period 5 min après dernier live détecté (gère les coupures réseau)
- Poll interval auto 30s en live, normal sinon

---

### Phase 4 : Web UI de contrôle (1 semaine)

**4.1 — Serveur web minimal**
- Bibliothèque `ESPAsyncWebServer` — non-bloquant, compatible avec le rendu temps réel
- Routes : `/status` (JSON), `/config` (GET/POST), `/restart`, `/next-scene`
- Page HTML simple servie depuis LittleFS (minifier pour tenir en flash)

**4.2 — Contrôles**
- Activer/désactiver compétitions et scènes (persisté en Preferences NVS)
- Luminosité (API HUB75 `setBrightness()`)
- Bouton redémarrage : `ESP.restart()`

**4.3 — Preview**
- Capturer le framebuffer courant et le servir en BMP ou RGB raw
- Convertir côté client JS en canvas (pixel art)

---

### Phase 5 : Robustesse et OTA (1 semaine)

**5.1 — Watchdog et récupération**
- Task watchdog sur le DataFetcher (redémarre si pas de fetch en 5 min)
- Fallback sur données disk si fetch réseau échoue
- `esp_task_wdt_reset()` régulier dans les boucles longues

**5.2 — OTA (Over-The-Air)**
- `ArduinoOTA` ou `Update` via upload HTTP depuis la web UI
- Partition OTA configurée dans `partitions.csv`

**5.3 — Persistance**
- Résultats et fixtures sauvegardés en JSON dans LittleFS après chaque fetch réussi
- Chargés au démarrage avant tout accès réseau (display fonctionne hors ligne)

---

## Points de vigilance spécifiques ESP32

1. **Heap fragmentation** : les gros buffers HTML (300KB) doivent être alloués d'un bloc en PSRAM avant usage et libérés immédiatement après parsing. Ne jamais garder le HTML brut en mémoire simultanément avec les framebuffers.

2. **Stack size des tâches** : les tâches avec HTTP et JSON parsing nécessitent une stack de 8-16KB. FreeRTOS default (2KB) crashe silencieusement.

3. **SSL/TLS** : ladepeche.fr est en HTTPS. `esp_http_client` supporte TLS avec le bundle de certificats (`ESP_CERTS_TLS_BUNDLE`). Ajouter `esp_tls_set_global_ca_store()` ou utiliser `client.setInsecure()` pour les tests (jamais en production).

4. **Concurrence** : les structures `MatchData` sont partagées entre le DataFetcher (Core 0) et le DisplayRenderer (Core 1). Utiliser un mutex FreeRTOS ou un double-buffer avec flag swap atomique (éviter `xSemaphoreTake` dans la boucle de rendu 30fps).

5. **Accents** : même convention que le projet Pi — strip accents avant affichage. Table de remplacement hardcodée : `{'é':'e', 'è':'e', 'ê':'e', 'à':'a', 'â':'a', 'î':'i', 'ô':'o', 'û':'u', 'ç':'c', 'É':'E', 'È':'E'}`.

6. **Reconnexion WiFi** : implémenter un event handler sur `WIFI_EVENT_STA_DISCONNECTED` qui relance `WiFi.reconnect()` avec backoff exponentiel. Sans ça, un redémarrage du routeur met fin au display.

7. **Timestamps** : toujours stocker les kickoffs en UTC (epoch 32-bit suffit jusqu'en 2106). Convertir en heure locale Paris uniquement à l'affichage avec `localtime()` (après `setenv TZ`).

---

## Ordre de priorité des features

| Priorité | Feature | Dépendances |
|----------|---------|-------------|
| P0 | Affichage HUB75 fonctionnel | Phase 1.1 |
| P0 | Fetch + parse Idalgo résultats | Phase 2.1-2.2 |
| P0 | ScoreboardScene résultats | Phase 3.1 |
| P1 | Live scores avec minute | Phase 2.2 + 3.1 |
| P1 | Fixtures avec horaires | Phase 2.3 + 3.2 |
| P1 | Classement LNR | Phase 2.5 + 3.3 |
| P1 | Mode live-priority | Phase 3.4 |
| P2 | Logos clubs | Phase 1.3 |
| P2 | Web UI contrôles | Phase 4 |
| P3 | OTA | Phase 5.2 |
| P3 | Fallback WorldRugby | Phase 2.4 |

---

## Référence — Projet Pi existant

Le code source complet de la version Raspberry Pi est dans ce même dépôt :
- `rugby_display/data/idalgo.py` — parsing Idalgo complet (résultats, live, fixtures, navigation journées)
- `rugby_display/data/worldrugby.py` — fallback WorldRugby
- `rugby_display/data/team_data.py` — table de correspondance noms/abréviations/logos (45 clubs)
- `rugby_display/display/scenes/scoreboard.py` — layout scoreboard avec logique inline/stacked
- `rugby_display/display/scenes/fixtures.py` — layout fixtures avec dates Paris
- `rugby_display/display/scenes/standings.py` — classement 4 colonnes

Ces fichiers sont la référence de vérité pour la logique de parsing et les layouts d'affichage. Sur ESP32, reproduire la même logique en C++.
