# Transitions d'animation — `grove::anim::Animator`

> **Statut** : 📋 PLAN (2026-08-01). Demande d'Alexi après cartographie des manques réels.
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
