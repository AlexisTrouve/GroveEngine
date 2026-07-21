# UI 9-slice + texte — HANDOFF

**Statut : SHIPPED AS DEBT.** Sur `master`, poussé **gitea (origin) + github**, HEAD **`afae92c`** (2026-07-21).
Ça marche et c'est testé, mais Alexi a jugé le rendu **« pas mal mais relativement mid »** → on l'a shippé en
**dette assumée**. Ne pas le présenter comme fini ; voir §Dette avant d'investir dessus.

Design complet : **[ui-nineslice.md](ui-nineslice.md)**. Ce doc = état + reprise + dette.

---

## 1. Ce qui est livré

### A. Bordures composées 9-slice (nine-patch) — boutons + fenêtres
Une texture de bord unique → coins nets + bord continu à toute taille. Archi = **primitive renderer +
helper maths pur** (option B, choix Alexi) :
- `grove::ui::computeNineSlice` (`include/grove/ui/NineSlice.h`) — maths pures header-only, réutilisable tout host.
- `render:nineslice:add/:update/:remove` — SceneCollector expanse 1 message → ≤9 sprites retained **au
  parse-time** dans le bucket world/HUD existant (ids enfants dans un espace top-bit réservé). Zéro pass neuf,
  zéro changement `finalize()`. Asset atlas-aware (`atlasUV ∘ sliceUV`).
- Widgets : bloc JSON `frame:{asset,srcW,srcH,inset|left/right/top/bottom}` sur bouton/fenêtre.
- **Teinte** : bouton = `bgColor` de l'état (hover re-teinte, art neutre attendu) ; fenêtre = **blanc** (art
  tel qu'authoré — le `bgColor` sombre écraserait un cadre coloré, bug attrapé au `--shot`).

### B. Texte — gras + alignement + padding bouton
- `render:text{,:add,:update}` + `TextCommand` portent `align` (0 gauche / 1 centre / 2 droite, **mesuré par
  ligne**) + `bold` (gras synthétique = double-tracé). `TextPass` applique offset + double-trace. Défauts =
  byte-identique à l'existant.
- **UILabel** : `style.align` (`left`/`center`/`right`, s'ancre sur `width`) + `style.bold`.
- **UIButton** : label **vraiment centré** (H+V, avant il démarrait au centre), `style.align` (défaut centre),
  `style.bold`, `style.padding` (inset du texte).
- `ui:set_position` accepte `width/height` optionnels (anime la taille d'un widget au runtime).

### C. Cadre ultra-standard + art
- `assets/textures/ui/frame_standard.png` (128², inset 16) — panneau arrondi neutre **blanc, tintable** = la
  frame par défaut.
- 4 cadres élaborés SVG→PNG : `frame_{tech,ornate,glossy,parchment}.png`.

---

## 2. Fichiers

| Zone | Fichiers |
|------|----------|
| Maths pures | `include/grove/ui/NineSlice.h` |
| Primitive renderer | `modules/BgfxRenderer/Scene/SceneCollector.{h,cpp}` (nineslice + text align/bold parse) |
| Pipeline texte | `modules/BgfxRenderer/Frame/FramePacket.h` (TextCommand) · `modules/BgfxRenderer/Passes/TextPass.cpp` |
| UIRenderer | `modules/UIModule/Rendering/UIRenderer.{h,cpp}` (updateNineSlice, updateText align/bold) |
| Widgets | `UIButton.{h,cpp}` · `UIWindow.{h,cpp}` · `UILabel.{h,cpp}` · `Core/UITree.cpp` · `UIModule.cpp` (set_position size) |
| Assets | `assets/textures/ui/frame_*.png` (5) · `assets/ui/{demo_nineslice,demo_nineslice_art,test_e2e_nineslice}.json` |
| Docs | `docs/design/ui-nineslice.md` · `docs/UI_TOPICS.md` · `docs/UI_WIDGETS.md` · `CLAUDE.md` |

---

## 3. Tests (tous verts)

| Test | Couche |
|------|--------|
| `NineSliceUnit` | maths pures (tiling, UV, squeeze, dégénéré) — 62 assert |
| `NineSliceCollectorTest` | primitive renderer (expansion, bucket, teinte, update/remove) — 55 |
| `UINineSliceE2E` (IT_060) | bouton + fenêtre via le vrai module .dll → `render:nineslice` — 12 |
| `NineSliceGpu` | readback pixel réel (bord rouge / centre bleu) — 9 |
| `SceneCollectorTest [text]` | round-trip align/bold + défauts |

```bash
# build (MinGW ; PATH: /c/ProgramData/mingw64/mingw64/bin)
cmake -B build && cmake --build build -j4
cd build && ctest -R "NineSlice|SceneCollector|Text|UI" --output-on-failure
```
Non-régression au dernier run : 61/61 + 306 assertions scene-collector.

## 4. Démos (preuve à l'œil)

```bash
# depuis la RACINE. --shot out.png = capture headless (fenêtre cachée), vérifiée par readback.
./build/tests/test_nineslice_demo        # frames PROCÉDURALES (SDF), + bouton auto-pulse
./build/tests/test_nineslice_art_demo    # 4 cadres PNG en ASSETS (asset:register) + section texte/standard
```
Interactif : survole/clique les boutons, drag la barre de titre / le coin d'une fenêtre.

---

## 5. ⚠️ DETTE — ce qui est « mid » (à lever quand ça resurgit)

Par ordre probable d'impact (à **confirmer avec Alexi** — il n'a pas détaillé) :

1. **Police 8×8 bitmap monospace** → texte pixelisé, une seule graisse. Le « gras » est un double-tracé
   synthétique (dépannage). *Lift* : une vraie police TTF/MSDF ou un BMFont haute-def avec variante bold.
   `BitmapFont` (`modules/BgfxRenderer/Text/`) est le point d'entrée.
2. **Chrome de fenêtre par-dessus le cadre** : la barre de titre + le bouton fermer sont dessinés SUR le bord
   du 9-slice → ça clashe visuellement. *Lift* : insérer la barre de titre DANS le cadre (respecter l'inset),
   ou un cadre pensé avec une zone titre. Voir `UIWindow::render`.
3. **Pas de gestion d'overflow texte** : un libellé long déborde d'un bouton étroit (le vrai centrage l'a rendu
   visible). *Lift* : ellipsis / clip / auto-shrink / wrap dans `TextPass` ou côté widget.

Secondaire :
- Teinte asymétrique (bouton = bgColor, fenêtre = blanc) figée en dur → un `frameTint` explicite par widget
  généraliserait.
- Un label centré exige une `width` explicite ; pas d'alignement vertical d'un bloc multi-ligne.

## 6. Reprise / prochaines pistes (non demandées)

- Attaquer la dette §5 (probablement la police en premier — c'est ce qui fait le plus « mid »).
- 9-slice sur d'autres conteneurs (panels, tooltips, modals) — même API `updateNineSlice`.
- Cadres via un **atlas** partagé (le chemin est déjà atlas-aware ; manquent des sub-sprites authorés).
- Piège connexe : `render:texture:create` a une clé `width/height` vs `w/h` (cf. mapview bake) — non lié.

## 7. Leçons

- **Le rendu ne se lit pas dans le code** : le 1er `--shot` a montré une teinte fenêtre écrasée (cadre
  invisible) invisible aux tests headless → le pixel readback (`NineSliceGpu`) + le `--shot` par l'œil sont la
  vraie preuve GPU. C'est LA leçon de ce chantier (sœur du bug blanc du mapview bake).
- Défauts partout (align 0, bold false, `frame` absent) → aucune régression sur le code existant.
