# UIModule

Complete UI widget system for GroveEngine with layout, scrolling, tooltips, and automatic input handling.

## Overview

UIModule provides a full-featured UI system that integrates with BgfxRenderer for rendering and InputModule for input. All communication happens via IIO topics, ensuring complete decoupling.

## Features

- **10 Widget Types**: Buttons, Labels, Panels, Checkboxes, Sliders, Text Inputs, Progress Bars, Images, Scroll Panels, Tooltips
- **Flexible Layout**: JSON-based UI definition with hierarchical widget trees
- **Automatic Input**: Consumes `input:*` topics from InputModule automatically
- **Rendering Integration**: Publishes `render:*` topics to BgfxRenderer
- **Layer Management**: UI renders on top of game content (layer 1000+)
- **Hot-Reload Support**: Full state preservation across module reloads

## Architecture

```
InputModule → IIO (input:mouse:*, input:keyboard:*)
                ↓
            UIModule
         (Widget Tree)
                ↓
        UIRenderer (publishes)
                ↓
       IIO (render:sprite, render:text)
                ↓
          BgfxRenderer
```

## Available Widgets

| Widget | Purpose | Events Published |
|--------|---------|------------------|
| **UIButton** | Clickable button | `ui:click`, `ui:action` |
| **UILabel** | Static text display | - |
| **UIPanel** | Container widget | - |
| **UICheckbox** | Toggle checkbox | `ui:value_changed` |
| **UISlider** | Value slider (horizontal/vertical) | `ui:value_changed` |
| **UITextInput** | Text input field | `ui:value_changed`, `ui:text_submitted` |
| **UIProgressBar** | Progress indicator | - |
| **UIImage** | Sprite/image display | - |
| **UIScrollPanel** | Scrollable container | `ui:scroll` |
| **UITooltip** | Hover tooltip | - |

## Configuration

```cpp
JsonDataNode config("config");
config.setInt("windowWidth", 1920);
config.setInt("windowHeight", 1080);
config.setString("layoutFile", "./assets/ui/main_menu.json");
config.setInt("baseLayer", 1000);  // UI renders above game content

uiModule->setConfiguration(config, uiIO.get(), nullptr);
```

## Usage

### Loading UIModule

```cpp
#include <grove/ModuleLoader.h>
#include <grove/IntraIOManager.h>

auto& ioManager = IntraIOManager::getInstance();
auto uiIO = ioManager.createInstance("ui_module");
auto gameIO = ioManager.createInstance("game_logic");

ModuleLoader uiLoader;
auto uiModule = uiLoader.load("./modules/UIModule.dll", "ui_module");

JsonDataNode config("config");
config.setString("layoutFile", "./ui/menu.json");
uiModule->setConfiguration(config, uiIO.get(), nullptr);
```

### Creating UI Layout (JSON)

`ui/menu.json`:
```json
{
  "widgets": [
    {
      "type": "UIPanel",
      "id": "background",
      "x": 0,
      "y": 0,
      "width": 1920,
      "height": 1080,
      "color": 2155905279
    },
    {
      "type": "UIButton",
      "id": "play_button",
      "x": 860,
      "y": 500,
      "width": 200,
      "height": 60,
      "text": "Play",
      "fontSize": 24,
      "action": "start_game"
    },
    {
      "type": "UILabel",
      "id": "title",
      "x": 760,
      "y": 300,
      "width": 400,
      "height": 100,
      "text": "My Awesome Game",
      "fontSize": 48,
      "color": 4294967295
    },
    {
      "type": "UISlider",
      "id": "volume_slider",
      "x": 800,
      "y": 650,
      "width": 320,
      "height": 40,
      "min": 0.0,
      "max": 100.0,
      "value": 75.0,
      "orientation": "horizontal"
    },
    {
      "type": "UICheckbox",
      "id": "fullscreen_toggle",
      "x": 800,
      "y": 720,
      "width": 30,
      "height": 30,
      "checked": false
    },
    {
      "type": "UIScrollPanel",
      "id": "settings_panel",
      "x": 100,
      "y": 100,
      "width": 400,
      "height": 600,
      "contentHeight": 1200,
      "scrollbarWidth": 20
    }
  ]
}
```

### Handling UI Events

```cpp
// Subscribe to UI events in your game module
gameIO->subscribe("ui:click");
gameIO->subscribe("ui:action");
gameIO->subscribe("ui:value_changed");

// In your game module's process()
void GameModule::process(const IDataNode& input) {
    while (m_io->hasMessages() > 0) {
        auto msg = m_io->pullMessage();

        if (msg.topic == "ui:action") {
            std::string action = msg.data->getString("action", "");
            std::string widgetId = msg.data->getString("widgetId", "");

            if (action == "start_game") {
                startGame();
            }
        }

        if (msg.topic == "ui:value_changed") {
            std::string widgetId = msg.data->getString("widgetId", "");

            if (widgetId == "volume_slider") {
                double value = msg.data->getDouble("value", 50.0);
                setVolume(value);
            }

            if (widgetId == "fullscreen_toggle") {
                bool checked = msg.data->getBool("value", false);
                setFullscreen(checked);
            }
        }
    }
}
```

## IIO Topics

### Topics Consumed (from InputModule)

| Topic | Payload | Description |
|-------|---------|-------------|
| `input:mouse:move` | `{x, y}` | Mouse position |
| `input:mouse:button` | `{button, pressed, x, y}` | Mouse click |
| `input:mouse:wheel` | `{delta}` | Mouse wheel |
| `input:keyboard:key` | `{scancode, pressed, ...}` | Key event |
| `input:keyboard:text` | `{text}` | Text input (for UITextInput) |

### Topics Published (UI Events)

| Topic | Payload | Description |
|-------|---------|-------------|
| `ui:click` | `{widgetId, x, y}` | Widget clicked |
| `ui:action` | `{widgetId, action}` | Button action triggered |
| `ui:value_changed` | `{widgetId, value}` | Slider/checkbox/input changed |
| `ui:text_submitted` | `{widgetId, text}` | Text input submitted (Enter) |
| `ui:hover` | `{widgetId, enter}` | Mouse entered/left widget |
| `ui:scroll` | `{widgetId, scrollX, scrollY}` | Scroll panel scrolled |

### Topics Published (Rendering)

| Topic | Payload | Description |
|-------|---------|-------------|
| `render:sprite` | `{x, y, w, h, color, layer, ...}` | UI rectangles/images |
| `render:text` | `{x, y, text, fontSize, color, layer}` | UI text |

## Widget Properties Reference

### UIButton
```json
{
  "type": "UIButton",
  "id": "my_button",
  "x": 100, "y": 100,
  "width": 200, "height": 50,
  "text": "Click Me",
  "fontSize": 24,
  "textColor": 4294967295,
  "bgColor": 3435973836,
  "hoverColor": 4286611711,
  "action": "button_clicked"
}
```

### UILabel
```json
{
  "type": "UILabel",
  "id": "my_label",
  "x": 100, "y": 100,
  "width": 300, "height": 50,
  "text": "Hello World",
  "fontSize": 32,
  "color": 4294967295
}
```

### UIPanel
```json
{
  "type": "UIPanel",
  "id": "my_panel",
  "x": 0, "y": 0,
  "width": 400, "height": 300,
  "color": 2155905279
}
```

### UISlider
```json
{
  "type": "UISlider",
  "id": "volume",
  "x": 100, "y": 100,
  "width": 300, "height": 30,
  "min": 0.0,
  "max": 100.0,
  "value": 50.0,
  "orientation": "horizontal"
}
```

### UICheckbox
```json
{
  "type": "UICheckbox",
  "id": "enable_vsync",
  "x": 100, "y": 100,
  "width": 30, "height": 30,
  "checked": true
}
```

### UITextInput
```json
{
  "type": "UITextInput",
  "id": "player_name",
  "x": 100, "y": 100,
  "width": 300, "height": 40,
  "text": "",
  "placeholder": "Enter name...",
  "fontSize": 20,
  "maxLength": 32
}
```

### UIProgressBar
```json
{
  "type": "UIProgressBar",
  "id": "loading",
  "x": 100, "y": 100,
  "width": 400, "height": 30,
  "value": 0.65,
  "bgColor": 2155905279,
  "fillColor": 4278255360
}
```

### UIImage
```json
{
  "type": "UIImage",
  "id": "logo",
  "x": 100, "y": 100,
  "width": 200, "height": 200,
  "textureId": 5
}
```

### UIScrollPanel
```json
{
  "type": "UIScrollPanel",
  "id": "inventory",
  "x": 100, "y": 100,
  "width": 400, "height": 600,
  "contentHeight": 1200,
  "scrollY": 0.0,
  "scrollbarWidth": 20,
  "bgColor": 2155905279
}
```

### UITooltip
```json
{
  "type": "UITooltip",
  "id": "help_tooltip",
  "x": 100, "y": 100,
  "width": 200, "height": 80,
  "text": "This is a helpful tooltip",
  "fontSize": 16,
  "visible": false
}
```

## Layer Management

UIModule uses **layer-based rendering** to ensure UI elements render correctly:

- **Game sprites**: Layer 0-999
- **UI elements**: Layer 1000+ (default baseLayer)
- **Tooltips**: Automatically use highest layer

Configure base layer in UIModule configuration:
```cpp
config.setInt("baseLayer", 1000);
```

## Hot-Reload Support

UIModule fully supports hot-reload with state preservation:

### State Preserved
- All widget properties (position, size, colors)
- Widget states (button hover, slider values, checkbox checked)
- Scroll positions
- Text input content

### State Not Preserved
- Transient animation states
- Mouse hover states (recalculated on next mouse move)

## Performance

- **Target**: < 1ms per frame for UI updates
- **Batching**: Multiple UI rectangles batched into single render commands
- **Event filtering**: Only processes mouse events within widget bounds
- **Layout caching**: Widget tree built once from JSON, not every frame

## Testing

### Visual Test
```bash
cmake -DGROVE_BUILD_UI_MODULE=ON -B build
cmake --build build --target test_ui_widgets
./build/tests/test_ui_widgets
```

### Integration Test (with InputModule + BgfxRenderer)
```bash
cmake -DGROVE_BUILD_BGFX_RENDERER=ON -DGROVE_BUILD_UI_MODULE=ON -DGROVE_BUILD_INPUT_MODULE=ON -B build
cmake --build build
cd build && ctest -R IT_014 --output-on-failure
```

## Dependencies

- **GroveEngine Core**: IModule, IIO, IDataNode
- **BgfxRenderer**: For rendering (via IIO, not direct dependency)
- **InputModule**: For input handling (via IIO, not direct dependency)
- **nlohmann/json**: JSON parsing
- **spdlog**: Logging

## Files

```
modules/UIModule/
├── README.md                     # This file
├── CMakeLists.txt               # Build configuration
├── UIModule.h                   # Main module
├── UIModule.cpp
├── Core/
│   ├── UIContext.h              # Global UI state
│   ├── UIContext.cpp
│   ├── UILayout.h               # Layout management
│   ├── UILayout.cpp
│   ├── UIStyle.h                # Widget styling
│   ├── UIStyle.cpp
│   ├── UITooltip.h              # Tooltip system
│   ├── UITooltip.cpp
│   ├── UITree.h                 # Widget hierarchy
│   ├── UITree.cpp
│   └── UIWidget.h               # Base widget interface
├── Widgets/
│   ├── UIButton.h/.cpp
│   ├── UILabel.h/.cpp
│   ├── UIPanel.h/.cpp
│   ├── UICheckbox.h/.cpp
│   ├── UISlider.h/.cpp
│   ├── UITextInput.h/.cpp
│   ├── UIProgressBar.h/.cpp
│   ├── UIImage.h/.cpp
│   └── UIScrollPanel.h/.cpp
└── Rendering/
    ├── UIRenderer.h             # Publishes render commands
    └── UIRenderer.cpp
```

## Implementation Phases

- ✅ **Phase 1**: Core widgets (Button, Label, Panel)
- ✅ **Phase 2**: Input widgets (Checkbox, Slider, TextInput)
- ✅ **Phase 3**: Advanced widgets (ProgressBar, Image)
- ✅ **Phase 4-5**: Layout system and styling
- ✅ **Phase 6**: Interactive demo
- ✅ **Phase 7**: ScrollPanel + Tooltips

## Extensibility

### Adding a Custom Widget

1. Create `Widgets/MyCustomWidget.h/.cpp`
2. Inherit from `UIWidget` base class
3. Implement `render()`, `handleInput()`, and event handlers
4. Add to `UILayout::createWidget()` factory
5. Use in JSON layouts with `"type": "MyCustomWidget"`

Example:
```cpp
class MyCustomWidget : public UIWidget {
public:
    void render(UIRenderer& renderer) override {
        // Publish render commands via renderer
        renderer.drawRect(m_x, m_y, m_width, m_height, m_color);
    }

    void onMouseDown(int button, double x, double y) override {
        // Handle click
        auto event = std::make_unique<JsonDataNode>("event");
        event->setString("widgetId", m_id);
        m_io->publish("ui:custom_event", std::move(event));
    }
};
```

## License

See LICENSE at project root.

---

**UIModule - Complete UI system for GroveEngine** 🎨
