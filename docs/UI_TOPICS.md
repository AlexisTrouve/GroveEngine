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
| `ui:set_visible` | `{id, visible}` | Show/hide a widget. Hiding PURGES its retained render entries (no ghost rects); showing re-registers on the next render. Recurses to children |
| `ui:set_position` | `{id, x, y}` | Move a widget at runtime (recomputes its absolute position). For the radial, x/y are its CENTRE — e.g. pop the action wheel centered on the cursor |
| `ui:radial:set_items` | `{id, count}` | Reconfigure a radial wheel to `count` slices at runtime (the pie tiles into ANY N: 2..8+). Generates generic items; a game normally sets real items via the layout JSON (`items[]`, `style.gap`/`style.margin` for the inter-slice gaps) |
| `ui:list:set_items` | `{id, items:[{id,label,subtitle?,icon?}]}` | Repopulate a `list` (the ship sidebar) at runtime as a FLAT list — the game pushes its current fleet, the list rebuilds its rows (resets scroll + selection). **The `items` array MUST be json-backed** (see note below) |
| `ui:list:set_groups` | `{id, groups:[{id,label,collapsed?,items:[{id,label,subtitle?,icon?}]}]}` | Repopulate a `list` as GROUPED warship wings — each group renders a collapsible header over its ships. Same json-backed constraint (the nested arrays must live in the node's JSON) |
| `ui:list:select` | `{id, index}` | Programmatically select a list ROW (e.g. pre-select a ship). Sets state only — does NOT re-emit `ui:list:selected` (that topic is reserved for the user's click, to avoid feedback loops) |
| `ui:drawer:toggle` | `{id}` | Open/close an edge drawer (it animates the slide itself) |
| `ui:drawer:set` | `{id, open}` | Force an edge drawer open (`true`) or closed (`false`) |
| `ui:modal:open` | `{id}` | Open a modal dialog (raises it on top; its dim traps all input behind) |
| `ui:modal:close` | `{id}` | Close a modal dialog (also fired automatically when the dim is clicked) |
| `ui:set_value` | `{id, value}` | Set slider/progressbar value |
| `ui:load` | `{layoutPath}` | Load new UI layout from file |
| `ui:resize` | `{width, height}` | Viewport resized — re-lays-out the whole UI against the new size. The **host** (which owns the window) publishes this on a window-resize event; UIModule stays decoupled from SDL. Widgets with `widthPercent`/`heightPercent` (a fraction `0..1` of the parent content box; the root's parent = the viewport, so `1.0` = fill the window) re-resolve and track the new size. Partial payloads tolerated (only a provided/positive dimension is applied) |

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
| `ui:window:closed` | `{id}` | An in-app window was closed (its close button clicked). The window hides itself + purges its retained entries; the game reacts (free state, etc.) |
| `ui:tab:changed` | `{widgetId, index}` | A tabbed container switched to page `index` (a tab was clicked). The tabs widget shows that page + hides/purges the others on its own |
| `ui:modal:closed` | `{id}` | A modal dialog closed (its dim was clicked, or `ui:modal:close`). The modal hides itself + purges its entries |
| `ui:list:selected` | `{id, groupId, index, itemId}` | A `list` ITEM row was clicked. `groupId` = its group (`""` for a flat/ungrouped list); `index` = its position WITHIN the group (flat: global); `itemId` = the item's stable `id` (survives a reorder, unlike the index). The list highlights the row on its own |
| `ui:list:group:toggled` | `{id, groupId, collapsed}` | A grouped `list`'s header was clicked → the group folded/unfolded. `collapsed` = the NEW state. The list re-projects its rows on its own |

> **⚠️ Array payloads must be json-backed.** IIO transports only a published node's JSON data
> (`getJsonData()` / `m_data`) — child nodes assembled via `setChild()` are NOT serialized. So a payload
> with a nested array (e.g. `ui:list:set_items`'s `items[]`) must carry that array in the node's JSON:
> build the message via the `JsonDataNode(name, json)` constructor with the array in the `json`, exactly
> as a layout file is parsed. (A scalar payload like `ui:radial:set_items {count}` is unaffected.)

### Rendering (Retained Mode)

| Topic | Payload | Description |
|-------|---------|-------------|
| `render:sprite:add` | `{renderId, x, y, scaleX, scaleY, color, textureId, layer, clipX?, clipY?, clipW?, clipH?}` | Register new sprite. Optional `clip{X,Y,W,H}` (screen px, present only inside a clipping container like a scroll panel) → the renderer scissors it; absent = no clip |
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

### Handling UI Events with Callbacks

```cpp
// Subscribe to UI events with callback handlers (in setConfiguration)
gameIO->subscribe("ui:action", [this](const grove::Message& msg) {
    std::string action = msg.data->getString("action", "");
    if (action == "start_game") {
        startGame();
    }
});

gameIO->subscribe("ui:value_changed", [this](const grove::Message& msg) {
    std::string widgetId = msg.data->getString("widgetId", "");
    if (widgetId == "volume_slider") {
        double value = msg.data->getDouble("value", 50.0);
        setVolume(value);
    }
});

// In game loop (process method)
while (m_io->hasMessages() > 0) {
    m_io->pullAndDispatch();  // Callbacks invoked automatically
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
// Subscribe to slider value changes (in setConfiguration)
gameIO->subscribe("ui:value_changed", [this](const grove::Message& msg) {
    std::string widgetId = msg.data->getString("widgetId", "");
    if (widgetId == "volume_slider") {
        double value = msg.data->getDouble("value", 50.0);
        setVolume(value);

        // Update label to show current value
        auto updateMsg = std::make_unique<JsonDataNode>("set_text");
        updateMsg->setString("id", "volume_label");
        updateMsg->setString("text", "Volume: " + std::to_string((int)value) + "%");
        m_io->publish("ui:set_text", std::move(updateMsg));
    }
});

// In process()
while (gameIO->hasMessages() > 0) {
    gameIO->pullAndDispatch();  // Callback invoked automatically
}
```
