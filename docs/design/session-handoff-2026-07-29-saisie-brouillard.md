# Handoff — session du 29 juillet 2026 : saisie de texte & brouillard réglable

> **État à la sortie** : `master` = **`b871b5a`** (le commit de ce document), arbre propre, suite
> **203/203**, **tout poussé** (gitea + github, mêmes SHAs). Aucun force push, aucune branche en
> suspens. Point de départ : `0ee6bc0`. **13 commits** à moi — le dernier étant ce handoff, écrit
> avant d'être commité : c'est pourquoi il a d'abord annoncé `cec09bd`, son prédécesseur.
>
> ⚠️ Session **parallèle** : le travail lumière (murs, filtres colorés) avançait sur `master` en même
> temps — voir son propre [handoff du 29](session-handoff-2026-07-29.md). J'ai rebasé **deux fois**
> pour l'intégrer ; les deux commits `b7d4782`/`79e083f` (filtres F1/F2) sont sous les miens.

Ce document dit **où on en est, ce qui est clos, ce qui ne l'est pas, et ce que la session a appris**.
Chaque chantier a son doc ; ici on ne garde que ce qui ne s'y trouve pas.

---

## 1. Saisie de texte — de « écrit pour du 8×8 monospace » à un champ correct

Plan autoportant : **[ui-textinput.md](ui-textinput.md)**. Cinq tranches, chacune rouge avant d'être
verte, chacune vérifiée en cassant le code.

| Tranche | Ce qu'elle livre | Verrouillé par |
|---|---|---|
| **T0** | mesure réelle du texte (`grove::text::Metrics`), curseur UTF-8-safe, transport `render:font:metrics`, clic → curseur | `TextMetricsUnit`, `IT_062` |
| **T1** | sélection : clavier, souris, double-clic mot, surlignage | `IT_063`, `SelectionHighlightGpu` |
| **T2** | presse-papiers `input:clipboard:*` | `IT_064`, `InputModuleStatic [clipboard]` |
| **T3** | `UITextArea` multiligne sur `grove::text::EditModel` partagé | `TextEditUnit`, `IT_065` |
| **T4** | retour à la ligne automatique (lignes VISUELLES ≠ logiques) | `TextWrapUnit`, `IT_066` |

**Cinq défauts PRÉEXISTANTS levés, aucun cherché** — c'est le vrai rendement de ce chantier :

1. Backspace cassait **tout accent** (curseur compté en octets). En français, un bug de tous les jours.
2. **Le curseur de saisie n'avait jamais été visible** — voir la leçon §3.1, elle dépasse ce widget.
3. **Ctrl+A/C/V étaient structurellement inatteignables** : SDL n'émet pas de `SDL_TEXTINPUT` sous
   Ctrl, donc le raccourci n'arrivait par *aucun* des deux chemins d'entrée. Le `// TODO: add ctrl`
   n'était que la moitié du problème.
4. `insertFilteredText` signalait le changement par la **longueur** → un collage à longueur constante
   (remplacer « abc » par « ZZZ ») n'émettait aucun événement : le champ était correct et le jeu ne le
   savait pas. Le pire des deux mondes.
5. **`InputModule` ne drainait jamais son inbox IIO.** Il ne faisait que publier ; toute souscription
   y aurait été silencieusement ignorée.

**Arbitrages tranchés** (les deux venaient d'Alexi) : `UITextArea` est un **widget séparé** au-dessus
d'un modèle partagé, pas un `multiline: true` — et Entrée y insère un saut de ligne, donc **Ctrl+Entrée
soumet**. Le champ monoligne ne bouge pas, verrouillé par un garde-fou explicite.

**L'ordre de travail est ce qui a rendu le découpage gratuit** : extraire le modèle, y **migrer
d'abord `UITextInput`** (les 63 tests existants valident le modèle), et seulement ensuite construire la
seconde vue. L'inverse aurait bâti le multiligne sur du code jamais éprouvé.

---

## 2. Brouillard du tilemap — trois constantes devenues des réglages

Détail : **[tilemap-renderer.md](tilemap-renderer.md)** (deux sections en fin de fichier).

Le brouillard mélangeait **déjà** les tuiles cachées avec une texture échantillonnée en espace monde
(wrap `Repeat`) plutôt qu'avec du noir. Mais tout était figé :

| | Avant | Après |
|---|---|---|
| Échelle | `worldPos / 64.0` **en dur dans le shader** | `fogScale` / `{scale}` |
| Dérive | inexistante | `{offsetX, offsetY}` rampés par le jeu |
| Bord | aligné sur la grille | `fogEdge` / `{edge}` — ondulation par bruit |
| Changement à chaud | impossible (boot seulement) | `render:tilemap:fog:style` |

**Défauts = rendu historique au pixel près**, donc aucun hôte existant ne bouge.

**La décision qui compte, pour la suite** : j'avais noté « reste ouvert : masque sous-tuile ». En
l'attaquant, c'était la **mauvaise réponse** — la visibilité est CONNUE par tuile (DAOS révèle en
minant), donc un masque 4× plus fin ne porte **aucune information de plus**, seulement un buffer 16×
plus gros à produire côté jeu. On perturbe la **lecture** du masque avec du bruit : bord organique à
information constante. Si quelqu'un repropose un masque sous-tuile, c'est ce raisonnement qu'il faut
réfuter d'abord.

⚠️ Garde-fou tenu partout : **seul le nuage bouge**, jamais le masque de révélation. Une dérive ne
doit pas re-cacher ce que le joueur a exploré.

---

## 3. Ce que la session a appris — transversal, réutilisable

### 3.1 Le renderer retained FIGE la couche d'une entrée à sa première publication

`UIRenderer.cpp` : `// Keep original layer (don't update it)`. Conséquence non évidente : **cacher une
entrée en la publiant avec `layer 0` la condamne à rester sous tout le reste, définitivement.**

C'est ce qui rendait **le curseur de saisie invisible dans tous les champs GroveEngine**. Personne ne
l'avait vu parce que le texte s'en sortait **par accident** : `TextPass` s'exécute après `SpritePass`
quelles que soient les couches — un rect n'a pas cette chance.

> **Tout widget qui publie `layer 0` pour cacher quelque chose porte le même bug latent.**
> Le remède : réserver les couches d'un bloc en tête de `render()`, et publier toute entrée cachée à
> taille nulle **sur sa couche définitive**.

### 3.2 Un bruit de test doit être BASSE FRÉQUENCE

Mon premier test du bord ondulé utilisait un damier 4×4 répété 8 fois sur le chunk. À cette fréquence
le GPU **descend dans les mips et rend la moyenne du damier — exactement 0.5** : perturbation
rigoureusement nulle, test rouge, code innocent.

### 3.3 Localiser par la mesure, deux fois payantes

- **Cas 1** : le clic-pour-placer-le-curseur rendait un résultat qui correspondait *exactement* au
  calcul monospace, alors que les logs prouvaient que la table de métriques était bien arrivée. Trois
  mesures (métriques reçues ? clic reçu ? curseur posé ?) ont montré que le code était juste depuis le
  début — **`cmake --build --target IT_xxx` ne reconstruit pas `libUIModule.dll`**, que le test charge
  à l'exécution. Le test tournait contre l'ancienne DLL.
- **Cas 2** : le bord ondulé ne bougeait pas d'un pixel. Deux mesures qui **séparent les causes** — la
  branche s'exécute-t-elle ? le bruit varie-t-il ? — ont isolé §3.2 en une itération. Rendre avec la
  texture 1×1 par défaut (bruit constant *par construction*) a montré un décalage net : la branche
  tournait, seul mon bruit était en cause.

Dans les deux cas, augmenter les valeurs au hasard aurait coûté des heures.

### 3.4 Ne jamais partager `_deps` entre deux builds

Configurer le worktree avec `-DFETCHCONTENT_BASE_DIR=<repo>/build/_deps` fait **partager les artefacts
compilés** avec le build principal : les deux se réécrivent `libspdlog.a` / `libCatch2.a` et produisent
des échecs de lien absurdes (`undefined reference to fmt::v9::format_error`) **sans rapport avec le
code**. Recette correcte en §5.

### 3.5 Master bouge sous les pieds

Deux sessions poussaient sur `master` ce jour-là. J'ai rebasé **deux fois**. La surface de collision
est restée minuscule et constante (`BgfxRendererModule.cpp` + `tests/CMakeLists.txt`), git a résolu
seul les deux fois.

> **Règle** : re-lancer la suite **complète** après CHAQUE rebase. Un vert sur l'ancienne base ne
> prouve rien — d'autant que le travail lumière touche `SpritePass`/`SceneCollector`/`FramePacket`,
> que les tests GPU de la saisie traversent.

---

## 4. Ce qui reste ouvert

**Sur la saisie** — rien. Le périmètre annoncé est tenu intégralement. Seul point hors périmètre
documenté : le **défilement horizontal** de `UITextArea`, qui n'a plus lieu d'être tant que `wrap` est
actif (il ne compte que pour `wrap: false`).

**Sur le brouillard** — rien d'identifié. Le masque sous-tuile a été examiné et **écarté avec son
raisonnement** (§2), ce n'est pas une dette.

**La piste que je n'ai pas suivie, et qui vaut le coup :** lire
**`../drifterra/docs/grove_integration.md`** en entier. C'est le contrat de topics entre le jeu et le
moteur, avec des sections explicitement « à prévoir côté moteur ». J'y ai aperçu au passage, sans
creuser, deux choses qui sentent le vrai problème :

- une **dette de test** assumée : « le rendu HUD/map n'est PAS vérifié automatiquement » ;
- un **plafond hot-reload de ~70-100 reloads par process** (Windows/MinGW).

Le `docs/BACKLOG.md` du moteur note par ailleurs un reste concret : **seul `UIRadial` surcharge
`releaseRenderEntries`** — les autres widgets multi-entrées (bouton avec texte, slider…) laissent
encore des entrées fantômes quand on les cache. Le correctif général existe, il n'est pas appliqué
partout. C'est de la même famille que §3.1.

---

## 5. Recettes d'environnement

**Build d'un worktree** (réutilise les sources téléchargées, ne partage AUCUN artefact) :

```bash
cmake -B build -G Ninja \
  -DFETCHCONTENT_SOURCE_DIR_BGFX=<repo>/build/_deps/bgfx-src \
  -DFETCHCONTENT_SOURCE_DIR_CATCH2=<repo>/build/_deps/catch2-src \
  -DFETCHCONTENT_SOURCE_DIR_NLOHMANN_JSON=<repo>/build/_deps/nlohmann_json-src \
  -DFETCHCONTENT_SOURCE_DIR_SPDLOG=<repo>/build/_deps/spdlog-src \
  -DGROVE_BUILD_BGFX_RENDERER=ON -DGROVE_BUILD_UI_MODULE=ON -DGROVE_BUILD_INPUT_MODULE=ON
```

**Régénérer un shader** (le build ne les compile pas) — `shaderc.exe` est dans
`build/_deps/bgfx-build/cmake/bgfx/`. La recette exacte est **en tête de chaque `.bin.h`**. Trois
profils réels (`spv` / `glsl 430` / `dx11 s_5_0`) + `mtl` qui reste un placeholder. Réassembler le
`.bin.h` dans cet ordre, en gardant l'en-tête de commentaire.

**Tests E2E de l'UI** : toujours construire la cible **module** en plus de la cible de test
(§3.3, cas 1).

**Exécuter un test hors ctest** : mettre `C:\ProgramData\mingw64\mingw64\bin` sur le `PATH`, sinon
l'exe sort en 127 (DLL runtime introuvable).

---

## 6. Où regarder en priorité

| Sujet | Doc |
|---|---|
| Saisie de texte, plan + journal des 5 tranches | [ui-textinput.md](ui-textinput.md) |
| Brouillard : échelle, dérive, bord | [tilemap-renderer.md](tilemap-renderer.md) (fin) |
| Widgets & topics UI, à jour | [UI_WIDGETS.md](../UI_WIDGETS.md), [UI_TOPICS.md](../UI_TOPICS.md) |
| Session lumière parallèle | [session-handoff-2026-07-29.md](session-handoff-2026-07-29.md) |
| Backlog moteur | [BACKLOG.md](../BACKLOG.md) |
