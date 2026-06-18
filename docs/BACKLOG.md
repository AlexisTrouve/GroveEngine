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

## Rendering
- **Tilemap high-perf — ✅ SHIPPED A → B** (2026-06-18/19). GPU index-texture (R16UI + texelFetch,
  1 draw/chunk), `texture2DArray` atlas, retained `render:tilemap:add/update/remove` (upload-once),
  derivative detail↔LOD crossfade with a palette-driven mipped color band. Tested headless +
  `[gpu]` readback. Full status, commits, and learnings in
  [docs/design/tilemap-renderer.md](design/tilemap-renderer.md#implementation-status--shipped-a--b-20260618).
  **Remaining (deferred):**
  - **②.2 LOD end-to-end readback** — render the tilemap offscreen, assert zoom-out pixel == average
    of the tile colors (needs reproducing the renderer's view-transform offscreen).
  - **A3.3 real atlas** — slice a game-supplied grid-PNG into `texture2DArray` layers (today the atlas
    is a procedural color array; binding a 2D texture to the array sampler would be invalid).
  - **A4.2 sub-rect patch** — `render:tilemap:update {id,x,y,w,h,tiles}` to patch a few texels (fog
    reveal, terrain edits) via the A1 region overload instead of replacing the whole grid.
  - mipped `R8` **fog/state** (scalar visibility), **multi-layer** chunks, **animated tiles**.
- **Render-side culling in passes** — SpritePass/TilemapPass skip instances outside the camera
  bounds before draw (same `visibleWorldBounds`). Couple with tilemap high-perf; measure first.
- **Runtime textures / painting** — let the game create a texture from pixel data at runtime and
  update its pixels (paint). Mostly EXPOSING existing RHI (`createTexture`/`updateTexture`,
  `ResourceCache::registerTexture`) via a topic (`render:texture:create`/`:update` → textureId).

## Animation (`grove::anim`)
- **Clip blending** (slice 4) — crossfade two clips by weight. Advanced; not needed for "simple".
- **FlipbookPlayer** — play/pause/speed wrapper around `Flipbook` (like `AnimationPlayer`), if the
  ergonomic sugar is wanted (the game can drive `Flipbook::uvAt(time)` directly today).
- Font residuals — uppercase accents drop the accent; œ/æ ligatures + a crisp TTF atlas = future.

## Sound (`SoundManager`)
- Channel groups, per-sound instance caps, ducking, music preload/unload. v1 is fire-and-forget
  SFX + looping-stoppable-by-id + music + buses + preload/unload.

## Quality / infra
- **`LimitsTest` flakiness** — SEGFAULTs intermittently in the FULL suite under load; passes
  deterministically in isolation (3/3). Pre-existing (stresses module-load limits), unrelated to
  recent feature work. Investigate the aggregate-run crash.
