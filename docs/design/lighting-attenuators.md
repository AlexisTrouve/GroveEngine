# Plan A — les milieux (absorption **et** diffusion)

> **Statut** : ✅ **A1 + A2 + A3 LIVRÉS le 2026-07-29.** Mesure de l'absorption : sonde à
> L0=140 / L1=74 / L2=33 (rapports 0,53 puis 0,24 contre 0,5 et 0,25 prédits ; un absorbeur linéaire
> donnerait 8). Mesure de la diffusion, sur fond **noir** : sans brouillard **0**, brouillard
> absorbant seul **0**, brouillard diffusant **255**. Verrouillé par `TransmittanceUnit [fog]` +
> `SceneCollectorTest [fog]` + `LightingGpu [fog]`/`[scatter]`. Suite complète 194/194.
>
> **Les trois plans sont clos.** Murs, filtres et milieux écrivent dans **une seule carte**, par
> **une seule passe**, avec **un seul blend** — ce que le socle annonçait, vérifié trois fois sur
> trois.
> **⚠️ RÉÉCRIT le 2026-07-29.** La version précédente ne couvrait que l'**absorption** et posait la
> diffusion hors périmètre. Le besoin réel — **des nébuleuses** — la rend obligatoire : une nébuleuse
> se *voit*. L'absorption seule aurait donné un vide qui s'assombrit sans raison visible, c'est-à-dire
> ce qui ressemble à un bug de réglage plutôt qu'à une atmosphère.
>
> **Socle** : [table de transmittance](lighting-transmittance-core.md) — lire d'abord.
> **Dépend de** : [plan W](lighting-walls.md) ✅ livré, [plan F](lighting-filters.md).

## 1. Trois choses portent le même mot

« Nébuleuse » (ou « brouillard ») en désigne trois, et les confondre coûterait cher :

| | Ce que c'est | Qui le porte |
|---|---|---|
| **Absorbe** | la lumière qui traverse est mangée — un phare porte moins loin | ✅ le socle le fait déjà |
| **Diffuse** | la lumière qui la traverse la fait **briller** — le faisceau visible de côté | 🆕 ce plan |
| **Émet** | on la voit **sans aucune lampe** | ❌ pas de la physique de lumière — un sprite ou une texture |

Le troisième n'est pas dans ce plan et ne devrait pas y entrer. L'aspect d'une nébuleuse au repos est
de l'**art**, pas un calcul d'éclairage ; le faire passer par le pipeline lumineux coûterait une
cible et une passe pour dessiner ce qu'un quad texturé dessine déjà.

## 2. En 2D, la diffusion est presque gratuite — et c'est ce qui change tout

En 3D, la diffusion est chère : il faut parcourir le **rayon de vue** à travers le volume et intégrer
ce qui rebondit vers l'œil à chaque pas.

**En vue à plat, ce rayon est perpendiculaire au plan. Il n'y a pas de profondeur à parcourir.**
L'intégrale s'effondre en un terme par pixel :

```
diffusé(px) = lumièreQuiArrive(px) × densité(px) × sigma
```

Et `lumièreQuiArrive`, la passe de lumière la calcule **déjà** — atténuation radiale, masque conique
et transmittance comprises. La diffusion n'ajoute donc pas un parcours : elle ajoute **une
multiplication et une lecture de densité**.

> ⚠️ **Hypothèse porteuse : la vue est à plat.** Le milieu est une couche vue perpendiculairement.
> Si le jeu donne une profondeur perçue — parallaxe forte, nébuleuse « devant » et « derrière » les
> vaisseaux — l'effondrement ne tient plus et ce plan est à revoir avant d'être écrit.

## 3. La conséquence structurelle, qui est le vrai sujet

Le composite fait aujourd'hui :

```
final = scene × (ambiant + lumière)
```

**Dans l'espace, `scene` est noire.** Donc `scene × lumière` vaut zéro, et **aucun faisceau ne serait
visible** — précisément le cas des nébuleuses. Le terme diffusé doit donc être **additif au résultat
final**, jamais multiplicatif avec la scène :

```
final = scene × (ambiant + lumière) + diffusé
```

C'est ce qui fait qu'une nébuleuse traversée par un moteur **brille dans le vide**, là où il n'y a
aucune surface à éclairer. Sans cette séparation on n'obtiendrait qu'un halo sur les objets solides,
et le vide resterait noir — l'inverse exact de l'effet recherché.

**C'est le seul changement d'architecture de ce plan.** Tout le reste est du câblage.

## 4. La densité, lue deux fois

La même donnée sert aux deux effets, à deux endroits différents :

| Effet | Où la densité est lue | Ce qu'elle produit |
|---|---|---|
| Absorption | **le long du rayon** lampe → fragment | la lumière qui arrive est réduite |
| Diffusion | **au fragment**, une seule lecture | ce qui reste est renvoyé vers l'œil |

Une seule carte alimente donc les deux, comme les murs et les filtres alimentent déjà la même. Le
socle ne change pas.

## 5. Surface d'écriture

| Topic | Charge | Notes |
|---|---|---|
| `render:fog` | `{x, y, w, h, density, color?, scatter?}` | volume rectangulaire, éphémère |
| `render:fog:add` / `:update` / `:remove` | `{renderId, …}` | retenu — une nébuleuse ne bouge pas vite |

**`x, y` = coin haut-gauche** (c'est un rect, même règle que les occulteurs).

`density` est le **coefficient α de Beer-Lambert**, pas une opacité 0..1 : il n'a pas de borne haute,
et doubler la distance traversée double son effet dans l'exposant. Nommer ce champ `opacity`
garantirait qu'on le règle à 1 en croyant saturer.

`scatter` (défaut 0) est le coefficient de **diffusion** — et le séparer de `density` est délibéré :
un milieu peut beaucoup absorber en diffusant peu (fumée noire) ou l'inverse (brume claire). Les
confondre supprimerait la moitié des matières exprimables.

`color` module l'absorption **sélective** : un milieu qui mange le bleu plus vite que le rouge donne
des couchers de soleil, et une nébuleuse teintée.

## 6. Découpage

| Tranche | Contenu | Preuve |
|---|---|---|
| **A1** | `render:fog` → carte de densité ; **absorption** seule | headless : oracle sur `exp(−α·d)` à trois distances. `[gpu]` : à **densité doublée**, la lumière restante est le **CARRÉ** de la précédente |
| **A2** ✅ | **diffusion** : le terme additif au composite | ✅ livré. Sur fond noir : 0 / 0 / **255** |
| **A3** ✅ | mode retenu + absorption colorée | ✅ livré. La couleur était déjà dans A1 ; A3 se réduit au mode retenu |
| **A4** ✅ | **nébuleuses** : densité radiale, hors périmètre à l'origine | ✅ livré. `render:nebula`. Core 0,63 / rasant 0,79 / hors disque **1,00** — le quad est invisible |

### Ce que ce découpage annonçait et qui s'est révélé plus simple

A2 prévoyait « **la cible additive** + le terme au composite ». **La cible n'a pas été nécessaire.**

Le coefficient de diffusion tient dans le canal **alpha de la carte d'occultation**, qui ne servait à
rien. Le blend étant multiplicatif, l'alpha accumule `Π(1 − scatter_i)` : des milieux superposés se
composent comme des couvertures indépendantes, commutativement, et le composite récupère le total par
`1 − alpha`. Les murs et les vitraux y écrivent 1 (ils ne diffusent pas) et l'effacement blanc laisse
1 — **zéro diffusion sans une seule branche**, et le contournement à coût nul intact.

Ni cible supplémentaire, ni passe supplémentaire : une lecture de texture de plus dans un composite
qui en faisait déjà deux.

### A4 — les nébuleuses, et pourquoi ce plan avait tort de les sortir du périmètre

Le §8 rangeait « le milieu texturé / animé — une densité non uniforme » hors périmètre, en ajoutant
qu'il s'agissait « probablement de la première extension demandée pour de vraies nébuleuses ».
**C'est arrivé le jour même.**

Avant de coder quoi que ce soit, la question a été posée à la mesure : peut-on approcher une
nébuleuse en **empilant des rects** de densités décroissantes ? Réponse rendue par le moteur —
quatorze **contours rectangulaires concentriques**, une ziggourat. Non.

**A4 livre `render:nebula`** : un **disque** dont la densité culmine au cœur et tombe à zéro
**exactement** au bord, avec la même courbe quadratique que l'atténuation d'une lampe. Ce zéro exact
au bord est ce qui rend le quad de découpe **invisible** — et c'est l'assertion principale du test
GPU, parce que c'est le défaut qui condamnerait la primitive (chaque nuage porterait une boîte).

Empiler des **disques**, en revanche, marche : chacun s'éteignant à son propre bord, la silhouette
combinée est organique. Ce qui échouait avec des rects réussit avec des volumes à bord nul.

Deux choix consignés :
- **une primitive de plus, pas un drapeau sur `FogCommand`** — un rect uniforme reste la bonne forme
  pour un banc de brume, et c'est ce qu'un auteur veut taper pour ça ;
- **rien n'est pré-converti côté CPU**, contrairement à toutes les autres matières : la densité varie
  par pixel, donc la conversion Beer-Lambert appartient au shader. Un collecteur « serviable » qui
  appliquerait `fogPerUnit` ici ferait exponentier deux fois.

Le shader **réutilise `vs_light`** : placer un quad unitaire à l'échelle d'un rayon autour d'un centre
monde est exactement le problème d'une lampe. Seul l'étage fragment diffère.

### Et ce qui s'est révélé déjà fait

A3 devait apporter « l'absorption colorée ». Elle était **déjà dans A1** : une fois
`fogPerUnit(density, canal)` écrit, la couleur ne coûtait rien de plus. A3 se réduit au mode retenu.

**Différence avec les filtres, et c'est une simplification** : la conversion d'un milieu **ne contient
aucune géométrie** (α est par-unité par définition), donc redimensionner un volume ne peut pas changer
en douce ce qu'il absorbe — le piège de F3 n'existe pas ici. Le registre stocke quand même les nombres
de l'auteur, parce qu'un update partiel qui ne nomme que `color` doit re-dériver depuis la densité
qu'il n'a pas redite.

### Pourquoi A1 teste un carré et pas « c'est plus sombre »

« Avec du brouillard c'est plus sombre » serait vert avec n'importe quel assombrissement — un facteur
constant passerait. Ce qui caractérise Beer-Lambert, c'est l'**exponentielle** : doubler α, ou
doubler la distance, **élève au carré** ce qui reste. C'est la seule assertion qui distingue une
absorption d'une soustraction.

### Pourquoi A2 exige un fond noir

Sur un fond clair, `scene × lumière` est déjà non nul : un test verrait de la lumière **avec ou
sans** le terme diffusé, et passerait au vert en ne prouvant rien. Le fond noir est ce qui rend le
terme additif la **seule** explication possible d'un pixel allumé.

Même piège que la caméra non déplacée de L2 et que les sondes à distance égale de W2 : un
discriminant placé là où les deux hypothèses coïncident.

## 7. Risques

1. **Le pas de marche fixe la précision de l'absorption.** Une nappe fine entre deux pas est
   invisible, une nappe traversée en biais est sous-estimée. Un milieu dense et mince est le pire cas.
2. **Densité et rayon de lampe interagissent mal à l'intuition** : `exp(−α·d)` combiné à
   l'atténuation `(1−d/r)²` chute très vite. Prévoir de baisser α d'un ordre de grandeur par rapport
   au réflexe, et **documenter une valeur de départ** plutôt que laisser chacun la chercher.
3. **Rien n'atténue l'ambiant.** Le terme ambiant n'a pas de trajet — il est global par construction.
   Une scène très brumeuse mais fortement ambiante ne paraîtra pas brumeuse. Cohérent avec le modèle,
   et parfaitement déroutant sans l'avoir écrit.
4. **La sur-brillance devient facile.** Le terme diffusé est additif et non écrêté (cible RGBA16F) ;
   une nébuleuse dense sous une lampe intense saturera. C'est voulu — c'est ce dont le bloom se
   nourrira — mais ça se règle, et il faudra le dire.

## 8. Hors périmètre

- **L'émission propre** (§1) — l'aspect d'une nébuleuse sans lumière. C'est de l'art.
- **La diffusion multiple** (la lumière qui rebondit plusieurs fois). Un seul rebond suffit
  visuellement en 2D ; le second coûterait une itération pour un gain que personne ne verrait.
- **Le milieu texturé / animé** — une densité non uniforme. Le rect uniforme est le socle
  d'authoring ; une texture de densité est l'extension naturelle, et probablement la première
  demandée pour de vraies nébuleuses.
- **Le brouillard de guerre**, qui ressemble mais n'a rien à voir : il masque de l'**information**,
  pas de la lumière, et il existe déjà côté tilemap (`render:tilemap:fog`). Deux systèmes, deux buts.

## 9. Sources

- [Volumetric Rendering, Part 1 — Beer-Lambert](https://wallisc.github.io/rendering/2020/05/02/Volumetric-Rendering-Part-1.html) — `I = I₀·exp(−α·d)`, et la séparation absorption / diffusion.
- [Volumetric Raymarching — GM Shaders](https://mini.gmshaders.com/p/volumetric) — l'accumulation pas à pas de densité et de transmittance.
- [Real-time cloudscapes with volumetric raymarching](https://blog.maximeheckel.com/posts/real-time-cloudscapes-with-volumetric-raymarching/) — pourquoi la diffusion est le terme qui *rend visible* le milieu, là où l'absorption ne fait que l'assombrir.
