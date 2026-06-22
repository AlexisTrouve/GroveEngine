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

### Missing (the real work — identical whichever path)
- **Anchoring** (anchor to parent edges/corners + offset) and **reflow on resize** (layout runs once
  at load today).
- **Grid** layout mode.
- **Clipping / scissor** — `UIScrollPanel.cpp:33` has a TODO; children currently overflow their
  bounds. **Foundational** — windows, lists, drawers, tabs all need it.
- **Dynamic z-order** — order is implicit tree-traversal order; no `bringToFront`/focus-stack.
- **Window** container (drag/resize/close/title/stack).
- **Docking / drawers** (4 edges, collapsible, slide).
- **Tabs**, **Tree/menu-hierarchy**, **List/Grid view (virtualized)**.
- **Modal/dialog** (dim + focus-trap + button bar).
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
| **1** | **Layout**: reflow-on-resize · anchoring · grid | — | oracle + E2E resize | sectioned menus that reflow, HUD anchored to corners |
| **2** | **Clipping/scissor** (`render:scissor` + UI honors it) | renderer | GPU readback + E2E | prerequisite for windows/lists/drawers |
| **3a** | **Z-order / focus-stack** (`bringToFront`, top-most capture) | — | E2E (back window → front) | display order |
| **3b** | **Window** (titlebar, drag, resize, close, **stackable**) | 2 + 3a | E2E (drag/resize/close/raise) | windows, stackable frames/sub-frames |
| **4** | **Tween** (slide/fade/scale via Easing) + **group opacity** | — | oracle (curve) + E2E | HUD alpha, basis for all transitions |
| **5a** | **Modal/Dialog** (dim + focus-trap + button bar) | 3a + 4 | E2E | dialogs |
| **5b** | **Drawers** 4 edges (collapsible, slide) | 1 + 2 + 4 | E2E | hidden menus top/bottom/left/right |
| **5c** | **Tabs / sections** | 2 | E2E | menu with sections |
| **5d** | **Tree / menu-hierarchy** (expand/collapse) | 2 | E2E | menu hierarchy |
| **5e** | **List/Grid view virtualized** (repeater + virtual scroll) | 1 + 2 | E2E + perf | partial ship sidebar |
| **6a** | **Animated panel** (hosts `grove::anim`/flipbook) | — | E2E (frame advances) | animated 2D scene |
| **6b** | **Audio/voice/radio player** (buttons + playlist + progress → `sound:*`) | — | E2E | voice, sound replay, music/radio player |
| **6c** | **Video panel** (image-sequence first, codec later) | 6a | E2E | video scene |
| **7** | **VN/Cutscene runtime** (data-driven Scene/Dialogue module) | 5a+6a+6b | E2E (script→choice→branch) | 2D image/animated/video scene **with choices** |

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
- **1.2 Anchoring** — `anchor` (9 anchors + `StretchH`/`StretchV`/`Fill`) + offset, resolved against
  the parent rect. Pure `resolveAnchor(parentRect, size, anchor, offset) → pos`.
  **Proof:** oracle per anchor + E2E (a bottom-right-anchored widget stays bottom-right after resize).
- **1.3 Grid** — `LayoutMode::Grid` (columns, gap, cell sizing). Pure
  `gridCellRect(index, cols, cellW, cellH, gap) → rect`.
  **Proof:** oracle + E2E (a button grid positioned correctly).

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
| 2026-06-22 | — | Design locked (this doc). Path A chosen. |
| 2026-06-22 | 1.1 reflow-on-resize | ✅ SHIPPED. `ui:resize` → `relayoutRoot()` + `widthPercent`/`heightPercent` relative sizing. Locked by `UILayoutUnit` + `IT_021`. Anchoring (1.2) + grid (1.3) still pending. |
