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
primitive, **action wheel** = action primitive, **no input device mandatory**. The zoom/LOD/camera
it leans on is mostly **already covered** — tilemap seamless zoom is shipped, *semantic* LOD
(detailed ship → icon at system zoom) is **game-side** by design (engine gives zoom + culling),
camera items are above. The genuine engine gaps it surfaces:
- **Action-wheel / radial-menu widget** — `UIModule` has button/label/panel/image; a **contextual
  radial menu** is net-new. It is the game's **action primitive** and the device-agnostic equalizer
  (same gesture on mouse / keyboard / gamepad). = a radial widget fed by `input:*`, publishing the
  picked action on `ui:*`. Why now-or-later: prerequisite of "no mandatory device"; pick it up when
  the first action-heavy slice (combat command) is framed.
- **Gamepad input + multi-source action binding** — `InputModule` feeds SDL keyboard/mouse →
  `input:*`; **no gamepad**. The "keyboard optional / mouse not mandatory" goal needs a pad to reach
  the same actions. = (a) SDL gamepad events surfaced as `input:*`; (b) actions bound to **several
  sources mapping one intent** (mouse/key/pad interchangeable). Payoff: controller-native → Steam
  Deck. Why now-or-later: settle before the control scheme hardens — retrofitting parity hurts like
  i18n/E2E (anticipate, don't scramble — same as the tilemap/audio).

## Rendering
- **Tilemap high-perf — ✅ SHIPPED A → B** (2026-06-18/19). GPU index-texture (R16UI + texelFetch,
  1 draw/chunk), `texture2DArray` atlas, retained `render:tilemap:add/update/remove` (upload-once),
  derivative detail↔LOD crossfade with a palette-driven mipped color band. Tested headless +
  `[gpu]` readback. Full status, commits, and learnings in
  [docs/design/tilemap-renderer.md](design/tilemap-renderer.md#implementation-status--shipped-a--b-20260618).
  **Remaining (deferred):**
  - **A4.2 sub-rect patch** — `render:tilemap:update {id,x,y,w,h,tiles}` to patch a few texels (fog
    reveal, terrain edits) via the A1 region overload instead of replacing the whole grid.
  - mipped `R8` **fog/state** (scalar visibility), **multi-layer** chunks, **animated tiles**.
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
