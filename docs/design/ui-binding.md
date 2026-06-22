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
3. **Reactivity** — `ui:data` topic + context version + re-resolve on change.
4. **Repeater** — `repeat`+`template`+child scope on any widget (instantiate-all; small N). Events-with-scope fall
   out for free.
5. **Conditional** — `if`.
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
- **Push the model**: `ui:data {<the whole view-model>}` (json-backed, like any IIO payload) → re-resolves all
  bindings. (Reactivity is "re-resolve everything on push" for now — fine for UI-sized data.)

## Status
- 2026-06-22 — **Steps 1-2 SHIPPED**: the `grove::uibind` socle (`UIBindingUnit`) + binding-in/events-out wired
  to widgets + `ui:data` (`IT_037`). **Next: step 3** (reactivity refinements — version / partial `ui:data:set`)
  OR jump to **step 4 (repeater)** which is the higher-value unlock (the list folds in at step 6). Modes liste
  actuels (simple/groupes) restent des fast-paths additifs.
