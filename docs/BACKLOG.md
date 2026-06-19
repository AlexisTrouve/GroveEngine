# GroveEngine — Backlog

Deferred engine work, with the *why now-or-later*. Not commitments; picked up when the game
(Drifterra) needs them or a bottleneck is measured. Shipped features live in the code + their
tests; this file is only what's **not** done yet.

## Camera / View
- **Multi-cam** (N viewports in one window) — minimap, tactical inset. = N bgfx views, each with
  its own screen viewport + transform; passes draw the (culled) scene per camera. Moderate
  renderer refactor; it's the **foundation** the multi-screen item reuses.
- **Multi-screen** (N OS windows / monitors) — would fit the "two scales, two screens" idea
  (system map on one, tactical/ground on the other). = N SDL windows → N bgfx framebuffers
  (`createFrameBuffer(nativeWindowHandle)`) + per-window input routing. Big lift; do **after**
  multi-cam (then each camera just targets a window/framebuffer instead of the backbuffer).
- **"Border de field of view"** — meaning unresolved (edge-scroll RTS? a visual frame at the
  view edge? fog-of-war boundary? camera clamp to world bounds?). Clarify before building.
  *(The FOV rect itself exists: `grove::camera::visibleWorldBounds` + `isVisible(margin)`.)*
- **Camera clamp to world bounds** — keep the camera inside the map (no scrolling into the void).
  Small pure helper in `grove::camera` if wanted.

## Input / UI
From Drifterra's interface decision (`../drifterra/docs/INTERFACE.md`): zoom = navigation
primitive, **action wheel** = action primitive, mouse/keyboard interchangeable (controller a later bonus). The zoom/LOD/camera
it leans on is mostly **already covered** — tilemap seamless zoom is shipped, *semantic* LOD
(detailed ship → icon at system zoom) is **game-side** by design (engine gives zoom + culling),
camera items are above. The genuine engine gaps it surfaces:
- **Action-wheel / radial-menu widget — ✅ SHIPPED** (2026-06-20). `UIModule` now has a `radial`
  widget (`UIRadial`): centered, **angular** selection (direction from center → segment), dead-zone
  cancel, `items[]` clockwise from top, emits `ui:action {widgetId, action, index}`. The angular
  model is the device-agnostic seam — mouse angle today, gamepad-stick angle / keyboard step later
  via `setSelectedIndex`. Geometry is pure (`RadialMath.h`, objective unit test); locked E2E by
  `IT_020` (real click → right action per direction). Docs: [UI_WIDGETS](../docs/UI_WIDGETS.md#uiradial).
  **Remaining (deferred):** true pie/wedge visuals (today = radial *layout* of rect+label tiles, no
  arc primitive); auto-close on selection (blocked on the retained-hide ghost-rect limit below).
- **Gamepad input — premature, parked.** `InputModule` feeds SDL keyboard/mouse → `input:*`; no
  gamepad. A controller path (→ controller-native, Steam Deck) is wanted *eventually* but **not a
  near-term need** — the radial's `setSelectedIndex` is the ready seam, but revisit only once a
  combat-command slice needs it. The enabling piece when it comes: SDL gamepad events as `input:*`
  + actions bound to several sources mapping one intent (pad/mouse/key interchangeable).
- **Retained-mode hide leaves ghost rects** — a widget with `visible=false` stops calling `render()`,
  so its last-published retained entries stay on screen (BgfxRenderer keeps drawing them). Affects
  *any* `ui:set_visible` toggle, not just the radial. Fix = on hide, publish `render:*:remove` for the
  widget's entries (or a per-widget "clear" hook). Pick up when a slice needs clean show/hide.

## Rendering
- **Tilemap high-perf — ✅ SHIPPED A → B** (2026-06-18/19). GPU index-texture (R16UI + texelFetch,
  1 draw/chunk), `texture2DArray` atlas, retained `render:tilemap:add/update/remove` (upload-once),
  derivative detail↔LOD crossfade with a palette-driven mipped color band. Tested headless +
  `[gpu]` readback. Full status, commits, and learnings in
  [docs/design/tilemap-renderer.md](design/tilemap-renderer.md#implementation-status--shipped-a--b-20260618).
  **Remaining (deferred):** **multi-layer** chunks (terrain + overlay), **animated tiles** (water/lava
  via a per-tile-type frame table → layer offset), **partial-fog reveal** (`{id,x,y,w,h,fogData}` to
  patch the fog sub-rect for cheap incremental reveal — today fog updates re-send the whole layer).
- **Render-side culling in passes** — SpritePass/TilemapPass skip instances outside the camera
  bounds before draw (same `visibleWorldBounds`). Couple with tilemap high-perf; measure first.
- **Runtime textures / painting** — let the game create a texture from pixel data at runtime and
  update its pixels (paint). Mostly EXPOSING existing RHI (`createTexture`/`updateTexture`,
  `ResourceCache::registerTexture`) via a topic (`render:texture:create`/`:update` → textureId).
- **Sprite mips / unit anti-aliasing at zoom-out** — units are free sprites (world-float coords,
  any sub-tile position) layered over the tilemap — already supported. BUT `TextureLoader` creates
  sprite textures with NO mips (mipLevels=1), so under strong zoom-out units can shimmer/alias.
  Fix = generate + upload mips for sprite textures (the RHI already does mips — see the tilemap LOD)
  + a readback test. Note: *semantic* LOD (detailed ship → blip/icon at system zoom) stays GAME-SIDE
  — the engine gives zoom + culling; the game picks what to submit.

## Animation (`grove::anim`)
- **Clip blending** (slice 4) — crossfade two clips by weight. Advanced; not needed for "simple".
- **FlipbookPlayer** — play/pause/speed wrapper around `Flipbook` (like `AnimationPlayer`), if the
  ergonomic sugar is wanted (the game can drive `Flipbook::uvAt(time)` directly today).
- Font residuals — uppercase accents drop the accent; œ/æ ligatures + a crisp TTF atlas = future.

## Sound (`SoundManager`)
- Channel groups, per-sound instance caps, ducking, music preload/unload. v1 is fire-and-forget
  SFX + looping-stoppable-by-id + music + buses + preload/unload.
- **Adaptive / interactive audio — TO PLAN (engine gap; de-risk it the way the tilemap was).**
  Today's `SoundManager` (SDL_mixer) does fire-and-forget music + SFX. It does NOT do adaptive
  audio: multi-**stem** layers mixed by game state, music **transitions quantized to the bar/beat**,
  **state-driven mixing**. Drifterra will want this. Two routes, both behind `ISoundBackend`:
  - **Custom AudioModule** — stems + a beat-clock + state→mix rules on top of the backend. More
    control, more work, stays in our SDL stack.
  - **Middleware (FMOD / Wwise)** — industry-standard adaptive-audio engines behind the interface.
    Powerful fast, but licensing + integration cost.
  Decide the route *before* the game needs it (anticipate, don't scramble — like the tilemap).

## Quality / infra
- **`LimitsTest` flakiness** — SEGFAULTs intermittently in the FULL suite under load; passes
  deterministically in isolation (3/3). Pre-existing (stresses module-load limits), unrelated to
  recent feature work. Investigate the aggregate-run crash.
