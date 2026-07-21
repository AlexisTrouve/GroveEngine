# UI 9-slice (nine-patch) — bordures composées

Statut : **SHIPPED** (branche `feat/ui-nineslice-borders`, 2026-07-21). Boutons + fenêtres.
Démo visuelle : `./build/tests/test_nineslice_demo` · artifact : capture + playground interactif.

## 1. But

Habiller un bouton ou une fenêtre avec **une seule texture de bord** qui reste correcte à **n'importe quelle
taille** : coins nets (non pixel-stretchés), arêtes/centre étirés, **bord continu**. Avant, un bouton n'avait
qu'un `borderColor`/`borderWidth` (rect plat de couleur). Le 9-slice (nine-patch) est la technique standard des
UI de jeu : découper la source en 3×3 (4 coins figés + 4 arêtes étirées sur un axe + 1 centre étiré sur deux).

## 2. Archi — 3 briques

**Choix (option B, tranché par Alexi) : primitive renderer + helper maths pur.** Écarté : expansion côté
UIModule en 9 sprites (numériques, hors du système d'assets/atlas, 9 entries/widget) — le renderer est le bon
foyer car l'UV d'atlas ne se compose (`atlasUV ∘ sliceUV`) que là où l'AssetManager vit.

1. **`grove::ui::computeNineSlice`** (`include/grove/ui/NineSlice.h`) — maths pures, header-only, std-only,
   **réutilisable par tout host** (comme `grove::anim`/`grove::camera`). Entrée : rect cible + `NinePatch`
   (dims source + marges L/R/T/B en px source). Sortie : jusqu'à 9 `NinePatchQuad` (dest rect + UV [0,1]).
   Coins à taille source native ; **squeeze proportionnel** si la cible est plus petite que L+R (resp. T+B) —
   les coins ne se chevauchent jamais, la bande centrale disparaît. Gardes : source/cible dégénérée → 0 quad.

2. **`render:nineslice:add`/`:update`/`:remove`** — primitive renderer (SceneCollector). **Expansion au
   parse-time** : UN message → jusqu'à 9 `SpriteInstance` insérées dans le bucket retained EXISTANT
   (`m_retainedSprites` world / `m_retainedHudSprites` HUD selon `space`). **Zéro nouveau pass, zéro changement
   à `finalize()`** — les 9 quads sont des sprites ordinaires (clip dans `reserved[]`, teinte dans `r,g,b,a`).
   - Enfants dans un **espace d'id réservé** (`nineSliceChildId` = bit de poids fort à 1, parent masqué 28 bits
     décalé de 4 + index 0..8) → jamais de collision avec un sprite retained normal (ids petits, bit fort à 0).
   - `add` == `update` == un **re-expand complet** (purge des 9 enfants dans les 2 buckets + reconstruit) ;
     `remove` purge les 9. Idempotent, robuste au changement de nombre de quads / d'espace.
   - Résolution texture : `asset` streamé (atlas-aware, `resolveSprite` → texId + sous-rect UV) l'emporte sur
     `textureId` numérique ; puis **composition `atlasUV ∘ sliceUV`** par quad.

3. **UIRenderer + widgets**. `UIRenderer::updateNineSlice(...)` — retained, change-détecté (rect / texture /
   teinte / clip / dims source / marges) → `publishNineSlice{Add,Update}` (topic ci-dessus, `space:"screen"`).
   - **UIButton** / **UIWindow** : bloc JSON `frame:{asset, srcW, srcH, inset | left/right/top/bottom}`
     (parsé dans UITree). Non-vide → le 9-slice **remplace** le border-rect plat + le fond ; les entrées plates
     sont réduites à zéro (jamais de co-dessin). `frame` absent → look plat inchangé (**zéro régression host**).

## 3. Contrat de teinte (⚠️ décision de design, issue du demo)

- **Bouton** : le cadre est teinté par le `bgColor` de l'**état courant** → hover/pressed **re-teintent
  gratuitement**. ⇒ l'art de cadre bouton doit être **neutre/blanc** (la teinte porte la couleur).
- **Fenêtre** : pas d'état hover → cadre dessiné en **teinte blanche** (l'art tel qu'authoré). Teinter par le
  `bgColor` sombre d'une fenêtre écraserait un cadre coloré en quasi-noir (bug attrapé au `--shot`, corrigé).

## 4. Anchor / convention

`render:nineslice` : `x,y` = **coin haut-gauche** de la boîte cible (comme `render:rect`/`text`, PAS un centre).
Chaque quad expansé devient une `SpriteInstance` dont le `x,y` est recentré (`coin + ½taille`) car le pipeline
sprite attend un centre.

## 5. Preuve (138 assertions, 4 couches)

| Test | Couche | Verrouille |
|------|--------|-----------|
| `NineSliceUnit` | maths pures (headless) | tiling exact, UV contigus, squeeze, dégénéré (62) |
| `NineSliceCollectorTest` | primitive renderer (IntraIO→FramePacket) | expansion, bucket world/HUD, teinte, update/remove (55) |
| `UINineSliceE2E` (IT_060) | module réel (dll + layout JSON) | bouton + fenêtre → `render:nineslice:add` (12) |
| `NineSliceGpu` | readback pixel (vrai GPU) | bord rouge / centre bleu = les UV échantillonnent la bonne région (9) |

Non-régression : 56/56 sur le périmètre UI/scene/GPU. **Leçon `--shot`** : le rendu correct ne se lit pas dans
le code — le premier `--shot` a montré teinte fenêtre écrasée (cadre invisible) ; le pixel readback est la preuve.

## 6. Fichiers

- `include/grove/ui/NineSlice.h` — helper maths (neuf).
- `modules/BgfxRenderer/Scene/SceneCollector.{h,cpp}` — primitive `render:nineslice:*` + expansion.
- `modules/UIModule/Rendering/UIRenderer.{h,cpp}` — `updateNineSlice` + publish + `RenderEntryType::NineSlice`.
- `modules/UIModule/Widgets/UIButton.{h,cpp}` · `UIWindow.{h,cpp}` — bloc `frame` + rendu.
- `modules/UIModule/Core/UITree.cpp` — parse du bloc `frame` (bouton + fenêtre).
- `modules/UIModule/UIModule.cpp` — `ui:set_position` accepte `width/height` (anime la taille au runtime).
- Tests : `tests/unit/test_nine_slice.cpp` · `tests/unit/test_nine_slice_gpu.cpp` ·
  `tests/integration/test_nine_slice_collector.cpp` · `tests/integration/IT_060_ui_nineslice_e2e.cpp`.
- Démo : `tests/visual/test_nineslice_demo.cpp` (+ `--shot`) · layouts `assets/ui/demo_nineslice.json` +
  `assets/ui/test_e2e_nineslice.json`.
- Docs : `docs/UI_TOPICS.md` (topic) · `docs/UI_WIDGETS.md` (bloc `frame`) · `CLAUDE.md`.

## 7. Follow-ons (non demandés)

- `frameTint` explicite par widget (aujourd'hui : bouton = bgColor d'état, fenêtre = blanc, en dur).
- Cadres via un **atlas** partagé (le chemin est déjà atlas-aware ; il manque juste des sub-sprites authorés).
- Étendre le 9-slice à d'autres conteneurs (panels, tooltips) si besoin — même API `updateNineSlice`.
- `render:texture:create` a un piège de clé `width/height` vs `w/h` (cf. mapview bake) — non lié mais à garder.
