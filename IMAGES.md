# IMAGES.md — Guide d'images pour le blog (devlog groveengine)

> **À quoi ça sert.** Le devlog est généré par un pipeline externe (**WanMira**, même outil que
> Drifterra et DAOS). Son agent lit les docs (`docs/`) **et embarque de vraies images du repo** dans
> les articles. Pour choisir la *bonne* image — pas une au hasard — il lit **ce fichier en priorité**.
> Sans ce guide, il globbe les fichiers à l'aveugle : il voit `04_three_lamps.png`, pas ce que ça
> montre. Avec, il embarque l'image juste avec son contexte.

## Contrat (pinné — ne pas changer sans prévenir l'agent WanMira)

- **Nom du guide** : `IMAGES.md`, **à la racine du repo**. Hard-codé côté WanMira.
- **Syntaxe d'embed** : l'agent insère via `![alt](repo: <chemin>)`. Le `<chemin>` est **relatif
  à la racine du repo** et doit résoudre **tel quel** depuis la racine.
- **Dossier** : `blog/` (pas `assets/blog/` — `assets/` ici contient les assets **du moteur**
  (textures, shaders, données de jeu), pas des visuels de communication ; les mélanger brouillerait
  les deux).

## Règles — non négociables

1. **Committé, pas seulement buildé.** Le blog **clone le repo** sur un serveur : une image non
   commitée (ou dans un dossier gitignoré comme `build/`) **n'existe pas** pour lui. Tout visuel
   destiné au blog vit dans un dossier **commité**. Un visuel né dans `build/` → **copié dans
   `blog/`** et référencé **là**.
2. **Promouvoir et inscrire = même geste, même commit.** On n'ajoute jamais une image à `blog/`
   sans ajouter son entrée ici, ni l'inverse. Conséquence : ce manifest ne décrit **jamais** un
   chemin mort, par construction — pas besoin de « se souvenir » de synchroniser. (Idem
   suppression/renommage : l'image et son entrée bougent ensemble.)
3. **Chemins exacts, relatifs à la racine.** Pas de chemin approximatif, pas de `./` superflu.
4. **Contexte, pas juste un nom de fichier.** Chaque entrée dit **ce que l'image montre** *et*
   **quand l'utiliser** (quel sujet d'article). C'est ça qui laisse l'agent choisir juste.

## Politique de capture (tranchée : libérale)

> On **commit large** : toute capture non-triviale d'un système visiblement à l'œuvre (un jalon, un
> avant/après, une preuve de rendu) part dans `blog/` + une entrée ici. Le blog **trie** — c'est son
> job, pas le nôtre. Seul exclu : le bruit pur (frame de crash sans intérêt).

**Exception outillée** : `blog/.gitignore` exclut `*.html`. Les pages de comparaison générées
réembarquent les PNG en **base64** — les commiter stockerait les mêmes images deux fois. Les sources
durables sont le **programme de capture** et les **PNG** ; la page se régénère depuis eux.

---

## Manifest

### Lumières 2D — L1 (plomberie + ambiant) et L2 (lumières radiales)

> **Une seule scène, cinq éclairages.** Les cinq captures rendent la **même** scène — sol de dalles
> en damier (deux gris, tuiles de 30 px), une bande de mur en haut, deux caisses brunes et un coffre
> vert, en quads plats sans aucun asset — en ne changeant **que l'éclairage**. C'est délibéré : la
> lumière est la seule variable entre les images, donc tout écart visible vient d'elle.
>
> Produit par `tests/visual/capture_lighting.cpp` (headless, écrit dans `blog/` par défaut). Les
> tests pixel prouvent les nombres ; ces images montrent l'image. **Piège consigné dans le
> programme** : la capture bind la vue **composite**, pas la vue 0 — dès que l'éclairage est actif,
> le module redirige la vue 0 dans la cible `scene`, donc capturer la vue 0 rendrait la scène
> **non éclairée**. Conception : [`docs/design/lighting-2d.md`](docs/design/lighting-2d.md).

#### `blog/01_unlit.png`
**La scène sans éclairage — la référence.** *(Damier gris clair pleine luminosité, bande de mur
bleu-gris en haut, les deux caisses brunes et le coffre vert bien lisibles.)* Aucun ambiant publié :
le monde part directement dans la vue 0, le **chemin à coût zéro** — l'éclairage n'est pas un étage
qu'on traverse toujours, il n'existe que si on le demande. C'est l'image de contrôle : tout ce que
les quatre suivantes changent, elles le changent par rapport à celle-ci. À utiliser pour : le
**avant/après** de l'éclairage, le principe « ne pas faire payer ce qu'on n'utilise pas », l'ouverture
d'un article sur le rendu 2D.

#### `blog/02_ambient.png`
**Ambiant seul — la plomberie prouvée sans une seule lampe (L1).** *(Même scène, écrasée dans un
bleu nuit très sombre ; on devine le damier et les caisses, aucune source lumineuse.)* L1 livre
**les cibles de rendu, l'ordre des vues et le composite** — et se prouve avec un simple ambiant qui
assombrit la scène, avant toute lumière radiale. Le vrai obstacle n'était pas la lumière mais
l'**ordonnancement** : monde en vue 0 et HUD en vue 1 sont collés, aucune place pour insérer le
composite entre les deux ; la sortie retenue a été d'ajouter `setViewOrder` au RHI (une ligne bgfx +
un no-op côté mock) plutôt que de renuméroter les vues codées en dur dans toutes les passes. À
utiliser pour : **découper une feature en tranches livrables** (la plomberie d'abord, l'effet
ensuite), le piège de l'ordre des vues, le composite `final = scene × (ambient + light)`.

#### `blog/03_one_lamp.png`
**Une lampe chaude au-dessus de la caisse de gauche (L2).** *(Halo ambre centré sur la caisse de
gauche : la caisse ressort en orange vif, les dalles autour s'éclaircissent en dégradé et le halo
s'éteint progressivement ; le reste de la scène reste dans la nuit bleue de l'ambiant.)* La lumière
radiale du cœur de L2 : un **quad additif** avec atténuation, dessiné dans une seconde cible, composé
avec la scène. Aucun shader de sprite, tilemap ou particule ne change — l'approche est découplée des
passes existantes, et le nombre de lumières n'est borné que par le taux de remplissage. À utiliser
pour : le **buffer d'accumulation + composite** (et pourquoi l'éclairage par sprite ne tenait pas :
il aurait fallu toucher tous les shaders et le batching n'y survivait pas), l'atténuation radiale.

#### `blog/04_three_lamps.png`
**Trois lampes colorées — l'accumulation additive rendue visible.** *(Une lampe chaude orange à
gauche, une froide bleue au centre-haut, une verte à droite ; chacune éclaire sa caisse, et là où
leurs halos se recouvrent le résultat dépasse ce que n'importe laquelle donne seule.)* C'est l'image
qui **justifie le format de la cible** : la somme peut dépasser 1 dans les zones de recouvrement,
donc le buffer de lumière est en **RGBA16F** et non en RGBA8. À utiliser pour : l'**accumulation
additive**, le choix du half-float, les lumières colorées, « pourquoi ce buffer n'est pas en 8 bits ».

#### `blog/05_overbright.png`
**Overbright — intensité 4, le cœur sature en blanc mais le dégradé survit.** *(Une seule grosse
lampe chaude au centre : le cœur du halo est blanc pur, et autour le dégradé se déploie sur toute la
scène jusqu'à la nuit ; les caisses proches sont noyées de lumière.)* Une intensité bien au-delà de 1.
En **RGBA8** cette image serait un disque blanc plat : tout ce qui dépasse serait clippé et le dégradé
perdu. La cible **half-float** conserve l'information au-delà de 1 — et c'est exactement ce dont le
**bloom** se nourrira. Écrêter dès L1 reviendrait à jeter l'information du post-traitement à venir. À
utiliser pour : le **HDR / plage dynamique**, la préparation du bloom, « une décision de format prise
pour une feature pas encore écrite », le coût d'une décision par défaut qu'on ne peut plus défaire.

#### `blog/06_cone.png`
**Lampe conique — un faisceau, pas un disque (2026-07-29).** *(Un cône de lumière chaude descend en
diagonale depuis le haut-gauche, éclairant une bande du sol dallé et une caisse ; le reste de la
scène reste dans la nuit bleutée, et le bord du faisceau se fond au lieu de trancher.)* Deux champs
optionnels — `dirDeg` + `spreadDeg` — transforment le disque en faisceau. Le bord **fond** : une
coupe angulaire nette se lirait comme une part de tarte en carton, exactement comme un falloff
linéaire se lit comme un disque à bord dur. Le masque est un **produit scalaire** contre l'axe, donc
ni `atan2` ni trigonométrie par pixel. À utiliser pour : les **projecteurs**, « la même raison
revient à deux endroits » (rupture de dérivée), le choix d'une formulation qui rend un cas
particulier gratuit (l'omni n'est même pas une branche).

#### `blog/07_thruster.png`
**Propulseur — l'émetteur et la lampe partagent les mêmes nombres (2026-07-29).** *(Une coque grise
allongée vue de profil, une tuyère claire à sa gauche, et un cône orange qui s'ouvre vers la gauche
en éclairant le sol et une caisse sur son passage.)* Le cas d'usage qui a **décidé la convention** :
les degrés viennent de `grove::fx::Emitter` et non des radians `a0`/`a1` de `render:sector`, si bien
qu'une flamme de propulseur et la lumière qu'elle projette prennent **les mêmes `dirDeg`/`spreadDeg`**.
Le jeu écrit le cône une fois au lieu de convertir entre deux conventions. À utiliser pour : les
**effets de propulsion**, « une convention se choisit sur l'usage, pas sur l'élégance », la
cohérence entre deux sous-systèmes d'un même moteur.

#### `blog/08_additive_plume.png`
**Sprites additifs — l'intersection sature en blanc (2026-07-29).** *(Fond quasi noir, deux quads
très étirés et tournés se croisant en X : l'un orange, l'autre bleu, et à leur intersection un cœur
blanc pur nettement plus brillant que chacun des deux.)* La preuve de `blend:"additive"` sur
`render:sprite`. Mesuré : intersection **255**, faisceau orange seul **54**, faisceau bleu seul
**28** — en mélange alpha l'intersection vaudrait la couleur du dernier dessiné, jamais davantage.
Aucun éclairage dans cette image : rien d'autre que le blend ne peut expliquer le blanc. À utiliser
pour : les **panaches de propulsion** façon Waterfall, « pourquoi il fallait un troisième mode
plutôt que réutiliser les particules », la différence visuelle entre additif et alpha.

---

<!--
  GABARIT — copier-coller pour chaque nouvelle image (promotion + inscription, même commit) :

  #### `blog/<nom>.png`
  **<Titre : ce que ça montre en une phrase> (<date>).** *(<Description factuelle de ce qu'on voit
  à l'écran.>)* <Le contexte : quel système, quelle décision, quel piège.> À utiliser pour : <les
  sujets d'article que cette image sert>.
-->
