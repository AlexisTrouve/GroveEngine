# Prompt pour Phase 6 & 7 - UIModule Final Features

## Contexte

Tu travailles sur **GroveEngine**, un moteur de jeu C++17 avec système de modules hot-reload. Le **UIModule** a été développé avec succès jusqu'à la **Phase 5** et est maintenant très fonctionnel.

### Architecture Existante

- **IModule** - Interface pour modules dynamiques (.so)
- **IDataNode** - Abstraction données structurées (JsonDataNode)
- **IIO (IntraIOManager)** - Pub/sub pour communication inter-modules
- **BgfxRenderer** - Rendu 2D avec bgfx (sprites, texte)

### État Actuel du UIModule

**Phases Complétées (1-5)**:

✅ **Phase 1: Core Foundation**
- `UIPanel`, `UILabel` - Widgets de base
- `UIRenderer` - Rendu via IIO (`render:sprite`, `render:text`)
- `UIContext` - État global UI (mouse, keyboard, focus)
- `UITree` - Chargement JSON → arbre de widgets

✅ **Phase 2: Layout System**
- `UILayout` - Layout automatique (vertical, horizontal, stack, absolute)
- Flexbox-like avec padding, spacing, alignment, flex sizing
- Measure + Layout passes (bottom-up → top-down)

✅ **Phase 3: Interaction & Events**
- `UIButton` - États (normal, hover, pressed, disabled)
- Hit testing récursif (point → widget)
- IIO events: `ui:click`, `ui:hover`, `ui:action`

✅ **Phase 4: More Widgets**
- `UIImage` - Affichage textures
- `UISlider` - Draggable value input (horizontal/vertical)
- `UICheckbox` - Toggle boolean
- `UIProgressBar` - Progress display read-only
- Event `ui:value_changed` pour slider/checkbox

✅ **Phase 5: Styling & Themes**
- `UIStyle` - Système de thèmes avec palette de couleurs
- `UITheme` - Définition thèmes (dark, light)
- `UIStyleManager` - Résolution styles (default ← theme ← inline)
- Références couleurs (`$primary` → couleur réelle)

### Structure Actuelle

```
modules/UIModule/
├── UIModule.cpp/h              # Module principal + event system
├── Core/
│   ├── UIWidget.h              # Interface base tous widgets
│   ├── UIContext.h/cpp         # État global + hit testing
│   ├── UITree.h/cpp            # Parsing JSON + factories
│   ├── UILayout.h/cpp          # Système de layout
│   └── UIStyle.h/cpp           # Thèmes et styles
├── Widgets/
│   ├── UIPanel.h/cpp
│   ├── UILabel.h/cpp
│   ├── UIButton.h/cpp
│   ├── UIImage.h/cpp
│   ├── UISlider.h/cpp
│   ├── UICheckbox.h/cpp
│   └── UIProgressBar.h/cpp
└── Rendering/
    └── UIRenderer.h/cpp        # Publish render commands via IIO
```

### Topics IIO

**Subscribed (Inputs)**:
- `input:mouse:move` → `{x, y}`
- `input:mouse:button` → `{button, pressed, x, y}`
- `input:keyboard` → `{keyCode, char}`
- `ui:load` → `{path}` - Load new layout
- `ui:set_visible` → `{id, visible}` - Show/hide widget

**Published (Outputs)**:
- `render:sprite` → Background, panels, images
- `render:text` → Labels, button text
- `ui:click` → `{widgetId, x, y}`
- `ui:hover` → `{widgetId, enter: bool}`
- `ui:action` → `{action, widgetId}` - Semantic action
- `ui:value_changed` → `{widgetId, value/checked}`

## Fichiers à Lire en Premier

1. `docs/PLAN_UI_MODULE.md` - Plan complet des 7 phases
2. `docs/UI_MODULE_PHASE2_COMPLETE.md` - Phase 2 (Layout)
3. `docs/UI_MODULE_PHASE3_COMPLETE.md` - Phase 3 (Interactions)
4. `modules/UIModule/Core/UIWidget.h` - Interface widget
5. `modules/UIModule/UIModule.cpp` - Event system principal
6. `modules/UIModule/Core/UIStyle.h` - Système de thèmes

## Phase 6: Text Input

### Objectif
Implémenter un champ de saisie de texte interactif.

### Composants à Créer

#### 1. `Widgets/UITextInput.h/cpp`

**Fonctionnalités**:
- Saisie de texte avec curseur
- Sélection de texte (drag pour sélectionner)
- Copier/coller basique (via clipboard système ou buffer interne)
- Curseur clignotant
- Input filtering (numbers only, max length, regex, etc.)
- Password mode (masquer caractères)

**États**:
- Normal, Focused, Disabled
- Cursor position (index dans string)
- Selection range (start, end)

**Events**:
- `ui:text_changed` → `{widgetId, text}`
- `ui:text_submit` → `{widgetId, text}` - Enter pressed

**JSON Exemple**:
```json
{
  "type": "textinput",
  "id": "username_input",
  "text": "",
  "placeholder": "Enter username...",
  "maxLength": 20,
  "filter": "alphanumeric",
  "onSubmit": "login:username"
}
```

#### 2. Gestion Clavier dans UIModule

**Extend `UIContext`**:
- Tracking du widget focused
- Keyboard event routing au widget focused

**Extend `UIModule::processInput()`**:
- Route `input:keyboard` vers widget focused
- Support Tab navigation (focus next/previous)

**Keyboard Events à Gérer**:
- Caractères imprimables → Ajouter au texte
- Backspace → Supprimer caractère avant curseur
- Delete → Supprimer caractère après curseur
- Left/Right arrows → Déplacer curseur
- Home/End → Début/fin de texte
- Ctrl+C/V → Copy/paste (optionnel)
- Enter → Submit

#### 3. Curseur Clignotant

**Animation**:
- Timer dans `UITextInput::update()` pour blink
- Render cursor si visible et focused
- Blink interval ~500ms

#### 4. Rendu

**UIRenderer Extension** (si nécessaire):
- Render curseur (ligne verticale)
- Render sélection (rectangle semi-transparent)
- Clip texte si trop long (scroll horizontal)

### Tests

**Fichier**: `tests/visual/test_27_ui_textinput.cpp`
- 3-4 text inputs avec différents filtres
- Placeholder text
- Password input
- Submit action qui affiche le texte en console

**JSON**: `assets/ui/test_textinput.json`

### Critères de Succès Phase 6

- [ ] UITextInput widget compile et fonctionne
- [ ] Saisie de texte visible en temps réel
- [ ] Curseur clignotant visible quand focused
- [ ] Backspace/Delete fonctionnels
- [ ] Event `ui:text_submit` publié sur Enter
- [ ] Input filtering fonctionne (numbers only, max length)
- [ ] Focus management (click pour focus, Tab pour next)
- [ ] Test visuel démontre toutes les fonctionnalités

## Phase 7: Advanced Features

### Objectif
Fonctionnalités avancées pour un système UI production-ready.

### 7.1 UIScrollPanel

**Scroll Container**:
- Panel avec scroll vertical/horizontal
- Scrollbars visuels (optionnels)
- Mouse wheel support
- Touch/drag scrolling
- Clip content (ne pas render en dehors bounds)

**JSON**:
```json
{
  "type": "scrollpanel",
  "width": 300,
  "height": 400,
  "scrollVertical": true,
  "scrollHorizontal": false,
  "showScrollbar": true
}
```

### 7.2 Drag & Drop

**Draggable Widgets**:
- Attribut `draggable: true` sur widget
- Événements `ui:drag_start`, `ui:drag_move`, `ui:drag_end`
- Drag preview (widget suit la souris)
- Drop zones avec `ui:drop` event

**Use Case**:
- Réorganiser items dans liste
- Drag & drop inventory items
- Move windows/panels

### 7.3 Tooltips

**Hover Tooltips**:
- Attribut `tooltip: "text"` sur widget
- Apparition après delay (~500ms hover)
- Position automatique (éviter bords écran)
- Style configurable

**JSON**:
```json
{
  "type": "button",
  "text": "Save",
  "tooltip": "Save your progress"
}
```

### 7.4 Animations

**Animation System**:
- Fade in/out (alpha)
- Slide in/out (position)
- Scale up/down
- Rotation (si supporté par renderer)

**Easing Functions**:
- Linear, EaseIn, EaseOut, EaseInOut

**JSON**:
```json
{
  "type": "panel",
  "animation": {
    "type": "fadeIn",
    "duration": 300,
    "easing": "easeOut"
  }
}
```

### 7.5 Data Binding

**Auto-sync Widget ↔ IDataNode**:
- Attribut `dataBinding: "path.to.data"`
- Widget auto-update quand data change
- Data auto-update quand widget change

**Exemple**:
```json
{
  "type": "slider",
  "id": "volume_slider",
  "dataBinding": "settings.audio.volume"
}
```

**Implementation**:
- UIModule subscribe à data changes
- Widget read/write via IDataNode
- Bidirectional sync

### 7.6 Hot-Reload des Layouts

**Runtime Reload**:
- Subscribe `ui:reload` topic
- Recharge JSON sans restart app
- Preserve widget state si possible
- Utile pour design iteration

### Priorisation Phase 7

**Must-Have** (priorité haute):
1. **UIScrollPanel** - Très utile pour listes longues
2. **Tooltips** - UX improvement significatif

**Nice-to-Have** (priorité moyenne):
3. **Animations** - Polish, pas critique
4. **Data Binding** - Convenience, mais peut être fait manuellement

**Optional** (priorité basse):
5. **Drag & Drop** - Cas d'usage spécifiques
6. **Hot-Reload** - Utile en dev, pas en prod

### Tests Phase 7

**test_28_ui_scroll.cpp**:
- ScrollPanel avec beaucoup de contenu
- Vertical + horizontal scroll
- Mouse wheel

**test_29_ui_advanced.cpp**:
- Tooltips sur plusieurs widgets
- Animations (fade in panel au start)
- Data binding demo (si implémenté)

## Ordre Recommandé d'Implémentation

### Partie 1: Phase 6 (UITextInput)
1. Créer `UITextInput.h/cpp` avec structure de base
2. Implémenter rendu texte + curseur
3. Implémenter keyboard input handling
4. Ajouter focus management à UIModule
5. Implémenter input filtering
6. Créer test visuel
7. Documenter Phase 6

### Partie 2: Phase 7.1 (ScrollPanel)
1. Créer `UIScrollPanel.h/cpp`
2. Implémenter scroll logic (offset calculation)
3. Implémenter mouse wheel support
4. Implémenter scrollbar rendering (optionnel)
5. Implémenter content clipping
6. Créer test avec long content
7. Documenter

### Partie 3: Phase 7.2 (Tooltips)
1. Créer `UITooltip.h/cpp` ou intégrer dans UIContext
2. Implémenter hover delay timer
3. Implémenter tooltip positioning
4. Intégrer dans widget factory (parse `tooltip` property)
5. Créer test
6. Documenter

### Partie 4: Phase 7.3+ (Optionnel)
- Animations si temps disponible
- Data binding si cas d'usage clair
- Drag & drop si besoin spécifique

## Notes Importantes

### Architecture

**Garder la cohérence**:
- Tous les widgets héritent `UIWidget`
- Communication via IIO pub/sub uniquement
- JSON configuration pour tout
- Factory pattern pour création widgets
- Hot-reload ready (serialize state dans `getState()`)

**Patterns Existants**:
- Hit testing dans `UIContext::hitTest()`
- Event dispatch dans `UIModule::updateUI()`
- Widget factories dans `UITree::registerDefaultWidgets()`
- Style resolution via `UIStyleManager`

### Performance

**Considérations**:
- Hit testing est O(n) widgets → OK pour UI (< 500 widgets typique)
- Layout calculation chaque frame → OK si pas trop profond
- Text input: éviter realloc à chaque caractère
- ScrollPanel: culling pour ne pas render widgets hors vue

### Limitations Connues à Adresser

**Text Centering**:
- UIRenderer n'a pas de text measurement API
- Texte des boutons pas vraiment centré
- Solution: Ajouter `measureText()` à UIRenderer ou BgfxRenderer

**Border Rendering**:
- Propriété `borderWidth`/`borderRadius` existe mais pas rendue
- Soit ajouter à UIRenderer, soit accepter limitation

**Focus Visual**:
- Pas d'indicateur visuel de focus actuellement
- Ajouter border highlight ou overlay pour widget focused

## Fichiers de Référence

### Widgets Existants (pour pattern)
- `modules/UIModule/Widgets/UIButton.cpp` - Interaction + states
- `modules/UIModule/Widgets/UISlider.cpp` - Drag handling
- `modules/UIModule/Widgets/UICheckbox.cpp` - Toggle state

### Core Systems
- `modules/UIModule/Core/UIContext.cpp` - Hit testing pattern
- `modules/UIModule/UIModule.cpp` - Event publishing pattern
- `modules/UIModule/Core/UITree.cpp` - Widget factory pattern

### Tests Existants
- `tests/visual/test_26_ui_buttons.cpp` - Input forwarding SDL → IIO
- `assets/ui/test_widgets.json` - JSON structure reference

## Build & Test

```bash
# Build UIModule
cmake -DGROVE_BUILD_UI_MODULE=ON -DGROVE_BUILD_BGFX_RENDERER=ON -B build-bgfx
cmake --build build-bgfx --target UIModule -j4

# Build test
cmake --build build-bgfx --target test_27_ui_textinput -j4

# Run test
cd build-bgfx/tests
./test_27_ui_textinput
```

## Critères de Succès Finaux

### Phase 6 Complete
- ✅ UITextInput fonctionne avec keyboard input
- ✅ Focus management implémenté
- ✅ Events `ui:text_changed` et `ui:text_submit`
- ✅ Input filtering fonctionne
- ✅ Test visuel démontre toutes les features

### Phase 7 Complete (Minimum)
- ✅ UIScrollPanel fonctionne avec mouse wheel
- ✅ Tooltips s'affichent au hover
- ✅ Tests visuels pour scroll + tooltips
- ✅ Documentation complète

### UIModule Production-Ready
- ✅ 8+ widget types utilisables
- ✅ Layout system flexible
- ✅ Theme system pour cohérence visuelle
- ✅ Event system complet
- ✅ Hot-reload support
- ✅ Tests couvrant toutes les features
- ✅ Documentation exhaustive

## Documentation à Créer

Après chaque phase:
- `docs/UI_MODULE_PHASE6_COMPLETE.md`
- `docs/UI_MODULE_PHASE7_COMPLETE.md`
- `docs/UI_MODULE_FINAL.md` - Guide complet d'utilisation

Inclure:
- Features implémentées
- JSON examples
- Event flow diagrams
- Limitations connues
- Best practices
- Performance notes

## Questions Fréquentes

**Q: Faut-il implémenter TOUTE la Phase 7?**
R: Non, focus sur ScrollPanel + Tooltips (priorité haute). Le reste est optionnel selon besoins.

**Q: Comment gérer clipboard pour copy/paste?**
R: Simplifier - buffer interne suffit pour MVP. Clipboard OS peut être ajouté plus tard.

**Q: Scroll horizontal pour TextInput si texte trop long?**
R: Oui, essentiel. Calculer offset pour garder curseur visible.

**Q: Multi-line text input?**
R: Pas nécessaire Phase 6. Single-line suffit. Multi-line = Phase 7+ si besoin.

**Q: Animation system: nouvelle classe ou intégré widgets?**
R: Nouvelle classe `UIAnimation` + `UIAnimator` manager. Garder widgets simples.

**Q: Data binding: pull ou push?**
R: Push (reactive). Subscribe aux changes IDataNode, update widget automatiquement.

## Bon Courage!

Le UIModule est déjà très solide (Phases 1-5). Phase 6 et 7 vont le rendre production-ready et feature-complete.

Focus sur:
1. **Qualité > Quantité** - Mieux vaut TextInput parfait que 10 features buggées
2. **Tests** - Chaque feature doit avoir un test visuel
3. **Documentation** - Code self-documenting + markdown docs
4. **Cohérence** - Suivre patterns existants

Les fondations sont excellentes, tu peux être fier du résultat! 🚀
