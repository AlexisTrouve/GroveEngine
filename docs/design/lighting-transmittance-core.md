# Socle — la table de transmittance polaire

> **Statut** : 📋 plan, rien d'implémenté.
> **Rôle** : **document central** des trois chantiers qui suivent. Les murs, les filtres et les
> atténuateurs ne sont pas trois techniques — ce sont **trois façons d'alimenter cette table**.
> Aucun des trois plans ne redécrit ce qui est ici.
>
> Plans qui en dépendent : [murs](lighting-walls.md) · [filtres](lighting-filters.md) ·
> [atténuateurs](lighting-attenuators.md)
> Prérequis livré : [lumières 2D L1+L2](lighting-2d.md)

## 1. La question unique

Les trois demandes se ramènent à **une seule** :

> combien de lumière, et de quelle couleur, survit du point A au point B ?

- un **mur** n'en laisse passer aucune ;
- un **filtre** en laisse passer une part, teintée ;
- un **atténuateur** en absorbe progressivement le long du trajet.

Ce ne sont pas trois mécanismes. C'est un seul — une **transmittance multiplicative accumulée le
long du rayon** — avec trois façons de la renseigner. Le mur est le cas dégénéré : transmittance
nulle.

**C'est la conclusion qui justifie ce document.** Faire les murs avec une technique dédiée puis
découvrir aux filtres qu'il faut changer de structure de données coûterait le premier chantier
entier.

## 2. Pourquoi le shadow map 1D ne suffit PAS

La technique canonique en 2D est le **shadow map 1D par lampe** ([2D Pixel Perfect
Shadows](https://github.com/mattdesl/lwjgl-basics/wiki/2D-Pixel-Perfect-Shadows), [Fast 2D shadows
using 1D shadow mapping](https://www.gamedeveloper.com/programming/fast-2d-shadows-in-unity-using-1d-shadow-mapping)) :
on rend les occulteurs autour de la lampe, on passe en polaire, et pour chaque angle on garde **la
distance au premier pixel opaque**. Une texture d'un pixel de haut suffit ; les lampes s'empilent en
lignes.

C'est excellent, et le coût devient indépendant du nombre de murs — on ne raisonne plus en géométrie
mais en pixels.

**Mais une distance est binaire par construction.** « Distance au premier occulteur » ne peut pas
exprimer « a traversé du verre rouge à 30 px puis du brouillard ». Il n'y a pas de place dans la
donnée pour le dire. Les filtres ne sont donc pas une extension de cette structure — ils la cassent.

## 3. La structure retenue

Par lampe, une table indexée **(angle θ, rayon r)** contenant la **transmittance RGB accumulée** de
la lampe jusqu'à `r` dans la direction `θ`.

```
T(θ, r) ∈ [0,1]³      // ce qui reste de la lumière à la distance r
```

L'éclairage devient alors, pour un fragment à la distance `d` et à l'angle `θ` d'une lampe :

```
contribution = couleur × intensité × atténuation(d, rayon) × T(θ, d)
```

Le terme `T` est le seul ajout par rapport à ce qui existe déjà en L2.

### La matière, côté entrée

Une carte d'occultation où chaque pixel porte sa **transmittance par unité de longueur** (RGB) :

| Matière | Transmittance / unité | Effet |
|---|---|---|
| vide | `(1, 1, 1)` | rien |
| mur | `(0, 0, 0)` | ombre dure |
| verre rouge | `(1, 0.3, 0.3)` | teinte au-delà |
| brouillard | `exp(−α)` | absorbe continûment |

L'accumulation est un **produit courant** le long du rayon :

```
T(θ, r) = Π  transmittanceParUnité(pixel)^(longueur du pas)
```

Beer-Lambert en tombe naturellement : `T = exp(−α·d)` s'écrit `pow(exp(−α), d)`
([Volumetric Rendering](https://wallisc.github.io/rendering/2020/05/02/Volumetric-Rendering-Part-1.html)).
Et un mur est simplement un pixel à zéro : une seule traversée annule tout le reste du rayon.

### Deux propriétés qui simplifient la vie

**Le produit est commutatif.** Traverser le verre rouge puis le bleu donne le même résultat que
l'inverse. **L'ordre des occulteurs n'a donc pas à être géré** — pas de tri, pas de profondeur. Ça
tombe bien, parce qu'un tri par lampe aurait été le coût dominant.

**Une seule carte d'entrée pour les trois chantiers.** Murs, filtres et brouillard écrivent dans la
même texture, avec des valeurs différentes. Les trois plans ajoutent chacun une façon de la
remplir — jamais une seconde structure.

## 4. Construire la table sans y passer la frame

Le calcul naïf — chaque texel `(θ, r)` marche depuis 0 — est en **O(N²) par angle** : à 256×256 ça
fait 16 M de pas par lampe. Inutilisable.

C'est un **produit préfixe**, donc un *scan*. En ping-pong façon Hillis-Steele, **log₂(N) passes**
suffisent : 8 passes de 256×256, soit ~500 k opérations de texel par lampe au lieu de 16 M.

> **Optimisation à garder pour plus tard, pas à faire d'emblée** : quand la scène ne contient que des
> occulteurs opaques (cas « murs seuls »), le produit préfixe dégénère en « première distance nulle »
> et le shadow map 1D redevient suffisant, en une passe et 1/256 de la mémoire. Le socle **peut**
> détecter ce cas et basculer. Mais deux chemins à maintenir avant d'avoir mesuré que le premier
> coûte trop cher, c'est le genre d'optimisation qu'on paie deux fois.

## 5. ⚠️ L'arbitrage à trancher AVANT la première ligne

**Une carte d'occultation par lampe, ou une seule partagée en espace écran ?**

| | Par lampe | Partagée (espace écran) |
|---|---|---|
| Justesse | exacte : la matière hors écran occulte quand même | les occulteurs **hors champ n'existent pas** |
| Coût | N rendus de la matière par frame | **un seul**, quel que soit le nombre de lampes |
| Artefact | aucun | une ombre disparaît quand son mur sort du cadre |

La plupart des implémentations 2D prennent la partagée et vivent avec l'artefact. **Ma
recommandation : partagée**, avec une marge autour du viewport pour repousser le défaut hors du
champ visible. Mais c'est un choix visible à l'écran, donc c'est le tien.

## 6. Budget, et ce qu'il faut réviser

L'auteur du shadow map 1D mesure la limite autour de **20 lampes** avant que le fill rate décroche —
et c'était sans transmittance ni table 2D. **Ça resserre nettement le « des dizaines de lampes »
écrit dans [lighting-2d.md](lighting-2d.md) §4.**

Il faudra donc **mesurer avant de promettre quoi que ce soit** : un banc qui monte le nombre de
lampes ombrées jusqu'au décrochage, comme celui des sprites. Et corriger la doc consommateur en
conséquence plutôt que de la laisser annoncer un budget qui n'a plus cours.

## 7. Ce que le socle livre, et ce qu'il ne livre pas

**Livre** : la table `T(θ, r)`, sa construction par scan, son échantillonnage dans la passe de
lumière, et la carte d'occultation vide.

**Ne livre pas** : aucun moyen d'y écrire de la matière. Le socle seul est **strictement neutre** —
table remplie de `(1,1,1)`, sortie identique à L2 au pixel près. C'est d'ailleurs sa preuve : le
socle est correct quand il ne change rien.

C'est la même discipline que L1, qui livrait la plomberie sans une seule lumière : un échec au socle
est un échec de structure, jamais de matière.

## 8. Sources

- [2D Pixel Perfect Shadows — mattdesl](https://github.com/mattdesl/lwjgl-basics/wiki/2D-Pixel-Perfect-Shadows) — la transformation polaire, les trois passes, et le chiffre des ~20 lampes.
- [Fast 2D shadows in Unity using 1D shadow mapping](https://www.gamedeveloper.com/programming/fast-2d-shadows-in-unity-using-1d-shadow-mapping) — la même technique, avec l'empilement des lampes en lignes d'une texture.
- [Volumetric Rendering, Part 1 — Beer-Lambert](https://wallisc.github.io/rendering/2020/05/02/Volumetric-Rendering-Part-1.html) — `I = I₀·exp(−α·d)`, d'où vient la forme multiplicative.
- [Volumetric Raymarching — GM Shaders](https://mini.gmshaders.com/p/volumetric) — l'accumulation le long du rayon, transposable en 2D.
- [Using Colored Translucent Shadows — Unreal](https://dev.epicgames.com/documentation/unreal-engine/using-colored-translucent-shadows-in-unreal-engine) — le modèle « lumière transmise × couleur du matériau », pondéré par l'opacité.
