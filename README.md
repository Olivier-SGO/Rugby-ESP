# 🏉 Rugby ESP32 Display

> Afficheur LED 256×64 en temps réel pour le rugby français et européen.
> Live scores, résultats, classements et programme des matchs sur 2 panneaux HUB75.

[![PlatformIO](https://img.shields.io/badge/PlatformIO-ES32S3-orange.svg)](https://platformio.org/)
[![Framework](https://img.shields.io/badge/Framework-Arduino-blue.svg)](https://www.arduino.cc/)
[![C++](https://img.shields.io/badge/C%2B%2B-17-purple.svg)](https://isocpp.org/)
[![License](https://img.shields.io/badge/License-MIT-green.svg)](LICENSE)

---

## ✨ Fonctionnalités

- **📊 Scores live** — minute de jeu, scores domicile/extérieur, logos 64×64
- **📅 Programme des matchs** — date et heure de kickoff (fixtures)
- **🏆 Classements** — scroll vertical continu avec zones playoffs et relégation
- **🌐 3 compétitions** — Top 14, Pro D2, Champions Cup
- **📡 Mise à jour OTA** — téléchargement automatique depuis GitHub Releases
- **🖥️ Web UI** — configuration WiFi, luminosité, scènes, mise à jour manuelle
- **📶 Mode AP** — point d'accès `RugbyDisplay-Setup` avec portail captif si pas de WiFi

---

## 🛠️ Hardware

| Composant | Modèle |
|---|---|
| Microcontrôleur | **Adafruit MatrixPortal S3** (ESP32-S3, 240MHz, 8MB Flash, 2MB PSRAM) |
| Panneaux LED | 2× HUB75 128×64 chaînés = **256×64 px** |

---

## 🏗️ Architecture

```
┌─────────────┐     ┌─────────────┐
│   Core 0    │     │   Core 1    │
│ DataFetcher │────▶│  Renderer   │
│  (WiFi/HTTP)│     │  (30fps)    │
└─────────────┘     └─────────────┘
       │                    │
       ▼                    ▼
   [MatchDB]          [HUB75 DMA]
```

- **Core 0** — Fetch HTTP, parsing HTML/JSON, NTP, OTA (stack 20KB)
- **Core 1** — Rendu 30fps DMA HUB75 (stack 10KB)
- **Client TLS frais par requête** — évite la corruption mbedtls

### Partition Flash

| Partition | Taille | Usage |
|---|---|---|
| `app0` (ota_0) | 2MB | Firmware courant |
| `app1` (ota_1) | 2MB | Slot OTA |
| `spiffs` | ~4MB | LittleFS — logos, Web UI, config |

---

## 🚀 Quick Start

### Prérequis

- [PlatformIO](https://platformio.org/install) (`pip install platformio`)
- Python 3 + Pillow (génération des logos)
- macOS / Linux / Windows

### Build & Flash

```bash
# Cloner
git clone https://github.com/Olivier-SGO/Rugby-ESP.git
cd Rugby-ESP

# Créer les credentials WiFi
cat > include/credentials.h <<EOF
#pragma once
#define WIFI_SSID     "MonReseau"
#define WIFI_PASSWORD "MonMotDePasse"
EOF

# Compiler
pio run -e matrixportal_s3

# Flasher le firmware
pio run -e matrixportal_s3 --target upload

# Flasher le filesystem (logos + Web UI)
pio run -e matrixportal_s3 --target uploadfs
```

> **Note** : Fermer le moniteur série avant le flash. Sur MatrixPortal S3, double-cliquez RESET si le flash échoue.

---

## 📱 Première utilisation

### 1. Branchez la carte
Alimentez la MatrixPortal S3 via USB-C. La LED onboard indique l'état :
| Couleur | État |
|---|---|
| 🔵 **Bleu** | Boot en cours |
| 🔷 **Cyan** | Fetch des données rugby |
| 🟢 **Vert** | Affichage actif |
| 🔴 **Rouge clignotant** | Erreur critique (PSRAM manquante ou échec Display.begin) |

### 2. Configuration WiFi (premier boot)
Si aucun réseau WiFi n'est configuré, la carte bascule automatiquement en **mode point d'accès** :
1. Sur votre téléphone ou ordinateur, connectez-vous au réseau **`RugbyDisplay-Setup`**
2. Un portail captif s'ouvre automatiquement (ou allez sur `http://192.168.4.1`)
3. Saisissez le **SSID** et le **mot de passe** de votre réseau WiFi
4. La carte sauvegarde les credentials en mémoire permanente (NVS) et redémarre

> 💡 **Astuce** : Si le portail ne s'ouvre pas, désactivez la connexion mobile (4G/5G) sur votre téléphone.

### 3. Web UI
Une fois connectée au WiFi, accédez à la configuration :
- **Adresse** : `http://rugby-display.local` (mDNS) ou l'IP affichée dans les logs série

**Paramètres disponibles** :
- 🏉 **Compétitions** — activer/désactiver Top 14, Pro D2, Champions Cup
- 💡 **Luminosité** — réglage de 0 à 255
- 🔄 **Mise à jour OTA** — activer/désactiver la mise à jour automatique
- ⏭️ **Scène suivante** — forcer manuellement le changement de scène

### 4. Affichage
L'écran alterne automatiquement entre les scènes (rotation toutes les ~10s) :
- **Scoreboard** — scores live avec minute de jeu et logos 64×64
- **Fixtures** — prochains matchs avec date et heure de kickoff
- **Classements** — scroll vertical avec zones playoffs (🟡 or) et relégation (🔴 rouge)

**Priorité live** : dès qu'un match est en cours, le scoreboard live remplace temporairement la rotation.

---

## 📡 Mise à jour OTA

### Automatique

1. Activer **"Mise à jour automatique"** dans la Web UI
2. Au prochain boot, l'ESP vérifie [GitHub Releases](https://github.com/Olivier-SGO/Rugby-ESP/releases/latest/download/version.json)
3. Si une nouvelle version existe : téléchargement → flash firmware → flash LittleFS → redémarrage

### Manuelle

Uploader un `.bin` directement depuis la Web UI :
- `POST /update/firmware` — firmware (`U_FLASH`)
- `POST /update/littlefs` — filesystem (`U_SPIFFS`)

### Créer une release

```bash
bash tools/create_release.sh v1.2.0
```

Le script compile, génère `version.json`, et pousse les assets sur le repo public [**rugby-display-releases**](https://github.com/Olivier-SGO/rugby-display-releases). Le code source reste privé sur ce repo.

---

## 🌐 Web UI

Accessible sur `http://rugby-display.local` (mDNS) ou par IP.

| Route | Méthode | Description |
|---|---|---|
| `/` | GET | Interface de configuration |
| `/status` | GET | WiFi, IP, heap, uptime |
| `/prefs` | GET/POST | Préférences compétitions et scènes |
| `/config` | POST | Luminosité |
| `/wifi` | GET/POST | Réseaux WiFi configurés |
| `/scan` | GET | Scan des réseaux disponibles |
| `/update/status` | GET | État OTA |
| `/update/check` | POST | Vérifier si une mise à jour est disponible |
| `/update/auto` | POST | Activer/désactiver auto-update |
| `/update/apply` | POST | Lancer la mise à jour |
| `/update/firmware` | POST | Upload manuel firmware |
| `/update/littlefs` | POST | Upload manuel filesystem |

---

## 📡 Sources de données

| Source | Données | Priorité |
|---|---|---|
| **Idalgo** (ladepeche.fr) | Résultats, live, fixtures, classements | 1 |

> **Note** : les parsers LNR (lnr.fr) et WorldRugby PulseLive sont présents dans le code source mais **non utilisés** dans la version actuelle. Toutes les données (résultats, fixtures, classements) proviennent d'Idalgo.

---

## 📁 Structure du projet

```
include/
  config.h              # Constantes HW, version firmware
  DisplayPrefs.h        # Préférences NVS
  MatchData.h           # Structs partagées
  TeamData.h            # Mapping 45 clubs
  fonts/                # Atkinson Hyperlegible (GFX)

src/
  main.cpp              # setup(), loop(), tâches FreeRTOS
  data/
    DataFetcher.cpp/h       # Orchestration WiFi/HTTP/NTP
    IdalgoParser.cpp/h      # Scraping HTML ladepeche.fr
    WorldRugbyAPI.cpp/h     # Fallback JSON (inactif)
    MatchDB.cpp/h           # Stockage RAM + persistance
    MatchRecord.cpp/h       # Enregistrement historique des matchs
    WiFiManager.cpp/h       # Gestion WiFi + mode AP
    OTAUpdater.cpp/h        # Mise à jour OTA
    WiFiClientSecureSmall.h # Client TLS avec buffers 4K
  display/
    DisplayManager.cpp/h  # Pilote HUB75 DMA
    SceneManager.cpp/h    # Rotation des scènes
    ScoreboardScene.cpp/h
    FixturesScene.cpp/h
    StandingsScene.cpp/h
    LogoLoader.cpp/h      # Chargement .bin RGB565 depuis LittleFS
    LogoCache.cpp/h       # Cache logos en PSRAM
    CompLogos.cpp/h       # Buffers statiques pour logos compétition
  web/
    WebUI.cpp/h         # Serveur web synchrone

data/
  logos/*.bin           # Logos clubs/compétitions RGB565
  index.html            # Web UI

tools/
  convert_logos.py      # Génération des logos
  create_release.sh     # Création de release GitHub
```

---

## 🧪 Simulateur Wokwi

```bash
pio run -e wokwi
# Ouvrir diagram.json dans VS Code (extension Wokwi)
```

---

## 📜 License

MIT — voir [LICENSE](LICENSE).

---

> 🏉 *UBB Fan*
