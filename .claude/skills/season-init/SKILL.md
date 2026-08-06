---
name: season-init
description: Met à jour le firmware Rugby ESP32 pour une nouvelle saison (Top14/Pro D2/Champions Cup) — nouvelles équipes, logos, purge du cache. À lancer manuellement chaque année vers fin juillet/mi-août, avant la reprise Pro D2 (généralement fin août).
---

# Season init — Rugby ESP32

Procédure pour préparer le firmware à une nouvelle saison. Basée sur la mise à jour 2026-2027 (voir git log / CLAUDE.md pour le détail de cette itération).

## 1. Rechercher les compositions officielles de la saison à venir

Utiliser WebSearch/WebFetch (ces données sont hors connaissances d'entraînement, toujours vérifier en live) :
- Top14 (14 clubs) : équipe(s) reléguée(s) en Pro D2, équipe(s) promue(s) de Pro D2 (barrage d'accession).
- Pro D2 (16 clubs) : équipe(s) reléguée(s) en Nationale, équipe(s) promue(s) de Nationale (champion + vainqueur barrage).
- Champions Cup (24 clubs, dont 8 Top14 déjà couverts) : liste des 16 clubs non-français qualifiés (8 Premiership + 8 URC) — change chaque année selon le classement de la saison précédente dans chaque championnat.

Comparer avec `include/TeamData.h` (`TEAM_TABLE`) pour identifier :
- les clubs à **ajouter** (absents de la table)
- les clubs à **retirer** (relégués en Nationale, ou non re-qualifiés en CC cette saison — Top14/ProD2 n'ont pas de 3e palier suivi par l'app)

Le Top14 n'a généralement pas besoin de changement si les mises à jour de l'an dernier ont déjà anticipé la montée/descente (vérifier quand même).

## 2. Mettre à jour `include/TeamData.h`

Ajouter/retirer les lignes dans `TEAM_TABLE`, dans la bonne section (Top14 / Pro D2 / Champions Cup). Chaque ligne : `{idalgo, worldrugby, lnr, canonical, abbrev, slug}`. Pour un club français, `idalgo`/`worldrugby`/`lnr` sont en général le même nom ; pour un club étranger (CC), `lnr` reste `""`.

## 3. Mettre à jour `tools/convert_logos.py`

- Miroir des mêmes ajouts/retraits dans la liste `CLUBS`.
- Pour un nouveau club, essayer d'abord `None` (CDN LNR par défaut) — fonctionne pour la plupart des clubs français. Si 404, chercher une image de logo (Wikipédia généralement) et mettre l'URL en override.
- Lancer la génération ciblée sur les nouveaux slugs uniquement (évite de re-télécharger tout et de se faire rate-limiter par Wikimedia) :
  ```bash
  python3 tools/convert_logos.py <slug1> <slug2> ...
  ```
  (le script accepte une liste de slugs en argv pour ne (re)générer qu'un sous-ensemble — sans argument il régénère tout)
- Supprimer manuellement les `.bin`/`_sm.bin` désormais inutiles dans `data/logos/` pour les clubs retirés de `TEAM_TABLE`.

## 4. Vérifier visuellement la qualité des nouveaux logos AVANT de valider

Les fichiers `.bin` sont du RGB565 brut, pas directement visualisables. Utiliser `tools/preview_logos.py` (écrit lors de la mise à jour 2026-2027) :
```bash
python3 tools/preview_logos.py <slug1> <slug2> ...
```
Écrit les PNG dans `tools/logo_previews/` (gitignoré, chemin stable dans le repo — pas un dossier temporaire qui disparaît en fin de session). Utiliser ensuite `Read` sur les PNG produits pour les afficher. Vérifier surtout la version `_sm` (16×16, utilisée dans les classements) — c'est là que les logos avec du texte fin deviennent illisibles.

## 5. Bumper `SEASON_TAG` — purge automatique du cache au boot

`SEASON_TAG` dans `include/config.h` doit changer à chaque saison (même règle que `TEAM_TABLE`). `MatchDB::load()` (`src/data/MatchDB.cpp`) compare ce tag à celui stocké dans `cache.json` au boot et purge automatiquement (`/cache.json` + `/cc_data.bin`) en cas de mismatch — **avant même de charger l'ancien classement/round_ids**. C'est le mécanisme principal, il s'exécute à chaque démarrage donc fonctionne aussi pour une carte mise à jour uniquement par **OTA** (pas d'accès réseau local requis). Ne pas oublier de bumper `SEASON_TAG` en même temps que `FIRMWARE_VERSION` — un oubli laisserait les cartes déjà à jour conserver l'ancien classement indéfiniment.

L'endpoint `POST /reset-cache` (`src/web/WebUI.cpp`) reste disponible en secours pour un test manuel, mais ne pas en dépendre pour le déploiement réel.

## 6. Version + build + déploiement

- Bump `FIRMWARE_VERSION` **et** `SEASON_TAG` dans `include/config.h`.
- ```bash
  export PATH="$HOME/Library/Python/3.9/bin:$PATH"
  pio run -e matrixportal_s3
  pio run -e matrixportal_s3 --target uploadfs
  pio run -e matrixportal_s3 --target upload --upload-port /dev/cu.usbmodem1101
  ```
  (La purge du cache 2025-26 se fait automatiquement au premier boot du nouveau firmware, flashé en local ou reçu par OTA.)
- Suivre `tools/create_release.sh` si une release GitHub est souhaitée (nécessaire pour que les cartes en OTA la récupèrent).

## 7. Vérification

- Surveiller le moniteur série (lancé par l'utilisateur, TTY réel requis — voir CLAUDE.md) pendant le premier fetch : repérer d'éventuels `"Unknown home/away/standing team: '...'"` qui signalent un nom Idalgo différent de celui supposé pour un nouveau club.
- Vérifier à l'écran : logos + noms corrects pour les nouveaux clubs, plus aucun résidu de classement/round de la saison précédente.

## Important

Cette procédure implique une vérification humaine (qualité visuelle des logos, flash de la carte physique) — ne pas l'exécuter de façon totalement autonome/non supervisée. Lancer ce skill manuellement en fin de saison morte, puis valider chaque étape avant de flasher.
