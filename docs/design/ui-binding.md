# JSON-UI templating & data-binding engine (design + roadmap)

> The "UI = JSON data-driven" vision: describe the UI in JSON (already true for static structure), bind it
> to engine `IDataNode` data, repeat templates over data arrays, and emit declarative context-bound events.
> Decided with Alexi (EXPLORE) on 2026-06-22. This doc is the resume point for the multi-slice build.

## Why
Today the static widget tree is fully JSON (15 factories in `UITree`, recursive `children`, props/style/anchors/
%/grid). What's missing for a data-driven UI: **dynamic/bound/repeated** content — runtime values arrive via
imperative topics (`ui:set_text`, `ui:list:set_items/set_groups`), there's no templating, binding, or repeater.
This engine fills that gap, generally (not list-only).

## The model (5 abstractions)
1. **Data context** = an `IDataNode` (JsonDataNode) the GAME owns and pushes. The "view model".
2. **Binding (in)** = `{{path}}` placeholders in any prop (`"text":"Credits: {{credits}}"`, `"value":"{{ship.hp}}"`,
   `"visible":"{{hasSelection}}"`) resolved against the widget's scope → data drives the view.
3. **Event (out)** = declarative event bindings (`"on":{"click":{"event":"fleet:recall","args":{"shipId":"{{id}}"}}}`)
   whose payload is `{{}}`-resolved against the SAME scope → the view drives the game. Generalises the existing
   `onClick → ui:action`.
4. **Repeater** = `"repeat":"{{fleet}}"` + `"template":{...}` on any widget → one instance per array element, each
   element becoming a **child scope** (the item). The list is the VIRTUALIZED specialisation of this.
5. **Conditional** = `"if":"{{flag}}"` (show/hide). Declarative, not an expression.

## The unifying insight
Binding (in) and events (out) are **symmetric**: both resolve `{{path}}` against the widget's **scope**
(`grove::uibind::Scope` = data + parent chain). So the foundational primitive is **the scope + the `{{}}`
resolver**; binding and events are its two consumers. An event fired from inside a repeater carries its item's
data for free (the old "in-row → itemId" routing, now general).

## Hard guardrail (NON-NEGOTIABLE)
**No expression language in templates.** Paths + string interpolation + a boolean `if`, nothing more. No
`{{hp > 0.5 ? a : b}}`, no `| filters`. Logic stays in the GAME (it computes `statusColor` and puts it in the
data). A mini-language in JSON is a bottomless pit (parsing/security/debug/maintenance) and turns layout into code.

## Conventions
- **Scope resolution:** a plain path resolves in the LOCAL scope; `$root.x` = the top scope; `$parent.x` = the
  enclosing scope. Predictable, repeater-nestable.
- **Reactivity:** versioned re-resolve. The game pushes data (`ui:data ...`), the context bumps a version, bound
  widgets re-resolve. No fine-grained per-path tracking initially (UI data is small).
- **Events:** **pure output first** — everything goes to the game, which decides everything (zero logic in JSON).
  A tiny ENUMERATED set of built-in UI actions (`toggle/open/close/switchTab`) may be added later IF needed —
  never a scripting layer.
- **Typed props:** resolving `{{}}` yields a string; non-string props (`value`/`visible`/`color`) use the typed
  resolvers (`resolveNumber`/`resolveBool`). C++ has no reflection → a bounded vocabulary of bindable props per
  widget (the fiddly bit).

## Roadmap (each step TDD, independently useful)
1. **Context + `{{}}` resolver + scope-chain** (the shared socle) — ✅ **SHIPPED** (`Core/UIBinding.{h,cpp}`,
   `grove::uibind`: `Scope`, `resolvePath`, `interpolate`, `resolveNumber/Bool`, `hasBindings`, `leafToString`).
   Pure, locked by `UIBindingUnit`. Built to serve BOTH directions.
2. **Binding-in + Events-out** (peers on the socle) — ✅ **SHIPPED**. At parse, `UITree::parseWidgetBindings`
   records (a) `bindings` = any scalar prop whose value contains `{{}}`, (b) `eventBindings` = the `on` block.
   `UIModule` holds the data context (`m_uiData`), `resolveAllBindings()` walks the tree applying each binding
   via `UIWidget::applyBoundProp` (base: visible/x/y/w/h; `UILabel` text; `UIProgressBar` value), and
   `fireWidgetEvent` publishes a widget's declared event with `{{}}`-resolved args. The game pushes the model
   via **`ui:data {<model>}`** (the whole payload becomes the root context → re-resolve). Locked by `IT_037`
   (push data → label renders the bound value on `render:text:*`; click a button → its declared event fires
   with bound args). **Deferred to their steps**: repeater scopes (events use the ROOT scope for now);
   purely-declarative widgets (an `on` with no legacy handler isn't surfaced by dispatch yet — the button in
   IT_037 keeps an `onClick` so it's returned on release).
3. **Reactivity** — ✅ **SHIPPED**. Partial, robust data updates so the game never re-sends the whole model:
   **`ui:data {<model>}`** (replace), **`ui:data:set {path, value}`** (deep path set via the pure tested
   `uibind::setAtPath` — creates intermediate objects, descends/extends arrays), **`ui:data:merge {<partial>}`**
   (nlohmann `merge_patch`, RFC 7386 deep merge; null deletes). Each re-resolves all bindings; each preserves
   the untouched rest of the model. Locked by `UIBindingUnit` (`setAtPath` edge cases) + `IT_038`
   (set/merge/nested-merge each update one field, the others intact). Versioned / per-binding re-resolve is a
   deferred PERF follow-on (re-resolve-all is fine for UI-sized data).
4. **Repeater** — ✅ **SHIPPED**. A host with `"repeat":"{{path}}"` + `"template":{...}` instantiates the
   template once per data-array element (`UIModule::expandRepeaters`, re-parsed via `UITree::parseWidget`).
   Each widget carries a `scopePath` (e.g. `"fleet.0"`) → bindings AND declarative events resolve against the
   ITEM (the "in-row routing", now general + free). Re-expands on every data push (instantiate-all; small N —
   virtualization = step 6). Rows stack by index (`y = i*height`; flexible layout = follow-on). Also fixed:
   dispatch surfaces a button with a declarative `on:click` even without a legacy `onClick`. Locked by `IT_039`
   (per-item label binding + per-item button event + re-expansion). Deferred: nested repeaters (a `repeat`
   inside a template — only root-scope hosts expand for now).
5. **Conditional** — ✅ **SHIPPED**. `"if":"{{flag}}"` on any widget: it renders only while the bound bool is
   true. When it goes false the subtree is hidden AND its retained entries are **released** (`render:*:remove`
   — no ghost, unlike a plain `"visible":"{{}}"` binding), and the hidden subtree is skipped. Evaluated against
   the widget's scope (so it works per-item in a repeater). Locked by `IT_040` (show → render / hide → purge /
   re-show → re-register).
6. **List = virtualized repeater** — fold the rowTemplate + in-row events + virtualization into the engine (the
   sharp edge: re-binding/positioning real widget subtrees on scroll without the UIScrollPanel "de-scroll" bug).

## Two sharp edges (known, deferred to their step)
- **Typed property set in C++** (step 2): no reflection → a per-widget bindable-prop setter (bounded vocabulary).
- **Repeater × virtualization** (step 6): re-instantiate/re-bind subtrees on scroll → the UIScrollPanel
  child-de-scroll trap returns; handled in isolation at step 6.

## Wire (what's usable now, steps 1-2)
- **Bind any prop** in the layout JSON: `"text":"Credits: {{credits}}"`, `"value":"{{ship.hp}}"`. String props
  interpolate; numeric/bool props take the single `{{path}}`. Currently applied: `text` (label), `value`
  (progressbar), `visible`/`x`/`y`/`width`/`height` (any). More props = extend `applyBoundProp` per widget.
- **Declare events**: `"on":{"click":{"event":"fleet:recall","args":{"shipId":"{{id}}"}}}` → publishes
  `fleet:recall {shipId: ...}` on click, args resolved against the scope.
- **Repeat a template** over a data array: `"repeat":"{{fleet}}","template":{...}` on a host widget → one
  template instance per element, each element the row's scope (so `{{name}}` / the row's events are per-item).
- **Conditionally render**: `"if":"{{flag}}"` on any widget → shown only while the bound bool is true (hides +
  purges its retained entries when false). Negation is NOT supported (no expression language) — bind the game's
  own boolean.
- **Push the model** (3 ways, each re-resolves; each preserves the untouched rest):
  - `ui:data {<whole model>}` — replace the entire context.
  - `ui:data:set {path, value}` — set one deep path (e.g. `{"path":"ship.hp","value":0.5}`).
  - `ui:data:merge {<partial>}` — deep-merge a patch (RFC 7386; a `null` value deletes a key).

## Status
- 2026-06-22 — **Steps 1-5 SHIPPED**: socle (`UIBindingUnit`) + binding-in/events-out (`IT_037`) + reactivity
  (`IT_038`) + **repeater** per-item scope (`IT_039`) + **`if`** show/hide+purge (`IT_040`). The data-driven
  engine is feature-complete for general widgets. **Next: step 6 — the LIST becomes a virtualized repeater**
  (fold rowTemplate + virtualization into the engine; the UIScrollPanel de-scroll sharp edge). Modes liste
  actuels (simple/groupes) restent des fast-paths additifs.
