# UIModule Phase 3: Interaction & Events - Implementation Complete

## Date
2025-11-28

## Summary
Successfully implemented the Interaction & Events system (Phase 3) for UIModule in GroveEngine. This adds interactive buttons, mouse hit testing, and event publishing capabilities.

## Components Implemented

### 1. Widgets/UIButton.h/cpp
**New interactive button widget** with full state management:

#### States
- **Normal**: Default resting state
- **Hover**: Mouse is over the button
- **Pressed**: Mouse button is down on the button
- **Disabled**: Button is non-interactive

#### Features
- Per-state styling (bgColor, textColor, borderColor, etc.)
- Hit testing (`containsPoint()`)
- Event handlers (`onMouseButton()`, `onMouseEnter()`, `onMouseLeave()`)
- Configurable `onClick` action
- Enable/disable functionality

#### Rendering
- Background rectangle with state-specific color
- Text rendering (centered approximation)
- Border support (placeholder)

### 2. Core/UIContext.cpp
**Hit testing and event dispatch implementation**:

#### Functions
- `hitTest()`: Recursive search to find topmost widget at point
  - Front-to-back traversal (reverse children order)
  - Only interactive widgets (buttons) are considered
  - Returns topmost hit widget

- `updateHoverState()`: Manages hover transitions
  - Calls `onMouseEnter()` when hover starts
  - Calls `onMouseLeave()` when hover ends
  - Traverses entire widget tree

- `dispatchMouseButton()`: Delivers mouse clicks
  - Hit tests to find target
  - Dispatches to button's `onMouseButton()`
  - Returns clicked widget for action publishing

### 3. UIModule.cpp Updates
**Enhanced `updateUI()` with full event system**:

#### Input Processing
- Subscribes to `input:mouse:move`, `input:mouse:button`, `input:keyboard`
- Updates UIContext with mouse position and button states
- Per-frame state tracking (`mousePressed`, `mouseReleased`)

#### Interaction Loop
1. **Hit Testing**: Find widget under mouse cursor
2. **Hover State**: Update hover state and call widget callbacks
3. **Event Publishing**: Publish `ui:hover` on state change
4. **Mouse Events**: Handle clicks and publish events
5. **Widget Update**: Call `update()` on all widgets

#### Events Published
- **`ui:hover`**: `{widgetId, enter: bool}`
  - Published when hover state changes
  - `enter: true` when entering, `false` when leaving

- **`ui:click`**: `{widgetId, x, y}`
  - Published on successful button click
  - Includes mouse coordinates

- **`ui:action`**: `{action, widgetId}`
  - Published when button's `onClick` is triggered
  - Example: `{action: "game:start", widgetId: "btn_play"}`
  - Logged to console for debugging

### 4. UITree.cpp - Button Factory
**JSON parsing for button configuration**:

#### Supported Properties
```json
{
  "type": "button",
  "text": "Click Me",
  "onClick": "game:start",
  "enabled": true,
  "style": {
    "fontSize": 18,
    "normal": { "bgColor": "0x444444FF", "textColor": "0xFFFFFFFF" },
    "hover": { "bgColor": "0x666666FF", "textColor": "0xFFFFFFFF" },
    "pressed": { "bgColor": "0x333333FF", "textColor": "0xFFFFFFFF" },
    "disabled": { "bgColor": "0x222222FF", "textColor": "0x666666FF" }
  }
}
```

#### Parsing
- All four states (normal, hover, pressed, disabled)
- Hex color strings → uint32_t conversion
- Font size configuration
- Enable/disable flag

## JSON Configuration Examples

### Simple Button
```json
{
  "type": "button",
  "id": "btn_play",
  "text": "Play",
  "width": 200,
  "height": 50,
  "onClick": "game:start"
}
```

### Styled Button with All States
```json
{
  "type": "button",
  "id": "btn_quit",
  "text": "Quit",
  "width": 200,
  "height": 50,
  "onClick": "app:quit",
  "style": {
    "fontSize": 18,
    "normal": {
      "bgColor": "0xe74c3cFF",
      "textColor": "0xFFFFFFFF"
    },
    "hover": {
      "bgColor": "0xec7063FF",
      "textColor": "0xFFFFFFFF"
    },
    "pressed": {
      "bgColor": "0xc0392bFF",
      "textColor": "0xFFFFFFFF"
    }
  }
}
```

### Disabled Button
```json
{
  "type": "button",
  "id": "btn_disabled",
  "text": "Disabled",
  "enabled": false,
  "style": {
    "disabled": {
      "bgColor": "0x34495eFF",
      "textColor": "0x7f8c8dFF"
    }
  }
}
```

## Test Files

### Visual Test
**File**: `tests/visual/test_26_ui_buttons.cpp`

#### Features Tested
- Button hover effects (color changes on mouse over)
- Button press effects (darker color on click)
- Event publishing (console output for all events)
- Disabled buttons (no interaction)
- Action handling (quit button exits app)

#### Test Layout
**JSON**: `assets/ui/test_buttons.json`
- 3 interactive buttons (Play, Options, Quit)
- 1 disabled button
- Color-coded for visual feedback
- Full state styling for each button

#### User Interaction
- Move mouse over buttons → Hover events
- Click buttons → Click + Action events
- Click "Quit" → App exits
- Disabled button → No interaction

**Build & Run**:
```bash
cmake -DGROVE_BUILD_UI_MODULE=ON -DGROVE_BUILD_BGFX_RENDERER=ON -B build-bgfx
cmake --build build-bgfx --target test_26_ui_buttons -j4
cd build-bgfx/tests
./test_26_ui_buttons
```

## Event System Architecture

### Input Flow
```
SDL Events → UIModule::processInput() → UIContext state
           → UIModule::updateUI() → Hit testing
           → Button event handlers → IIO publish
```

### Event Topics

#### Subscribed (Input)
| Topic | Data | Description |
|-------|------|-------------|
| `input:mouse:move` | `{x, y}` | Mouse position update |
| `input:mouse:button` | `{button, pressed, x, y}` | Mouse click/release |
| `input:keyboard` | `{keyCode, char}` | Keyboard input |

#### Published (Output)
| Topic | Data | Description |
|-------|------|-------------|
| `ui:hover` | `{widgetId, enter: bool}` | Hover state change |
| `ui:click` | `{widgetId, x, y}` | Button clicked |
| `ui:action` | `{action, widgetId}` | Button action triggered |

### Event Flow Example
```
1. User moves mouse → SDL_MOUSEMOTION
2. Test forwards to IIO → input:mouse:move
3. UIModule receives → Updates UIContext.mouseX/mouseY
4. Hit testing finds button → hoveredWidgetId = "btn_play"
5. updateHoverState() → btn_play.onMouseEnter()
6. Publish → ui:hover {widgetId: "btn_play", enter: true}

7. User clicks → SDL_MOUSEBUTTONDOWN
8. Test forwards → input:mouse:button {pressed: true}
9. UIModule → dispatchMouseButton()
10. Button → onMouseButton() returns true
11. Publish → ui:click {widgetId: "btn_play", x: 350, y: 200}

12. User releases → SDL_MOUSEBUTTONUP
13. dispatchMouseButton() again
14. Button still hovered → Click complete!
15. Publish → ui:action {action: "game:start", widgetId: "btn_play"}
16. Console log: "Button 'btn_play' clicked, action: game:start"
```

## Build Changes

### CMakeLists.txt Updates
1. **modules/UIModule/CMakeLists.txt**:
   - Added `Core/UIContext.cpp`
   - Added `Widgets/UIButton.cpp`

2. **tests/CMakeLists.txt**:
   - Added `test_26_ui_buttons` target

### Dependencies
- No new external dependencies
- Uses existing UIRenderer for drawing
- Uses existing IIO for events

## Files Created (4)

1. `modules/UIModule/Widgets/UIButton.h`
2. `modules/UIModule/Widgets/UIButton.cpp`
3. `modules/UIModule/Core/UIContext.cpp`
4. `assets/ui/test_buttons.json`
5. `tests/visual/test_26_ui_buttons.cpp`
6. `docs/UI_MODULE_PHASE3_COMPLETE.md`

## Files Modified (4)

1. `modules/UIModule/UIModule.cpp` - Event system in updateUI()
2. `modules/UIModule/Core/UITree.cpp` - Button factory registration
3. `modules/UIModule/CMakeLists.txt` - Added new sources
4. `tests/CMakeLists.txt` - Added test target

## Verification

### Compilation
✅ All code compiles without errors or warnings
✅ `UIModule` builds successfully with button support
✅ Test executable builds and links

### Code Quality
✅ Follows GroveEngine coding conventions
✅ Proper state management (Normal/Hover/Pressed/Disabled)
✅ Event-driven architecture (IIO pub/sub)
✅ Recursive hit testing (correct front-to-back order)
✅ Clean separation: rendering vs. interaction logic

## Known Limitations

### Text Rendering
- **No text centering**: UIRenderer doesn't support centered text alignment yet
- **Approximation**: Text position calculated but not truly centered
- **Future**: Needs text measurement API for proper centering

### Border Rendering
- **Placeholder**: Border properties exist but not rendered
- **Future**: UIRenderer needs border/outline support

### Focus Management
- **Not implemented**: Tab navigation not yet supported
- **No visual focus**: Focus indicator not implemented
- **Phase 3.5**: Can be added later without breaking changes

## Design Decisions

### Hit Testing
- **Front-to-back**: Uses reverse children order for correct z-order
- **Type-based**: Only certain widgets (buttons) are hit-testable
- **Recursive**: Searches entire tree for deepest match

### Event Publishing
- **Separate events**: `ui:click` and `ui:action` are distinct
  - `ui:click`: Low-level mouse event
  - `ui:action`: High-level semantic action
- **Logging**: Actions logged to console for debugging

### State Management
- **Per-frame reset**: `beginFrame()` clears transient state
- **Persistent hover**: Hover state persists across frames
- **Click detection**: Requires press AND release while hovering

## Performance Notes

- **Hit testing**: O(n) where n = number of visible widgets
- **Per-frame**: Hit testing runs every frame (acceptable for UI)
- **Early exit**: Stops at first hit (front-to-back traversal)
- **Typical UI**: < 100 widgets, negligible overhead

## Next Steps: Phase 4

Phase 4 will add more interactive widgets:
- **UIImage**: Display textures
- **UISlider**: Draggable value input
- **UICheckbox**: Boolean toggle
- **UIProgressBar**: Read-only progress display

## Phase 3 Status: ✅ COMPLETE

All Phase 3 objectives achieved:
- ✅ UIButton widget with state management
- ✅ Hit testing (point → widget lookup)
- ✅ Mouse event handling (hover, click, press)
- ✅ Event publishing (`ui:hover`, `ui:click`, `ui:action`)
- ✅ Enabled/disabled button states
- ✅ JSON configuration with per-state styling
- ✅ Visual test with interactive demo
- ✅ Event logging and debugging
- ✅ Full integration with Phase 2 layout system

The interaction system is fully functional and ready for use!
