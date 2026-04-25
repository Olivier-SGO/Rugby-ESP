# Optimisation du fetch HTTPS sur ESP32-S3 — Retour d'expérience

**Date :** 2026-04-22  
**Contexte :** Rugby ESP32 Display, fetch des pages Idalgo (ladepeche.fr / rugbyrama.fr) sur ESP32-S3 avec Arduino-ESP32 v3.x.

---

## 1. Problématique

L'ESP32-S3 (240 MHz, 320 KB SRAM) doit télécharger des pages HTML de ~500–900 KB via HTTPS (TLS 1.2). Contraintes :
- **Pas de PSRAM** utilisable pour un buffer complet (la PSRAM est lente en écriture, ~1 KB/s en direct)
- **Heap limité** (~114 KB libres après init display)
- **WDT** sensible aux boucles bloquantes > 1 s sur Core 1
- **Framework Arduino-ESP32 v3.x** : `HTTPClient` + `WiFiClientSecure` ont des comportements différents de v2.x

---

## 2. Méthodes testées

### 2.1 HTTPClient + WiFiClientSecure (approche "standard")

```cpp
HTTPClient http;
WiFiClientSecure client;
client.setInsecure();
http.begin(client, url);
int code = http.GET();
```

**Résultat :** `HTTP -1` systématique sur `rugbyrama.fr` et `ladepeche.fr`.

**Cause probable :** dans Arduino-ESP32 v3.x, `HTTPClient::begin(WiFiClient&, String)` a un comportement erratique avec `WiFiClientSecure`. Le handshake TLS réussit mais `http.GET()` retourne -1 (échec de `connectToHost` interne).

**Verdict :** ❌ Inutilisable en l'état.

---

### 2.2 Fetch manuel HTTP/1.1 sur WiFiClientSecure

```cpp
WiFiClientSecure* client = new WiFiClientSecure;
client->setInsecure();
client->connect(host, 443);
client->print("GET " + path + " HTTP/1.1\r\n...");
// lecture manuelle des headers + body
```

**Résultat :** fonctionne. TLS handshake OK, HTTP 200, données reçues.

**Problèmes rencontrés :**
- **Buffer PSRAM de 1 MB** (`ps_malloc` + `ps_readBytes`) → débit catastrophique (~1 KB/s, 110 s pour une page de 110 KB). La PSRAM n'est pas faite pour du streaming direct.
- **Boucle `readBytesUntil('\n')` bloquante** → WDT reset si le serveur tarde.
- **Client réutilisé** (`WiFiClientSecure` partagé entre requêtes) → corruption du contexte mbedtls après 2-3 requêtes, handshake qui dégrade de 5 s → 25 s → `connection refused`.

**Verdict :** ✅ Fonctionnel, mais nécessite :
- Buffer **SRAM** (pas PSRAM)
- Parsing en **streaming** (pas de buffer complet)
- **Client frais** par `fetchAll()` ou reconnexion explicite

---

### 2.3 Keep-alive TLS (client partagé, requêtes séquentielles)

Idée : un seul handshake pour N requêtes sur le même host.

```cpp
WiFiClientSecure client;
client.setInsecure();
// requête 1 : handshake + download
// requête 2 : réutilise le socket (Connection: keep-alive)
```

**Résultat sur rugbyrama.fr :**
- Premier handshake : **48 s** (WAF tarpit / challenge JA3)
- Reconnexions suivantes : **0,5–1,2 s** (session TLS réutilisée ou IP whitelistée)
- Le serveur ferme quand même la connexion après chaque réponse → re-handshake à chaque requête

**Verdict :** ⚠️ Partiel. Le gain est limité car le serveur ne garde pas le socket ouvert.

---

### 2.4 Approche finale retenue

```cpp
// DataFetcher::fetchAll()
WiFiClientSecure client;
client.setInsecure();

IdalgoParser idalgo;
CompetitionData d;

// Top14
idalgo.fetch("https://www.ladepeche.fr/sports/resultats-sportifs/rugby/top-14/phase-reguliere/resultats", d, client);
fetchNextJournee(d, "top-14", idalgo, client);  // journee-N + journee-N+1

// ProD2
idalgo.fetch("https://www.ladepeche.fr/sports/resultats-sportifs/rugby/pro-d2/phase-reguliere/resultats", d, client);
fetchNextJournee(d, "pro-d2", idalgo, client);

// CC
fetchCC(idalgo, d, client);

client.stop();
```

**Points clés :**
- **Fetch manuel** (pas `HTTPClient`)
- **Buffer 8 KB en SRAM** (`malloc`), parsing streaming chunk par chunk
- **Client frais** par cycle `fetchAll()`
- **Pages résultats seules** (pas de page calendrier doublon — la page résultats contient déjà standings + résultats + fixtures de la journée en cours)
- **`fetchNextJournee`** pour récupérer la journée N+1 explicitement

---

## 3. Benchmarks TLS observés

### 3.1 ladepeche.fr (site retenu)

| Métrique | Valeur |
|----------|--------|
| Cipher suite | `ECDHE-RSA-AES256-GCM-SHA384`, RSA 2048-bit |
| Handshake TLS (premier) | **~1,5–4,5 s** |
| Handshake TLS (reconnexions) | **~0,5–1 s** |
| Download page Top14 | ~472 KB en ~3–5 s |
| Download page ProD2 | ~497 KB en ~3–5 s |
| Download page CC | ~936 KB en ~6–8 s |

**Pourquoi ladepeche.fr est meilleur :** pas de WAF tarpit agressif. Le premier handshake est rapide.

### 3.2 rugbyrama.fr (abandonné)

| Métrique | Valeur |
|----------|--------|
| Cipher suite | Identique (`ECDHE-RSA-AES256-GCM-SHA384`) |
| Handshake TLS (premier) | **~14–48 s** (WAF challenge JA3) |
| Handshake TLS (reconnexions) | **~0,5–1,2 s** (IP whitelist après premier défi) |
| Download | identique |

**Conclusion :** même infrastructure, même certificat, mais `rugbyrama.fr` a un WAF qui ralentit les fingerprints TLS non-navigateur (mbedtls vs OpenSSL).

---

## 4. Stockage structuré économe en ressources

### 4.1 Contexte mémoire

Après boot complet (logos PSRAM, display DMA, web server) :
- **Heap libre** : ~114 KB
- **Max block** : ~70 KB
- Pas de PSRAM utilisable pour du streaming rapide

### 4.2 Struct `CompetitionData` (RAM)

```cpp
struct CompetitionData {
    MatchData results[MAX_MATCHES];      // 8 × ~48 B
    MatchData fixtures[MAX_MATCHES];     // 8 × ~48 B
    StandingEntry standings[MAX_STANDING]; // 16 × ~24 B
    uint8_t result_count, fixture_count, standing_count;
    uint8_t current_round;
    uint32_t round_url_base;  // pour générer les URLs de journee
};
```

Taille totale : **< 2 KB** par compétition. 3 compétitions = ~6 KB.

### 4.3 Persistance binaire compacte — `CCMatchRecord`

Pour la Champions Cup (ou toute compétition), un record binaire de **51 octets** :

```cpp
struct CCMatchRecord {
    uint32_t epoch;           // kickoff UTC
    char home_slug[8];        // slug canonique
    char away_slug[8];
    int8_t home_score;        // -1 = non joué
    int8_t away_score;
    uint8_t status;           // 0=prévu, 1=terminé, 2=live
    uint8_t round;
    char group;               // poule A/B/C/D...
    uint8_t crc8;             // checksum
} __attribute__((packed));
```

**Avantages :**
- 51 octets × 50 matchs = **2,5 KB** (vs ~20 KB en JSON texte)
- Lecture/écriture O(1), pas de parsing JSON
- CRC8 pour détecter la corruption (LittleFS sur flash wear-leveling)

**Implémentation :** `src/data/MatchRecord.cpp` avec `fromMatchData()` / `toMatchData()`.

### 4.4 Persistance JSON (fallback)

`MatchDB` persiste aussi en JSON LittleFS pour le debug et la compatibilité Web UI :
- `db_top14.json`
- `db_prod2.json`
- `db_cc.json`

Mais le JSON n'est **pas lu au boot** — c'est le binaire `MatchRecord` qui est utilisé pour le chargement rapide.

---

## 5. Architecture du parsing streaming

### 5.1 Buffer circulaire / overlap

```cpp
static const size_t BUF_SZ = 8192;        // buffer principal
static const size_t MAX_BLOCK_SCAN = 2048; // look-ahead pour un bloc incomplet

char* _buf = (char*)malloc(BUF_SZ + 1);

// Lecture par chunks
size_t rd = client.readBytes(_buf + overlapLen, space);
_buf[overlapLen + rd] = '\0';

size_t deferOffset = parseChunk(_buf, overlapLen + rd, out, isLast);

// Overlap : garder le début du dernier bloc incomplet
if (!isLast && deferOffset < total) {
    memmove(_buf, _buf + deferOffset, total - deferOffset);
    overlapLen = total - deferOffset;
}
```

**Pourquoi 8 KB :** avec 4 KB, on avait des `buffer full, dropping` sur les pages calendrier (> 700 KB) car les blocs HTML entre deux `data-match` pouvaient dépasser 4 KB.

### 5.2 Détection de `round_url_base`

Pour générer les URLs de journee dynamiques, le parser extrait l'ID interne Idalgo :

```cpp
// href="/sports/resultats-sportifs/rugby/top-14/phase-reguliere/resultats/11587/journee-21"
// round_url_base = id + round = 11587 + 21 = 11608
// URL journee-22 : /resultats/11586/journee-22  (car 11608 - 22 = 11586)
```

**Piège :** sur `rugbyrama.fr`, l'URL contient `/resultats/` **deux fois** (`/resultats/rugby/.../resultats/ID/journee-N`). Il faut chercher le **dernier** `/resultats/` suivi d'un chiffre, pas le premier.

---

## 6. Checklist pour un nouveau site source

Si on doit migrer vers un autre site Idalgo :

1. [ ] Vérifier avec `curl` que le site répond en < 1 s et retourne 200
2. [ ] Tester le handshake TLS depuis l'ESP32 (log du temps `client.connect()`)
3. [ ] Si premier handshake > 10 s → tester `ladepeche.fr` comme fallback
4. [ ] Vérifier la structure HTML (`data-match`, `data-status`, classes CSS)
5. [ ] Vérifier les liens de journee et corriger `round_url_base` si besoin
6. [ ] Vérifier que les standings sont présents dans la page résultats
7. [ ] Supprimer les requêtes doublons (calendrier si identique à résultats)

---

## 7. Recommandations

| Situation | Recommandation |
|-----------|----------------|
| Site avec WAF tarpit | Changer de source (ladepeche.fr est plus clément) |
| Handshake > 10 s | Ne pas réutiliser le client TLS — client frais + accepter le délai |
| Pages > 500 KB | Buffer 8 KB minimum, parsing streaming obligatoire |
| Pas de PSRAM | Jamais de `ps_readBytes`/`ps_malloc` pour du streaming réseau |
| Fréquence de fetch | Normal : toutes les 5 min. Live : toutes les 30–60 s |
| Persistance | Binaire 51 octets pour le boot rapide, JSON pour le debug |
