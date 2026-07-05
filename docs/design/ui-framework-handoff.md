# UI Framework — Session Handoff (2026-06-22)

> **Purpose:** resume the UI framework build-out in a FRESH session with zero prior chat context.
> Read this top-to-bottom once, then **[ui-framework.md](ui-framework.md)** for the plan + status log.
> This doc is the tribal knowledge (patterns, gotchas, recipes) that isn't obvious from the code.

---

## 0. Re-entry checklist (do this first)

1. Read this doc + `docs/design/ui-framework.md` (plan, slice catalog, status log, §8 risks).
2. `git log --oneline -20` — confirm you're at/after `139e1bd` (perf measure-cache). Remote = **gitea**.
3. Build + run the UI suite to confirm green baseline (commands in §4). Expect **25 UI tests pass**.
4. Pick the next slice from §8. Each slice = **red test first → impl → green → commit** (TDD, non-negotiable per the project doctrine: "une UI non testée E2E n'existe pas").
5. The repo is clean and pushed; the only uncommitted file is `Testing/Temporary/CTestCostData.txt` (a ctest junk artifact — ignore it, never stage it).

---

## 1. What this work is

Turning the flat retained-mode `UIModule` into a **full game-UI framework** for the Drifterra game —
**by hand** (path A: extend the custom IIO-based UIModule, NOT adopt RmlUi), to preserve GroveEngine's
coherence (everything via IIO topics, E2E-tested, engine/game separation). Decision + rationale in
ui-framework.md §1. **In-app windows, NOT OS windows** (multi-OS-window is a separate deferred backlog item).

---

## 2. What shipped this session (commits `9497534..139e1bd`, all on gitea)

The UIModule went from a flat widget set to a capable framework. Slices, each TDD/GPU-proven, regression green:

| Area | Shipped | Locked by |
|---|---|---|
| **Layout** | reflow-on-resize (`ui:resize`), relative `%` sizing, anchoring (9 anchors), grid mode | `UILayoutUnit` + IT_021/022/023 |
| **Clipping** | sprite scissor + text scissor (GPU), UIRenderer clip-stack, SceneCollector parse, hit-test clip | `SpriteClipGpu`/`TextClipGpu` [gpu] + IT_024/025 |
| **Z-order** | `UIWidget::bringToFront()` (sibling-order model) | `UIZOrderUnit` |
| **Window** (in-app) | chrome (titlebar+close), opaque, clipped content, raise/drag/close/**resize** | IT_026/027/028/032 |
| **Tabs** | sectioned container, page switching, `ui:tab:changed` | IT_029 |
| **Drawers** | edge-docked (4 edges) sliding collapsible panel, `ui:drawer:*` | IT_030 |
| **Modal** | centered dialog + dim focus-trap, `ui:modal:*` | IT_031 |
| **List/Sidebar** | data-driven ship list — wheel + **scrollbar/drag-scroll** / clip / single-select / **virtualized** / **grouped** (collapsible wings) / **virtualized TEMPLATE rows**, `ui:list:*` | IT_033/34/35/41/42 + `UIListUnit` |
| **Input-capture** | `ui:capture {mouse,keyboard}` — anti-click-through (game skips world input under UI / during a UI grab) | IT_036 |
| **JSON binding engine** | `grove::uibind` `{{path}}` resolver + scope-chain · binding-in (`applyBoundProp`) · declarative events-out (`on`) · reactivity (`ui:data`/`:set`/`:merge`, `setAtPath`) · repeater (`repeat`+`template`, per-item scope) · `if` · virtualized template list. NO expression language. Design: `docs/design/ui-binding.md` | `UIBindingUnit` + IT_037→042 |
| **Full demo** | "Fleet Command" — the whole stack in one JSON screen (`assets/ui/demo_fleet_command.json`); visual `tests/visual/test_ui_full_demo.cpp` (run from repo root) | IT_043 |
| **Perf** | flow layout measures each child 1× (was 3×); virtualized-list idle gate (no re-window on idle frames) | `UILayoutUnit` + IT_042 |

New widgets: `UIWindow`, `UITabs`, `UIDrawer`, `UIModal`, `UIList` (+ the layout/clip/z-order/binding extensions to the base + `grove::uibind`).

---

## 3. Architecture you MUST know (the patterns)

These are the conventions every NEW container widget follows. Internalize them before adding one.

### 3.1 The "container" pattern (window / tabs / drawer / modal all do this)
A container that holds content children and clips them:
- **`clipsHitTest() override { return true; }`** + **`hitClipRect(x,y,w,h) override`** → returns the
  content rect (the area children live in). The slice-2b hit-test gate (`UIContext.cpp::hitTest`) skips
  descending into children when the cursor is outside this rect (so clipped-away children aren't clickable).
- **Opaque absorb:** add an `else if (type == "yourwidget")` case in `hitTest()` that returns the widget
  when `pointInBounds()` — so a click on its chrome/empty area is ABSORBED, never leaking to widgets behind.
- **`render()`:** draw chrome, then `renderer.pushClip(contentRect)` → `renderChildren(renderer)` → `popClip()`.
  The clip rect is published with every child render entry; the renderer turns it into a `bgfx::setScissor`.
- **Content positioning in `update()` (NOT computeAbsolutePosition):** set each child's `absX/absY` to the
  CONTENT origin + child's relative x/y, then recurse `grandChild->computeAbsolutePosition()`, then
  `child->update()`. This mirrors `UIScrollPanel`. Why: `computeAbsolutePosition()` only knows
  parent.absX + child.x — it doesn't know about the titlebar offset / scroll offset / centered dialog, so
  it gives the wrong position. The per-frame `update()` corrects it before `render()`.
  - Consequence: at LOAD, `relayoutRoot()` runs `computeAbsolutePosition()` which positions content children
    WRONG (un-offset) for one frame; the first `update()` fixes it before the first `render()`. Fine in practice.
- **`releaseRenderEntries(renderer) override`:** unregister your EXTRA render entry ids (titlebar, close,
  per-tab, grip…) then call `UIWidget::releaseRenderEntries(renderer)` (drops `m_renderId` + recurses).
  This is the ghost-rect fix — a hidden widget must purge its retained entries.

### 3.2 Interaction lives in UIModule, centralized (not in the widget)
Window drag/resize/close, tab switch, modal close, drawer toggle are driven by **UIModule**, not the widget:
- **Why centralized:** two overlapping windows would otherwise fight over a drag; the topmost-under-cursor
  must be resolved ONCE. See `UIModule::handleWindowInteraction()`.
- **Why before the child-update pass:** `bringToFront()` and close mutate `root->children`; doing that
  during the `m_root->update()` iteration would invalidate it. So `handleWindowInteraction()` runs in
  `updateUI()` AFTER the click dispatch but BEFORE `m_root->update()`. Topic handlers (open/close/toggle)
  run in `processInput()` (also before the update pass) → safe to `bringToFront` there too.
- **The click path:** `dispatchMouseButton` (UIContext) hit-tests and, for an interactive widget, returns
  it; UIModule's `updateUI()` then has a `if (widgetType == "...")` branch that publishes the event /
  switches state. For widgets with no per-press handler but that need UIModule to act (tabs, modal), add a
  `if (pressed) return target;` case in `dispatchMouseButton` so `clickedWidget` is set.
- **Drag pattern:** a press grabs (store `m_draggingXId` + offset); each frame while `mouseDown`, move;
  on `!mouseDown`, clear. Mirrors the slider drag (`m_draggingSliderId`).

### 3.3 Clipping mechanism (slice 2)
- A clip rect rides in **`SpriteInstance.reserved[]`** (i_data3 — uploaded to GPU but IGNORED by the sprite
  shader; `SpritePass` reads it CPU-side → `bgfx::setScissor` per batch). For text it's `TextCommand.clip*`.
  **No GPU layout change, no shader change.** Non-clipped sprites (`reserved[2]<=0`) keep the same batching
  → perf-neutral. `SceneCollector` parses an optional `clip{X,Y,W,H}` from `render:sprite/text` into those.
- `UIRenderer` has a **clip stack** (`pushClip/popClip/currentClip`, cleared each `beginFrame`); retained
  entries capture the active clip and re-publish on change.

### 3.4 Other key facts
- **z-order = sibling order.** Last child renders on top (highest layers via `nextLayer()`), reverse-order
  hit-test finds it first. `bringToFront()` = move to the back of `parent->children`.
- **Retained render:** widgets `registerEntry()` once, `updateRect/Sprite/Text` only re-publish on change
  (dirty compare). UI draws on bgfx **view 0/1** which are set to `ViewMode::Sequential`.
- **Layout:** `UIPanel::update` runs `UILayout::measure/layout/computeAbsolutePosition` every frame for
  non-Absolute panels. The new container widgets override `update()` and do their own positioning (cheap),
  they do NOT use `UILayout`.
- **Input-capture / anti-click-through:** `UIModule` publishes `ui:capture {mouse, keyboard}` (on change,
  in `publishCaptureState`). `mouse` = `hitTest(cursor) != null` (the UI absorbs there) OR `m_pointerGrabbed`
  (a press landed on the UI and is still held — capture persists for the whole drag, even off-widget). The
  GAME latches this and skips world input (camera/world clicks/shortcuts) while captured. This is the
  `WantCaptureMouse` pattern; it's general (every absorbing widget), so any new interactive widget gets
  click-through protection for free as long as its `hitTest` returns itself. Locked by `IT_036`.

---

## 4. Build, test, prove

```bash
# From the repo root. Build dir already configured (Ninja, GROVE_BUILD_UI_MODULE=ON, BGFX ON).
cmake -B build                       # reconfigure ONLY when you add a new test/target to CMakeLists
cmake --build build --target UIModule <test_target>   # build the dll + your test

# Run a test (use --test-dir; do NOT cd):
ctest --test-dir build -R "UIWindowE2E" --output-on-failure
# Full UI regression (the green baseline = 23 tests):
ctest --test-dir build -R "UI|Radial|InputUI" --output-on-failure
```

- **E2E tests** load `../modules/libUIModule.so` (`.dll` on Windows) at runtime via `ModuleLoader`; they
  drive it with raw IIO messages (`input:mouse:*`, `ui:*`) and assert published `ui:*` events. **They are
  headless** (no rendering) — so they test hit-test/logic, NOT pixels. The pattern: build a fixture JSON,
  `process()` to pump frames, click, assert the action. The **click-flip** idiom (same click hits A before /
  B after a change) is the workhorse; see IT_021/027/029.
- **GPU tests** (`[gpu]`, `SpriteClipGpu`/`TextClipGpu`/`TilemapLodGpu`) DO run here (real bgfx, ~0.5s each) —
  they render offscreen + read back pixels. They need `SDL2.dll` next to the exe (a CMake `foreach` copies
  it — add your new gpu test to that list). Used to prove RENDERING (clipping, LOD) objectively.
- **Unit oracles** (`UILayoutUnit`, `UIZOrderUnit`, `RadialMathUnit`) link `UIModule_static` (or are
  header-only) and assert pure geometry. Fast.
- **TDD:** write the test first, build, SEE IT RED (for a behavior change), implement, GREEN. For new
  isolated widgets the test is green-on-first-build but constructed to be decisive (can't pass without the
  feature). Always run the FULL UI regression before committing.

---

## 5. Git & push (IMPORTANT — there's a gotcha)

- The canonical remote is **gitea** (`StillHammer/groveengine`). The configured `gitea` remote URL is **SSH**
  (`git@git.etheryale.com`) and **port 22 is UNREACHABLE** from here (it's behind Tailscale/etheryale infra).
- **Push via HTTPS + the Clash proxy** (auth comes from Git Credential Manager / Windows vault, no token in URL):
  ```bash
  git -c http.proxy=http://127.0.0.1:7897 push https://git.etheryale.com/StillHammer/groveengine.git master:master
  git update-ref refs/remotes/gitea/master refs/heads/master   # sync the local tracking ref after
  ```
  (If the proxy fails: check Clash is running on Windows, port 7897. Details: `documentationGlobal/git-auth.md`.)
- **No force push. Commit on master** (the repo's whole history is on master; no feature-branch workflow here).
- Commit messages MUST end with: `Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>`.
- The LF↔CRLF warnings on commit are harmless (git normalizes per .gitattributes).

---

## 6. Recipe: add a new container widget (≈ what window/tabs/drawer/modal did)

1. `modules/UIModule/Widgets/UIYourThing.{h,cpp}` — subclass `UIWidget`. Implement `update`, `render`,
   `getType`, `clipsHitTest`, `hitClipRect`, `releaseRenderEntries`, `pointInBounds`, geometry helpers.
   Follow §3.1 (content in update, pushClip in render, purge extras).
2. `modules/UIModule/Core/UIContext.cpp` — `#include` it; add the `else if (type=="yourthing")` **absorb**
   case in `hitTest()`; if UIModule must act on a press, add a `if (pressed) return target;` case in
   `dispatchMouseButton`.
3. `modules/UIModule/Core/UITree.cpp` — `#include` it; add a `registerWidget("yourthing", ...)` factory
   (parse props + style). **Arrays** are iterated by numeric index over OBJECTS: `getChildReadOnly("0")`
   then read fields (string arrays don't work — use `[{...}]`, like radial `items[]` / tabs `tabs[]`).
4. `modules/UIModule/UIModule.cpp` — `#include` it; subscribe any `ui:yourthing:*` topics (in
   `setConfiguration`'s `if (m_io)` block); add a click branch / interaction (in `updateUI` or a centralized
   helper like `handleWindowInteraction`, per §3.2).
5. `modules/UIModule/CMakeLists.txt` — add `Widgets/UIYourThing.cpp` to BOTH `add_library` blocks
   (`UIModule` shared + `UIModule_static`) — there's a `replace_all` of the source list.
6. `assets/ui/test_e2e_yourthing.json` fixture + `tests/integration/IT_0XX_*.cpp` E2E + register it in
   `tests/CMakeLists.txt` (mirror the IT_029 block). Build, run RED→GREEN, full UI regression.
7. Docs: `docs/UI_TOPICS.md` (new topics) + `docs/UI_WIDGETS.md` (widget props) + `ui-framework.md` status row.

---

## 7. Gotchas / pitfalls (hard-won this session)

- **`parseCommonProperties` runs AFTER your factory** and sets `visible = getBool("visible", true)`. So a
  widget that should default HIDDEN (modal) can't rely on the factory — set `"visible": false` in the JSON.
- **Root-level scalar `"flex": N` on a child is SILENTLY DROPPED** — `UITree` gates it on `hasChild("flex")`,
  which returns false for scalars (only objects/arrays count; `JsonDataNode.cpp:121`). Use
  `"layout": { "flex": N }` (parsed via `getDouble`). Flag-and-don't-rely-on the root-level form.
- **IIO messages transport ONLY a node's JSON (`m_data`), NOT child nodes built via `setChild()`.**
  `IntraIO::publish` does `jsonDataCopy = node->getJsonData()` (m_data only) and the manager re-wraps that
  json on delivery — so a payload with a nested array (e.g. `ui:list:set_items`'s `items[]`) must carry the
  array IN the json: build via `JsonDataNode(name, json)` with the array in the `json`, like a layout file.
  A `setChild`-assembled array (data in `m_children`, empty `m_data`) arrives EMPTY. Cost me a red on IT_033's
  repopulate step. Scalar payloads (`{id, index}`, `{count}`) are unaffected. (`src/IntraIO.cpp:74`.)
- **`computeAbsolutePosition()` doesn't know about content offsets** (titlebar/scroll/dialog-centering). Set
  content children's `absX/absY` manually in `update()` (§3.1). The one-load-frame mismatch is harmless.
- **Mutating `root->children` (bringToFront/close) must happen OUTSIDE the child-update iteration** — do it in
  `handleWindowInteraction()` (before `m_root->update()`) or in a topic handler (in `processInput`). §3.2.
- **A `const ListRow*` / any pointer INTO a widget's internal vector dangles after a mutator that rebuilds it.**
  `UIList::toggleGroup()` rebuilds `m_rows`, so the header-click handler must COPY `r->groupId` to a local
  BEFORE calling `toggleGroup` — else the published event reads freed memory (cost an IT_034 red: the
  `toggled` event carried an empty groupId while the toggle itself worked). Snapshot row fields before any
  list mutator. Same shape lurks anywhere you hold a pointer across a `setItems`/`setGroups`/`toggleGroup`.
- **`SpriteInstance.reserved[]` is now the clip rect** (CPU-read). Don't repurpose it for the sprite shader
  without coordinating the clipping path.
- **Editing a `.sc` shader does nothing** until you regen its `.bin.h` by hand (`shaderc --bin2c`) — the build
  doesn't auto-compile shaders. Often avoidable by reusing the `debug`/`color` program. (memory: `shader-bin-h-regen`.)
- **GPU tests** silently `WARN`-skip without a GPU/SDL; here they RUN, so a green is real. Always add a new gpu
  test to the `SDL2.dll`-copy `foreach` in `tests/CMakeLists.txt` or it fails to load under ctest (0xc0000135).
- **Showcase cwd trap** (`test_renderer_showcase` must run from `build/`; exe locked while its window is open) —
  memory `showcase-cwd-trap`.

---

## 8. What's left (pick from here)

From Alexi's original ask, still to build (all sit on the now-complete foundation):

- **JSON-UI templating & data-binding engine — 🚧 ACTIVE THRUST** (decided with Alexi, the "UI = JSON
  data-driven" vision). Design + roadmap: **`docs/design/ui-binding.md`**. ✅ **Steps 1-2 SHIPPED** — (1) the
  shared `{{}}` resolver + scope-chain (`Core/UIBinding.{h,cpp}`, `grove::uibind`, `UIBindingUnit`); (2)
  binding-IN (props ← `{{path}}`, recorded at parse by `UITree::parseWidgetBindings`, applied via
  `UIWidget::applyBoundProp`) + events-OUT (declarative `on` events, `fireWidgetEvent`) + `ui:data {<model>}`
  (`IT_037`); (3) robust reactivity — `ui:data` (replace) / `ui:data:set {path,value}` (deep set via
  `uibind::setAtPath`) / `ui:data:merge` (RFC 7386), each re-resolves + preserves the rest (`IT_038`); (4)
  **repeater** — `"repeat":"{{path}}"`+`"template"` on any host instantiates the template per item, each
  widget carrying a `scopePath` so bindings AND events resolve per-item (`expandRepeaters`, `IT_039`); (5)
  **`if`** — `"if":"{{flag}}"` renders only while the bound bool is true, hiding + RELEASING the subtree's
  retained entries when false (`IT_040`); (6) **virtualized template list** — a `list` with `"repeat"`+
  `"template"` renders only the visible rows as a pooled, scroll-recycled set of template instances
  (`UIModule::updateTemplateLists`, `UIList::renderTemplate`), per-item binding & events, scales to thousands
  (`IT_041`). **🎉 THE ENGINE IS COMPLETE** (all 6 steps). Remaining = non-blocking follow-ons: versioned/
  per-binding re-resolve (perf), nested repeaters, flexible instance layout, keyed reconciliation. **Hard guardrail: no expression language** — paths + interpolation + boolean `if` only; logic stays
  game-side. This SUBSUMES the list's "custom row templates" follow-on (a row template = a JSON widget subtree
  repeated + bound). Sharp edges still ahead: typed-prop set per widget (partly done — extend `applyBoundProp`);
  repeater × virtualization (step 6, the UIScrollPanel de-scroll trap).

- **List / Grid view — the ship sidebar** (his marquee). ✅ **SHIPPED + VIRTUALIZED + GROUPED** — `UIList`
  (`Widgets/UIList.{h,cpp}`): data-driven, wheel-scroll, clipped, single-select → `ui:list:selected`,
  runtime `ui:list:set_items` / `ui:list:set_groups` / `ui:list:select`. **Virtualized** — recycled
  viewport-bounded id-pool (`ensurePool` + `visibleRange()`); 1000 rows → ~20 entries. **Grouped** — FLAT
  (`setItems`) or GROUPED warship wings (`setGroups`, collapsible headers); both project onto a flat
  `ListRow` (header\|item) sequence (`rebuildRows`) so everything downstream is group-agnostic; header click
  → `ui:list:group:toggled`, item carries its `groupId`. Locked by `IT_033` + `IT_034` + `UIListUnit`.
  **Scroll**: wheel + visual scrollbar (draggable thumb) + content drag-to-scroll (threshold; select moved
  to release, suppressed on drag). **What's LEFT on it** (follow-ons): **custom row templates** (today fixed
  icon+label+subtitle), **multi-select**, **grid mode**, **multi-level tree** (today one level: groups → items).
- **Tree / menu-hierarchy** (5d) — expand/collapse nodes. Medium.
- **Rich content** (6): **animated panel** — ✅ SHIPPED (`UIFlipbook`, slice 6a: hosts `grove::anim` SpriteSheet+
  Flipbook; closed the renderer's animated-UV gap — `updateSpriteUV` + UV in change-detection; locked by `IT_054`);
  **audio/voice/radio player** — ✅ SHIPPED (slice 6b): the engine now publishes `sound:music:position
  {path,elapsed,duration}` (backend-owned clock — `ISoundBackend` getMusicPosition/Duration, SDL_mixer 2.6+ /
  Mock; locked by `SoundManagerUnit [position]`) + a radio-player UI composed from existing widgets (Play/Stop →
  `sound:*` via declarative `on:click`, bound now-playing/progress; locked by `IT_055`); **video**
  (heavy/isolated — image-sequence first, codec later; last).
- **VN / cutscene runtime** (7) — **decided: engine-side full runtime** (a `Scene`/`Dialogue` module reading a
  data-driven script: nodes/choices/branches/voice/video). Sits on top of the content widgets. **Big**; fresh session.
- **Perf — dirty-gated layout** — the bigger perf win (skip layout on static frames). **Correctness-sensitive**
  (a missed invalidation = stale layout) and must not freeze per-frame animations. Design notes in
  ui-framework.md §8. Do it deliberately, with a test that asserts layout STILL updates on every relevant change.

Done & not re-listed: layout (1), clipping (2), z-order + in-app Window incl. resize (3), Tabs (5c),
Drawers (5b), Modal (5a), perf measure-cache.

Also still open (housekeeping, never done): **register this plan in ProjectMind** (`createPlan` + `planToTasks`,
park the stale "ThreadedModuleSystem Phase 2" plan). The repo + `ui-framework.md` are the source of truth meanwhile.

---

## 9. File map

- **Plan / status:** `docs/design/ui-framework.md` (slice catalog + dated status log + §8 risks).
- **Topics / widget props:** `docs/UI_TOPICS.md`, `docs/UI_WIDGETS.md`.
- **Core:** `modules/UIModule/Core/` — `UIWidget.h` (base: hierarchy, clip/zorder virtuals, bringToFront),
  `UILayout.{h,cpp}` (flow/grid + percent + anchor + measure-cache), `UIContext.cpp` (hit-test + dispatch +
  per-type absorb cases), `UITree.cpp` (JSON factories), `UIModule.{h,cpp}` (topics, interaction, window/modal
  handling).
- **Widgets:** `modules/UIModule/Widgets/` — `UIWindow`, `UITabs`, `UIDrawer`, `UIModal`, `UIList`
  (data-driven ship sidebar), `UIScrollPanel` (the clip/scroll reference), `UIRadial`, plus the basic ones.
- **Rendering:** `modules/UIModule/Rendering/UIRenderer.{h,cpp}` (retained publish + clip stack).
- **Renderer side of clipping:** `modules/BgfxRenderer/Frame/FramePacket.h` (SpriteInstance.reserved =
  clip; TextCommand.clip), `Passes/SpritePass.cpp` + `Passes/TextPass.cpp` (setScissor), `Scene/SceneCollector.cpp`
  (parse clip).
- **Tests:** `tests/integration/IT_0{16..32}_*.cpp` (E2E), `tests/unit/test_ui_layout.cpp` + `test_ui_zorder.cpp`
  + `test_sprite_clip_gpu.cpp` + `test_text_clip_gpu.cpp`, fixtures in `assets/ui/test_e2e_*.json`, all wired in
  `tests/CMakeLists.txt`.
