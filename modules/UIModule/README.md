# UIModule

⚠️ **Development Stage**: Experimental, part of non-deterministic engine. See [main README](../../README.md#current-status) for limitations.

Complete UI widget system for GroveEngine with layout, scrolling, tooltips, and automatic input handling.

## Features

- **10 Widget Types**: Button, Label, Panel, Checkbox, Slider, TextInput, ProgressBar, Image, ScrollPanel, Tooltip
- **JSON-Based Layouts**: Define UI hierarchies in JSON files
- **Automatic Input Handling**: Consumes `input:*` topics from InputModule
- **Retained Mode Rendering**: Widgets cache state, reducing IIO traffic by 85%+
- **Layer Management**: UI renders on top of game content (layer 1000+)
- **Hot-Reload Support**: Full state preservation across module reloads
- **Thread-Safe Design**: Architecture ready for future multi-threaded systems (currently single-threaded only)

## Quick Start

```cpp
#include <grove/ModuleLoader.h>
#include <grove/IntraIOManager.h>

// Create IIO instances
auto& ioManager = IntraIOManager::getInstance();
auto uiIO = ioManager.createInstance("ui_module");
auto gameIO = ioManager.createInstance("game");

// Load UIModule
ModuleLoader uiLoader;
auto uiModule = uiLoader.load("./modules/UIModule.dll", "ui_module");

// Configure
JsonDataNode config("config");
config.setInt("windowWidth", 1920);
config.setInt("windowHeight", 1080);
config.setString("layoutFile", "./ui/menu.json");
config.setInt("baseLayer", 1000);
uiModule->setConfiguration(config, uiIO.get(), nullptr);

// Subscribe to UI events
gameIO->subscribe("ui:action");
gameIO->subscribe("ui:value_changed");

// Game loop
while(running) {
    // Handle UI events
    while (gameIO->hasMessages() > 0) {
        auto msg = gameIO->pullMessage();
        if (msg.topic == "ui:action") {
            std::string action = msg.data->getString("action", "");
            handleAction(action);
        }
    }

    uiModule->process(deltaTime);
}
```

## Documentation

- **[Widget Reference](../../docs/UI_WIDGETS.md)** - All widgets with JSON properties
- **[IIO Topics](../../docs/UI_TOPICS.md)** - Complete topic reference and usage examples
- **[Architecture & Design](../../docs/UI_ARCHITECTURE.md)** - Threading, limitations, future features
- **[Rendering](../../docs/UI_RENDERING.md)** - Retained mode rendering architecture

## Example UI Layout

`ui/menu.json`:
```json
{
  "widgets": [
    {
      "type": "panel",
      "id": "background",
      "x": 0, "y": 0,
      "width": 1920, "height": 1080,
      "style": {"bgColor": "0x2d3436FF"}
    },
    {
      "type": "button",
      "id": "play_button",
      "x": 860, "y": 500,
      "width": 200, "height": 60,
      "text": "Play Game",
      "onClick": "start_game",
      "style": {
        "normal": {"bgColor": "0x0984e3FF"},
        "hover": {"bgColor": "0x74b9ffFF"}
      }
    },
    {
      "type": "slider",
      "id": "volume_slider",
      "x": 800, "y": 650,
      "width": 320, "height": 40,
      "min": 0.0, "max": 100.0, "value": 75.0
    }
  ]
}
```

See [Widget Reference](../../docs/UI_WIDGETS.md) for all widget properties.

## Building

```bash
cmake -DGROVE_BUILD_UI_MODULE=ON -B build
cmake --build build -j4
```

## Testing

```bash
# Visual showcase (run from project root for correct asset paths)
./build/tests/test_ui_showcase

# Integration test
cd build && ctest -R IT_014 --output-on-failure
```

## Dependencies

- **GroveEngine Core**: IModule, IIO, IDataNode
- **BgfxRenderer**: For rendering (via IIO, not direct dependency)
- **InputModule**: For input (via IIO, not direct dependency)
- **nlohmann/json**: JSON parsing
- **spdlog**: Logging

## Implementation Status

- ✅ **Phase 1-3**: Core widgets (Button, Label, Panel, Checkbox, Slider, TextInput, ProgressBar, Image)
- ✅ **Phase 4-5**: Layout system and styling
- ✅ **Phase 6**: Interactive demo
- ✅ **Phase 7**: ScrollPanel + Tooltips
- ✅ **Phase 8**: Retained mode rendering

## Files Structure

```
modules/UIModule/
├── README.md                     # This file
├── CMakeLists.txt               # Build configuration
├── UIModule.h/.cpp              # Main module
├── Core/
│   ├── UIContext.h/.cpp         # Global UI state
│   ├── UILayout.h/.cpp          # Layout management
│   ├── UITooltip.h/.cpp         # Tooltip system
│   ├── UITree.h/.cpp            # Widget hierarchy
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
    ├── UIRenderer.h/.cpp        # Publishes render commands
```

## License

See LICENSE at project root.

---

**UIModule - Complete UI system for GroveEngine** 🎨
