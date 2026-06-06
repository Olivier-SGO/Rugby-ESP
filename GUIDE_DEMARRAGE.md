# Guide de démarrage — Rugby ESP32 Display

> Ce guide est destiné à l'utilisateur final. Aucune connaissance technique n'est requise.

---

## 1. Contenu du boîtier

Votre **Rugby Display** est composé de :
- Une carte **MatrixPortal S3** (le cerveau bleu avec la LED et le port USB-C)
- **2 panneaux LED HUB75** chaînés (surface totale 256×64 pixels)
- Un câble **USB-C** pour l'alimentation

---

## 2. Branchement

1. Branchez le câble USB-C dans la carte MatrixPortal S3.
2. Branchez l'autre extrémité sur un chargeur USB (5V, 2A minimum recommandé).
3. La LED sur la carte s'allume immédiatement.

> **Conseil** : utilisez un chargeur mural de bonne qualité. Un port USB d'ordinateur peut ne pas fournir assez de courant pour les panneaux LED à pleine luminosité.

---

## 3. Premier boot — suivez la LED

La LED RGB sur la carte vous indique l'état du démarrage :

| Couleur LED | Signification | Temps approx. |
|-------------|---------------|---------------|
| **Bleu** | Démarrage en cours | 2–3 secondes |
| **Vert** | Afficheur initialisé, test couleurs | 3 secondes |
| **Cyan** | Connexion WiFi + récupération des données | 30–90 secondes |
| **Éteinte** (ou faible) | Fonctionnement normal | — |

> **Important** : l'étape **cyan** peut durer jusqu'à **1 minute 30** lors du premier démarrage ou après une longue coupure. L'ESP télécharge les scores, calendriers et classements depuis Internet. **Ne débranchez pas pendant cette phase.**

---

## 4. Configuration WiFi (première utilisation)

Au premier démarrage, le display ne connaît pas votre réseau WiFi. Il démarre automatiquement un **point d'accès** :

- **Nom du réseau** : `RugbyDisplay-Setup`
- **Mot de passe** : `rugby2024`

### Étapes :
1. Sur votre téléphone ou ordinateur, connectez-vous au réseau WiFi `RugbyDisplay-Setup`.
2. Ouvrez un navigateur et allez à l'adresse : **`http://rugby-display.local`** ou **`http://192.168.4.1`**
3. Dans la section **"Réseaux enregistrés"**, vos réseaux déjà configurés apparaissent. Cliquez sur **"Scanner les réseaux"** pour en ajouter un nouveau, sélectionnez-le, entrez le mot de passe, puis **"Ajouter à la liste"**.
4. Cliquez sur **"Sauvegarder et redémarrer"**. Le display redémarre et se connecte à votre réseau.

> **Multi-réseau** : vous pouvez enregistrer plusieurs réseaux WiFi (domicile + hotspot téléphone, par exemple). Le display se connecte automatiquement au plus fort disponible.

> **Note** : si vous déplacez le display dans un nouveau lieu, il tente de se connecter aux réseaux connus pendant **2 minutes**, puis passe en mode point d'accès pendant **5 minutes**. Vous pouvez alors ajouter le nouveau réseau sans effacer les anciens.

---

## 5. Accéder à l'interface Web (Web UI)

Vous pouvez contrôler le display depuis votre téléphone ou ordinateur :

- **En mode normal (WiFi connecté)** : `http://rugby-display.local` — sur le même réseau WiFi
- **En mode AP (RugbyDisplay-Setup)** : `http://rugby-display.local` ou `http://192.168.4.1` — après vous être connecté au réseau `RugbyDisplay-Setup`
- **Alternative** : utilisez l'adresse IP affichée brièvement sur l'écran au boot (ex: `http://192.168.1.42`)

### Fonctions disponibles dans la Web UI :
- **Scène suivante / Championnat suivant / Championnat précédent** : forcer le changement d'affichage
- **Luminosité** : régler l'intensité des LED (10–255)
- **Durées** : temps d'affichage des résultats, prochains matches et classements
- **Championnats** : activer/désactiver Top 14, Pro D2, Champions Cup
- **Réseaux WiFi** : ajouter, modifier ou supprimer des réseaux
- **Mise à jour** : vérifier et installer les mises à jour automatiques
- **Redémarrer** : redémarrer à distance le display

---

## 6. Ce qui s'affiche

Le display fait défiler automatiquement les informations des championnats activés :

### Résultats (Scoreboard)
- Logos des équipes, score final ou score en direct
- Minute de jeu pour les matches en cours (vert)
- Journée ou phase (poule, 1/4, 1/2, Finale)
- **Durée** : 8 secondes par match

### Prochains matches (Fixtures)
- Date et heure du coup d'envoi
- Logos des équipes
- **Durée** : 8 secondes par match

### Classement (Standings)
- Défilement vertical des équipes avec points et couleurs
- Or = playoffs, Blanc = milieu de tableau, Rouge = relégation
- **Durée** : 20 secondes

### Ordre d'affichage
Les scènes sont groupées par championnat : **Top 14** → **Pro D2** → **Champions Cup**, puis elles recommencent en boucle.

---

## 7. Fréquence de rafraîchissement des données

Le display se connecte automatiquement à Internet pour mettre à jour les données :

| Situation | Fréquence |
|-----------|-----------|
| **Fonctionnement normal** (pas de match imminent) | Toutes les **3 minutes**, un championnat à la fois |
| **Match imminent ou en cours** (hot mode) | Toutes les **1 minute** |
| **Match en direct (live)** | Toutes les **30 secondes** |
| **Heure (NTP)** | Toutes les **1 heure** |

> **Données couvertes** : Top 14, Pro D2, Champions Cup — résultats, calendriers et classements.

---

## 8. Boutons physiques

Deux boutons sont situés sur la carte MatrixPortal S3 :

| Bouton | Appui court (< 0,6 s) | Appui long (> 0,6 s) | Très long (> 5 s) |
|--------|----------------------|----------------------|-------------------|
| **UP** (▲) | Championnat **suivant** | Augmente la **luminosité** (+10) | — |
| **DOWN** (▼) | Championnat **précédent** | Diminue la **luminosité** (-10) | — |
| **UP + DOWN** | — | — | **Reset WiFi** (efface tous les réseaux et redémarre) |

> La luminosité est sauvegardée automatiquement. Elle persiste après un redémarrage.

---

## 9. Dépannage

### L'écran reste noir ou affiche du bruit
- Vérifiez le branchement USB-C et l'alimentation (2A minimum).
- Débranchez et rebranchez le câble.
- Si la LED clignote **rouge**, c'est une erreur d'initialisation de l'afficheur. Contactez le support.

### La LED reste cyan très longtemps (> 2 minutes)
- Vérifiez que le WiFi est bien configuré (voir §4).
- Le serveur de données (ladepeche.fr) peut être temporairement lent. Attendez encore 1–2 minutes.
- Si le problème persiste, redémarrez le display (bouton Reset sur la carte, ou débranchez/rebranchez).

### Le display ne se connecte pas au WiFi
- Vérifiez que le réseau est en 2,4 GHz (le ESP32-S3 ne supporte pas le 5 GHz).
- Assurez-vous que le mot de passe est correct dans la Web UI.
- Si vous avez changé de box ou de mot de passe, le display passera en mode AP au bout de **2 minutes**. Connectez-vous à `RugbyDisplay-Setup` et ajoutez le nouveau réseau.
- Vous pouvez enregistrer plusieurs réseaux (domicile + hotspot) — le display se connecte au premier disponible.

### L'adresse `rugby-display.local` ne fonctionne pas
- `rugby-display.local` fonctionne aussi en mode AP (`RugbyDisplay-Setup`) — connectez-vous d'abord à ce réseau.
- Sur certains téléphones Android, le mDNS ne fonctionne pas bien. Utilisez `http://192.168.4.1` en mode AP, ou l'IP locale en mode normal.

### Je veux changer de réseau WiFi

**Méthode rapide (factory reset physique)** :
1. Maintenez les deux boutons **UP + DOWN** enfoncés pendant **5 secondes**
2. Le display efface tous les réseaux enregistrés et redémarre automatiquement
3. Au boot suivant, il passe en mode `RugbyDisplay-Setup`. Reconfigurez (voir §4).

**Méthode automatique** :
1. Placez le display hors de portée de son réseau WiFi connu (ou éteignez votre box).
2. Attendez **2 minutes** — le display tente de se reconnecter, puis passe en mode `RugbyDisplay-Setup` pendant 5 minutes.
3. Connectez-vous à `RugbyDisplay-Setup` et ajoutez ou modifiez un réseau (voir §4).

---

## 10. Bonnes pratiques

- **Ne débranchez pas** le display pendant la phase cyan (récupération des données).
- Évitez les coupures de courant répétées ; préférez le bouton **Redémarrer** de la Web UI.
- En cas de déplacement, vous n'avez rien à faire de spécial : le display détectera automatiquement l'absence de WiFi connu et proposera le mode configuration après 2 minutes.
- La luminosité par défaut est **80**. Vous pouvez l'augmenter jusqu'à **255**, mais cela consomme plus et réchauffe les panneaux LED.
- **Indicateur mode AP** : quand le display est en point d'accès (`RugbyDisplay-Setup`), le texte **"AP"** en orange apparaît à côté de l'icône WiFi déconnectée sur l'écran.
- **Hotspot téléphone** : vous pouvez enregistrer le hotspot de votre iPhone ou Android comme réseau de secours. Activez le hotspot avant de démarrer le display pour qu'il soit visible lors du scan.

---

## 11. Caractéristiques techniques (récapitulatif)

| Caractéristique | Valeur |
|-----------------|--------|
| Résolution | 256 × 64 pixels |
| Taille panneaux | 2× 128×64 chaînés |
| Alimentation | USB-C, 5V, 2A |
| Connexion | WiFi 2,4 GHz |
| Compétitions | Top 14, Pro D2, Champions Cup |
| Rafraîchissement normal | 3 min |
| Rafraîchissement live | 30 s |

---

*Version du guide : v1.2 — Firmware 1.3.9*
