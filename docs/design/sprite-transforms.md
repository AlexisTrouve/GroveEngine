# Transforms de sprite — rotation & miroir

> **Statut** : ✅ livré. `rotation` existait de longue date ; `flipX`/`flipY` ajoutés le 2026-07-28.
> **Origine** : demande DAOS (personnages *paper-doll* : un perso = plusieurs pièces en couches,
> animées procéduralement — marche, coup de pioche).
> **Contrainte transverse** : additif. Un sprite qui ne déclare rien sort **strictement inchangé** —
> Drifterra partage le moteur.

## 1. Ce qui existe

| Champ | Type | Défaut | Effet |
|---|---|---|---|
| `rotation` | radians | `0` | rotation autour du **centre du sprite**, donc `(cx,cy)` |
| `flipX` | booléen | `false` | miroir horizontal |
| `flipY` | booléen | `false` | miroir vertical |

Pas de `pivotX`/`pivotY` : le pivot **est** `(cx,cy)`, l'ancre que le sprite fournit déjà
(cf. `render-anchor-convention.md` — `cx,cy` = CENTRE pour les sprites). Un pivot séparé serait une
seconde source de vérité pour la même information.

## 2. Ordre d'application

```
flip  →  rotate  →  scale  →  translate
```

Concrètement, dans le pipeline : le **miroir vit dans l'espace texture** (échange d'UV), donc il
s'applique à l'image *avant* que le quad ne subisse quoi que ce soit ; puis `vs_sprite` applique
l'échelle en local, la rotation, et la translation :

```glsl
vec2 localPos = (a_position.xy - vec2(0.5, 0.5)) * scale;   // scale en LOCAL d'abord
// ... puis rotation autour de l'origine locale (= le centre du sprite), puis translate
```

⚠️ L'échelle est appliquée **avant** la rotation, et pas l'inverse : faire tourner puis multiplier par
une échelle non uniforme **cisaille** le sprite. Invisible sur un carré (une échelle uniforme commute
avec la rotation), catastrophique sur un rectangle tourné. C'est déjà commenté dans le shader ; ne pas
« simplifier » cet ordre.

## 3. Pourquoi le miroir est un échange d'UV

Trois implémentations étaient possibles. Le choix n'est pas arbitraire.

| Approche | Verdict |
|---|---|
| `scaleX` négatif | ❌ inverse le **sens apparent** de la rotation quand on combine les deux — l'ambiguïté que DAOS signalait |
| Nouveau champ par instance + branche shader | ❌ alourdit un chemin délibérément maigre (`SpriteInstance` est le format du batch massif) |
| **Échange d'UV** | ✅ retenu |

L'échange d'UV a trois propriétés qui comptent :

1. **Il mirroite DANS le sous-rect d'atlas.** Un sprite qui référence un `asset` reçoit des UV
   partielles (`u0..u1` ⊂ `0..1`). Un miroir naïf qui écrirait `1..0` aurait cassé **tout** sprite
   d'atlas en affichant le voisin. Échanger `u0` et `u1` mirroite à l'intérieur du sous-rect.
2. **Il compose correctement avec `rotation`.** L'image est mirroitée dans son quad, puis le quad
   tourne — soit exactement `flip → rotate`.
3. **Il ne coûte rien.** Pas de changement de shader, pas d'octet supplémentaire par instance.

`v_texcoord0 = mix(vec2(u0,v0), vec2(u1,v1), a_position.xy)` interpole linéairement : `u0 > u1` est
parfaitement valide, aucune hypothèse d'ordre n'est faite côté GPU.

## 4. Deux limites, assumées

### `textureId: 0` — le miroir est un no-op visuel

Un sprite sans texture est un **quad plein teinté**. Il n'y a pas d'image à mirroiter : le flip est
mathématiquement sans objet. C'est inhérent à ce qu'est un quad uni, pas un oubli d'implémentation.

### `render:sprite:update` — non honoré

Le miroir est appliqué sur **`render:sprite`** (éphémère) et **`render:sprite:add`** (retenu), les deux
chemins qui construisent leurs UV **de zéro** — un échange y est donc non ambigu.

`:update` est un chemin **incrémental** qui ne reconstruit pas ses UV. Y appliquer un échange rendrait
le résultat dépendant du nombre de messages reçus : deux updates portant `flipX: true` se
double-flipperaient et reviendraient à l'état non mirroité. Un miroir dont le résultat dépend du
nombre de fois où on l'a demandé n'est pas un miroir.

*Suivi si le besoin apparaît* : porter l'**intention** de flip dans `SpriteInstance` plutôt que de la
cuire dans les UV, et l'appliquer à la résolution de texture. Ça coûte un champ par instance sur le
chemin maigre — à ne payer que si un consommateur en a réellement besoin en mode retenu.

## 5. Ce que ça verrouille

`SceneCollectorTest [flip]` :

- `flipX` sur un **sous-rect d'atlas** (`0.25..0.75`) → U échangés **dans** le sous-rect, V intacts,
  centre inchangé ;
- `flipX` + `flipY` + `rotation` → les quatre UV échangés, **angle intact** ;
- **aucun champ de flip → UV strictement inchangées.** C'est la non-régression : un défaut qui
  normaliserait `u0 < u1` aurait cassé tous les sprites existants en silence.

Rouge d'abord sur les deux premiers, vert d'emblée sur le troisième.

## 6. Où c'est

- `modules/BgfxRenderer/Scene/SceneCollector.cpp` — `applySpriteFlip()` + ses deux sites d'appel.
- `modules/BgfxRenderer/Shaders/vs_sprite.sc` — rotation/échelle (inchangé par ce travail).
- `tests/integration/test_scene_collector.cpp` — `[flip]`.
- Guide consommateur : `docs/DEVELOPER_GUIDE.md` § *Sprite transforms*.
