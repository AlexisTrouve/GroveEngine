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

   > ⚠️ **Cette propriété était vraie en théorie et fausse en pratique pendant trois jours** — corrigé
   > le 2026-07-31. Le flip était appliqué **avant** `resolveSpriteTexture`, qui écrase les quatre UV
   > par le sous-rect de l'atlas : sur un sprite porté par un `asset`, le miroir était donc
   > **silencieusement jeté**. Il ne fonctionnait que sur un `textureId` numérique aux UV écrites à la
   > main — c'est-à-dire **exactement le cas que le test posait**, et aucun de ceux qu'un jeu utilise.
   > DAOS l'a signalé après l'avoir contourné par un `scaleX` négatif, soit la ligne ❌ du tableau
   > ci-dessus : le contournement leur a coûté de devoir cesser de négocier l'angle de leurs poses.
   >
   > **L'ordre d'appel EST la fonctionnalité** : `resolveSpriteTexture` d'abord, `applySpriteFlip`
   > ensuite. Deux lignes, mais l'inverse n'échoue nulle part bruyamment — il rend juste le champ inerte.
2. **Il compose correctement avec `rotation`.** L'image est mirroitée dans son quad, puis le quad
   tourne — soit exactement `flip → rotate`.
3. **Il ne coûte rien.** Pas de changement de shader, pas d'octet supplémentaire par instance.

`v_texcoord0 = mix(vec2(u0,v0), vec2(u1,v1), a_position.xy)` interpole linéairement : `u0 > u1` est
parfaitement valide, aucune hypothèse d'ordre n'est faite côté GPU.

## 4. Deux limites, assumées

### `textureId: 0` — le miroir est un no-op visuel

Un sprite sans texture est un **quad plein teinté**. Il n'y a pas d'image à mirroiter : le flip est
mathématiquement sans objet. C'est inhérent à ce qu'est un quad uni, pas un oubli d'implémentation.

### ~~`render:sprite:update` — non honoré~~ → ✅ **LIVRÉ le 2026-07-31**

*Cette section décrivait une limite. Elle est levée ; le raisonnement est gardé parce que le
diagnostic était juste et la conclusion fausse.*

**Ce qui était écrit** : `:update` est un chemin incrémental qui ne reconstruit pas ses UV ; y
appliquer un échange rendrait le résultat dépendant du **nombre de messages reçus** (deux updates
`flipX:true` se double-flipperaient). Le suivi proposé était de porter l'intention de flip dans
`SpriteInstance` — **un champ de plus sur le chemin maigre**.

**Le double-flip était réel, le remède était trop cher.** Il suffit que `:update` traite l'apparence
en **instantané complet** — relire `u0..v1` (défaut `0,0,1,1`), résoudre la texture, puis flipper. Le
flip s'applique alors toujours à des UV **fraîchement dérivées**, jamais à des UV déjà mirroitées :
idempotent par construction, sans un octet de plus par instance. La règle existait déjà **dans la
même fonction**, pour le clip (*« Re-resolve the clip every update (full snapshot) »*) — il fallait
l'étendre, pas inventer.

Conséquence de contrat, à connaître : sur `:update`, `u0..v1` / `flipX` / `flipY` / `blend` omis
**reviennent au défaut**, là où `cx/cy/scale/layer/color` omis **conservent** leur valeur. La
frontière n'est pas cosmétique — elle sépare ce qui décrit une **apparence** (qu'on republie en
entier) de ce qui décrit une **position** (qu'on ajuste). Vérifié sur les deux publishers du dépôt :
`UIRenderer` envoie toujours ses UV, `FxModule` n'en envoie jamais et retombe donc exactement sur ce
que son `:add` produisait.

⚠️ **Le vrai coût de cette limite n'était pas le flip.** Tant que `:update` ignorait `u0..v1`,
`UIRenderer` envoyait des UV neuves à chaque frame pour animer un `UIFlipbook` **que le collector
jetait** : l'animation retenue ne pouvait pas bouger à l'écran. Personne ne l'a vu parce que son E2E
(`IT_054`) observe le **message publié**, pas le paquet rendu.

## 5. Ce que ça verrouille

`SceneCollectorTest [flip]` :

- `flipX` sur un **sous-rect d'atlas** (`0.25..0.75`) → U échangés **dans** le sous-rect, V intacts,
  centre inchangé ;
- `flipX` + `flipY` + `rotation` → les quatre UV échangés, **angle intact** ;
- **aucun champ de flip → UV strictement inchangées.** C'est la non-régression : un défaut qui
  normaliserait `u0 < u1` aurait cassé tous les sprites existants en silence.

Rouge d'abord sur les deux premiers, vert d'emblée sur le troisième.

**Ajouté le 2026-07-31** — les cinq cas que les précédents ne pouvaient pas voir, tous assertés sur le
**FramePacket** et passant par un **vrai `AssetManager`** (aucun test n'en câblait un sur le collector
avant, ce qui est le trou de couverture en une ligne) :

- `flipX` sur un sprite porté par un **`asset`**, en immédiat **et** en retenu (`:add`) ;
- un sprite retenu qui **fait demi-tour** : deux `:update` consécutifs `flipX:true` doivent donner le
  **même** résultat (idempotence — c'est l'assertion qui distingue l'instantané de la bascule), puis
  `flipX` omis restaure l'orientation ;
- `:update` portant des **UV explicites** (la cellule de flipbook) ;
- `:update` portant **`blend:"additive"`**.

Plus une non-régression : la charge utile exacte de `FxModule` (asset, jamais d'UV) doit garder son
sous-rect après un `:update` — elle était **verte avant comme après**, c'est le seul garde-fou de la
bascule sémantique.

## 6. Où c'est

- `modules/BgfxRenderer/Scene/SceneCollector.cpp` — `applySpriteFlip()` + ses deux sites d'appel.
- `modules/BgfxRenderer/Shaders/vs_sprite.sc` — rotation/échelle (inchangé par ce travail).
- `tests/integration/test_scene_collector.cpp` — `[flip]`.
- Guide consommateur : `docs/DEVELOPER_GUIDE.md` § *Sprite transforms*.
