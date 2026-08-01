# `grove::anim::Animator` — ce que ça change (ou pas) pour les jeux consommateurs

> **À qui ça s'adresse** : aux agents/devs des projets qui consomment GroveEngine (DAOS, Drifterra,
> Fractax). Écrit côté moteur le 2026-08-01, après avoir **lu le code des consommateurs** plutôt que
> supposé ce dont ils avaient besoin.
>
> **Ce document n'est pas une invitation à migrer.** Il dit franchement pour qui la brique est
> utilisable, pour qui elle ne l'est pas, et — là où on a trouvé quelque chose en regardant — ce qui
> mérite un coup d'œil **dans votre code, pas dans le nôtre**.

## 1. Ce qui a été livré

`grove::anim::Animator` (`include/grove/anim/Animator.h`, header-only, std seul) : des **états
nommés** au-dessus de `Clip`/`AnimationPlayer`, avec un **fondu croisé** au changement d'état.

```cpp
anim.setDefaultFade(0.15f);
anim.addState("walk", &clipWalk);
anim.addState("attack", &clipAttack, Once{"idle"});   // joue une fois, revient seul
anim.play(grounded ? "walk" : "fall");                 // appelable CHAQUE frame : play() est idempotent
anim.update(dt, hierarchy);
```

Ce que le moteur prend : la **couture** (mélanger, doser, courber). Ce qu'il ne prend pas : la
**décision** — pas de conditions, pas de prédicats, pas de blend tree. Votre jeu dit *quand*.

Détail : `docs/design/anim-state-machine.md` · guide : DEVELOPER_GUIDE §*Animation* ·
démo visuelle : `blog/animator_fade.gif`.

---

## 2. DAOS — **pas utilisable en l'état**, et c'est un constat, pas une réserve

Vérifié dans `DAOS/src/`, pas supposé. Deux incompatibilités **structurelles** :

| | GroveEngine `Animator` | DAOS |
|---|---|---|
| Squelette | `grove::anim::Hierarchy`, nœuds indexés par **entier** | `Skeleton`/`Joint`/`poseSkeleton()`, joints indexés par **chaîne** (`"hipNear"`) — `src/Appearance.h` |
| Source de la pose | un `Clip` de **keyframes**, échantillonné dans le temps | des **fonctions procédurales** — `angles["hipNear"] = 0.6·sin(p)` |

La seconde est la plus profonde : l'`Animator` mélange **deux poses échantillonnées depuis des
clips**. DAOS n'a pas de clip à échantillonner — la pose est calculée directement. Brancher
l'`Animator` supposerait de convertir les poses procédurales en clips, ce qui **perdrait** ce
qu'elles ont de bien (une foulée dont la cadence suit la vitesse réelle du nain, `walkPhase[i] +=
|dx| · 9.0` — un clip à durée fixe ne fait pas ça sans effort).

**Conclusion : ne branchez pas l'`Animator`.** Votre modèle n'est pas en retard sur le nôtre, il est
différent, et sur un point il est meilleur.

---

## 3. ⚠️ Ce qu'on a trouvé **chez vous** en regardant — le seul point actionnable

`src/WorldRenderScene.cpp`, la pose procédurale par état :

```cpp
angles.clear();                                             // l.1492 (et 10 autres sites)
if (moving) {
    angles["hipNear"] = 0.6f * sin(p);                      // jusqu'à ±0.6 rad (≈ 34°)
    angles["hipFar"]  = 0.6f * sin(p + kPi);
    ...
} else {
    angles["shoulderNear"] = 0.06f * sin(b);                // la respiration
    angles["shoulderFar"]  = -0.06f * sin(b);
    // ⚠️ hipNear / hipFar ne sont PAS reposés
}
```

Et côté lecture (`src/Appearance.cpp` l.145) : un joint absent de la map vaut **0**.

**Donc : quand un nain s'arrête, ses jambes passent de mi-foulée à zéro en UNE frame.** Jusqu'à 34°
de saut, à chaque arrêt, pour chaque colon. C'est exactement le claquement que montre le pantin rouge
de `blog/animator_fade.gif` — sauf que là il est dans votre build.

Le même motif `angles.clear()` + branches par état apparaît à **onze endroits** du fichier
(l.1492, 1900, 2051, 2208, 2379, 2395, 2658, 2675, 2835, 3168, 3407). On ne les a pas tous lus ;
si le défaut est structurel il l'est probablement partout, et une correction faite une fois au bon
endroit vaut mieux que onze.

> ⚠️ **Ce n'est pas un rapport de bug moteur, et ce n'est pas vérifié à l'œil de notre côté** — c'est
> une lecture de code. La preuve serait une capture chez vous : un nain qui court, puis s'arrête.

---

## 4. Ce qui est transférable **sans rien migrer**

Un fondu croisé sur votre `unordered_map<string,float> angles` tient en une dizaine de lignes : garder
la map de la frame précédente, et interpoler vers la nouvelle pendant N ms. La seule brique qui vaut
d'être prise au moteur plutôt que réécrite, c'est le **mélange d'angle par l'arc le plus court** :

```cpp
// Sinon un angle qui passe de 3.0 à -3.0 traverse ZÉRO : le membre fait presque un tour
// complet dans le mauvais sens pendant toute la durée du fondu.
float delta = std::fmod(b - a + kPi, 2*kPi);
if (delta < 0.0f) delta += 2*kPi;
delta -= kPi;
return a + delta * t;
```

⚠️ **Vous en avez plus besoin que nous.** Nos angles viennent de keyframes écrites à la main, donc
bornées ; les vôtres sortent de `sin()` accumulé (`walkPhase[i] += ...` ne se remet jamais à zéro),
donc ils franchissent ±π en permanence.

Une note pour le moteur : cette fonction est aujourd'hui dans `grove::anim::detail`, donc pas API
publique. Si vous la voulez, dites-le — on la sort en `grove::anim::blendAngle`, c'est deux minutes
et ça ne présume rien de votre architecture.

---

## 5. Ce qu'on ne propose **pas**

Migrer DAOS sur `grove::anim`. Vous venez de livrer ragdoll, wall-catch et orientation sur votre
système ; le remplacer coûterait cher pour un gain qui n'est pas établi. La règle du moteur est de
suivre les besoins **mesurés** et de ne pas deviner — le fait mesurable ici, c'est le claquement à
l'arrêt, et il se corrige chez vous **sans rien migrer**.

---

## 6. Sans rapport avec l'anim, mais pour vous : votre contournement `scaleX` négatif est obsolète

Votre commit `0300981` miroite les pièces par un `scaleX` négatif parce que « le `flipX` du renderer
ne miroite pas les pièces `asset` ». **C'était un vrai bug moteur, il est corrigé** (2026-07-31) :
`applySpriteFlip` s'exécutait avant la résolution d'asset, qui écrasait les UV. Le flip est désormais
appliqué après, et il est **aussi honoré sur `render:sprite:update`** — ce qui n'était pas le cas.

Deux conséquences pour vous, à votre rythme :

- Vous pouvez revenir à `flipX`. ⚠️ Votre commit note que le `scaleX < 0` inverse aussi le sens
  apparent de la rotation, et que vous avez **cessé de négocier `ang`** en conséquence : repasser à
  `flipX` demande de re-négocier cet angle. Ne le faites pas à moitié.
- Plus important : `render:sprite:update` traite désormais l'apparence en **instantané complet**
  (`u0..v1`, `flipX/flipY`, `blend` — omis = défaut, comme `clip`). Le **mode retenu** devient donc
  utilisable pour une couche personnage, là où il fallait jusqu'ici ré-émettre chaque pièce à chaque
  frame en mode immédiat. À votre échelle (N colons × M pièces), ça vaut probablement une mesure.

Détail : `docs/design/sprite-transforms.md` · DEVELOPER_GUIDE §*Sprite transforms*.
