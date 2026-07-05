# UI Framework — Work Plan & Design

> **Status:** design locked, execution not started (2026-06-22).
> **Decision owner:** Alexi. **Scope:** turn the flat retained-mode `UIModule` into a full
> game-UI framework (windows, layout, docking, rich content, VN runtime) **by hand**, without
> sacrificing GroveEngine's architectural coherence.

This doc is the single source of truth for the UI build-out. It records *why* we chose to extend
the custom system, *what exists today*, the *slice sequence*, the *proof* each slice owes, and the
*open questions*. Pick up work here; update the status column as slices land.

---

## 1. The decision — build, don't adopt (path A)

The ask (Drifterra needs a complex game UI): partial sidebars with ships in them, menus with
sections, menu hierarchy, 2D image/animated/video scenes **with choices** + voice + sound replay,
a music/radio player, collapsible drawers on all four edges, HUD alpha, display-order control,
movable/stackable windows, frames and sub-frames.

Three paths were weighed:

- **A — extend the custom `UIModule`** (retained-mode over IIO). Keep hierarchy + layout + retained
  rendering + hit-test + JSON + hot-reload + the E2E harness; *add* the missing backbone.
- **B — embed a library** (RmlUi: retained, HTML/CSS-like, bgfx-friendly). Free flex/grid/anchor/
  rich-text/transitions, but its `RenderInterface` is a **parallel render path** (not via `render:*`),
  it takes over input/hit-test, the **in-house E2E harness no longer applies**, and hot-reload state
  must be redone. Low reversibility once Drifterra depends on it.
- **C — hybrid** (custom HUD + a lib for rich menus/VN). A real industry pattern, but two systems and
  two test contracts to maintain.

**Chosen: A.** The inventory (§2) showed ~60% of a UI engine is *already there and tested*; throwing
that away for RmlUi would pay dearly for a shortcut while breaking the three pillars that make
GroveEngine coherent — **everything via IIO topics**, **untested-UI-doesn't-exist E2E**, and
**engine/game separation**. Coherence is the engine's main asset; we don't trade it for a shortcut.

---

## 2. Current state — what exists vs what's missing

Inventory of `modules/UIModule/` (2026-06-22). The skeleton is more complete than "start from zero".

### Already there (reused, not rebuilt)
- **Hierarchy** — `UIWidget` (`Core/UIWidget.h`): parent/children, `addChild`, recursive `findById`,
  recursive `computeAbsolutePosition`.
- **A layout engine** — `UILayout` (`Core/UILayout.{h,cpp}`): two-pass `measure`/`layout`, 4 modes
  (`Vertical`/`Horizontal`/`Stack`/`Absolute`), padding/margin/spacing, align/justify, **flex-grow**,
  min/max constraints.
- **Retained rendering** — `UIRenderer`: `registerEntry`/`update*`/`unregisterEntry` with dirty flags
  (`m_geometryDirty`/`m_appearanceDirty`), `releaseRenderEntries` (the ghost-rect fix). Primitives:
  `render:sprite`, `render:text`, `render:sector` (rects = sprite with `textureId=0`).
- **Input** — hit-test top-most (reverse tree order), hover (`ui:hover`), focus (TextInput only),
  keyboard (edit keys + UTF-8 text), mouse wheel (scrollpanel).
- **11 widgets** — Panel, Label, Button, Checkbox, Slider, TextInput, ProgressBar, Image,
  ScrollPanel, Radial, Tooltip(manager).
- **Alpha** RGBA everywhere; **JSON data-driven** (`UITree`); **hot-reload** with state preservation;
  **E2E harness** (inject real click → assert `ui:*` topic).

### Was missing — now ✅ SHIPPED (historical list; the backbone landed)
- ✅ **Anchoring** + **reflow on resize** — `IT_021`/`IT_022`.
- ✅ **Grid** layout mode — `IT_023`.
- ✅ **Clipping / scissor** — a ScrollPanel pushes its rect on the clip stack → children's render entries
  carry it → bgfx scissor. Locked by `IT_024`/`IT_025` (UI wiring) + `SpriteClipGpu`/`TextClipGpu` (GPU).
  *(Narrow residual: a non-absolute-layout child panel re-derives its absX and de-scrolls — rare, noted
  in `UIScrollPanel.cpp` for a future scrollpanel rework.)*
- ✅ **Dynamic z-order / focus-stack** (`bringToFront`) — `IT_027`.
- ✅ **Window** container (drag/resize/close/title/stack) — `IT_026`/`IT_028`/`IT_032`.
- ✅ **Drawers** (4 edges, collapsible, slide) — `IT_030`.
- ✅ **Tabs** — `IT_029` · **Tree/menu-hierarchy** — one-level shipped (`IT_034`), **multi-level still TODO** ·
  **List/Grid virtualized** — `IT_033`/`IT_041`.
- ✅ **Modal/dialog** (dim + focus-trap + button bar) — `IT_031`.
- ✅ **Tween** (slide/fade/scale) — exercised by the drawer/modal transitions (`IT_030`/`IT_031`).
- **Tween/transition** system (slide/fade/scale) and **group opacity** (fading a whole subtree;
  today opacity is only the color AA channel).
- **Rich-content widgets** — animated panel, audio/voice/radio player, video.

---

## 3. Principles governing the plan

1. **Independent slices.** Each slice compiles, is tested, committed, and **Drifterra can consume it**
   without waiting for the next. We don't start N+1 until N is green *and* eye-validated.
2. **Proof is mandatory.** Pure geometry → an objective oracle test (the `RadialMath`/`SectorMesh`
   pattern). Interaction → an **E2E test that really clicks**. No E2E ⇒ it doesn't exist. **Red test
   first.**
3. **Reuse the existing engine.** `UILayout` (already measure/layout + flex), `grove::anim::Easing`
   (8 curves → UI transitions), `SoundManager` (the audio player is *assembly*, not a new engine),
   the `render:sector` pattern (a new colored primitive can reuse the colour shader — no new shader).
4. **Engine = primitives, game = content/semantics.** The engine gives Window/List/Drawer/Tween +
   the VN building blocks; *which* ships, *which* script = Drifterra. (Exception: the VN **runtime**
   itself is engine-side — see §6.)
5. **IIO-first, surgical, commented.** No widget-to-widget calls, no direct renderer/input access.
   Surgical changes on existing files; new code modular + QUOI/POURQUOI/COMMENT comments.

---

## 4. Execution spine (dependency-ordered)

```
1. Layout engine (THE MVP)        ──┐ visible immediately, no dependency
2. Clipping / scissor (FOUNDATION)  │ prerequisite for every container
3. Z-order/focus + Window         ◄─┘ the structural backbone
4. Tween / transitions (small)        reuses Easing — unblocks drawers + modal fade
5. Modal · Drawers · Tabs · Tree · List-grid virtualized   (the containers)
6. Content widgets: animated panel · audio/radio player · video
7. VN / Cutscene runtime (Scene/Dialogue module)           (sits on top of all)
```

Layout goes first (the chosen MVP) even though clipping is foundational, because layout doesn't
*need* clipping and delivers visible value (HUD anchored to corners, panels reflow) fast. Clipping
lands right after, before any window/list.

---

## 5. Catalog — every requested item has a home

| # | Slice | Depends on | Proof | Covers (from the ask) |
|---|---|---|---|---|
| **1** ✅ | **Layout**: reflow-on-resize · anchoring · grid | — | `IT_021`/`IT_022`/`IT_023` | sectioned menus that reflow, HUD anchored to corners |
| **2** ✅ | **Clipping/scissor** (`render:scissor` + UI honors it) | renderer | `IT_024`/`IT_025` + `SpriteClipGpu`/`TextClipGpu` | prerequisite for windows/lists/drawers |
| **3a** ✅ | **Z-order / focus-stack** (`bringToFront`, top-most capture) | — | `IT_027` | display order |
| **3b** ✅ | **Window** (titlebar, drag, resize, close, **stackable**) | 2 + 3a | `IT_026`/`IT_028`/`IT_032` | windows, stackable frames/sub-frames |
| **4** ✅ | **Tween** (slide/fade/scale via Easing) + **group opacity** | — | via drawer/modal transitions (`IT_030`/`IT_031`) | HUD alpha, basis for all transitions |
| **5a** ✅ | **Modal/Dialog** (dim + focus-trap + button bar) | 3a + 4 | `IT_031` | dialogs |
| **5b** ✅ | **Drawers** 4 edges (collapsible, slide) | 1 + 2 + 4 | `IT_030` | hidden menus top/bottom/left/right |
| **5c** ✅ | **Tabs / sections** | 2 | `IT_029` | menu with sections |
| **5d** ✅ | **Tree / menu-hierarchy** (expand/collapse) — ✅ SHIPPED (N-level via `UIList::setTree`) | 2 | `IT_056` + `UIListUnit [tree]` | menu hierarchy, warship groups |
| **5e** | **List/Grid view virtualized** (repeater + select + virtual scroll) — ✅ SHIPPED | 1 + 2 | E2E + perf | partial ship sidebar |
| **6a** ✅ | **Animated panel** (hosts `grove::anim`/flipbook) — ✅ SHIPPED | — | `IT_054` (UV cell advances) | animated 2D scene |
| **6b** ✅ | **Audio/voice/radio player** (buttons + progress → `sound:*`) — ✅ SHIPPED | — | `SoundManagerUnit [position]` + `IT_055` | voice, sound replay, music/radio player |
| **6c** 🚧 | **Video panel** — 🚧 6c-0 shipped (VideoModule + A/V sync + `render:texture:upload` raw-pixel path, a frame renders); ffmpeg-CLI MP4 backend (6c-1) remains | 6a+6b | `VideoSyncUnit` + `IT_058` + `RuntimeTextureGpu` | video scene (MP4 + audio) |
| **7** ✅ | **VN/Cutscene runtime** (data-driven Dialogue module) — ✅ SHIPPED (MVP) | 5a+6a+6b | `DialogueRuntimeUnit` + `IT_057` (script→choice→branch) | 2D image/animated/video scene **with choices** |

Video is deliberately last and isolated (heavy: codec decode → texture upload); we ship via image
sequence before a real codec.

---

## 6. Decided

- **VN/cutscene = full engine runtime** (Alexi's call). A `Scene`/`Dialogue` engine module reading a
  data-driven script (nodes, choices, branches, voice, video). Reusable beyond Drifterra. Sits *on
  top* of the backbone → parked until containers + clipping exist (slice 7).
- **MVP = the layout engine** — concretely an **extension of `UILayout`**, not a rewrite (§7).

---

## 7. MVP detail — slice 1 (layout engine), ready to execute

Extends `Core/UILayout.{h,cpp}` + `UIWidget` sizing fields + `UITree` JSON + a reflow trigger in
`UIModule`. Pure geometry first (oracle-locked), then wired + E2E.

- **1.1 Reflow-on-resize — ✅ SHIPPED** (2026-06-22). `ui:resize {w,h}` → `UIModule::relayoutRoot()`
  re-runs `measure`/`layout`/`computeAbsolutePosition` from the root against the new viewport. Relative
  sizing landed: `widthPercent`/`heightPercent` (fraction `0..1` of the parent content box; root's
  parent = the viewport, so `1.0` = fill). Resolved inside `UILayout` (main axis = a fixed reservation
  taken before flex; cross axis / stack / absolute = direct fraction) and, for the root, in
  `relayoutRoot()`. The host publishes `ui:resize` on window resize; UIModule stays SDL-decoupled.
  **Proof:** `UILayoutUnit` oracle (reflow + percent re-resolution) + `IT_021` E2E (the SAME click hits
  the right button before the resize, the left one after — geometry provably moved).
- **1.2 Anchoring — ✅ SHIPPED** (2026-06-22). `anchor` (9 positional anchors: corners/edges/center)
  + `anchorOffset {x,y}`, resolved by the pure `UILayout::resolveAnchor(...)` against the parent content
  box. Applied in the **Absolute** branch of layout (flow modes position by the flow). *Fill* is NOT a
  separate anchor — that's `widthPercent/heightPercent:1.0` from 1.1 (no redundant Stretch enum).
  **Proof:** oracle per anchor + offset + padded-box origin (`UILayoutUnit`) + `IT_022` E2E (a
  bottom-right-anchored button follows the corner across a resize — old spot empty, new corner hits).
- **1.3 Grid — ✅ SHIPPED** (2026-06-22). `LayoutMode::Grid` — `columns` + `spacing` (gap) + `rowHeight`
  (cell height; 0 = square). Cells share the content width across the columns, so the grid **reflows** on
  resize; children flow row-major via the pure `UILayout::gridCellRect(index, cols, cellW, cellH, gap)`.
  **Proof:** oracle (cell placement + cells-fill-width reflow, `UILayoutUnit`) + `IT_023` E2E (a 2-col
  grid's cells grow on resize — the same click hits the right cell before, the left one after).

**Slice 1 (layout engine MVP) is COMPLETE** — reflow-on-resize + relative sizing + anchoring + grid.

**Open question to resolve at 1.1:** where does the window-resize signal come from — InputModule/SDL
or does BgfxRenderer already publish it? Verify at slice start; add the source cleanly if none exists.
**Also:** extend `extractState`/`restoreState` for the new anchor/layout fields so hot-reload preserves
them.

---

## 8. Open questions / risks

- **Resize signal source** (see §7) — the one unknown blocking slice 1.1.
- **Clipping mechanism** (slice 2) — per-entry clip-rect threaded through `render:sprite/text` and
  applied via `bgfx::setScissor` per draw, vs stencil. Per-entry clip-rect is the likely choice
  (simple, robust, retained-friendly); decide at slice start.
- **Virtualization** (slice 5e) — the ship roster must not render off-screen children (today
  ScrollPanel renders all children). Needs the clip rect + a windowed repeater.
- **Scope discipline** — the ask is large ("everything a game UI needs"). The guardrail is §3.1:
  slice-by-slice, each shipped + tested + consumable, no slice N+1 before N is green.
- **ProjectMind is stale** for this repo (active plan still "ThreadedModuleSystem Phase 2"). Park that
  plan and register this one once execution starts; the code + git remain the source of truth.
- **Perf — the dirty-gated layout (deliberate follow-on).** `UIPanel::update` runs `UILayout::layout`
  every frame even when nothing changed. The safe half (measure-once) shipped; the big win is to SKIP
  the layout pass on unchanged frames. It's risky: a missed invalidation → stale layout (a visual bug
  no test catches unless it tests that exact scenario), and it must NOT freeze per-frame animations
  (drawer slide, window drag/resize). The robust approach is a cheap per-frame "layout signature"
  (panel size + each child's size/visibility/flex/percent) that re-lays-out only on change — but the
  signature interacts with the fact that `layout()` overwrites child sizes (input vs output). Do it
  deliberately, with a test that asserts the layout STILL updates on every relevant change.
- **Found while building 1.1 (pre-existing, flagged not fixed — surgical):** a root-level scalar
  `"flex": N` on a child is **silently dropped** — `UITree::parseCommonProperties` gates flex on
  `hasChild("flex")`, which returns false for scalars (only objects/arrays count, `JsonDataNode.cpp:121`).
  The reliable form is `"layout": { "flex": N }` (parsed via `getDouble`). Visual fixtures use the broken
  root-level form but were never E2E hit-tested, so nobody noticed. Cheap one-line fix (read flex
  unconditionally with a sentinel default) when we next touch UITree.

---

## 9. Status log

| Date | Slice | State |
|---|---|---|
| 2026-07-05 | 6c-0 Video seam | 🚧 SHIPPED (the seam; render + MP4 to come). New `VideoModule` + pure `video::VideoSync` (A/V sync math: master clock → frame index + changed/dropped/ended, header-only like BeatClock) + `IVideoBackend` (decode behind an interface, like ISoundBackend). **Audio is the master clock** (the picture holds/advances/DROPS frames to stay locked to `sound:music:position` from 6b; a silent clip uses a dt clock). On `video:play` the module creates a runtime texture + a sprite once, then per changed frame publishes `render:texture:upload {id,w,h,+blob pixels}` (the raw-RGBA path) + `video:frame`; emits `video:ended`. `GROVE_BUILD_VIDEO_MODULE` + `VideoModule_static`. Locked by `VideoSyncUnit` (index/drop/ended; **prove-it-bites**: never-drop → RED) + `IT_058` (silent frame-by-frame to end + audio-master-clock-with-drop, via `MockVideoBackend`). **Remaining**: 6c-0c = the renderer consuming `render:texture:upload` (arbitrary-pixel texture — runtime textures are solid-color only today) so a frame actually renders; 6c-1 = `FfmpegCliBackend` (spawn ffmpeg → pipe frames+audio, real MP4/H.264/AAC — Alexi's pick over linking libav). |
| 2026-07-05 | 7 VN/Cutscene runtime | ✅ SHIPPED (MVP). A new engine module: **`DialogueModule`** wrapping the pure header-only **`DialogueRuntime`** (a node/choice/branch state machine — parse `{start, nodes:{id:{speaker?,text,background?,voice?, goto? | choices:[{text,goto}]}}}`, `start`/`advance`/`choose(i)`/`goToNode(id)`/`isEnd`, no IIO/render coupling like `grove::anim`). **Binding-driven** (the payoff of the whole binding engine): on entering a node the module pushes `ui:data:merge {scene:{id,speaker,text,background,isEnd,choices:[{text,goto}]}}` → a game-authored VN screen renders it (bound labels + a **choice repeater** whose buttons fire `scene:goto {node}` — id-based, a string that survives declarative events, per the 6b finding); plays `voice` via `sound:sfx`; emits `scene:node`/`scene:end`. Consumes `scene:load`/`scene:advance`/`scene:choose {index}` (int OR string)/`scene:goto {node}`. State machine = engine, script + VN screen = game (demo: `assets/dialogue/demo_script.json` + `assets/ui/demo_vn_screen.json`). New `GROVE_BUILD_DIALOGUE_MODULE` + `DialogueModule_static` (SDL-free, static like SoundManager). Locked by `DialogueRuntimeUnit` (parse/advance/branch/terminal/bad-moves; **prove-it-bites**: choose ignoring the index → RED) + `IT_057` (load→advance→choose-branch→end through the module + a string-index/voice/goto-by-id case). Follow-ons: conditions/flags/variables (no expression language), **video (6c)**, auto-advance timer, typewriter, mid-scene save. |
| 2026-07-05 | 5d Multi-level tree | ✅ SHIPPED. `UIList` gains an N-LEVEL TREE mode (`setTree` / `isTree` / `parseTree`, `ListNode {id,label,icon?,collapsed?,children}`) beside flat + one-level groups. `projectNodes()` recursively flattens the tree into the SAME `m_rows` sequence — internal nodes → collapsible headers (reuse the group header render + `toggleGroup`), leaves → selectable items, each level indented (`ListRow.depth`, render indent = `padding × depth`). **Surgical + neutral for flat/grouped**: the shared render indent went from `m_grouped ? padding : 0` to `padding × depth` with depths assigned (header 0 / grouped item 1 / flat item 0) so IT_033/IT_034 output is byte-identical; everything downstream (virtualization/scroll/clip/hit-test/selection) already keys off `m_rows` → arbitrary depth "just works". `toggleGroup` recurses `m_nodes` too (same UIModule header click). Factory `list` reads `nodes:[…]`; runtime `ui:list:set_tree`. Locked by `IT_056` (deep-leaf select / nested-expand click-flip / recursive collapse; **prove-it-bites**: projectNodes ignoring `collapsed` → RED) + `UIListUnit [tree]` (parseTree recursion + per-level depth + recursive collapse rowcount). Full UI regression (52 incl. vessel screens) green. |
| 2026-07-05 | 6b Audio/radio player | ✅ SHIPPED. Two halves. **Engine primitive**: `SoundManager` now publishes **`sound:music:position {path, elapsed, duration}`** — a real playback clock for a progress bar (the module was fire-and-forget). Added to `ISoundBackend` (non-pure, `-1` default = unsupported → ABI-safe): `getMusicPosition`/`getMusicDuration` + `updateMusic(dt)`. The **backend owns the clock** (music plays at wall-clock regardless of game pause/slowmo): `SDLMixerBackend` reads SDL_mixer 2.6+ (`Mix_GetMusicPosition`/`Mix_MusicDuration`, version-guarded), the Mock advances via the fed `dt` (deterministic headless). `SoundManagerModule::tickMusicPosition` polls + publishes while playing, throttled ~15 Hz. Locked by `SoundManagerUnit [position]` (elapsed advances / path+duration carried / silent after stop; **prove-it-bites**: constant elapsed → RED). **UI half**: a radio-player layout (`assets/ui/test_e2e_radio.json`) composed from EXISTING widgets — Play/Stop buttons fire `sound:music`/`sound:music:stop` via declarative `on:click`, a now-playing label binds `{{radio.title}}`, a progress bar binds `{{radio.progress}}`. Zero new widget. Locked by `IT_055` (click→real sound topic + label reacts to `ui:data`). Follow-ons: numeric declarative args (a volume slider → `sound:volume`), `sound:music:ended`, playlist/next (game-side state). |
| 2026-07-05 | 6a Animated panel | ✅ SHIPPED. `UIFlipbook` widget (`Widgets/UIFlipbook.{h,cpp}`) hosts a `grove::anim::SpriteSheet` + `Flipbook`; `update(dt)` advances an internal clock, `render()` resolves `uvAt(time)` → the current sheet cell's UV. **Load-bearing engine gap closed**: the retained sprite path could NOT carry animated UVs — `RenderEntry` had no UV fields, `publishSprite*` hardcoded `u0=v0=0,u1=v1=1`, and change-detection ignored UVs. Added `UIRenderer::updateSpriteUV` + UV fields threaded through `updateSpriteImpl`/`publishSprite*` (defaulted to `0/1` → every non-flipbook caller byte-identical) + UVs added to change-detection (a UV-only cell advance now republishes). `UITree` `"flipbook"` factory (columns/rows/count/fps/loop; frames = natural 0..N-1). Locked by `IT_054` (cell UV advances through ≥2 distinct values incl. 0.25 via `render:sprite:update`; **prove-it-bites**: removing the UV change-detection → RED). Full UI regression green (UV defaults keep the other 48 tests byte-identical). Follow-ons: `asset`-streamed sheets (cell×atlas UV compose), custom frame order, `ui:anim:set` play/pause. |
| 2026-06-22 | — | Design locked (this doc). Path A chosen. |
| 2026-06-22 | 1.1 reflow-on-resize | ✅ SHIPPED. `ui:resize` → `relayoutRoot()` + `widthPercent`/`heightPercent` relative sizing. Locked by `UILayoutUnit` + `IT_021`. |
| 2026-06-22 | 1.2 anchoring | ✅ SHIPPED. 9 positional anchors + `anchorOffset`, `resolveAnchor()` in the absolute branch. Locked by `UILayoutUnit` + `IT_022`. |
| 2026-06-22 | 1.3 grid | ✅ SHIPPED. `LayoutMode::Grid` (columns/gap/rowHeight), cells fill width → reflow, `gridCellRect()`. Locked by `UILayoutUnit` + `IT_023`. **Slice 1 (layout MVP) complete.** Next: slice 2 (clipping → window/z-order). |
| 2026-06-22 | 2a-1 sprite scissor | ✅ SHIPPED. Clip rect rides in `SpriteInstance.reserved[]`; `SpritePass` reads it → per-batch `bgfx::setScissor` (no GPU/shader change; non-clipped sprites = batching unchanged). Locked by `SpriteClipGpu` [gpu]. |
| 2026-06-22 | 2a-2a text scissor | ✅ SHIPPED. `TextCommand` clip rect; `TextPass` breaks the glyph batch on clip change → per-flush `setScissor` (now transient-buffered + per-flush state — fixes a latent multi-flush bug). Locked by `TextClipGpu` [gpu]. |
| 2026-06-22 | 2a-2b UI clip wiring | ✅ SHIPPED. `UIRenderer` clip-stack (`pushClip`/`popClip`); retained entries carry the active clip (re-published on change); `SceneCollector` parses it → `reserved[]` / `TextCommand`; `UIScrollPanel` pushes its visible rect around its children. **Chain complete**: UI → IIO → renderer scissor. Locked by `IT_024` (child entry carries panel clip) + the 2a-1/2a-2a GPU scissor. **Visual clipping usable.** |
| 2026-06-22 | 2b hit-test clip | ✅ SHIPPED. `UIWidget::clipsHitTest()` (virtual, default false; `UIScrollPanel` = true); `hitTest()` skips a clipper's subtree when the cursor is outside its bounds. Locked by `IT_025` (a child below the panel is un-clickable) + `IT_019` (a scrolled-in child stays clickable). **Slice 2 (clipping) COMPLETE — the foundation for windows/drawers/tabs.** |
| 2026-06-22 | 3a z-order primitive | ✅ SHIPPED. `UIWidget::bringToFront()` — z-order = sibling order (last = top + hit first), so raising = move to the back of `parent->children`. Locked by `UIZOrderUnit`. NB: in-app windows, NOT OS windows (that's the deferred multi-screen item). |
| 2026-06-22 | 3b-1 Window (static) | ✅ SHIPPED. `UIWindow` — title bar (title + close) + content clipped below it (slice-2 clip); **opaque** (hitTest absorbs clicks in bounds, no leak behind). `hitClipRect()` virtual added (window clips to content rect; ScrollPanel keeps full bounds). `UITree` `"window"` factory. Locked by `IT_026` (content clickable / title-bar absorbed / below-content clipped). |
| 2026-06-22 | 3b-2 Window interaction | ✅ SHIPPED. `UIModule::handleWindowInteraction()` (centralized on the topmost window under the cursor): **raise-on-click** (`bringToFront`), **title-bar drag**, **close** (hide + purge + `ui:window:closed`). Locked by `IT_027` (raise flips the z-order) + `IT_028` (drag moves it, close hides it). |
| 2026-06-22 | 3b-3 Window resize | ✅ SHIPPED. Bottom-right grip on `UIWindow` (`resizable`/`minWidth`/`minHeight`); dragging it grows the window (min-clamped), handled in the same centralized `handleWindowInteraction`. Locked by `IT_032` (a content button below the small window's content area becomes reachable after enlarging). **Slice 3 (in-app Window) fully complete: drag + resize + close + z-order.** |
| 2026-06-22 | perf (layout) | ✅ SHIPPED (safe half). `layoutVertical`/`layoutHorizontal` now measure each child ONCE (was up to 3× — fixed-size/flex/cross-axis — and `measure()` recurses). Identical output (locked by `UILayoutUnit` + layout E2Es). **Deliberate follow-on:** the bigger win = skip layout on static frames (a dirty-gate) — correctness-sensitive (a missed invalidation = stale layout) and must coexist with per-frame animations, so NOT rushed. See §8. |
| 2026-06-22 | 5c Tabs | ✅ SHIPPED. `UITabs` — tab bar of N buttons + N pages (children), one shown at a time (others hidden + purged), content clipped, opaque. Click a tab → switch active page + `ui:tab:changed {id,index}`. `UITree` `"tabs"` factory (`tabs:[{label}]`). Locked by `IT_029` (tab click flips the active page). Reuses slice-2 clip + the window/opaque pattern. |
| 2026-06-22 | 5b Drawers | ✅ SHIPPED. `UIDrawer` — edge-docked (`left`/`right`/`top`/`bottom`) collapsible panel that **slides** in/out (smoothstep over `slideDuration`), fills the viewport along the edge, content clipped, opaque on screen, purged when fully closed. Toggled via `ui:drawer:toggle`/`ui:drawer:set {id,open}`. Locked by `IT_030` (closed=unreachable → toggle→sliding→open=reachable → close→unreachable). NB: brings a self-contained per-frame slide animation (the tween foundation, slice 4, lands here). |
| 2026-06-22 | 5a Modal | ✅ SHIPPED. `UIModal` — centered dialog over a full-screen dim that **traps** input (absorbs every click outside the dialog → nothing behind reachable); content clipped to the dialog. `ui:modal:open` (raises it) / `ui:modal:close`; a click on the dim closes it (`ui:modal:closed`). Locked by `IT_031` (closed→bg clickable; open→bg trapped + dialog clickable; outside-click closes; bg reachable again). |
| 2026-06-22 | 5e List/Sidebar MVP | ✅ SHIPPED (MVP). `UIList` — the **ship sidebar**: data-driven (`items[{id,label,subtitle?,icon?}]`) scrollable/clipped/selectable list. Self-managed retained row-id pool (NOT child widgets → sidesteps the UIScrollPanel re-layout-descroll limitation), wheel scroll (routed like scrollpanel), opaque hit-test absorb, scroll-aware `rowAt()`, single-select → `ui:list:selected {id,index,itemId}`. Runtime `ui:list:set_items` + `ui:list:select`. Locked by `IT_033` (select / scroll+clip click-flip / runtime repopulate) + `UIListUnit`. **Surfaced a real engine fact: IIO transports only a node's JSON (`m_data`) — array payloads must be json-backed, not `setChild`-assembled** (UI_TOPICS §note + handoff §7). |
| 2026-06-22 | FULL demo — "Fleet Command" | ✅ SHIPPED. One screen composing the ENTIRE framework + JSON binding engine, described in `assets/ui/demo_fleet_command.json`: HUD (anchored, bound `{{credits}}`/`{{turn}}` + buttons), a **virtualized templated data-bound fleet list** (per-row HP bar + select event), an in-app **window** (drag/resize/close), **tabs**, a **drawer**, a **modal**, the action **radial**, **`if`** conditionals, declarative **events**, and **reactivity** (a "game" host pushes `ui:data`/`:merge` — incl. live hull-wear churn). Visual runner `tests/visual/test_ui_full_demo.cpp` (run from project root: `./build/tests/test_ui_full_demo`). **Headless verification: `IT_043`** drives the same layout (binding renders / row click → `fleet:select` / select→merge updates the detail + flips the `if` / turn reactivity / drawer event) — the demo is exactly what's tested. |
| 2026-06-22 | Perf: template-list idle gate | ✅ SHIPPED. `updateTemplateLists` skips a virtualized list's re-window + re-resolve when no input changed (scroll / data version / list geometry) — `UIList::windowDirty(version)` latches a signature; `UIModule` bumps `m_dataVersion` per push. Idle frames now do zero data-driven work per list. Locked by `IT_042` (`templateWindowOps` health counter: unchanged on idle, +1 on scroll/data) + `IT_041` (behaviour). **Honest:** at ~40-row scale this is hygiene/headroom; the bigger perf items (dirty-gated layout, fine-grained re-resolve, keyed reconciliation) are DEFERRED as premature + correctness-sensitive — see `ui-binding.md`. |
| 2026-06-22 | JSON-UI virtualized template list (step 6) | ✅ SHIPPED — **the binding engine is COMPLETE**. A `list` with `"repeat":"{{fleet}}"` + a widget-subtree `"template"` renders ONLY the visible rows as POOLED template instances (the list's children), recycled on scroll. `UIModule::updateTemplateLists` windows the data array each frame to a viewport-bounded pool, maps each slot→item (sets `scopePath`+y), resolves it, hides+purges the rest; the list provides viewport/scroll/scrollbar/clip + renders its pooled children (`UIList::renderTemplate`); `expandRepeaters` skips lists. Rows are real widgets → per-item binding & events free; the de-scroll trap is avoided (list owns positioning, Absolute instances). Locked by `IT_041` (100-ship fleet → ~6 rows instantiated not 100 / per-item click → its id / scroll re-binds the pool: far rows render, top click picks the scrolled-to item). Plain/grouped list modes still work without a template. Engine done — see `ui-binding.md`. |
| 2026-06-22 | JSON-UI conditional `if` (step 5) | ✅ SHIPPED. `"if":"{{flag}}"` on any widget → renders only while the bound bool is true; when false the subtree is hidden AND its retained entries **released** (`render:*:remove` — no ghost, unlike a plain `visible` binding) + the hidden subtree skipped. Evaluated against the widget's scope (works per-item in a repeater). No negation (no expression language). Locked by `IT_040` (show→render / hide→purge / re-show→re-register). Next: step 6 (list = virtualized repeater). |
| 2026-06-22 | JSON-UI repeater (step 4) | ✅ SHIPPED — the big unlock. A host with `"repeat":"{{fleet}}"` + `"template":{...}` instantiates the template per data-array element (`UIModule::expandRepeaters`, re-parsed via the now-public `UITree::parseWidget`). Each widget carries a `scopePath` (`"fleet.0"`) → bindings AND declarative events resolve against the **item** (the in-row routing, now general + free). Re-expands on every data push (instantiate-all; virtualization = step 6); rows stack by index. Also: dispatch surfaces a button with a declarative `on:click` (no legacy `onClick` needed). Locked by `IT_039` (per-item label binding + per-item button event carries its `{{id}}` + re-expansion). Deferred: nested repeaters. Next: `if` (5) then list-as-virtualized-repeater (6). |
| 2026-06-22 | JSON-UI reactivity (step 3) | ✅ SHIPPED ("faut être solide"). Robust partial data updates so the game never re-sends the whole model: **`ui:data`** (replace) + **`ui:data:set {path,value}`** (deep path set via the pure tested `uibind::setAtPath` — creates intermediate objects, descends/extends arrays) + **`ui:data:merge {<partial>}`** (nlohmann `merge_patch`, RFC 7386; null deletes). Each re-resolves all bindings + preserves the untouched rest. Locked by `UIBindingUnit` (setAtPath edges) + `IT_038` (set / merge / nested-merge each update one field). Versioned re-resolve = deferred perf follow-on. Next: repeater (step 4). |
| 2026-06-22 | JSON-UI binding-in + events-out (step 2) | ✅ SHIPPED. The two peers on the socle, wired to widgets. **Binding-in**: `UITree::parseWidgetBindings` records any `{{}}`-containing prop + the `on` block at parse; `UIModule::resolveAllBindings` applies them via `UIWidget::applyBoundProp` (base visible/x/y/w/h; `UILabel` text; `UIProgressBar` value). **Events-out**: `fireWidgetEvent` publishes a widget's declared `on:click` event with `{{}}`-resolved args (generalises `onClick→ui:action`). The game pushes the model via **`ui:data {<model>}`** (whole payload = root context → re-resolve). Locked by `IT_037` (push data → label renders bound value on `render:text:*`; click → declared event fires with bound args). Next: repeater (step 4). Doc: `ui-binding.md`. |
| 2026-06-22 | JSON-UI binding socle (step 1) | ✅ SHIPPED. Start of the **templating/data-binding engine** ("UI = JSON data-driven", decided with Alexi — design + roadmap in `docs/design/ui-binding.md`). Step 1 = the shared **`{{}}` resolver + scope-chain** (`Core/UIBinding.{h,cpp}`, `grove::uibind`): `Scope` (data + parent), `resolvePath` (objects + array indices, `$root.`/`$parent.` prefixes), `interpolate` (string props), `resolveNumber`/`resolveBool` (typed props), `hasBindings`/`leafToString`. Pure, no widget/IIO coupling — the foundation BOTH data-binding (in) and the event system (out) sit on. Guardrail: **no expression language** (paths + interpolation + `if` only; logic stays game-side). Locked by `UIBindingUnit`. Next: step 2 (binding-in + events-out wired to widgets, E2E). |
| 2026-06-22 | Input-capture (anti-click-through) | ✅ SHIPPED. `UIModule` publishes **`ui:capture {mouse, keyboard}`** (on change) so the game skips WORLD input under the UI / during a UI drag (no click-through to camera/world). `mouse` = pointer over an interactive widget (hitTest absorbs) OR an active grab (a press that landed on the UI, held until release — so a drag that leaves the widget still captures). `keyboard` = a widget is focused. The `WantCaptureMouse` pattern; general (covers every widget, not just the list). Locked by `IT_036` (over-UI=true / over-empty=false / off-widget-grab persists / release=false). Wired off the existing `hitTest` + a `m_pointerGrabbed` latch in `updateUI`. |
| 2026-06-22 | List scrollbar + drag | ✅ SHIPPED. `UIList` gains a **visual scrollbar** (track + thumb, shown only when content overflows) + **drag-to-scroll**: drag the **thumb** (proportional) or the **content** (1:1 grab-and-pull, past a `dragThreshold` so it disambiguates from a click). Selection moved to **release**, suppressed if the press became a scroll-drag (a scrollbar-column press never selects). Scrollbar render ids registered with the bg in `ensurePool`; drag state driven in `update()` (runs after the click dispatch, so release-select reads the accumulated drag flag). Locked by `IT_035` (content-drag + thumb-drag click-flips + plain-click-still-selects) + `UIListUnit` (thumb geometry). IT_033/IT_034 stay green (a click = press+release at one spot → still selects on release). Style: `scrollbarColor`/`scrollbarTrackColor`/`scrollbarWidth`. |
| 2026-06-22 | warship GROUPS | ✅ SHIPPED. `UIList` gains **collapsible groups** (warship wings) — the engine SYSTEM; Drifterra builds the final UI. Data is FLAT (`setItems`) or GROUPED (`setGroups`: `groups:[{id,label,collapsed?,items:[…]}]`); both project onto a flat `ListRow` (header\|item) sequence (`rebuildRows`) so virtualization/scroll/clip/hit-test are unchanged. A header click folds/unfolds (`ui:list:group:toggled {id,groupId,collapsed}`); an item click selects with its `groupId` (`ui:list:selected` += `groupId`). Selection follows its item across collapses (tracked by itemId). `ui:list:set_groups` runtime. Locked by `IT_034` (select-with-group / expand click-flip / collapse-hides) + `UIListUnit` (parseGroups + grouped projection + toggle). **Caught + fixed a use-after-free**: the header-click handler passed `r->groupId` (a pointer INTO m_rows) to `toggleGroup`, which rebuilds m_rows → dangling; the toggled event carried an empty groupId. Fix: copy the id before toggling. IT_034 locks it (it failed on exactly that symptom). |
| 2026-06-22 | 5e Virtualization | ✅ SHIPPED. `UIList` now registers render entries for the **on-screen window only** — a recycled, viewport-bounded id-pool (`ensurePool`, grow-only) remapped to items `[first .. first+count)` each frame via `visibleRange()`; slots past the window are hidden (no ghosts). A 1000-item list registers ~20 entries, not ~4000 → O(visible)/frame, not O(N). Repopulate no longer needs a pool release (slots are rewritten/hidden). Locked by `UIListUnit` (the `entryCount() < 60` bound on a 1000-item render — RED-first against the O(N) version — + `visibleRange` deep-scroll window) + `IT_033` regression (pool-remap-under-scroll proven through the full dispatch path). Remaining list follow-ons: visual scrollbar + drag-to-scroll, row templates, multi-select, grid mode. |
