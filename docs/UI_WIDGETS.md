# UIModule - Widget Reference

Complete reference for all available widgets and their properties.

## Widget Overview

| Widget | Purpose | Events |
|--------|---------|--------|
| **UIButton** | Clickable button | `ui:click`, `ui:action` |
| **UILabel** | Static/dynamic text | - |
| **UIPanel** | Container widget | - |
| **UICheckbox** | Toggle checkbox | `ui:value_changed` |
| **UISlider** | Value slider | `ui:value_changed` |
| **UITextInput** | Text entry field | `ui:value_changed`, `ui:text_submitted` |
| **UIProgressBar** | Progress indicator | - |
| **UIImage** | Sprite/texture display | - |
| **UIFlipbook** | Animated sprite-sheet panel (grove::anim) | - |
| **UIScrollPanel** | Scrollable container | `ui:scroll` |
| **UITooltip** | Hover tooltip | - |
| **UIRadial** | Action wheel / radial menu (angular selection) | `ui:action` |
| **UIList** | Data-driven scrollable selectable list (the ship sidebar) | `ui:list:selected` |

## Common Properties

All widgets support these base properties:

```json
{
  "type": "WidgetType",
  "id": "unique_id",
  "x": 0,
  "y": 0,
  "width": 100,
  "height": 100,
  "visible": true,
  "tooltip": "Optional tooltip text"
}
```

### Responsive sizing & anchoring

Widgets can size and position themselves *relative to their parent* so the UI **reflows** when the
window resizes (the host publishes `ui:resize {width,height}` — see [UI_TOPICS](UI_TOPICS.md)). Both
re-resolve on every layout pass, so they track the parent automatically.

```json
{
  "type": "panel",
  "widthPercent": 1.0,        // fraction 0..1 of the parent content box (root's parent = the viewport)
  "heightPercent": 1.0,       // 1.0 = fill. Omit / 0 = use the absolute width/height above.
  "anchor": "bottom-right",   // pin to a parent point: top-left|top|top-right|left|center|right|
                              //   bottom-left|bottom|bottom-right (default none = keep x/y)
  "anchorOffset": { "x": -10, "y": -10 }   // pixel nudge after anchoring (+x right, +y down)
}
```

- **`widthPercent`/`heightPercent`** — relative size. On the main axis of a flex container the percent
  is a fixed reservation taken before flex shares the rest; on the cross axis / `stack` / `absolute`
  it's a direct fraction.
- **`anchor`** — positional only (use `widthPercent/heightPercent:1.0` to *fill*). Resolved for children
  of an **`absolute`-mode** parent (in flow layouts the flow positions the child, so the anchor is
  ignored). Ideal for HUD elements glued to a corner.

A container can also use the **`grid`** layout mode — `"layout": { "type": "grid", "columns": N,
"spacing": gap, "rowHeight": h }`. Cells share the content width across `N` columns (so the grid reflows
on resize); `rowHeight` sets the cell height (omit / 0 = square cells). Children flow row-major. Good for
rosters / inventories.

## UIButton

Clickable button with hover/press states.

```json
{
  "type": "button",
  "id": "my_button",
  "x": 100,
  "y": 100,
  "width": 200,
  "height": 50,
  "text": "Click Me",
  "onClick": "button_action",
  "style": {
    "normal": {
      "bgColor": "0x0984e3FF",
      "textColor": "0xFFFFFFFF",
      "textureId": 0
    },
    "hover": {
      "bgColor": "0x74b9ffFF",
      "textColor": "0xFFFFFFFF"
    },
    "pressed": {
      "bgColor": "0x0652a1FF",
      "textColor": "0xFFFFFFFF"
    }
  }
}
```

**Properties:**
- `text` - Button label text
- `onClick` - Action name published to `ui:action`
- `asset` - Streamed **asset id** (string, e.g. `"icons/iron"`) — draws the bg as a sprite resolved by the
  AssetManager (atlas-aware, on-demand stream + budget). Bindable: `"asset":"{{icon}}"`. Wins over `texture`/`textureId`.
- `frame` - **9-slice (nine-patch) border** (see below) — a composed border texture that gives the button a
  continuous, crisp border at any size. When set, it REPLACES the flat border-rect + bg fill.
- `style` - Visual states (normal, hover, pressed)
  - `bgColor` - Background color (hex RGBA) — also the **tint of the 9-slice frame**, so hover/pressed re-tint it
  - `textColor` - Text color (hex RGBA)
  - `textureId` - Sprite texture ID (0 = solid color)

**`frame` — composed 9-slice border** (buttons AND windows):

```json
"frame": { "asset": "ui/button_frame", "srcW": 32, "srcH": 32, "inset": 8 }
```

- `asset` - streamed border art id (atlas-aware). A numeric `textureId` alternative is also accepted.
- `srcW`, `srcH` - the border art's **native pixel dimensions**.
- `inset` - margin thickness (px, source space) applied to all four sides. Override per-side with
  `left` / `right` / `top` / `bottom`. The margins keep their native pixel size (crisp corners); the edges
  stretch along one axis and the centre fills — so the border stays continuous at any button/window size.
- Publishes `render:nineslice:add` (one entry, expanded renderer-side into up to 9 quads). Empty/absent
  `frame` → the classic flat look, unchanged.

**Events:**
- `ui:click` - `{widgetId, x, y}`
- `ui:action` - `{widgetId, action}` where action = onClick value

## UILabel

Static or dynamic text display.

```json
{
  "type": "label",
  "id": "my_label",
  "x": 100,
  "y": 100,
  "width": 300,
  "height": 50,
  "text": "Hello World",
  "style": {
    "fontSize": 24,
    "color": "0xFFFFFFFF"
  }
}
```

**Properties:**
- `text` - Label text (can be updated via `ui:set_text`)
- `style.fontSize` - Font size in pixels
- `style.color` - Text color (hex RGBA)

**Dynamic Updates:**
```cpp
auto msg = std::make_unique<JsonDataNode>("set_text");
msg->setString("id", "my_label");
msg->setString("text", "New Text");
m_io->publish("ui:set_text", std::move(msg));
```

## UIPanel

Container widget with background color.

```json
{
  "type": "panel",
  "id": "my_panel",
  "x": 0,
  "y": 0,
  "width": 400,
  "height": 300,
  "style": {
    "bgColor": "0x2d3436FF"
  }
}
```

**Properties:**
- `style.bgColor` - Background color (hex RGBA, use `0x00000000` for transparent)

## UICheckbox

Toggle checkbox with check state.

```json
{
  "type": "checkbox",
  "id": "enable_vsync",
  "x": 100,
  "y": 100,
  "width": 24,
  "height": 24,
  "checked": true,
  "text": "Enable VSync"
}
```

**Properties:**
- `checked` - Initial checked state
- `text` - Optional label text next to checkbox

**Events:**
- `ui:value_changed` - `{widgetId, checked}`

## UISlider

Horizontal or vertical value slider.

```json
{
  "type": "slider",
  "id": "volume_slider",
  "x": 100,
  "y": 100,
  "width": 300,
  "height": 24,
  "min": 0.0,
  "max": 100.0,
  "value": 50.0,
  "orientation": "horizontal"
}
```

**Properties:**
- `min` - Minimum value
- `max` - Maximum value
- `value` - Current value
- `orientation` - "horizontal" or "vertical"

**Events:**
- `ui:value_changed` - `{widgetId, value, min, max}`

## UITextInput

Text entry field with cursor and focus state.

```json
{
  "type": "textinput",
  "id": "player_name",
  "x": 100,
  "y": 100,
  "width": 300,
  "height": 40,
  "text": "",
  "placeholder": "Enter name...",
  "maxLength": 32,
  "style": {
    "fontSize": 20,
    "textColor": "0xFFFFFFFF",
    "bgColor": "0x34495eFF",
    "borderColor": "0x666666FF"
  }
}
```

**Properties:**
- `text` - Initial text
- `placeholder` - Placeholder text when empty
- `maxLength` - Maximum character limit
- `style.fontSize` - Font size
- `style.textColor` - Text color
- `style.bgColor` - Background color
- `style.borderColor` - Border color (changes to blue when focused)

**Events:**
- `ui:value_changed` - `{widgetId, text}` - on each character change
- `ui:text_submitted` - `{widgetId, text}` - on Enter key

## UIProgressBar

Progress indicator (0.0 to 1.0).

```json
{
  "type": "progressbar",
  "id": "loading_bar",
  "x": 100,
  "y": 100,
  "width": 400,
  "height": 30,
  "value": 0.65,
  "style": {
    "bgColor": "0x34495eFF",
    "fillColor": "0x2ecc71FF"
  }
}
```

**Properties:**
- `value` - Progress value (0.0 = empty, 1.0 = full)
- `style.bgColor` - Background color
- `style.fillColor` - Fill color

**Dynamic Updates:**
```cpp
auto msg = std::make_unique<JsonDataNode>("set_value");
msg->setString("id", "loading_bar");
msg->setDouble("value", 0.75);  // 75%
m_io->publish("ui:set_value", std::move(msg));
```

## UIImage

Display a sprite/texture.

```json
{
  "type": "image",
  "id": "logo",
  "x": 100,
  "y": 100,
  "width": 200,
  "height": 200,
  "textureId": 5
}
```

**Properties:**
- `textureId` - Texture ID from BgfxRenderer
- `asset` - Streamed **asset id** (string) — resolved by the AssetManager (atlas-aware). Wins over `textureId`. Bindable via `{{...}}`.

## UIFlipbook

Animated sprite-sheet panel (slice 6a). Plays a `columns × rows` sheet cell-by-cell over time — the "animated 2D scene" widget. Backed by `grove::anim` (SpriteSheet + Flipbook); the renderer streams the current cell's UV via `render:sprite` (retained — only republishes when the cell flips).

```json
{
  "type": "flipbook",
  "id": "explosion",
  "x": 100,
  "y": 100,
  "width": 64,
  "height": 64,
  "textureId": 5,
  "columns": 4,
  "rows": 1,
  "fps": 12,
  "loop": true
}
```

**Properties:**
- `textureId` - Texture ID of the sprite sheet (numeric; a dedicated sheet texture — `asset`-streamed sheets are a follow-on)
- `columns` / `rows` - Sheet grid dimensions (equal cells, row-major)
- `count` - Optional cap on usable frames for a partial last row (0 = full `columns×rows` grid)
- `fps` - Playback rate (uniform per-frame duration = 1/fps)
- `loop` - Loop at the end (true) or hold the last frame (false)
- `style.tintColor` - RGBA tint (e.g. `"0xFFFFFFFF"`)

Plays automatically. Frame order is the natural sheet order (0..N-1); a custom frame list and a `ui:anim:set` play/pause toggle are follow-ons.

## UIScrollPanel

Scrollable container with vertical scrollbar.

```json
{
  "type": "scrollpanel",
  "id": "inventory_panel",
  "x": 100,
  "y": 100,
  "width": 400,
  "height": 600,
  "contentHeight": 1200,
  "scrollY": 0.0,
  "scrollbarWidth": 20,
  "style": {
    "bgColor": "0x2d3436FF"
  }
}
```

**Properties:**
- `contentHeight` - Total height of scrollable content
- `scrollY` - Initial scroll position (0.0 = top)
- `scrollbarWidth` - Width of scrollbar in pixels
- `style.bgColor` - Background color

**Events:**
- `ui:scroll` - `{widgetId, scrollY}`

## UITooltip

Hover tooltip (managed automatically by UIModule).

```json
{
  "type": "tooltip",
  "id": "help_tooltip",
  "x": 100,
  "y": 100,
  "width": 200,
  "height": 60,
  "text": "This is a helpful tooltip",
  "visible": false,
  "style": {
    "fontSize": 14,
    "bgColor": "0x2c3e50FF",
    "textColor": "0xFFFFFFFF"
  }
}
```

**Note:** Tooltips are automatically shown when `tooltip` property is set on any widget:

```json
{
  "type": "button",
  "id": "save_button",
  "tooltip": "Save your progress",
  ...
}
```

## UIRadial

Action-wheel / radial menu. Centered on `(x, y)` (the wheel **center**, not the top-left like
rect widgets). Selection is **angular** — the segment is chosen by the *direction* from the
center — which makes it input-agnostic: mouse angle today, gamepad-stick angle / keyboard step
later through the same model.

```json
{
  "type": "radial",
  "id": "action_wheel",
  "x": 640,
  "y": 360,
  "innerRadius": 40,
  "outerRadius": 160,
  "visible": false,
  "items": [
    { "action": "act:move",   "text": "Move" },
    { "action": "act:attack", "text": "Attack" },
    { "action": "act:build",  "text": "Build" }
  ],
  "style": {
    "bgColor": "0x000000A0",
    "itemColor": "0x34495EFF",
    "hoverColor": "0x2ECC71FF",
    "textColor": "0xFFFFFFFF",
    "fontSize": 16
  }
}
```

**Properties:**
- `x, y` - wheel **center** (not top-left).
- `innerRadius` - dead-zone radius; releasing inside it **cancels** (no action).
- `outerRadius` - outer edge of the active band (and the hit-test disc).
- `items[]` - wedges in **clockwise order from the top**. Each: `action` (published string),
  `text` (label), `textureId` (optional icon, 0 = none).
- `style` - `bgColor` (backdrop), `itemColor` (wedge at rest), `hoverColor` (wedge under the
  pointer), `textColor`, `fontSize`.

**Events:**
- `ui:action` - `{widgetId, action, index}` on release over a wedge (`index` = clockwise-from-top).
- Releasing in the center dead-zone emits nothing (cancel).

**Open/close:** the wheel is a dumb view — it does **not** auto-hide. The game opens it
(`ui:set_visible` → true, e.g. on right-click) and closes it on `ui:action`. (Retained-mode
rendering doesn't purge a hidden widget's render entries — a known engine limitation — so the
widget never hides itself, to avoid leaving ghost rects.)

## UIList

The **ship sidebar**: a scrollable, clipped, selectable list whose rows are GENERATED from item data
(a data-driven repeater). The game pushes a fleet of `items`; the list builds one row each, single-select,
emitting `ui:list:selected` on a click. Scroll with the mouse wheel; rows clip to the list bounds.

```json
{
  "type": "list",
  "id": "fleet",
  "x": 20, "y": 60, "width": 220, "height": 400,
  "rowHeight": 44,
  "items": [
    { "id": "ship-7a3", "label": "Aurora",   "subtitle": "Frigate · idle",   "icon": 12 },
    { "id": "ship-9c1", "label": "Borealis", "subtitle": "Hauler · mining",   "icon": 13 }
  ],
  "style": {
    "bgColor": "0x1d2430FF", "rowColor": "0x232c3aFF", "rowAltColor": "0x202836FF",
    "hoverColor": "0x2c3a4eFF", "selectedColor": "0x3a6ea5FF",
    "labelColor": "0xFFFFFFFF", "subtitleColor": "0x9fb0c4FF",
    "fontSize": 14, "subtitleFontSize": 11
  }
}
```

**Properties:**
- `rowHeight` - height of each row (px).
- `padding` - inner left padding + icon/text gap.
- `iconSize` - square icon size when a row has an `icon`.
- `items[]` - the data. Each: `id` (stable id echoed in the event — use a uuid, not the index),
  `label` (primary text), `subtitle` (optional 2nd line, `""`/absent = none), `icon` (optional
  texture id, `0` = none).
- `style` - `bgColor`, `rowColor`/`rowAltColor` (zebra), `hoverColor`, `selectedColor`, `labelColor`,
  `subtitleColor`, `fontSize`, `subtitleFontSize`.

**Grouped (warship wings).** Instead of `items`, give the list `groups` — each a collapsible header over
its ships. Clicking a header folds/unfolds the group; clicking a ship selects it (the event carries its
`groupId`). One-level grouping (groups → items). For arbitrary depth, use `nodes` (tree mode) below.

```json
{
  "type": "list", "id": "fleet", "x": 20, "y": 60, "width": 240, "height": 460, "rowHeight": 40,
  "groups": [
    { "id": "alpha", "label": "Alpha Wing", "items": [
      { "id": "ship-7a3", "label": "Aurora",  "subtitle": "Frigate" },
      { "id": "ship-9c1", "label": "Borealis", "subtitle": "Hauler" } ] },
    { "id": "bravo", "label": "Bravo Wing", "collapsed": true, "items": [
      { "id": "ship-1f8", "label": "Cygnus", "subtitle": "Scout" } ] }
  ],
  "style": { "headerColor": "0x2c3540FF", "headerLabelColor": "0xFFFFFFFF" }
}
```

**Tree (N-level hierarchy).** Instead of `items`/`groups`, give the list `nodes` — a recursive tree. A node
with a non-empty `children` renders as a collapsible header; a leaf (no children) renders as a selectable
item. Each level indents further. Headers reuse the same fold/unfold + `ui:list:group:toggled` as groups;
leaves select with `ui:list:selected` (its `itemId`; `index` = the running leaf order). Any depth.

```json
{
  "type": "list", "id": "tree", "x": 20, "y": 60, "width": 300, "height": 460, "rowHeight": 40,
  "nodes": [
    { "id": "fleet", "label": "Fleet", "children": [
      { "id": "alpha", "label": "Wing Alpha", "children": [
        { "id": "a1", "label": "Ship A1" },
        { "id": "a2", "label": "Ship A2" } ] },
      { "id": "beta", "label": "Wing Beta", "collapsed": true, "children": [
        { "id": "b1", "label": "Ship B1", "icon": 5 } ] } ] }
  ]
}
```

Node fields: `id`, `label`, `collapsed?` (internal nodes), `icon?` (leaf texture id), `children?` (array).

**Events:**
- `ui:list:selected` - `{id, groupId, index, itemId}` when an ITEM row is clicked (`groupId` is `""` for a
  flat list; `index` is within the group — or the running leaf order in tree mode). The list highlights the row.
- `ui:list:group:toggled` - `{id, groupId, collapsed}` when a group/tree header is clicked (the new state).

**Runtime:**
- `ui:list:set_items {id, items:[...]}` - repopulate as a FLAT list (resets scroll + selection).
- `ui:list:set_groups {id, groups:[...]}` - repopulate as GROUPED wings. **The arrays must be json-backed**
  (IIO serializes only a node's JSON — see UI_TOPICS.md).
- `ui:list:set_tree {id, nodes:[...]}` - repopulate as an N-level TREE (resets scroll + selection).
- `ui:list:select {id, index}` - programmatic pre-selection by row index (no event re-emit).

**Scroll & select:** mouse **wheel**, a **visual scrollbar** (track + draggable thumb, shown only when the
content overflows), and **content drag-to-scroll** (grab a row and pull — past a small threshold it becomes
a scroll, so it doesn't fire a select). Selection happens on **release** and only if the press wasn't a
drag. Style: `scrollbarColor` / `scrollbarTrackColor` / `scrollbarWidth` (0 = no visual bar).

**Scope:** wheel + scrollbar + drag-to-scroll + single-select + clip + **virtualization** (only on-screen
rows get render entries — a recycled, viewport-bounded id-pool remapped to the scrolled window, so a 10k-row
list registers ~viewport-many entries) + **collapsible groups** (warship wings) + **N-level trees** (`nodes`).
Internally everything is a flat sequence of `ListRow` (header | item) that flat, grouped, and tree data project onto, so
virtualization/scroll/clip operate the same way. Deliberate follow-ons: custom row templates, multi-select,
grid mode, multi-level tree. The container follows the standard pattern (opaque hit-test absorb, content
clipped, recycled row-id pool) — see UI_ARCHITECTURE / the handoff.

## Creating Custom Widgets

1. Create `Widgets/MyWidget.h/.cpp`
2. Inherit from `UIWidget`
3. Implement required methods:

```cpp
class MyWidget : public UIWidget {
public:
    void update(UIContext& ctx, float deltaTime) override;
    void render(UIRenderer& renderer) override;
    std::string getType() const override { return "mywidget"; }

    // Event handlers
    bool onMouseButton(int button, bool pressed, float x, float y) override;
    void onMouseMove(float x, float y) override;
};
```

4. Register in `UITree::createWidget()`:

```cpp
if (type == "mywidget") {
    auto widget = std::make_unique<MyWidget>();
    // ... configure from JSON
    return widget;
}
```

5. Use in JSON layouts:

```json
{
  "type": "mywidget",
  "id": "custom1",
  ...
}
```
