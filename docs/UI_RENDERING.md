# UI Retained Mode Rendering

This document describes GroveEngine's retained mode rendering architecture for UIModule, which reduces IIO message traffic by caching render state and only publishing updates when visual properties change.

## Overview

GroveEngine supports two rendering modes:

- **Immediate mode** (legacy): Widgets publish render commands every frame via `render:sprite` and `render:text` topics
- **Retained mode** (current): Widgets register render entries once and publish updates only when properties change via `render:sprite:add/update/remove` and `render:text:add/update/remove` topics

The retained mode dramatically reduces IIO message traffic for static or mostly-static UIs. For a typical UI with 20 widgets where only 2-3 widgets change per frame (e.g., button hover states), this reduces render messages from 20+ per frame to 2-3 per frame.

## Architecture

### UIRenderer

The `UIRenderer` class manages render state and publishes to IIO topics consumed by `BgfxRenderer`.

**Key Components:**

```cpp
class UIRenderer {
    // Entry registration
    uint32_t registerEntry();                  // Returns unique render ID
    void unregisterEntry(uint32_t renderId);  // Removes entry on widget destruction

    // Retained mode updates (only publish if changed)
    bool updateRect(uint32_t renderId, float x, float y, float w, float h,
                    uint32_t color, int layer);
    bool updateText(uint32_t renderId, float x, float y, const std::string& text,
                    float fontSize, uint32_t color, int layer);
    bool updateSprite(uint32_t renderId, float x, float y, float w, float h,
                      int textureId, uint32_t color, int layer);

private:
    std::unordered_map<uint32_t, RenderEntry> m_entries;  // Cached state
    uint32_t m_nextRenderId = 1;                          // ID generator
};
```

**Render Entry State:**

```cpp
struct RenderEntry {
    RenderEntryType type;  // Rect, Sprite, or Text
    float x, y, w, h;
    uint32_t color;
    int textureId;
    int layer;
    std::string text;
    float fontSize;
};
```

The `UIRenderer` caches previous render state for each registered entry. When `updateRect/updateText/updateSprite` is called, it compares the new parameters against the cached state. If nothing changed, it returns `false` and publishes no message. If any parameter changed, it updates the cache and publishes an update message.

### UIWidget Base Class

All widgets inherit from `UIWidget` which provides retained mode infrastructure:

```cpp
class UIWidget {
protected:
    uint32_t m_renderId = 0;           // Unique ID (0 = not registered)
    bool m_geometryDirty = true;       // Position/size changed
    bool m_appearanceDirty = true;     // Color/style changed
    bool m_registered = false;         // Has registered with renderer
    WidgetDestroyCallback m_destroyCallback;  // Cleanup on destruction

public:
    uint32_t getRenderId() const;
    void setRenderId(uint32_t id);
    bool isDirty() const;
    bool isRegistered() const;
    void setRegistered(bool reg);
    void markGeometryDirty();
    void markAppearanceDirty();
    void clearDirtyFlags();
    void setDestroyCallback(WidgetDestroyCallback callback);
};
```

**Widget Lifecycle:**

1. **First render**: Widget calls `renderer.registerEntry()` to get a `renderId`, then calls `updateRect/updateText/updateSprite` which publishes `add` message
2. **Subsequent renders**: Widget calls `updateRect/updateText/updateSprite` with same `renderId`. If state changed, publishes `update` message. If unchanged, no message published.
3. **Destruction**: Widget destructor invokes `m_destroyCallback`, which calls `renderer.unregisterEntry()` to publish `remove` message

### SceneCollector

The `SceneCollector` in `BgfxRenderer` consumes both immediate and retained mode messages:

```cpp
class SceneCollector {
private:
    // Retained mode: persistent state (not cleared each frame)
    std::unordered_map<uint32_t, SpriteInstance> m_retainedSprites;
    std::unordered_map<uint32_t, TextCommand> m_retainedTexts;
    std::unordered_map<uint32_t, std::string> m_retainedTextStrings;

    // Immediate mode: ephemeral state (cleared each frame)
    std::vector<SpriteInstance> m_sprites;
    std::vector<TextCommand> m_texts;
    std::vector<std::string> m_textStrings;

    // Message handlers
    void parseSpriteAdd(const IDataNode& data);      // Add to m_retainedSprites
    void parseSpriteUpdate(const IDataNode& data);   // Update m_retainedSprites
    void parseSpriteRemove(const IDataNode& data);   // Remove from m_retainedSprites
    void parseTextAdd(const IDataNode& data);        // Add to m_retainedTexts
    void parseTextUpdate(const IDataNode& data);     // Update m_retainedTexts
    void parseTextRemove(const IDataNode& data);     // Remove from m_retainedTexts
    void parseSprite(const IDataNode& data);         // Add to m_sprites (legacy)
    void parseText(const IDataNode& data);           // Add to m_texts (legacy)
};
```

During `finalize()`, `SceneCollector` merges retained and ephemeral entries into a single `FramePacket` that is rendered.

## IIO Protocol

### Retained Mode Topics

**Sprites (rectangles and textured sprites):**

| Topic | Payload | Description |
|-------|---------|-------------|
| `render:sprite:add` | `{renderId, x, y, scaleX, scaleY, color, textureId, layer}` | Register new sprite |
| `render:sprite:update` | `{renderId, x, y, scaleX, scaleY, color, textureId, layer}` | Update existing sprite |
| `render:sprite:remove` | `{renderId}` | Unregister sprite |

**Text:**

| Topic | Payload | Description |
|-------|---------|-------------|
| `render:text:add` | `{renderId, x, y, text, fontSize, color, layer}` | Register new text |
| `render:text:update` | `{renderId, x, y, text, fontSize, color, layer}` | Update existing text |
| `render:text:remove` | `{renderId}` | Unregister text |

**Payload Fields:**

- `renderId` (int): Unique identifier for this render entry (assigned by `UIRenderer.registerEntry()`)
- `x, y` (double): Position in screen coordinates
  - For sprites: center position
  - For text: top-left position
- `scaleX, scaleY` (double): Sprite dimensions (width/height)
- `color` (int): RGBA color in format `0xRRGGBBAA`
- `textureId` (int): Texture atlas ID (0 = white/solid color)
- `layer` (int): Render layer (higher = on top)
- `text` (string): Text content
- `fontSize` (double): Font size in pixels

### Immediate Mode Topics (Legacy)

| Topic | Payload | Description |
|-------|---------|-------------|
| `render:sprite` | `{x, y, scaleX, scaleY, color, textureId, layer}` | Ephemeral sprite (1 frame) |
| `render:text` | `{x, y, text, fontSize, color, layer}` | Ephemeral text (1 frame) |

These topics are still supported for compatibility and for truly ephemeral content (e.g., debug overlays, particle effects).

## Widget Migration Pattern

### Simple Widget (UILabel)

A simple widget with one render entry:

```cpp
void UILabel::render(UIRenderer& renderer) {
    if (text.empty()) return;

    // Register with renderer on first render
    if (!m_registered) {
        m_renderId = renderer.registerEntry();
        m_registered = true;

        // Set destroy callback to unregister
        setDestroyCallback([&renderer](uint32_t id) {
            renderer.unregisterEntry(id);
        });
    }

    // Retained mode: only publish if changed
    int layer = renderer.nextLayer();
    renderer.updateText(m_renderId, absX, absY, text, fontSize, color, layer);
}
```

**First frame:**
- `registerEntry()` returns `renderId = 1`
- `updateText()` sees no cached entry, stores state, publishes `render:text:add` with `renderId=1`

**Subsequent frames (text unchanged):**
- `updateText()` compares cached state, detects no change, returns `false`, no message published

**Subsequent frames (text changed):**
- `updateText()` detects change, updates cache, publishes `render:text:update` with `renderId=1`

**Widget destruction:**
- Destructor invokes callback
- Callback calls `unregisterEntry(1)`
- Publishes `render:text:remove` with `renderId=1`

### Complex Widget (UIButton)

A widget with multiple render entries (background + text):

```cpp
void UIButton::render(UIRenderer& renderer) {
    // Register with renderer on first render (need 2 entries: bg + text)
    if (!m_registered) {
        m_renderId = renderer.registerEntry();       // Background
        m_textRenderId = renderer.registerEntry();   // Text
        m_registered = true;

        // Set destroy callback to unregister both
        setDestroyCallback([&renderer, textId = m_textRenderId](uint32_t id) {
            renderer.unregisterEntry(id);
            renderer.unregisterEntry(textId);
        });
    }

    const ButtonStyle& style = getCurrentStyle();

    // Render background
    int bgLayer = renderer.nextLayer();
    if (style.useTexture && style.textureId > 0) {
        renderer.updateSprite(m_renderId, absX, absY, width, height,
                              style.textureId, style.bgColor, bgLayer);
    } else {
        renderer.updateRect(m_renderId, absX, absY, width, height,
                            style.bgColor, bgLayer);
    }

    // Render text centered
    if (!text.empty()) {
        int textLayer = renderer.nextLayer();
        float textX = absX + width * 0.5f;
        float textY = absY + height * 0.5f;
        renderer.updateText(m_textRenderId, textX, textY, text, fontSize,
                            style.textColor, textLayer);
    }

    renderChildren(renderer);
}
```

**Key Points:**

- Each visual element (background, text) gets its own `renderId`
- Destroy callback must unregister all entries
- Style changes (hover, pressed) trigger color updates, but not position/size updates

## Performance Characteristics

### Static UI

For a UI with 20 static widgets (e.g., main menu):

- **Frame 1**: 20 `add` messages published
- **Frame 2+**: 0 messages published (no changes)

### Dynamic UI

For a UI with 20 widgets where 3 change per frame (e.g., button hover states):

- **Immediate mode**: 20 messages per frame (all widgets republish)
- **Retained mode**: 3 messages per frame (only changed widgets publish `update`)

### Highly Dynamic UI

For a UI where all widgets change every frame (e.g., scrolling text):

- **Immediate mode**: N messages per frame (one per widget)
- **Retained mode**: N messages per frame (all widgets publish `update`)
- **Overhead**: Retained mode has comparison overhead, but message payload is identical

### Message Frequency Breakdown

| Scenario | Widgets | Changed/Frame | Immediate Mode | Retained Mode | Reduction |
|----------|---------|---------------|----------------|---------------|-----------|
| Static UI | 20 | 0 | 20 | 0 | 100% |
| Mostly static | 20 | 3 | 20 | 3 | 85% |
| Half dynamic | 20 | 10 | 20 | 10 | 50% |
| Fully dynamic | 20 | 20 | 20 | 20 | 0% |

The retained mode is most effective for UIs where the majority of widgets remain static across frames (main menus, HUDs, settings panels).

## Change Detection

The `UIRenderer` uses epsilon-based floating point comparison for position/size:

```cpp
static bool floatEqual(float a, float b, float epsilon = 0.001f) {
    return std::fabs(a - b) < epsilon;
}
```

This prevents spurious updates from floating point precision issues. For colors, text, and integer properties, exact equality is used.

## Layer Management

Each widget's render layer is assigned once during the `add` message and remains stable. This ensures consistent Z-ordering across frames without requiring layer updates.

```cpp
bool UIRenderer::updateRect(uint32_t renderId, float x, float y, float w, float h,
                            uint32_t color, int layer) {
    auto it = m_entries.find(renderId);
    if (it == m_entries.end()) {
        // New entry - store layer
        RenderEntry entry;
        entry.layer = layer;
        m_entries[renderId] = entry;
        publishSpriteAdd(renderId, x, y, w, h, 0, color, layer);
        return true;
    }

    // Update - ignore layer parameter, use original layer
    RenderEntry& entry = it->second;
    bool changed = !floatEqual(entry.x, x) || !floatEqual(entry.y, y) ||
                   !floatEqual(entry.w, w) || !floatEqual(entry.h, h) ||
                   entry.color != color;

    if (changed) {
        entry.x = x; entry.y = y; entry.w = w; entry.h = h; entry.color = color;
        publishSpriteUpdate(renderId, x, y, w, h, 0, color, entry.layer);
        return true;
    }

    return false;
}
```

## Migration Guide

### Converting Immediate Mode to Retained Mode

**Before (immediate mode):**

```cpp
void MyWidget::render(UIRenderer& renderer) {
    int layer = renderer.nextLayer();
    renderer.drawRect(absX, absY, width, height, color);  // Publishes every frame
}
```

**After (retained mode):**

```cpp
void MyWidget::render(UIRenderer& renderer) {
    // Register on first render
    if (!m_registered) {
        m_renderId = renderer.registerEntry();
        m_registered = true;
        setDestroyCallback([&renderer](uint32_t id) {
            renderer.unregisterEntry(id);
        });
    }

    // Only publishes if changed
    int layer = renderer.nextLayer();
    renderer.updateRect(m_renderId, absX, absY, width, height, color, layer);
}
```

**Required changes:**

1. Add `m_renderId` member to widget class (inherited from `UIWidget`)
2. Register entry on first render with `registerEntry()`
3. Set destroy callback to unregister entry
4. Replace `drawRect/drawText/drawSprite` with `updateRect/updateText/updateSprite`

### Handling Multiple Render Entries

For widgets that render multiple elements (e.g., button with background + text):

```cpp
class UIButton : public UIWidget {
protected:
    uint32_t m_renderId = 0;       // Background render ID (inherited)
    uint32_t m_textRenderId = 0;   // Text render ID (additional)
};

void UIButton::render(UIRenderer& renderer) {
    if (!m_registered) {
        m_renderId = renderer.registerEntry();       // Background
        m_textRenderId = renderer.registerEntry();   // Text
        m_registered = true;

        // Unregister both on destruction
        setDestroyCallback([&renderer, textId = m_textRenderId](uint32_t id) {
            renderer.unregisterEntry(id);
            renderer.unregisterEntry(textId);
        });
    }

    // Render background
    int bgLayer = renderer.nextLayer();
    renderer.updateRect(m_renderId, absX, absY, width, height, bgColor, bgLayer);

    // Render text
    int textLayer = renderer.nextLayer();
    renderer.updateText(m_textRenderId, textX, textY, text, fontSize, textColor, textLayer);
}
```

## Compatibility

Both immediate and retained mode topics are fully supported. Existing code using `render:sprite` and `render:text` continues to work. The `SceneCollector` merges both modes during frame finalization:

```cpp
FramePacket SceneCollector::finalize(FrameAllocator& allocator) {
    // Merge retained + ephemeral sprites
    size_t totalSprites = m_retainedSprites.size() + m_sprites.size();
    SpriteInstance* sprites = allocator.allocateArray<SpriteInstance>(totalSprites);

    size_t idx = 0;
    for (const auto& [renderId, sprite] : m_retainedSprites) {
        sprites[idx++] = sprite;
    }
    std::memcpy(&sprites[idx], m_sprites.data(),
                m_sprites.size() * sizeof(SpriteInstance));

    packet.sprites = sprites;
    packet.spriteCount = totalSprites;
    // ...
}
```

This allows gradual migration and mixed-mode rendering where some widgets use retained mode and others use immediate mode.

## Implementation Files

**UIModule:**
- `C:\Users\alexi\Documents\projects\groveengine\modules\UIModule\Rendering\UIRenderer.h`
- `C:\Users\alexi\Documents\projects\groveengine\modules\UIModule\Rendering\UIRenderer.cpp`
- `C:\Users\alexi\Documents\projects\groveengine\modules\UIModule\Core\UIWidget.h`

**BgfxRenderer:**
- `C:\Users\alexi\Documents\projects\groveengine\modules\BgfxRenderer\Scene\SceneCollector.h`
- `C:\Users\alexi\Documents\projects\groveengine\modules\BgfxRenderer\Scene\SceneCollector.cpp`

**Example Widgets:**
- `C:\Users\alexi\Documents\projects\groveengine\modules\UIModule\Widgets\UILabel.cpp` (simple, 1 entry)
- `C:\Users\alexi\Documents\projects\groveengine\modules\UIModule\Widgets\UIButton.cpp` (complex, 2 entries)
- All other widgets in `modules/UIModule/Widgets/`
