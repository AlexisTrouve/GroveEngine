# UIModule - Plan d'implémentation

## Vue d'ensemble

Module UI déclaratif avec configuration JSON, hiérarchie de widgets retained-mode, et intégration IIO pour le rendu et les événements.

## Architecture

```
modules/UIModule/
├── UIModule.cpp/h              # Module principal IModule
├── Core/
│   ├── UIContext.cpp/h         # État global (focus, hover, active, drag)
│   ├── UILayout.cpp/h          # Système de layout (flexbox-like)
│   ├── UIStyle.cpp/h           # Thèmes, couleurs, marges, fonts
│   └── UITree.cpp/h            # Arbre de widgets, parsing JSON
├── Widgets/
│   ├── UIWidget.h              # Interface de base
│   ├── UIPanel.cpp/h           # Container avec children + layout
│   ├── UIButton.cpp/h          # Bouton cliquable
│   ├── UILabel.cpp/h           # Texte statique/dynamique
│   ├── UIImage.cpp/h           # Affichage texture
│   ├── UISlider.cpp/h          # Slider horizontal/vertical
│   ├── UICheckbox.cpp/h        # Toggle on/off
│   ├── UITextInput.cpp/h       # Champ de saisie texte
│   └── UIProgressBar.cpp/h     # Barre de progression
└── Rendering/
    └── UIRenderer.cpp/h        # Génère sprites/text via IIO topics
```

## Phases d'implémentation

### Phase 1: Core Foundation
**Objectif:** Infrastructure de base, rendu d'un panel simple

**Fichiers:**
- `UIModule.cpp/h` - Module IModule avec setConfiguration/process/shutdown
- `Core/UIWidget.h` - Interface de base pour tous les widgets
- `Core/UIContext.cpp/h` - État global UI
- `Core/UITree.cpp/h` - Chargement JSON → arbre de widgets
- `Widgets/UIPanel.cpp/h` - Premier widget container
- `Widgets/UILabel.cpp/h` - Affichage texte simple
- `Rendering/UIRenderer.cpp/h` - Envoi des sprites/text via IIO

**Topics IIO:**
- Subscribe: `input:mouse`, `input:keyboard`
- Publish: `render:sprite`, `render:text`

**Test:** Afficher un panel avec un label "Hello UI"

---

### Phase 2: Layout System
**Objectif:** Positionnement automatique des widgets

**Composants:**
- Layout modes: `vertical`, `horizontal`, `stack` (superposé), `absolute`
- Propriétés: `padding`, `margin`, `spacing`, `align`, `justify`
- Sizing: `width`, `height`, `minWidth`, `maxWidth`, `flex`

**Algorithme:**
1. Mesure récursive (bottom-up) - calcul des tailles préférées
2. Layout récursif (top-down) - assignation des positions finales

**JSON exemple:**
```json
{
  "type": "panel",
  "layout": "vertical",
  "padding": 10,
  "spacing": 5,
  "align": "center",
  "children": [...]
}
```

---

### Phase 3: Interaction & Events
**Objectif:** Boutons cliquables, gestion du focus

**Composants:**
- `UIButton.cpp/h` - États: normal, hover, pressed, disabled
- Hit testing récursif (point → widget)
- Propagation d'événements (bubble up)
- Focus management (tab navigation)

**Events IIO (publish):**
- `ui:click` - `{ widgetId, x, y }`
- `ui:hover` - `{ widgetId, enter: bool }`
- `ui:focus` - `{ widgetId }`
- `ui:action` - `{ action: "game:start" }` (depuis onClick du JSON)

**JSON exemple:**
```json
{
  "type": "button",
  "id": "btn_play",
  "text": "Jouer",
  "onClick": "game:start",
  "style": {
    "normal": { "bgColor": "0x444444FF" },
    "hover": { "bgColor": "0x666666FF" },
    "pressed": { "bgColor": "0x333333FF" }
  }
}
```

---

### Phase 4: More Widgets
**Objectif:** Widgets interactifs avancés

**Widgets:**
- `UIImage.cpp/h` - Affiche une texture par ID ou path
- `UISlider.cpp/h` - Valeur numérique avec drag
- `UICheckbox.cpp/h` - Toggle boolean
- `UIProgressBar.cpp/h` - Affichage read-only d'une valeur

**Events IIO:**
- `ui:value_changed` - `{ widgetId, value, oldValue }`

**JSON exemple:**
```json
{
  "type": "slider",
  "id": "volume",
  "min": 0,
  "max": 100,
  "value": 80,
  "onChange": "settings:volume"
}
```

---

### Phase 5: Styling & Themes
**Objectif:** Système de thèmes réutilisables

**Composants:**
- `UIStyle.cpp/h` - Définition des styles par widget type
- Héritage de styles (widget → parent → theme → default)
- Fichier de thème JSON séparé

**Theme JSON:**
```json
{
  "name": "dark",
  "colors": {
    "primary": "0x3498dbFF",
    "secondary": "0x2ecc71FF",
    "background": "0x2c3e50FF",
    "text": "0xecf0f1FF"
  },
  "button": {
    "padding": [10, 20],
    "borderRadius": 4,
    "fontSize": 14,
    "normal": { "bgColor": "$primary" },
    "hover": { "bgColor": "$secondary" }
  },
  "panel": {
    "bgColor": "$background",
    "padding": 15
  }
}
```

---

### Phase 6: Text Input
**Objectif:** Saisie de texte

**Composants:**
- `UITextInput.cpp/h` - Champ de saisie
- Cursor position, selection
- Clipboard (copy/paste basique)
- Input filtering (numbers only, max length, etc.)

**Events IIO:**
- `ui:text_changed` - `{ widgetId, text }`
- `ui:text_submit` - `{ widgetId, text }` (Enter pressed)

---

### Phase 7: Advanced Features
**Objectif:** Fonctionnalités avancées

**Features:**
- Scrollable panels (UIScrollPanel)
- Drag & drop
- Tooltips
- Animations (fade, slide)
- Data binding (widget ↔ IDataNode automatique)
- Hot-reload des layouts JSON

---

## Format JSON complet

```json
{
  "id": "main_menu",
  "type": "panel",
  "style": {
    "bgColor": "0x2c3e50FF",
    "padding": 30
  },
  "layout": {
    "type": "vertical",
    "spacing": 15,
    "align": "center"
  },
  "children": [
    {
      "type": "label",
      "text": "Mon Super Jeu",
      "style": { "fontSize": 32, "color": "0xFFFFFFFF" }
    },
    {
      "type": "image",
      "textureId": 1,
      "width": 200,
      "height": 100
    },
    {
      "type": "panel",
      "layout": { "type": "vertical", "spacing": 10 },
      "children": [
        {
          "type": "button",
          "id": "btn_play",
          "text": "Nouvelle Partie",
          "onClick": "game:new"
        },
        {
          "type": "button",
          "id": "btn_load",
          "text": "Charger",
          "onClick": "game:load"
        },
        {
          "type": "button",
          "id": "btn_options",
          "text": "Options",
          "onClick": "ui:show_options"
        },
        {
          "type": "button",
          "id": "btn_quit",
          "text": "Quitter",
          "onClick": "app:quit"
        }
      ]
    },
    {
      "type": "panel",
      "id": "options_panel",
      "visible": false,
      "layout": { "type": "vertical", "spacing": 8 },
      "children": [
        {
          "type": "label",
          "text": "Volume"
        },
        {
          "type": "slider",
          "id": "volume_slider",
          "min": 0,
          "max": 100,
          "value": 80,
          "onChange": "settings:volume"
        },
        {
          "type": "checkbox",
          "id": "fullscreen_check",
          "text": "Plein écran",
          "checked": false,
          "onChange": "settings:fullscreen"
        }
      ]
    }
  ]
}
```

---

## Intégration IIO

### Topics consommés (subscribe)
| Topic | Description |
|-------|-------------|
| `input:mouse:move` | Position souris pour hover |
| `input:mouse:button` | Clicks pour interaction |
| `input:keyboard` | Saisie texte, navigation |
| `ui:load` | Charger un layout JSON |
| `ui:set_value` | Modifier valeur d'un widget |
| `ui:set_visible` | Afficher/masquer un widget |

### Topics publiés (publish)
| Topic | Description |
|-------|-------------|
| `render:sprite` | Background des panels/buttons |
| `render:text` | Labels, textes des boutons |
| `ui:click` | Widget cliqué |
| `ui:value_changed` | Valeur slider/checkbox modifiée |
| `ui:action` | Action custom (onClick) |

---

## Dépendances

- `grove_impl` - IModule, IDataNode, IIO
- `BgfxRenderer` - Pour le rendu (via IIO, pas de dépendance directe)
- `nlohmann/json` ou `JsonDataNode` existant pour parsing

---

## Tests

### Test Phase 1
```cpp
// test_24_ui_basic.cpp
// Affiche un panel avec label
JsonDataNode config;
config.setString("layoutFile", "test_ui_basic.json");
uiModule->setConfiguration(config, io, nullptr);
// Loop: process() → vérifie render:sprite et render:text publiés
```

### Test Phase 3
```cpp
// test_25_ui_button.cpp
// Simule des clicks, vérifie les events ui:action
io->publish("input:mouse:button", mouseData);
// Vérifie que ui:action avec "game:start" est publié
```

---

## Estimation

| Phase | Complexité | Description |
|-------|------------|-------------|
| 1 | Moyenne | Core + Panel + Label + Renderer |
| 2 | Moyenne | Layout system |
| 3 | Moyenne | Button + Events + Hit testing |
| 4 | Facile | Widgets supplémentaires |
| 5 | Facile | Theming |
| 6 | Moyenne | Text input |
| 7 | Complexe | Features avancées |

**Ordre recommandé:** 1 → 2 → 3 → 4 → 5 → 6 → 7

On commence par la Phase 1 ?
