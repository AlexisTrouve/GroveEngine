# Full Stack Interactive Demo

**Complete integration test demonstrating BgfxRenderer + UIModule + InputModule working together**

## What This Demonstrates

This demo is a **complete, working example** of how to build a real application with GroveEngine, showing:

1. ✅ **BgfxRenderer** - 2D rendering (sprites, text, clear color)
2. ✅ **UIModule** - Interactive UI (buttons, sliders, panels, labels)
3. ✅ **InputModule** - Input capture (mouse clicks, keyboard)
4. ✅ **IIO Communication** - All modules talk via pub/sub topics
5. ✅ **Game Logic** - Responding to UI events and updating state
6. ✅ **Hit Testing** - Click detection on UI widgets (raycasting 2D)

## Features Demonstrated

### Rendering (BgfxRenderer)
- ✅ Sprite rendering with layers
- ✅ Text rendering
- ✅ Clear color changes
- ✅ Dynamic sprite batching (spawns hundreds of sprites)

### UI (UIModule)
- ✅ **UIButton** - "Spawn", "Clear", "Toggle Background" buttons
- ✅ **UISlider** - Speed control (horizontal slider)
- ✅ **UIPanel** - Semi-transparent control panel
- ✅ **UILabel** - Title and status labels
- ✅ **Hit testing** - Click detection with AABB collision
- ✅ **Hover states** - Button visual feedback
- ✅ **Event publishing** - `ui:action`, `ui:value_changed`

### Input (InputModule)
- ✅ Mouse click capture (SDL → IIO)
- ✅ Mouse move for hover detection
- ✅ Keyboard input (SPACE to spawn, ESC to exit)
- ✅ Thread-safe event buffering

### Game Logic
- ✅ Subscribes to UI events (`ui:action`, `ui:value_changed`)
- ✅ Maintains game state (sprites with physics)
- ✅ Publishes render commands to BgfxRenderer
- ✅ Responds to keyboard input

## Message Flow Example

Here's what happens when you click the "Spawn" button:

```
1. SDL_Event (SDL_MOUSEBUTTONDOWN)
        ↓
2. InputModule.feedEvent(&event)
        ↓
3. InputModule.process() → Converts to IIO
        ↓
4. IIO: input:mouse:button {button: 0, pressed: true, x: 100, y: 180}
        ↓
5. UIModule.processInput() ← Subscribes to input:mouse:button
        ↓
6. UIModule.updateUI()
    - hitTest(x=100, y=180) → finds UIButton "spawn_button"
    - Button.containsPoint(100, 180) → true!
        ↓
7. IIO: ui:action {action: "spawn_sprite", widgetId: "spawn_button"}
        ↓
8. GameLogic.update() ← Subscribes to ui:action
    - action == "spawn_sprite" → spawnSprite()
        ↓
9. IIO: render:sprite {x, y, color, layer, ...} (for each sprite)
        ↓
10. BgfxRenderer.process() ← Subscribes to render:sprite
    - Batches sprites by texture
    - Renders to screen
```

**Complete end-to-end flow validated!**

## Building

### Windows

```bash
# Configure with all modules
cmake -B build -G "MinGW Makefiles" ^
  -DGROVE_BUILD_BGFX_RENDERER=ON ^
  -DGROVE_BUILD_UI_MODULE=ON ^
  -DGROVE_BUILD_INPUT_MODULE=ON

# Build
cmake --build build -j4

# Run (option 1: script)
run_full_stack_demo.bat

# Run (option 2: manual)
cd build/tests
test_full_stack_interactive.exe
```

### Linux

```bash
# Configure
cmake -B build \
  -DGROVE_BUILD_BGFX_RENDERER=ON \
  -DGROVE_BUILD_UI_MODULE=ON \
  -DGROVE_BUILD_INPUT_MODULE=ON

# Build
cmake --build build -j4

# Run
./build/tests/test_full_stack_interactive
```

## Controls

| Input | Action |
|-------|--------|
| **Mouse Click** | Click UI buttons, drag slider |
| **SPACE** | Spawn a sprite from keyboard |
| **ESC** | Exit demo |

### UI Buttons

- **Spawn** - Create a bouncing sprite
- **Clear** - Remove all sprites
- **Toggle Background** - Switch between dark/light background

### Slider

- **Speed Slider** - Control sprite spawn velocity (10-500)

## What You'll See

1. **Control Panel** (semi-transparent gray panel on left)
   - Title: "Control Panel"
   - Spawn/Clear buttons
   - Speed slider
   - Background toggle button

2. **Background Sprites** (layer 5)
   - Colorful squares bouncing around
   - Physics simulation (velocity, wall bouncing)
   - Each sprite spawned at random position with random color

3. **UI Text** (layer 2000, above everything)
   - Sprite count: "Sprites: 42 (Press SPACE to spawn)"

4. **Background Color** (toggleable)
   - Dark: #1a1a1a
   - Light: #303030

## Code Structure

```cpp
// Main loop
while (running) {
    // 1. SDL events
    while (SDL_PollEvent(&event)) {
        inputModule->feedEvent(&event);  // Thread-safe injection
    }

    // 2. Process modules
    inputModuleBase->process(input);   // SDL → IIO
    uiModule->process(input);          // IIO → UI events
    gameLogic.update(deltaTime);       // Game logic
    gameLogic.render(rendererIO);      // Game → Render commands
    renderer->process(input);          // Render frame
}
```

## Performance

- **Target**: 60 FPS with vsync
- **Sprite count**: Tested with 200+ sprites without performance degradation
- **UI update**: < 1ms per frame
- **Hit testing**: O(n) where n = visible widgets (fast for typical UIs)

## Troubleshooting

### "Failed to load BgfxRenderer"
- Make sure `build/modules/BgfxRenderer.dll` exists
- Rebuild: `cmake --build build --target BgfxRenderer`

### "Failed to load UIModule"
- Make sure `build/modules/UIModule.dll` exists
- Rebuild: `cmake --build build --target UIModule`

### "Failed to load InputModule"
- Make sure `build/modules/InputModule.dll` exists
- Rebuild: `cmake --build build --target InputModule`

### Black screen or no rendering
- Check bgfx backend initialization (look for logs)
- Try different backend: Edit code to set `backend = "opengl"` or `"dx11"`

### Buttons don't respond to clicks
- Check logs for "Click event received" messages
- Verify hit testing is working (should see hover events)
- Make sure UIModule is subscribed to `input:mouse:button`

## Learning Resources

This demo is the **best starting point** for learning GroveEngine development:

1. **Read the code** - `tests/visual/test_full_stack_interactive.cpp` (well-commented)
2. **Modify UI layout** - Change widget positions, add new buttons
3. **Add game features** - Try adding player control, collision detection
4. **Experiment with topics** - Add custom IIO messages

## See Also

- [DEVELOPER_GUIDE.md](../../docs/DEVELOPER_GUIDE.md) - Complete GroveEngine guide
- [BgfxRenderer README](../../modules/BgfxRenderer/README.md) - Renderer details
- [UIModule README](../../modules/UIModule/README.md) - UI system details
- [InputModule README](../../modules/InputModule/README.md) - Input handling details

---

**Happy coding with GroveEngine!** 🌳🎮
