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
- **Tilemap high-perf — NEXT MAJOR PROJECT.** Full design pinned + SOTA-validated in
  [docs/design/tilemap-renderer.md](design/tilemap-renderer.md): GPU index-texture (R16UI +
  texelFetch, 1 draw/chunk), `texture2DArray` atlas, derivative LOD + palette-driven mipped color
  band, mipped scalar fog. Needed because Drifterra will have **tens of thousands of tiles** with
  seamless zoom — the current per-frame CSV re-parse + per-tile work is O(map). Implementation = an
  RHI capability bump (integer/array textures, point-clamp samplers, a `*_tilemap` shader),
  sliced A (detail) → B (LOD). Per-frame visible-window culling is already done (TilemapPass).
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
