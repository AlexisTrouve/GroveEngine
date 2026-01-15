# UIModule - Architecture & Design

## Architecture Overview

```
InputModule → IIO (input:*)
                ↓
            UIModule
         (Widget Tree)
                ↓
        UIRenderer (publishes)
                ↓
       IIO (render:*)
                ↓
          BgfxRenderer
```

All communication happens via IIO topics - no direct module-to-module calls.

## Current Limitations

### No Direct Data Binding

UIModule **does not** have built-in data binding. Updates must flow through the game module:

```
Slider → ui:value_changed → Game Module → ui:set_text → Label
```

This is **intentional** to maintain the IIO-based architecture where all communication goes through topics. The game module acts as the central coordinator.

**Example:**
```cpp
// Slider value changed
if (msg.topic == "ui:value_changed" && widgetId == "volume_slider") {
    double value = msg.data->getDouble("value", 0);
    setVolume(value);

    // Update label (must go through game module)
    auto updateMsg = std::make_unique<JsonDataNode>("set_text");
    updateMsg->setString("id", "volume_label");
    updateMsg->setString("text", "Volume: " + std::to_string((int)value) + "%");
    m_io->publish("ui:set_text", std::move(updateMsg));
}
```

### Message Latency in Single-Threaded Mode

**Current test showcases:** ~16ms latency (1 frame @ 60 FPS)

**Cause:** Messages are queued until the next `pullMessage()` call in the game loop.

```cpp
// Single-threaded game loop (test showcase)
while(running) {
    handleInput();         // UIModule publishes event
    processUIEvents();     // Game receives event (next frame!)
    update();
    render();
}
```

**Solution:** Run modules in separate threads (production architecture).

## Threading Model

### Current: Single-Threaded (Tests)

Test showcases run all modules in a single thread for simplicity:

```cpp
void main() {
    auto uiModule = loadModule("UIModule");
    auto renderer = loadModule("BgfxRenderer");

    while(running) {
        // All in same thread - sequential execution
        processInput();
        uiModule->process(deltaTime);
        renderer->process(deltaTime);
        SDL_GL_SwapWindow(window);
    }
}
```

**Latency:** ~16ms (next frame polling)

### Production: Multi-Threaded

Each module runs in its own thread:

```cpp
// UIModule thread @ 60 FPS
void uiThread() {
    while(running) {
        // Receive inputs from queue (filled by InputModule thread)
        while(io->hasMessages()) {
            handleMessage(io->pullMessage());
        }

        update(deltaTime);

        // Publish events (immediately queued to Game thread)
        io->publish("ui:value_changed", msg);

        sleep(16ms);
    }
}

// Game thread @ 60 FPS
void gameThread() {
    while(running) {
        // Pull messages from queue (latency < 1ms)
        while(io->hasMessages()) {
            handleMessage(io->pullMessage());  // Already in queue!
        }

        updateGameLogic(deltaTime);
        sleep(16ms);
    }
}
```

**Latency:** < 1ms (just mutex lock + memcpy)

### IntraIO Message Delivery

IntraIO uses a **queue-based** system with push-on-publish, pull-on-consume:

1. **Module A publishes:** `io->publish("topic", msg)`
   - Message immediately delivered to Module B's queue
   - No batching delay (batch thread is for low-freq subscriptions only)

2. **Module B pulls:** `io->pullMessage()`
   - Returns message from queue
   - No network/serialization overhead

**With threading:** Messages are available in the queue immediately, so the next `pullMessage()` call retrieves them with minimal latency.

**Without threading:** All `pullMessage()` calls happen sequentially in the game loop, so messages wait until the next frame.

## Layer Management

UIModule uses layer-based rendering to ensure proper draw order:

- **Game sprites**: Layer 0-999
- **UI base layer**: 1000 (configurable via `baseLayer` config)
- **UI widgets**: baseLayer + widget index
- **Tooltips**: Highest layer (automatic)

```cpp
config.setInt("baseLayer", 1000);  // UI renders above game
```

## Hot-Reload Support

UIModule fully supports hot-reload with state preservation.

### State Preserved
- Widget properties (position, size, colors)
- Widget states (checkbox checked, slider values, text input content)
- Scroll positions
- Widget hierarchy

### State Not Preserved
- Transient animation states
- Mouse hover states (recalculated on next mouse move)
- Focus state (recalculated on next interaction)

### How It Works

1. **Extract State:**
   ```cpp
   nlohmann::json UIModule::extractState() {
       json state;
       // Serialize all widget properties
       return state;
   }
   ```

2. **Reload Module:**
   ```cpp
   moduleLoader.reload();  // Unload .dll, recompile, reload
   ```

3. **Restore State:**
   ```cpp
   void UIModule::restoreState(const nlohmann::json& state) {
       // Restore widget properties from JSON
   }
   ```

## Performance

### Retained Mode Rendering

UIModule uses **retained mode** to optimize IIO traffic:

**Message Reduction:**
- Static UI (20 widgets, 0 changes): 100% reduction (0 messages/frame after registration)
- Mostly static (20 widgets, 3 changes): 85% reduction (3 vs 20 messages)
- Fully dynamic (20 widgets, 20 changes): 0% reduction (comparison overhead)

**Implementation:**
- Widgets cache render state
- Compare against previous state each frame
- Only publish `render:*:update` if changed

See [UI_RENDERING.md](UI_RENDERING.md) for details.

### Target Performance

- **UI update:** < 1ms per frame
- **Render command generation:** < 0.5ms per frame
- **Message routing:** < 0.1ms per message
- **Widget count:** Up to 100+ widgets without performance issues

## Future Enhancements

### Planned Features

#### Data Binding (Optional)

Link widget properties to game variables with automatic sync:

```json
{
  "type": "label",
  "text": "${player.health}",
  "bindTo": "player.health"
}
```

**Note:** Will remain optional to preserve IIO architecture for those who prefer explicit control.

#### Animations

Tweening, fades, transitions:

```json
{
  "type": "panel",
  "animations": {
    "enter": {"type": "fade", "duration": 0.3},
    "exit": {"type": "slide", "direction": "left", "duration": 0.2}
  }
}
```

#### Flexible Layout

Anchors, constraints, flex, grid:

```json
{
  "type": "button",
  "anchor": "bottom-right",
  "offset": {"x": -20, "y": -20}
}
```

```json
{
  "type": "panel",
  "layout": "flex",
  "flexDirection": "column",
  "gap": 10
}
```

#### Drag & Drop

```json
{
  "type": "image",
  "draggable": true,
  "dragGroup": "inventory"
}
```

#### Rich Text

Markdown/BBCode formatting:

```json
{
  "type": "label",
  "text": "**Bold** *italic* `code`",
  "richText": true
}
```

#### Themes

Swappable style sheets:

```json
{
  "theme": "dark",
  "themeFile": "themes/dark.json"
}
```

#### 9-Slice Sprites

Scalable sprite borders:

```json
{
  "type": "panel",
  "sprite": "panel_border.png",
  "sliceMode": "9-slice",
  "sliceInsets": {"top": 8, "right": 8, "bottom": 8, "left": 8}
}
```

#### Input Validation

Regex patterns for text inputs:

```json
{
  "type": "textinput",
  "validation": "^[a-zA-Z0-9]+$",
  "errorMessage": "Alphanumeric only"
}
```

### Not Planned

These features violate core design principles and will **never** be added:

- ❌ **Direct widget-to-widget communication** - All communication must go through IIO topics
- ❌ **Embedded game logic in widgets** - Widgets are pure UI, game logic stays in game modules
- ❌ **Direct renderer access** - Widgets publish render commands via IIO, never call renderer directly
- ❌ **Direct input polling** - Widgets consume `input:*` topics, never poll input devices directly

## Design Principles

1. **IIO-First:** All communication via topics, no direct coupling
2. **Retained Mode:** Cache state, minimize IIO traffic
3. **Hot-Reload Safe:** Full state preservation across reloads
4. **Thread-Safe:** Designed for multi-threaded production use
5. **Module Independence:** UIModule never imports BgfxRenderer or InputModule headers
6. **Game Logic Separation:** Widgets are dumb views, game modules handle logic

## Integration with Other Modules

### With BgfxRenderer

UIModule → `render:sprite:*`, `render:text:*` → BgfxRenderer

No direct dependency. UIModule doesn't know BgfxRenderer exists.

### With InputModule

InputModule → `input:*` → UIModule

No direct dependency. UIModule doesn't know InputModule exists.

### With Game Module

Bidirectional via IIO:
- Game → `ui:set_text`, `ui:set_visible` → UIModule
- UIModule → `ui:action`, `ui:value_changed` → Game

Game module coordinates all interactions.
