# Plan W — les murs (occultation opaque)

> **Statut** : 📋 plan, rien d'implémenté.
> **Socle** : [table de transmittance polaire](lighting-transmittance-core.md) — **lire d'abord**.
> Ce plan n'y ajoute qu'une chose : de quoi écrire de la matière opaque dans la carte d'occultation.
> **Prérequis livré** : [lumières 2D L1+L2](lighting-2d.md)

## 1. Ce que ça donne

Une lampe cesse de traverser les murs. Derrière un obstacle, la contribution de cette lampe tombe à
zéro — une ombre portée, nette, qui suit la lampe quand elle bouge.

C'est le premier des trois parce que c'est le seul qui se lit d'un coup d'œil : soit l'ombre est là,
soit elle n'y est pas. Les filtres et les atténuateurs demandent de comparer deux nuances.

## 2. Ce que ça ajoute au socle

**Une seule chose** : un occulteur écrit `(0,0,0)` dans la carte de transmittance. Le socle fait le
reste — le produit préfixe s'annule au premier zéro rencontré et reste nul sur toute la suite du
rayon. L'ombre n'est pas un cas particulier du code, c'est une conséquence arithmétique.

## 3. Surface d'écriture

| Topic | Charge | Notes |
|---|---|---|
| `render:occluder` | `{x, y, w, h}` | rectangle opaque, **éphémère** (republié chaque frame) |
| `render:occluder:add` / `:update` / `:remove` | `{renderId, x, y, w, h}` | mode **retenu**, pour la géométrie statique du décor |

**`x, y` = coin haut-gauche** — c'est un rectangle, pas une primitive centrée : la convention
d'ancrage veut que le nom porte l'ancre, et `x,y` est le nom d'un coin
([render-anchor-convention.md](render-anchor-convention.md)).

**Le mode retenu n'est pas une optimisation prématurée ici**, contrairement aux lumières : les murs
d'un niveau ne bougent pas, et les republier chaque frame ferait payer un coût proportionnel à la
taille du décor pour une donnée constante. C'est l'inverse exact du raisonnement qui a rendu les
lumières éphémères — et la raison est la même : suivre ce que la donnée fait réellement.

## 4. Découpage

| Tranche | Contenu | Preuve |
|---|---|---|
| **W1** | `render:occluder` éphémère → carte d'occultation | headless : le rect atterrit dans le paquet, ancré au coin ; rien de publié ⇒ tableau nul |
| **W2** | l'ombre à l'écran | `[gpu]` : un point **derrière** le mur est nettement plus sombre qu'un point à côté, à distance **égale** de la lampe |
| **W3** | mode retenu (`:add`/`:update`/`:remove`) | headless : persiste à travers `clear()`, disparaît sur `:remove` |

### L'assertion qui fait tout le travail en W2

**À distance égale de la lampe.** C'est le point à ne pas rater : comparer un pixel derrière le mur
à un pixel « loin » prouverait seulement que l'atténuation radiale fonctionne — ce que L2 fait déjà.
Les deux sondes doivent être **sur le même cercle** autour de la lampe, l'une occultée et l'autre
non. Sans ça, le test serait vert avec une occultation entièrement absente.

C'est le même piège que la caméra non déplacée du test de L2 : un discriminant placé là où les deux
hypothèses coïncident.

## 5. Risques

1. **Occulteurs hors champ** — dépend de l'arbitrage §5 du socle. Avec une carte partagée en espace
   écran, un mur qui sort du cadre cesse d'occulter, et son ombre disparaît **alors que la lampe est
   toujours visible**. La marge autour du viewport repousse le défaut sans l'éliminer.
2. **Résolution angulaire** — la table a N angles ; à grande distance de la lampe, deux angles
   voisins s'écartent de plus d'un pixel et le bord de l'ombre crénelle. L'article de référence
   traite ça par un flou gaussien sur plusieurs angles à l'échantillonnage. À prévoir dans W2, pas
   après : un bord d'ombre en escalier se voit immédiatement.
3. **Auto-occultation** — un sprite qui est à la fois éclairé et occulteur s'ombre lui-même sur son
   bord côté lampe. Attendu physiquement, souvent laid en 2D vu de dessus. Prévoir de pouvoir
   déclarer un occulteur **sans** qu'il s'auto-ombre, ou accepter et documenter.

## 6. Hors périmètre

- **Occulteurs non rectangulaires** (polygones, masque par sprite). Le rect couvre le décor de
  tuiles ; une forme arbitraire est une tranche à part.
- **Ombres douces** au sens pénombre physique (lampe de rayon non nul). Le flou du risque 2 est un
  anticrénelage, pas une pénombre — ne pas confondre les deux dans la doc consommateur.
- **Occultation de la VUE** (le joueur ne voit pas derrière le mur). C'est un système de visibilité,
  pas d'éclairage ; ça vit côté gameplay, pas dans le renderer. Question restée ouverte lors du
  cadrage — à trancher si le besoin existe.

## 7. Sources

- [2D Pixel Perfect Shadows — mattdesl](https://github.com/mattdesl/lwjgl-basics/wiki/2D-Pixel-Perfect-Shadows) — les trois passes, le flou angulaire contre le crénelage du bord, et la limite mesurée ~20 lampes.
- [Fast 2D shadows in Unity using 1D shadow mapping](https://www.gamedeveloper.com/programming/fast-2d-shadows-in-unity-using-1d-shadow-mapping) — la transformation polaire pas à pas.
