# Prompt pour implémenter le UIModule

## Contexte

Tu travailles sur GroveEngine, un moteur de jeu C++17 avec système de modules hot-reload. Le projet utilise:

- **IModule** - Interface pour les modules dynamiques (.so)
- **IDataNode** - Abstraction pour données structurées (implémenté par JsonDataNode)
- **IIO (IntraIOManager)** - Système pub/sub pour communication inter-modules
- **BgfxRenderer** - Module de rendu 2D avec bgfx (sprites, text, particules)

## Tâche

Implémenter le **UIModule** - un système UI déclaratif avec:
- Configuration via JSON (layouts, styles, thèmes)
- Hiérarchie de widgets (Panel, Button, Label, Slider, etc.)
- Rendu via IIO topics (`render:sprite`, `render:text`)
- Gestion des inputs via IIO (`input:mouse`, `input:keyboard`)

## Fichiers à lire en premier

1. `plans/PLAN_UI_MODULE.md` - Plan détaillé des 7 phases
2. `CLAUDE.md` - Instructions du projet, patterns de code
3. `include/grove/IModule.h` - Interface module
4. `include/grove/IDataNode.h` - Interface données
5. `modules/BgfxRenderer/BgfxRendererModule.cpp` - Exemple de module existant
6. `src/grove/JsonDataNode.cpp` - Implémentation IDataNode

## Phase à implémenter

Commence par la **Phase 1: Core Foundation**:

1. Créer la structure de dossiers:
   ```
   modules/UIModule/
   ├── UIModule.cpp/h
   ├── Core/
   │   ├── UIWidget.h
   │   ├── UIContext.cpp/h
   │   └── UITree.cpp/h
   ├── Widgets/
   │   ├── UIPanel.cpp/h
   │   └── UILabel.cpp/h
   └── Rendering/
       └── UIRenderer.cpp/h
   ```

2. Implémenter `UIModule` comme IModule:
   - `setConfiguration()` - Charge le fichier JSON de layout
   - `process()` - Update l'UI, publie les render commands
   - `shutdown()` - Cleanup

3. Implémenter `UIWidget` (interface de base):
   ```cpp
   class UIWidget {
   public:
       virtual ~UIWidget() = default;
       virtual void update(UIContext& ctx, float deltaTime) = 0;
       virtual void render(UIRenderer& renderer) = 0;

       std::string id;
       float x, y, width, height;
       bool visible = true;
       UIWidget* parent = nullptr;
       std::vector<std::unique_ptr<UIWidget>> children;
   };
   ```

4. Implémenter `UIPanel` et `UILabel`

5. Implémenter `UIRenderer` qui publie sur IIO:
   ```cpp
   void UIRenderer::drawRect(float x, float y, float w, float h, uint32_t color) {
       auto sprite = std::make_unique<JsonDataNode>("sprite");
       sprite->setDouble("x", x);
       sprite->setDouble("y", y);
       sprite->setDouble("width", w);
       sprite->setDouble("height", h);
       sprite->setInt("color", color);
       sprite->setInt("textureId", 0); // White texture
       m_io->publish("render:sprite", std::move(sprite));
   }
   ```

6. Créer un test `tests/visual/test_24_ui_basic.cpp`

## JSON de test

```json
{
  "id": "test_panel",
  "type": "panel",
  "x": 100,
  "y": 100,
  "width": 300,
  "height": 200,
  "style": {
    "bgColor": "0x333333FF"
  },
  "children": [
    {
      "type": "label",
      "text": "Hello UI!",
      "x": 10,
      "y": 10,
      "style": {
        "fontSize": 16,
        "color": "0xFFFFFFFF"
      }
    }
  ]
}
```

## Build

Ajouter au CMakeLists.txt principal:
```cmake
if(GROVE_BUILD_UI_MODULE)
    add_subdirectory(modules/UIModule)
endif()
```

Build:
```bash
cmake -DGROVE_BUILD_UI_MODULE=ON -DGROVE_BUILD_BGFX_RENDERER=ON -B build-bgfx
cmake --build build-bgfx -j4
```

## Critères de succès Phase 1

- [ ] Module compile et se charge dynamiquement
- [ ] Parse un JSON de layout simple
- [ ] Affiche un panel (rectangle coloré) via `render:sprite`
- [ ] Affiche un label (texte) via `render:text`
- [ ] Test visuel fonctionnel

## Notes importantes

- Utiliser `JsonDataNode` pour parser les layouts (pas de lib externe)
- Le rendu passe par IIO, pas d'appels directs à bgfx
- Suivre les patterns de `BgfxRendererModule` pour la structure
- Layer UI = 1000+ (au-dessus des sprites de jeu)
