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

### Matière — murs (plan W) et filtres colorés (plan F)

> **La même scène que ci-dessus, et le même programme de capture.** Ces quatre images racontent une
> seule chose : la matière a cessé d'être binaire. Jusqu'au plan F, un obstacle bloquait la lumière ou
> ne la bloquait pas. Un filtre écrit une **couleur** là où un mur écrit du noir — et le moteur ne
> distingue pas les deux cas, c'est le même produit accumulé le long du rayon.
> Conception : [`docs/design/lighting-walls.md`](docs/design/lighting-walls.md) ·
> [`docs/design/lighting-filters.md`](docs/design/lighting-filters.md) · le socle commun :
> [`docs/design/lighting-transmittance-core.md`](docs/design/lighting-transmittance-core.md).

#### `blog/09_wall_shadow.png`
**Un mur projette une ombre — le côté droit ne reçoit plus que l'ambiant (2026-07-29).** *(Une lampe
chaude éclaire la moitié gauche d'un sol dallé ; une bande verticale sombre traverse l'image, et tout
ce qui est à sa droite retombe dans la nuit bleue, caisse comprise.)* L'ombre n'est **pas un cas
particulier du code** : le mur écrit une transmittance nulle dans la carte d'occultation, et un zéro
annule le produit courant que le shader de lampe accumule le long du rayon. Tout ce qui suit sur ce
rayon s'éteint par **conséquence arithmétique**, sans une seule branche. À utiliser pour : les
**ombres portées 2D**, « le cas dégénéré d'un mécanisme général plutôt qu'un mécanisme dédié », la
marche d'accumulation dans le shader.

#### `blog/10_filter_red.png`
**Le même plan, le mur remplacé par un vitrail rouge (2026-07-29).** *(Image strictement identique à
la précédente — même lampe, même position, même rectangle — sauf que la bande verticale est rouge et
que la lumière la traverse : tout le côté droit est baigné d'un rouge sourd qui s'atténue avec la
distance.)* **La paire 09/10 est la meilleure image du chantier** : une seule chose change entre les
deux, ce que la matière transmet. C'est aussi la justification du socle commun — livrer les murs avec
un shadow map 1D (qui ne stocke qu'une *distance*, donc binaire par construction) aurait obligé à tout
jeter en arrivant aux filtres. À utiliser pour : le **avant/après mur→filtre**, « choisir la structure
de données sur le chantier N+1, pas sur le chantier N », la transmission colorée.

#### `blog/11_filter_stack.png`
**Deux vitraux superposés — trois zones, et aucun tri (2026-07-29).** *(Un panneau ambre pleine
hauteur, puis un panneau magenta qui ne couvre que la moitié supérieure ; à droite on distingue une
zone brune traversée par l'ambre seul, et une zone violette sombre traversée par les deux.)* La zone
« les deux » est le **produit** des deux teintes. Comme un produit est commutatif, **l'ordre de
publication ne peut pas changer l'image** : pas de tri par profondeur, pas de structure ordonnée par
lampe. C'est un cas rare où la physique du modèle *supprime* du code — le réflexe aurait été de
commencer par construire ce tri. On y voit aussi que le modèle **mord fort** : deux teintes moyennes
donnent une zone très sombre. À utiliser pour : la **commutativité comme choix d'architecture**,
« la propriété qui économise un chantier », les pièges d'un modèle multiplicatif.

#### `blog/12_stained_window.png`
**Le vitrail : maçonnerie opaque, deux ouvertures teintées, deux faisceaux (2026-07-29).** *(Une
lampe hors champ à gauche éclaire un sol clair ; un mur vertical sombre coupe l'image, percé de deux
ouvertures — l'une rouge, l'autre bleue — d'où partent deux faisceaux colorés qui s'ouvrent en éventail
sur le sol obscur de droite, séparés par une bande d'ombre franche.)* Murs et vitraux dans la **même
image**, écrits par la **même passe** dans la **même carte** : ce sont les deux extrémités d'une seule
échelle, l'un ne transmet rien, l'autre transmet une couleur. C'est l'image de couverture du chantier.
À utiliser pour : l'**illustration principale** d'un article sur l'éclairage 2D, « un mécanisme, trois
demandes » (murs / filtres / brouillard), l'ouverture ou la clôture d'un devblog rendu.

### Animé

#### `blog/13_light_sweep.gif`
**Les ombres SUIVENT la lampe — les trois matières à l'œuvre en même temps (2026-07-29).** *(Une
lampe monte et descend le long du mur de gauche ; à travers deux ouvertures teintées, un faisceau
rouge et un faisceau bleu balayent la salle de droite en éventail, éclairant les caisses au passage,
et l'ombre des segments de maçonnerie pivote avec la source. Les faisceaux sont visibles **dans
l'air**, pas seulement là où ils touchent le sol.)*

**C'est la seule image qui prouve ce qu'aucune fixe ne peut** : rien n'est précalculé, tout est
recalculé par frame à partir de la position de la lampe. On y voit d'un coup les **murs** (l'ombre
tourne), les **filtres** (chaque ouverture colore son faisceau) et la **diffusion** (le milieu rend
les faisceaux visibles dans le vide — sans elle il n'y aurait que deux taches sur le sol).

Produit par `capture_lighting <dir> anim` puis `tools/make_gif.py`. À utiliser pour : l'**ouverture
d'un article** sur l'éclairage 2D, une démo « ça tourne vraiment », ou partout où il faut montrer que
l'éclairage est dynamique et pas peint à la main.

#### `blog/14_fog_lighthouse.gif`
**Un phare dans la brume — le milieu devient le sujet (2026-07-29).** *(Fond noir étoilé. Deux
faisceaux opposés balayent l'écran en tournant sur un tour complet ; ils sont visibles **dans l'air**
sur toute leur longueur, s'affaiblissent avec la distance, et les rochers qu'ils croisent y découpent
des couloirs d'ombre nets qui tournent avec eux.)*

Là où [`13_light_sweep.gif`](blog/13_light_sweep.gif) se sert du brouillard comme d'un **révélateur**
neutre, celui-ci montre ce que le milieu fait **en propre** : l'absorption qui **compose avec la
distance** (le faisceau s'éteint en s'éloignant, exponentiellement — pas linéairement) et la
**diffusion**, qui le rend visible là où il ne touche aucune surface. Un faisceau volumétrique n'est
pas dessinable en sprite : c'est de la matière éclairée, pas une surface.

À utiliser pour : un article sur le **brouillard / les milieux / les nébuleuses**, la loi de
Beer-Lambert « ce n'est pas un assombrissement », les **rais de lumière** (god rays) en 2D, ou pour
illustrer qu'un même mécanisme porte murs, filtres et milieux.

### Nébuleuses — un milieu à densité variable

> **Pourquoi une primitive de plus.** `render:fog` est un **rectangle de densité uniforme** : la
> bonne forme pour un banc de brume ou une pièce enfumée, et inutilisable en nébuleuse — bords
> francs, intérieur plat. Empiler des rects pour simuler un dégradé a été **mesuré** : ça donne une
> ziggourat de contours concentriques, pas un nuage. `render:nebula` est un **disque** dont la
> densité culmine au cœur et tombe à zéro **exactement** au bord.

#### `blog/15_nebula.png`
**Une nébuleuse éclairée de côté (2026-07-29).** *(Fond noir. Un nuage aux contours organiques et
irréguliers, vivement éclairé sur sa face gauche et s'assombrissant vers la droite, avec des
variations de teinte rose et ambre en son sein.)* La lampe est **à côté** du nuage, pas dedans :
c'est ce qui distingue un **milieu** du halo d'une lampe. Quatre volumes se chevauchent — chacun
s'éteignant à son propre bord, la silhouette combinée n'a aucun contour géométrique. À utiliser
pour : un article sur les **nébuleuses / milieux volumétriques**, « pourquoi un rectangle ne suffit
pas », la composition de volumes.

#### `blog/16_nebula_drift.gif`
**La source tourne autour du nuage (2026-07-29).** *(Le nuage reste immobile pendant qu'une source
lumineuse en fait le tour ; sa face éclairée voyage avec elle, la face opposée restant sombre.)*
**L'image fixe ne peut pas trancher entre un milieu et un halo** — les deux sont une tache
lumineuse. Déplacer la source règle la question : un halo suivrait la lampe, un nuage reste où il
est et change seulement de côté éclairé. À utiliser pour : la même famille de sujets que 15, et
partout où il faut prouver qu'un volume est **de la matière éclairée**, pas un sprite peint.

### Bloom — le post-traitement (plan B)

> **Ce que ces images doivent faire comprendre.** Le bloom ne se nourrit **pas** du buffer de lumière
> mais de la **frame composée**. C'est le seul choix d'architecture du plan B, et la paire 17/18 le
> prouve d'une façon qu'aucun texte ne remplace : **il n'y a aucune lampe dans ces deux images.** Un
> bloom alimenté par les lampes les aurait laissées identiques.

#### `blog/17_bloom_off.png`
**La référence : deux faisceaux additifs, sans bloom (2026-07-30).** *(Fond noir. Un faisceau orange
et un faisceau bleu se croisent en X ; leur zone de recouvrement forme un losange BLANC à bords
francs.)* Aucune lampe, un ambiant **blanc** — qui est neutre par construction et la façon documentée
d'obtenir du post-traitement sans look éclairé. Le losange blanc est la seule zone dont la somme
additive dépasse 1 : c'est le **sur-brillant**, et c'est exactement ce que le seuil va isoler. À
utiliser pour : le membre « avant » de la paire, une explication du mélange additif, ou « qu'est-ce
qu'un pixel sur-exposé ».

#### `blog/18_bloom_on.png`
**La même image, bloom activé (2026-07-30).** *(Strictement la même scène ; le losange du croisement
irradie maintenant une lueur douce qui débourre sur les faisceaux et sur le fond noir, tandis que les
bras non recouverts restent nets.)* **Seul le losange brille** — les bras, sous le seuil, ne
contribuent rien. C'est la preuve visuelle des deux décisions du plan : la source est la frame
composée (aucune lampe ici), et le seuil n'extrait que ce qui dépasse 1. À utiliser pour : le membre
« après » de la paire, un article sur le **seuil et le sur-brillant**, ou pourquoi une cible RGBA16F
était le prérequis.

#### `blog/19_bloom_lamp.png`
**Le cas canonique : un halo qu'un mur arrête (2026-07-30).** *(Une pièce sombre au sol dallé, une
ampoule au centre gauche entourée d'un halo chaud qui déborde bien au-delà du disque de la lampe, une
caisse orange éclairée, et sur la droite un mur vertical derrière lequel tout est noir.)* Deux choses
dans une seule image : la lueur **déborde** du rayon de la lampe, et elle **s'arrête net** au mur —
parce qu'un occulteur bloque la lumière **avant** que la lueur n'existe, le bloom ne pouvant pas
éclairer ce que l'ombre a déjà éteint. À utiliser pour : l'illustration par défaut du bloom, un
article sur l'**ordre des passes** (« pourquoi le bloom ne traverse pas un mur »), ou la composition
éclairage + post-traitement.

#### `blog/20_bloom_ramp.gif`
**La rampe d'intensité, aller-retour (2026-07-30).** *(La scène de la lampe, immobile ; seule
l'intensité du bloom monte de 0 à 2 puis redescend, si bien que le halo enfle et se rétracte en
boucle.)* **Un avant/après montre que le bloom existe ; il ne montre pas où se situe le point
d'équilibre** entre « invisible » et « délavé » — or c'est la seule question qu'un auteur se pose
devant ce bouton. Le retour à 0 n'est pas une coquetterie de boucle : il fait repasser par l'état
**éteint** à chaque tour, donc l'œil compare à la référence sans avoir à s'en souvenir. À utiliser
pour : un article sur le **réglage** d'un effet, la démonstration qu'un paramètre est un continuum et
non un interrupteur, ou l'illustration animée principale du bloom.

### Instrument de mesure

#### `blog/90_edge_probe.png`
**La sonde qui a servi à chiffrer le crénelage des ombres (2026-07-29).** *(Un aplat gris uniforme,
une lampe hors champ en haut à gauche, un bloc opaque, et une seule diagonale d'ombre nette qui
traverse l'image. Aucun décor.)* **Ce n'est pas une plaque de blog, c'est un instrument.** Les
captures 09-12 ont un sol dallé et une bande de mur : impossible d'y lire la forme d'un bord d'ombre
sans confondre les tuiles avec l'artefact. Cette sonde ne contient qu'une seule arête, donc elle se
mesure colonne par colonne — c'est elle qui a donné « palier moyen 3,9 px, max 13 px » avant, et
« 1,8 px, max 3 px » après. À utiliser pour : un article sur le **débogage visuel** (« rendre le
défaut mesurable avant de le corriger »), la différence entre voir un bug et le chiffrer, ou en
avant/après avec sa propre version d'avant.

---

<!--
  GABARIT — copier-coller pour chaque nouvelle image (promotion + inscription, même commit) :

  #### `blog/<nom>.png`
  **<Titre : ce que ça montre en une phrase> (<date>).** *(<Description factuelle de ce qu'on voit
  à l'écran.>)* <Le contexte : quel système, quelle décision, quel piège.> À utiliser pour : <les
  sujets d'article que cette image sert>.
-->
