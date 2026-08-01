# Transitions d'animation — `grove::anim::Animator`

> **Statut** : ✅ **LIVRÉ** (A1→A4, 2026-08-01) — 20 cas dans `AnimatorUnit`, famille anim 7/7.
> Ce document reste le « pourquoi » et garde les **écarts au plan** (§7) ; le « quoi » vit dans
> `include/grove/anim/Animator.h` et le DEVELOPER_GUIDE §*Animation*.
> Plan initial : Alexi, après cartographie des manques réels.
> **Origine** : `grove::anim` a `Clip`, `AnimationPlayer` et huit courbes d'easing — et **aucun
> moyen d'enchaîner deux clips**. DAOS coud en ce moment marche / chute / grimpe / rattrapage à la
> main (`plan_character_ecs.md`, commits `d13084f`/`7827c20`).
> **Contrainte transverse** : `grove::anim` est **pur** — header-only, std seul, zéro couplage
> renderer/IIO/SDL. Ce qu'on ajoute doit le rester, sinon on perd ce qui fait sa valeur.

---

## 1. La frontière — l'arbitrage demandé, et sa réponse

C'est le point qui devait être tranché **avant** d'écrire une ligne, parce qu'il décide de ce que le
moteur possède pour toujours.

Une « machine à états d'animation » recouvre deux choses qu'on confond systématiquement :

| | Quoi | À qui |
|---|---|---|
| **La couture** | échantillonner deux clips, mélanger la pose, gérer la durée et la courbe du fondu | **MOTEUR** |
| **La décision** | *quand* passer de `walk` à `fall` | **JEU** |

**Le moteur prend la première, jamais la seconde.** Le jeu appelle `play("fall")` ; l'`Animator`
s'occupe du raccord. Il n'y a **ni condition, ni prédicat, ni `when velocity.y > 0`** — pas de
langage d'expression, pas de graphe de conditions.

Ce n'est pas une position de principe, c'est le **précédent du dépôt** :

- `DialogueModule` expose `scene:goto {node}` — un id, la décision reste au jeu ; ses conditions
  déclaratives sont volontairement **sans OR ni imbrication** (« pas un langage d'expression »).
- `FxModule` offre une **bibliothèque FIXE** de comportements à paramètres numériques, et refuse
  explicitement `follow`/`path` parce que c'est du mouvement de gameplay.
- Le pathfinding reste chez les jeux (Daedalium), le moteur n'y touche pas.

⚠️ **Conséquence assumée** : pas de *blend tree*, pas de couches additives, pas de masques par
membre. Ce sont les briques qui font qu'un système d'anim commence à décider du gameplay. Si un jeu
en a besoin un jour, ce sera sur demande **mesurée**, pas par anticipation.

---

## 2. Le fait technique qui décide du design

`Clip::apply(time, hierarchy)` **écrit dans les transforms LOCAUX** de la hiérarchie. Il ne renvoie
pas une pose — il n'y a aucun objet « pose » dans `grove::anim`.

Deux voies s'ouvraient :

| | (a) Introduire un type `Pose` | **(b) Tampon de scratch** |
|---|---|---|
| Modèle de données | nouveau (pose éparse : nodeId × propriété → valeur) | **aucun** |
| `Clip::apply` | à réécrire en `sample(time, Pose&)` | **inchangé** |
| Nœud touché par un seul des deux clips | ambigu — mélangé contre quoi ? | tombe juste tout seul (cf. §3) |
| Coût par frame | une pose éparse | une copie du vecteur de locaux (~20 nœuds : rien) |

**Retenu : (b).** Le mélange se fait sur les **locaux**, jamais sur les mondiaux — mélanger des
transforms composés donnerait n'importe quoi (la moyenne de deux positions mondiales n'est pas la
position d'une articulation moyenne). Ça tombe naturellement puisque `apply` écrit des locaux, mais
il faut l'écrire noir sur blanc : la tentation de mélanger après `Hierarchy::update()` existe.

---

## 3. Comment le fondu marche

Pendant un fondu de durée `d`, à l'instant `u = elapsed/d` :

```
1. clipSortant->apply(tSortant, hierarchy)     // la hiérarchie porte la pose A
2. scratch = hierarchy.locaux                  // on la met de côté
3. clipEntrant->apply(tEntrant, hierarchy)     // la hiérarchie porte la pose B
4. hierarchy.locaux = lerp(scratch, hierarchy, ease(courbe, u))
```

**Un nœud que seul le clip A pilote** : après l'étape 3 il porte encore la valeur de A (B ne l'a pas
touché), donc il se mélange A→A et ne bouge pas. C'est le comportement voulu, et il sort de la
construction plutôt que d'un cas particulier.

### ⚠️ La rotation ne se lerpe PAS comme un scalaire

Un `Track` porte des flottants bruts. Mélanger une rotation de `3.10` vers `−3.10` en interpolant
linéairement traverse **0** — le membre fait presque un tour complet dans le mauvais sens, pendant
toute la durée du fondu, puis se remet d'aplomb. C'est le bug classique du cross-fade et il est
invisible tant que les deux angles ne straddlent pas le passage à ±π.

Le mélange prend donc **l'arc le plus court** pour `rotation`, et un lerp droit pour x/y/scale.

> **Piège de test à ne pas se tendre** : des angles « propres » (0 → π/2) donnent le même résultat
> avec les deux implémentations. Le cas qui discrimine est **exactement** celui qui straddle le
> passage. Choisir les angles faciles rendrait la mesure aveugle — la leçon du mois.

### Les flipbooks restent dehors

Un `Flipbook` est une suite de frames **discrètes**. Mélanger deux flipbooks veut dire dessiner les
deux avec de l'alpha, donc parler au renderer — ce qui casserait la pureté de `grove::anim`. Un état
porté par un flipbook fait donc une **coupe franche**, et c'est documenté plutôt que contourné. La
machine à états couvre les états portés par un `Clip`.

---

## 4. La surface

```cpp
grove::anim::Animator anim;

anim.addState("idle", &clipIdle);                        // boucle par défaut
anim.addState("walk", &clipWalk);
anim.addState("attack", &clipAttack, Once{"idle"});      // joue une fois puis revient
anim.setDefaultFade(0.15f);                              // secondes

anim.play("walk");                 // fondu depuis l'état courant
anim.play("attack", 0.05f);        // fondu plus court pour ce passage
anim.update(dt, hierarchy);        // avance + mélange + écrit les locaux
hierarchy.update();                // composition monde — inchangé, appelé par le jeu
```

### ⚠️ `play()` est IDEMPOTENT — et c'est la propriété la plus importante du lot

Un jeu appelle `play("walk")` **à chaque frame** tant que le personnage marche. C'est la façon
naturelle d'écrire le code appelant, et une implémentation naïve **relance le clip à chaque frame** :
le personnage reste figé sur sa première image, pour toujours.

`play(name)` sur l'état **déjà courant** est donc un **no-op**. On publie un ÉTAT, pas une
TRANSITION.

> C'est mot pour mot le défaut corrigé le 2026-07-31 sur `render:sprite:update` (le flip qui
> basculait au lieu de valoir). Même classe, deux couches différentes, une semaine d'écart. Ça vaut
> d'être nommé comme un motif du dépôt et pas comme une anecdote.

Cas restants, tous fail-soft (le moteur ne jette jamais parce qu'un nom d'anim est faux) :

- `play()` sur un nom inconnu → ignoré + un log une seule fois. Un personnage qui garde son anim
  précédente est infiniment moins grave qu'un crash sur une faute de frappe.
- `play()` pendant un fondu déjà en cours → le fondu courant devient le nouveau sortant (on
  échantillonne sa pose mélangée à l'instant du basculement, pas le clip A d'origine).
- Fondu de durée 0 → coupe franche, chemin explicite (pas une division par zéro).
- Un état `Once` dont le clip finit → bascule sur sa cible avec le fondu par défaut.

---

## 5. Les tranches

| # | Contenu | Preuve |
|---|---|---|
| **A1** | `Animator` : états nommés, `play`, `update`, **sans** fondu (coupe franche) | idempotence de `play`, nom inconnu, état courant |
| **A2** | Fondu croisé + easing + **arc le plus court sur la rotation** | l'angle qui straddle ±π ; un fondu à mi-course ; durée 0 |
| **A3** | `Once{"cible"}` — une passe puis retour | fin de clip → bascule ; ne boucle pas |
| **A4** | Doc : DEVELOPER_GUIDE + `CLAUDE.md` + ce fichier remis à l'état livré | — |

Rouge d'abord à chaque tranche. Les oracles sont **numériques** (une pose mélangée est une valeur
exacte, pas un jugement visuel) — donc entièrement headless, comme le reste de `grove::anim`.

**Hors périmètre, explicitement** : conditions/prédicats, blend tree, couches additives, masques par
membre, IK, root motion. Et **aucun topic IIO** — `grove::anim` est une bibliothèque qu'on inclut,
pas un module ; un jeu qui veut piloter ses anims par le bus le fait chez lui.

---

## 6. Ce que ça ne résout pas

DAOS coud **aussi** de la logique : « si la chute dépasse tant, rattraper au mur ». Ça reste chez
eux, et c'est voulu — l'`Animator` leur enlève la couture des poses, pas la décision. Si après
livraison ils cousent encore autant, c'est que le manque était ailleurs et il faudra le remesurer
plutôt que d'élargir ce système.

---

## 7. Écarts au plan — ce que la construction a corrigé

*Consignés parce qu'un plan qu'on relit sans ses écarts se lit comme s'il avait eu raison partout.*

### 7.1 Le fondu ne marche PAS comme le §3 le décrivait

Le §3 posait une recette où **les deux clips avancent** pendant le fondu (« 1. clipSortant->apply(tSortant) »).
**Ce n'est pas ce qui est livré.** Le sortant est une **pose figée** (snapshot blending) : le mélange
part de ce qui était réellement à l'écran à la dernière frame.

Ce n'est pas un renoncement en cours de route — c'est le **§4 du même document qui a tranché contre
le §3**. Il exigeait qu'un `play()` pendant un fondu reparte de « sa pose mélangée à l'instant du
basculement ». Avec deux lecteurs qui avancent, cette pose n'existe nulle part : il aurait fallu la
matérialiser dans un tampon… c'est-à-dire faire du snapshot blending pour ce seul cas, et maintenir
**deux mécanismes** de fondu. En figeant toujours, il n'y en a qu'un, et le cas dur devient exact.

Le prix, mesurable et assumé : le sortant ne s'anime plus pendant le fondu. Sur 0,1–0,2 s c'est
imperceptible ; l'à-coup ré-entrant, lui, se voit — et dans un jeu de plateforme il arrive à chaque
enchaînement rapide.

> **Ce que ça apprend** : deux sections d'un même plan peuvent être individuellement raisonnables et
> mutuellement incompatibles. Ça ne se voit qu'à l'implémentation, et c'est une bonne raison d'écrire
> le plan **avant** sans le croire **pendant**.

⚠️ Conséquence sur le raisonnement du §3 (« un nœud que seul le clip A pilote ») : il portait sur la
recette à deux lecteurs. Avec une pose figée le résultat est le même — un nœud que l'entrant ne
touche pas garde la valeur de la frame précédente, donc mélange une valeur vers elle-même — mais par
un autre chemin. La conclusion tient, la démonstration du §3 non.

### 7.2 Deux trous que le plan n'avait pas vus

- **Un état non bouclé TERMINÉ devait rester rejouable.** Ma première version d'A1 court-circuitait
  sur `nom == courant`, ce qui rendait un coup d'épée fini **impossible à relancer**. L'invariant
  correct est « ne jamais rembobiner une anim **en cours** ». Sur un état bouclé les deux
  formulations coïncident — c'est ce qui rend la nuance invisible, et pourquoi elle a survécu à
  l'écriture du plan **et** à la première passe de tests. Trouvée en relisant ma propre correction.
- **L'ordre de l'enchaînement `Once`** dans `update()` : il doit venir **après** la mémorisation de
  la pose, sinon le fondu vers la cible part de la frame précédente. Décalage d'une frame, invisible
  en test unitaire. Le plan ne disait rien de l'ordre.

### 7.3 Ce qui a tenu exactement

La **frontière** du §1 (couture au moteur, décision au jeu) n'a jamais eu à bouger pendant les quatre
tranches — y compris quand `Once` a introduit une bascule *automatique*, qui est le point où un
système d'anim commence habituellement à décider. Elle a tenu parce qu'un `Once` décrit le CLIP
(« il ne boucle pas et il mène là »), jamais une condition de jeu.

L'API esquissée au §4 est livrée telle quelle (`addState`, `play(name, fade?)`, `Once{...}`,
`setDefaultFade`), à un ajout près : `setFadeEasing`.

### 7.4 Comment le rouge a été obtenu, tranche par tranche

Parce que « test rouge d'abord » ne veut pas dire la même chose selon ce qu'on écrit :

| | Rouge obtenu par | Ce qu'il a montré |
|---|---|---|
| **A1** | l'implémentation **naïve** de `play()` | 5 au lieu de 50 : le perso figé sur sa 1re image |
| **A2** | **sabotage** (impl et tests écrits ensemble) | lerp naïf → 0.0 au lieu de π ; mélange coupé → 200 au lieu de 125 |
| **A3** | la **donnée posée sans être câblée** | les deux bascules tombent, le reste passe |

A2 est le cas faible des trois : un rouge fabriqué après coup prouve que le test **discrimine**, pas
qu'il a guidé l'écriture. D'où le protocole appliqué — `grep` de contrôle avant de croire le rouge,
restauration **et reconstruction** avant de croire le vert (c'est la seconde moitié qui piège).
