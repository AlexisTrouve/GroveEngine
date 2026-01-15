# UIModule - IIO Topics Reference

Complete reference of all IIO topics consumed and published by UIModule.

## Topics Consumed

### From InputModule

| Topic | Payload | Description |
|-------|---------|-------------|
| `input:mouse:move` | `{x, y}` | Mouse position |
| `input:mouse:button` | `{button, pressed, x, y}` | Mouse click |
| `input:mouse:wheel` | `{delta}` | Mouse wheel |
| `input:keyboard` | `{keyCode, pressed, char}` | Keyboard event |

### UI Control Commands

| Topic | Payload | Description |
|-------|---------|-------------|
| `ui:set_text` | `{id, text}` | Update label text dynamically |
| `ui:set_visible` | `{id, visible}` | Show/hide widget |
| `ui:set_value` | `{id, value}` | Set slider/progressbar value |
| `ui:load` | `{layoutPath}` | Load new UI layout from file |

## Topics Published

### UI Events

| Topic | Payload | Description |
|-------|---------|-------------|
| `ui:click` | `{widgetId, x, y}` | Widget clicked |
| `ui:action` | `{widgetId, action}` | Button action triggered |
| `ui:value_changed` | `{widgetId, value}` | Slider/checkbox/input changed |
| `ui:text_submitted` | `{widgetId, text}` | Text input submitted (Enter) |
| `ui:hover` | `{widgetId, enter}` | Mouse entered/left widget |
| `ui:scroll` | `{widgetId, scrollX, scrollY}` | Scroll panel scrolled |

### Rendering (Retained Mode)

| Topic | Payload | Description |
|-------|---------|-------------|
| `render:sprite:add` | `{renderId, x, y, scaleX, scaleY, color, textureId, layer}` | Register new sprite |
| `render:sprite:update` | `{renderId, x, y, scaleX, scaleY, color, textureId, layer}` | Update existing sprite |
| `render:sprite:remove` | `{renderId}` | Unregister sprite |
| `render:text:add` | `{renderId, x, y, text, fontSize, color, layer}` | Register new text |
| `render:text:update` | `{renderId, x, y, text, fontSize, color, layer}` | Update existing text |
| `render:text:remove` | `{renderId}` | Unregister text |

### Rendering (Immediate Mode - Legacy)

| Topic | Payload | Description |
|-------|---------|-------------|
| `render:sprite` | `{x, y, w, h, color, layer, ...}` | Ephemeral sprite (1 frame) |
| `render:text` | `{x, y, text, fontSize, color, layer}` | Ephemeral text (1 frame) |

See [UI Rendering Documentation](UI_RENDERING.md) for details on retained vs immediate mode.

## Usage Examples

### Handling UI Events

```cpp
// Subscribe to UI events
gameIO->subscribe("ui:action");
gameIO->subscribe("ui:value_changed");

// In game loop
while (m_io->hasMessages() > 0) {
    auto msg = m_io->pullMessage();

    if (msg.topic == "ui:action") {
        std::string action = msg.data->getString("action", "");
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
    }
}
```

### Updating UI Dynamically

```cpp
// Update label text
auto msg = std::make_unique<JsonDataNode>("set_text");
msg->setString("id", "score_label");
msg->setString("text", "Score: " + std::to_string(score));
m_io->publish("ui:set_text", std::move(msg));

// Hide/show widget
auto msg = std::make_unique<JsonDataNode>("set_visible");
msg->setString("id", "loading_panel");
msg->setBool("visible", false);
m_io->publish("ui:set_visible", std::move(msg));

// Update progress bar
auto msg = std::make_unique<JsonDataNode>("set_value");
msg->setString("id", "health_bar");
msg->setDouble("value", 0.75);  // 75%
m_io->publish("ui:set_value", std::move(msg));
```

### Slider + Label Pattern

Common pattern: update a label when a slider changes.

```cpp
if (msg.topic == "ui:value_changed" && widgetId == "volume_slider") {
    double value = msg.data->getDouble("value", 50.0);
    setVolume(value);

    // Update label to show current value
    auto updateMsg = std::make_unique<JsonDataNode>("set_text");
    updateMsg->setString("id", "volume_label");
    updateMsg->setString("text", "Volume: " + std::to_string((int)value) + "%");
    m_io->publish("ui:set_text", std::move(updateMsg));
}
```
