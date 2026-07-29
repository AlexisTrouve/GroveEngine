# Saisie de texte — sélection, presse-papiers, multiligne

> **Statut :** plan (2026-07-29). Branche worktree `worktree-ui-textinput`.
> **Périmètre :** faire de `UITextInput` un champ de saisie *correct* — au sens où un utilisateur
> qui a déjà tapé dans un champ de texte ailleurs ne remarque rien d'anormal.
> **Débloque :** renommer un vaisseau, chat, console de debug in-game, tout formulaire.

---

## 1. Constat — l'état réel du widget (mesuré, pas supposé)

`UITextInput` a été écrit pour la police **8×8 monospace** et n'a jamais été repris depuis. Le passage
à une vraie TTF proportionnelle (chantier 9-slice, 2026-07-28) l'a laissé derrière. Quatre défauts,
tous vérifiés dans le code :

| # | Défaut | Preuve | Conséquence |
|---|---|---|---|
| **D1** | **Le curseur est positionné en monospace** — `getCursorPixelOffset()` retourne `cursorPosition * CHAR_WIDTH` avec `CHAR_WIDTH = 8.0f` en dur | `UITextInput.cpp:301-304`, `UITextInput.h:173` | Avec Roboto (proportionnel), le curseur dérive de plus en plus à droite du vrai point d'insertion. Même famille de bug latent que l'ancrage-centre des glyphes : **le monospace le masquait** |
| **D2** | **Le curseur est indexé en OCTETS, pas en codepoints** — `text.erase(cursorPosition - 1, 1)` retire **un octet** | `UITextInput.cpp:269, 277, 231` | Backspace sur « é » (2 octets UTF-8) coupe le codepoint en deux → texte corrompu. **En français, c'est un bug de tous les jours.** Ironique : `insertFilteredText` a été écrit *exprès* pour l'UTF-8 multi-octets en entrée, mais la suppression le recasse |
| **D3** | **Le clic ne place pas le curseur** | `UITextInput.cpp:138` — `// TODO: Calculate click position and set cursor there` | On ne peut éditer qu'en bout de chaîne, aux flèches |
| **D4** | **Les modificateurs sont jetés** — `bool ctrl = false; // TODO: Add ctrl modifier to UIContext` | `UIModule.cpp:911` | Ctrl+A/C/V sont **indétectables**, donc les branches `ctrl && keyCode=='c'` de `onKeyInput` (`UITextInput.cpp:185-196`) sont du code mort |

Et trois absences assumées, annoncées dans l'en-tête du header (`UITextInput.h:49,54`) :
sélection (« future »), copier/coller (« future »), multiligne (jamais évoqué).

**Bonne nouvelle sur D4 :** `InputModule` publie **déjà** `shift`/`ctrl`/`alt` dans
`input:keyboard:key` (`Core/InputConverter.cpp:38-41`, alimentés par `KMOD_*` dans
`Backends/SDLBackend.cpp:33-35`). C'est l'UIModule qui les laisse tomber au sol. **Aucune modification
d'InputModule n'est nécessaire** pour les modificateurs — le câble existe, il n'est pas branché.

---

## 2. Le vrai problème d'architecture : mesurer le texte

Tout le reste en découle. Placer un curseur, dessiner un surlignage de sélection, convertir un clic en
index, faire défiler horizontalement : **ce sont tous des questions de largeur de glyphe**. Or
l'UIModule ne connaît pas la police — elle vit dans BgfxRenderer (`Text/BitmapFont.h`, atlas TTF cuit
par `loadTTF`), et l'UIModule est délibérément découplé du renderer (il ne fait que publier des
topics).

### Options

| | Approche | Verdict |
|---|---|---|
| **A** | **Aller-retour par topic** : l'UIModule demande `render:text:measure`, le renderer répond | ❌ Asynchrone sur un chemin qui doit être synchrone (placer un curseur *dans* la frame du clic). Latence + machine à états pour rien |
| **B** | **Le renderer POUSSE sa table de métriques** au chargement de police ; l'UIModule la met en cache et mesure localement | ✅ **Retenu.** Un seul sens, pas de requête. La mesure redevient une fonction pure, testable headless |
| **C** | L'UIModule recuit sa propre copie de la TTF (stb_truetype) | ❌ Duplique le travail et **peut diverger** de ce qui est réellement dessiné. Le pire des mondes : deux vérités |
| **D** | Rester approximatif, exposer `charWidth` en propriété JSON | ❌ C'est le statu quo habillé. Un champ de saisie dont le curseur ment n'est pas un champ de saisie |

### Ce que B implique

Un cœur **pur, header-only** — `include/grove/text/TextMetrics.h`, `grove::text` — exactement le
patron déjà éprouvé par `grove::anim`, `grove::light`, `grove::fx`, et **surtout** par
`TextFit.h` du même sous-système, dont la leçon centrale était : *« l'avance des glyphes est un
callable, donc ce fichier ne sait rien de la police ni du GPU — c'est ce qui le rend testable
headless »*. On reprend exactement ce contrat, avec une table de données au lieu d'un callable.

```
grove::text::Metrics          // table codepoint → avance (à la taille de base) + hauteur de ligne
  measure(str, fontSize)      // largeur d'une chaîne
  xAtIndex(str, byteIdx, ...) // position pixel d'un index → curseur, ancre de sélection
  indexAtX(str, x, ...)       // pixel → index (frontière de codepoint la plus proche) → clic
  prevIndex/nextIndex(str, i) // pas de curseur UTF-8-safe → répare D2
```

Table **absente** ⇒ repli sur l'avance monospace historique (`CHAR_WIDTH`) : le comportement actuel
reste **strictement inchangé** tant que personne ne charge de police. C'est le patron « défaut à coût
zéro » déjà utilisé pour `render:ambient` et `maxWidth` — un consommateur existant ne voit rien bouger.

**Transport :** le renderer publie `render:font:metrics {baseSize, lineHeight, advances}` quand une
police est chargée (au boot via `fontPath`, et sur `render:font` à chaud). L'UIModule s'y abonne et
alimente son `Metrics`. La table est petite (ASCII + Latin-1 ≈ 224 entrées) et ne circule qu'au
changement de police, pas par frame.

**Bénéfice au-delà de ce chantier :** un jeu qui veut mesurer son propre texte de HUD (centrer un
libellé, dimensionner une bulle) obtient la même API sans passer par le renderer.

---

## 3. Découpage en tranches

Chaque tranche : **test rouge d'abord → implémentation → vert → commit**. Aucune tranche N+1 avant que
N soit verte.

### T0 — Le socle : mesurer et se déplacer correctement *(prérequis de tout le reste)*

Sans ça, un surlignage de sélection est peint au mauvais endroit et un clic tombe sur le mauvais
caractère : construire T1 sur D1+D2 reviendrait à décorer une fondation fausse.

- **T0a — `grove::text::Metrics`** (`include/grove/text/TextMetrics.h`, pur, header-only).
  *Rouge :* `TextMetricsUnit` avec une table **volontairement proportionnelle** (fake : `i`=4, `M`=20)
  — un oracle qu'une implémentation monospace ne peut pas satisfaire. Cas : mesure, aller-retour
  `xAtIndex`/`indexAtX`, frontières UTF-8 (« é », « — »), chaîne vide, x hors bornes.
- **T0b — UTF-8 (D2).** Backspace/Suppr/flèches passent par `prevIndex`/`nextIndex`.
  *Rouge :* E2E — taper « é » puis Backspace ⇒ le champ est **vide**, pas un demi-octet.
- **T0c — Transport (D1).** `render:font:metrics` publié par BgfxRenderer, consommé par UIModule,
  injecté dans les widgets ; repli monospace si absent.
  *Rouge :* E2E — après une table proportionnelle injectée, la position du curseur pour un index donné
  **diffère** de `index × 8`.
- **T0d — Clic → curseur (D3).** *Rouge :* E2E — clic à x donné ⇒ index attendu ; clic au-delà de la
  fin ⇒ fin de chaîne.

### T1 — Sélection

- Modèle : `selectionAnchor` + `cursorPosition`, sélection = `[min, max)`. Ancre == curseur ⇒ pas de
  sélection (aucun état supplémentaire à synchroniser).
- **Modificateurs (D4)** : `shift`/`ctrl` propagés dans `UIContext` depuis `input:keyboard:key`
  (le payload les porte déjà) — supprime le `TODO` de `UIModule.cpp:911`.
- Shift+flèches / Shift+Début/Fin étendent ; une flèche seule **replie** la sélection (sur le bord
  correspondant, pas sur la position du curseur — c'est la convention partout).
- Glisser-souris sélectionne ; **double-clic** sélectionne le mot ; Ctrl+A tout.
- Taper ou effacer **avec** une sélection la **remplace**.
- Rendu : un rectangle de surlignage derrière le texte — `selectionColor` **existe déjà** dans
  `TextInputStyle` (`UITextInput.h:38`) et n'est utilisé nulle part.
- *Preuve :* E2E pour le modèle + **un test `[gpu]` au pixel** pour le surlignage. Le rendu ne se lit
  pas dans le code — c'est la leçon n°1 du chantier 9-slice (`ui-nineslice-handoff.md` §7).

### T2 — Presse-papiers

- **Rien n'existe** : `grep -ri clipboard` sur tout le dépôt ⇒ 0 occurrence. Il faut créer le chemin.
- L'UIModule est SDL-free ⇒ il passe par IIO. `InputModule` possède SDL, donc le presse-papiers :
  - UIModule → `input:clipboard:set {text}` (copier/couper) et `input:clipboard:get` (demande)
  - InputModule → `input:clipboard:text {text}` en réponse (`SDL_GetClipboardText`)
- **Le collage a une frame de latence** (requête → réponse). Invisible à l'œil humain ; c'est le prix
  du découplage SDL et il est documenté ici plutôt que contourné par un raccourci.
- *Rouge :* E2E avec un faux répondeur presse-papiers dans le test (le test publie la réponse) ⇒
  Ctrl+V insère au curseur, Ctrl+X copie **et** supprime la sélection.

### T3 — Multiligne *(le gros morceau)*

- **Extraire d'abord le modèle d'édition pur** : `grove::text::EditModel` (tampon + curseur +
  sélection + opérations insérer/supprimer/déplacer/sélectionner). `UITextInput` et le multiligne
  deviennent deux **vues minces** au-dessus du même modèle testé unitairement.
  *Pourquoi :* l'alternative — un `multiline: true` greffé sur `UITextInput` — transforme le widget en
  monstre où chaque méthode se ramifie sur un booléen. La modularité prime (doctrine §III.1).
- Puis la vue : Entrée insère `\n`, flèches haut/bas, défilement vertical (réutilise le clip existant),
  une entrée de rendu par ligne visible (pool recyclé, comme la liste virtualisée), retour à la ligne
  automatique optionnel.

---

## 4. Arbitrages à trancher

1. **Multiligne : widget séparé ou mode ?** Je recommande **`UITextArea` séparé** au-dessus d'un
   `EditModel` partagé (T3). Un mode booléen serait plus court à écrire *aujourd'hui* et plus coûteux
   à tenir *ensuite*.
2. **Sémantique d'Entrée en multiligne.** Entrée insère un saut de ligne ⇒ la soumission passe à
   **Ctrl+Entrée**. Le `onSubmit` du monoligne ne bouge pas.
3. **Où s'arrête ce chantier ?** T0+T1+T2 = « champ de saisie correct » et se tient tout seul.
   T3 est un second bloc, plus gros que les trois premiers réunis. **Livrable par défaut : T0→T2**,
   T3 sur ordre.

---

## 5. Risques

- **Ordre d'arrivée des métriques.** La table arrive après le chargement de la police ; une frame
  d'UI peut la précéder. Repli monospace ⇒ dégradation, jamais de plantage. Un test doit verrouiller
  que le repli produit **exactement** l'ancien comportement.
- **Le rendu ne se prouve pas headless.** Les E2E de l'UIModule sont sans pixels : ils prouvent la
  logique (index, modèle de sélection), pas l'image. D'où le test `[gpu]` sur le surlignage.
- **Régression sur les écrans existants.** Le champ est utilisé dans les démos/écrans JSON ; toute
  tranche finit par la régression UI complète (`ctest -R "UI|Radial|InputUI"`) avant commit.
- **Périmètre du presse-papiers.** Uniquement du texte brut. Pas d'images, pas de formats riches.

---

## 6. Journal de statut

| Date | Tranche | État |
|---|---|---|
| 2026-07-29 | Plan | Rédigé. Constat vérifié dans le code (D1-D4), architecture de mesure tranchée (option B) |
| 2026-07-29 | **T0a** | ✅ `grove::text::Metrics` + `Utf8.h` remonté dans `include/grove/text/`. `TextMetricsUnit` 17 cas / 233 assertions. **Vérifié adversarialement** : rebranché sur l'implémentation monospace + pas-en-octets, 8 cas passent au rouge |
| 2026-07-29 | **T0b** | ✅ D2 levé. Backspace/Suppr/flèches passent par `prevIndex`/`nextIndex` ; `setCursorPosition` devient l'entonnoir qui recolle toute position sur une frontière de codepoint. `IT_062`, vu ROUGE avant correctif (5 cas sur 6) |
| 2026-07-29 | **T0c** | ✅ D1 levé. `render:font:metrics` publié par BgfxRenderer (au boot ET sur `render:font`), consommé par UIModule → `UIContext::fontMetrics` → widget. Encodage dense partagé (`TextMetricsWire.h`) car IIO ne transporte que le JSON propre du nœud. Publié aussi pour la 8x8 (avances toutes à 8 = repli historique exact) |
| 2026-07-29 | **T0d** | ✅ D3 levé. Le clic place le curseur (`indexAtX`), y compris sur du texte accentué (balayage de tout le champ : aucune position de clic ne corrompt la chaîne) |

**T0 est COMPLET** (D1, D2, D3 levés ; D4 reste, il appartient à T1). Régression : 63/63 verts
(UI + Radial + InputUI + Text + Render + Scene, tests GPU inclus).

### Piège rencontré — à ne pas repayer

Le premier passage de T0d a échoué avec un résultat qui correspondait *exactement* au calcul du repli
monospace, alors que la table de métriques était bien arrivée (prouvé par le log du module). Cause
réelle : **`cmake --build build --target IT_062...` ne reconstruit pas `libUIModule.dll`**, que le
test charge à l'exécution via `ModuleLoader` — le test tournait donc contre l'ancienne DLL. Localisé
en trois mesures (métriques reçues ? clic reçu ? curseur posé ?) plutôt qu'en devinant. **Toute
tranche suivante doit reconstruire la CIBLE MODULE, pas seulement la cible de test.**
