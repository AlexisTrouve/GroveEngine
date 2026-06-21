# GroveEngine — Backlog

Deferred engine work, with the *why now-or-later*. Not commitments; picked up when the game
(Drifterra) needs them or a bottleneck is measured. Shipped features live in the code + their
tests; this file is only what's **not** done yet.

## Camera / View
- **Zoom continuum (galaxy↔system↔ship↔interior) — ✅ engine math SHIPPED** (2026-06-20). The
  continuous motion was already there (`grove::camera` zoomAt/damp/clampZoom); now `ZoomLadder`
  (`Scene/ZoomLadder.h`) adds **readable plateaus** (`snap`) + the **strata/transition seam**
  (`blend` → active strata + log-space smoothstep crossfade `t`), locked by `ZoomLadderUnit`.
  **Remaining = GAME-SIDE by design** (engine gives the seam, not the content): *what* is
  rendered/simulated per strata (semantic zoom), the actual content cross-fade during a transition,
  and galaxy-scale culling/floating-origin. A renderer-side showcase **demo** of the continuum (prove
  the 2→N strata transition à l'œil, INTERFACE.md "à prouver tôt") is the natural next visual step.
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
- **Sprite mips / unit anti-aliasing at zoom-out — ✅ SHIPPED** (2026-06-21). `TextureLoader::loadFromMemory`
  now builds a box-filtered RGBA8 mip chain (`grove::tex::buildRgba8MipChain`, `Resources/MipChain.h`,
  NPOT-safe) and uploads it with `mipLevels`, so `createTexture` makes a mipped texture and the default
  sampler trilinear-filters it — free unit sprites stop shimmering/aliasing under strong zoom-out. Locked
  by an eye-free oracle (`test_lod_color [mip]`: coarsest mip of a checkerboard == the average) + the
  existing tilemap LOD GPU test (same `createTexture(mipLevels)` + trilinear path) + eye-validated in the
  showcase. **Scope:** sprite 2D textures only — the array-atlas/tilemap path has its own LOD band
  (untouched). *semantic* LOD (detailed ship → blip/icon) stays GAME-SIDE.

## Animation (`grove::anim`)
- **Clip blending** (slice 4) — crossfade two clips by weight. Advanced; not needed for "simple".
- **FlipbookPlayer** — play/pause/speed wrapper around `Flipbook` (like `AnimationPlayer`), if the
  ergonomic sugar is wanted (the game can drive `Flipbook::uvAt(time)` directly today).
- Font residuals — uppercase accents drop the accent; œ/æ ligatures + a crisp TTF atlas = future.

## Sound (`SoundManager`)
- Channel groups, per-sound instance caps, ducking, music preload/unload. v1 is fire-and-forget
  SFX + looping-stoppable-by-id + music + buses + preload/unload.
- **Adaptive / interactive audio — ✅ slices 1-3 SHIPPED (logic), route = CUSTOM** (2026-06-20). Route decided
  (Alexi): **custom**, behind `ISoundBackend` — not middleware (FMOD/Wwise stays a future swap behind
  the same interface if sample-accurate DSP is ever needed; the adaptive *logic* is backend-agnostic
  + headless-testable, so the proof needs no real audio). **Slice 1 done = state-driven vertical
  layering**: `audio:layer`/`audio:intent`/`audio:mix`/`audio:layer:stop` → a pure `AdaptiveMixer`
  (stems crossfade calm→peak by tension, ramped) driving the backend via the new
  `ISoundBackend::setSoundVolume`. **Slice 2 done = bar-quantized transitions**: a pure `BeatClock`
  (`audio:tempo {bpm,beatsPerBar}`) + `audio:intent {tension, quantize:"bar"|"beat"|"now"}` — a
  quantized intent is staged and released when the clock crosses the next bar/beat ("transitions
  calées sur la mesure"); clock stopped ⇒ immediate. Locked by `SoundManagerUnit` `[adaptive]`. Lives
  INSIDE SoundManager (owns the backend + the per-frame tick; a 2nd module would fight SDL_mixer's
  singleton). **Slice 3 done**: `audio:cue {path,volume?,quantize?}` (one-shot music-bus stinger,
  beat/bar-quantizable, reuses BeatClock) + leitmotif — `audio:layer {…,theme,state}` tags an
  arrangement (tension-exempt) and `audio:theme {id,state}` crossfades to it (soft→twisted→broken
  follows the entity state). `SoundManagerUnit [adaptive]` now 18 cases (32 for the whole module).
  **The adaptive-music vision is shipped as LOGIC** (vertical layers + bar-quantized transitions +
  cues + leitmotifs), all headless-verified; what remains is **content** — real stem assets for an
  audible "by ear" pass (compos late) — and any DSP niceties. **Risk flagged:** SDL_mixer plays the N
  stems as independent looping channels — no
  sample-accurate phase-lock between stems; fine for fade-in/out layering, the trigger to reconsider
  middleware if strict phase-lock is ever required.

## Quality / infra
- **`LimitsTest` flakiness** — SEGFAULTs intermittently in the FULL suite under load; passes
  deterministically in isolation (3/3). Pre-existing (stresses module-load limits), unrelated to
  recent feature work. Investigate the aggregate-run crash.
