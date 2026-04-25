# Lessons Learned — Rugby ESP32 Display

Fichier de référence des bugs rencontrés, des fausses pistes et des solutions validées en production. À consulter avant toute nouvelle modification critique.

---

## 1. Heap fragmentation during boot fetch (TLS handshake failure)

**Symptôme** : Après le parse Top14, les fetches ProD2 et CC retournent HTTP -1. Le max contiguous heap chute de ~50K à ~19K entre les fetches.

**Cause** : Le renderer task (Core 1, 30fps, prio 2) alloue/libère des buffers en continu pendant que le boot fetch (Core 0) fait des TLS handshakes. Le handshake TLS nécessite ~35-40K de RAM contiguë. La fragmentation par le renderer laisse des blocs trop petits.

**Fausses pistes** :
- Augmenter les délais entre fetches → n'aide pas, le renderer continue de fragmenter
- Réduire la taille du parser buffer → le TLS handshake reste le goulot d'étranglement
- Utiliser PSRAM pour le parser buffer → ne résout pas le besoin de SRAM contiguë pour TLS

**Solution validée** : `vTaskSuspend(rendererHandle)` avant `fetchAll()`, `vTaskResume()` après. Le renderer est figé pendant tout le boot fetch, préservant le bloc contigu.

**Code** :
```cpp
static void bootFetchTask(void*) {
    // ... WiFi + NTP ...
    if (rendererHandle) vTaskSuspend(rendererHandle);
    Fetcher.fetchAll();
    if (rendererHandle) vTaskResume(rendererHandle);
    s_bootFetchDone = true;
    vTaskDelete(nullptr);
}
```

---

## 2. HTML entities not decoded → team names unrecognized

**Symptôme** : "Stade Français" et "Bordeaux-Bègles" ne sont pas reconnus par `findTeam()`. Les noms s'affichent en brut ou avec des caractères bizarres.

**Cause** : `ladepeche.fr` encode certains caractères accentués en entités HTML :
- `Stade Fran&ccedil;ais` (ç)
- `Bordeaux-B&egrave;gles` (è)
- `B&eacute;ziers` (é)
- `Angoul&ecirc;me` (ê)

Le parser `readClassText()` extrait le texte brut sans décoder ces entités. `findTeam()` compare `"Stade Fran&ccedil;ais"` avec `"Stade Français"` → échec. `stripAccents()` ne peut pas remplacer `&ccedil;` car c'est une séquence ASCII de 7 caractères, pas un caractère UTF-8.

**Fausses pistes** :
- Ajouter des variantes dans `TEAM_TABLE` (ex: `{"Stade Fran&ccedil;ais", ...}`) → non scalable, chaque site peut encoder différemment
- Modifier `stripAccents` pour gérer les entités → mélange deux couches différentes

**Solution validée** : Fonction `decodeHtmlEntities()` appelée sur tous les noms d'équipe extraits (matches + standings + calendar).

**Bug initial dans l'implémentation** :
```cpp
// ERREUR — MAP[m].ent inclut déjà le ';' final
if (strncmp(s + i + 1, MAP[m].ent, elen) == 0 && s[i + 1 + elen] == ';')
// "ccedil;" a elen=7, s[i+1+7] pointe sur le caractère APRÈS l'entité
// Condition toujours fausse → aucun remplacement

// CORRECT
if (strncmp(s + i + 1, MAP[m].ent, elen) == 0)
// strncmp compare déjà les elen caractères incluant le ';'
// Skip : i += elen + 1 (le +1 c'est le '&' initial)
```

**Endroits à modifier** (5 points dans `IdalgoParser.cpp`) :
- `parseMatchBlock` : `localteam_txt`, `visitorteam_txt`
- `parseStandingBlock` : `a_idalgo_content_standing_name`
- `parseCalendarPoolBlock` : `title` attribute (home + away)

---

## 3. LittleFS not flashed → Web UI "Not found"

**Symptôme** : La Web UI affiche "Not found" sur `/`.

**Cause** : `LittleFS.begin(false)` échoue silencieusement si la partition LittleFS n'a jamais été flashée ou a été corrompue par un hard reset. Le fichier `/index.html` n'existe pas.

**Solution** : `pio run -e matrixportal_s3 --target uploadfs`

**Note** : Après un hard reset complet ou un flash qui efface la flash, il faut **toujours** refaire `uploadfs`. Le firmware et le filesystem sont deux opérations séparées.

---

## 4. MatrixPortal S3 upload failure — "No serial data received"

**Symptôme** : `pio run --target upload` échoue avec `Failed to connect to ESP32-S3: No serial data received`.

**Cause** : Le reset automatique via DTR/RTS ne fonctionne pas toujours (port CDC/ACM, moniteur série ouvert, ou timing).

**Solution** : **Double-cliquer rapidement** sur le bouton RESET physique de la board. La LED NeoPixel passe en vert vif (mode bootloader). Lancer l'upload immédiatement après.

**Alternative** : Fermer tout moniteur série et réessayer — parfois le port est verrouillé.

---

## 5. Missing last match — `data-match="` delimiter bug

**Symptôme** : Top14 n'a que 6 matchs au lieu de 7, ProD2 n'a que 6-7 au lieu de 8. Le dernier match (souvent le dimanche) est systématiquement manqué.

**Cause** : `parseChunk` utilisait `data-match="` comme délimiteur de début de bloc et le prochain `data-match="` comme fin. Pour le DERNIER match de la page, il n'y a pas de `data-match` suivant → le bloc s'étendait jusqu'à la fin de la page (272KB au lieu de ~2.5KB). De plus, dans le HTML de `ladepeche.fr`, `data-status="0"` est AVANT `data-match="..."` dans la balise `<div>` :
```html
<div ... data-status="0" data-code-status="0" data-match="0-55776" ...>
```
`readAttrVal(start, "data-status", ...)` cherchait `data-status` en avant depuis `start` (qui pointait sur `data-match`). Il ne trouvait rien pour le dernier match (car pas de match suivant avec `data-status` dans le bloc). `parseMatchBlock` retournait `nullptr`.

**Solution** : Changer le délimiteur de bloc pour `<div class="div_idalgo_dom_match div_idalgo_dom_match_rugby"` avec `</li>` comme fin. Ainsi tous les attributs (`data-status`, `data-match`) sont dans le bloc, et chaque bloc fait ~2.4-2.6KB.

```cpp
const char* divStart = strstr(pos, "<div class=\"div_idalgo_dom_match div_idalgo_dom_match_rugby");
const char* blockEnd = strstr(divStart, "</li>");
if (blockEnd) blockEnd += 5;
```

---

## 6. ProD2 boundary bug — trailing match block ignored

**Symptôme** : Les derniers matchs d'une page ProD2 ne sont pas parsés.

**Cause** : `parseChunk` vérifiait `(!isLast && blockEnd == end)` pour décider de traiter le bloc. Si le bloc se termine exactement à la fin du buffer mais n'est pas le dernier chunk, il était ignoré.

**Solution** : Changer la condition en `(!isLast && nextDiv == nullptr)` — si on trouve un div de fin de bloc mais pas de div de début suivant, c'est un bloc incomplet qu'on garde pour le prochain chunk. Si on trouve un div de début suivant, on traite le bloc.

---

## 7. CC Champions Cup — from 6 requests to 1

**Symptôme** : Le fetch CC échouait parfois (timeouts, heap épuisé) avec l'approche de scan des 5 phases (6 requêtes, ~1.5MB).

**Solution** : Utiliser la page `.../champions-cup/calendrier` qui contient **tous** les matchs (pools + finals) en un seul HTML de ~378KB. Parser le format `li_idalgo_content_calendar_cup_date_match` qui couvre les 63 matchs de pool + les finales.

**Astuce** : La page contient aussi un format alternatif `li_idalgo_content_result_match` (14 blocs, 100% redondant). Ne pas s'y fier, le format pool est suffisant.

---

## 8. Standings parsing — avoiding duplicate tables

**Symptôme** : Le classement contenait 42 lignes au lieu de 14 (Top14) ou 16 (ProD2).

**Cause** : La page HTML contient plusieurs tableaux : général, domicile, extérieur, forme (J-5). Tous utilisent la même classe `li_idalgo_content_standing`.

**Solution** : Heuristique d'arrêt — quand `rank == 1` alors qu'on a déjà des entrées, c'est le début d'un nouveau tableau. On s'arrête là.

---

## 9. WiFiClient::setTimeout() takes seconds, not milliseconds

**Piège** : `client->setTimeout(15)` = 15 secondes, pas 15 ms. Ne pas mettre de valeur énorme.

---

## 10. Task stack size — HTTP/TLS needs 16KB minimum

**Piège** : Le défaut FreeRTOS (2-4KB) fait crasher silencieusement dès qu'on fait du HTTPS. Toute tâche qui fait du fetch HTTP/JSON/TLS doit avoir **au moins 16KB** de stack, 20KB pour être tranquille.

---

## 11. Renderer task preemption — direct display writes in loop()

**Piège** : Le renderer (Core 1, prio 2) préempte `loop()` (prio 1) en moins de 33ms. Toute écriture directe sur l'affichage dans `loop()` sans suspendre le renderer provoque des artefacts/corruption.

**Solution** : `vTaskSuspend(rendererHandle)` avant toute écriture directe, `vTaskResume()` après.

---

## Checklist avant chaque flash hardware

1. Build : `pio run -e matrixportal_s3`
2. Fermer le moniteur série (libère le port)
3. Flash firmware : `pio run -e matrixportal_s3 --target upload`
4. **Flash filesystem** : `pio run -e matrixportal_s3 --target uploadfs` (si nouvelle board ou hard reset)
5. Vérifier les logs : DNS OK, NTP OK, 3 fetches OK, pas de `Unknown team`
6. Vérifier la Web UI : `curl http://<IP>/` doit retourner le HTML
