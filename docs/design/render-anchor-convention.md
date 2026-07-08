# Render anchor convention — `x,y` = corner · `cx,cy` = center

**Audience:** an AI (Claude) writes gameplay code against the `render:*` IIO contract and must get it
**right the first time** — it pattern-matches + reads the contract, it does not iterate fast at a REPL.
So the contract must kill the three AI failure modes: (1) **silent divergence** between similar
primitives; (2) **implicit contract** buried in code; (3) **subtle non-crashing mismatch** invisible to
tests. This doc is the law + the migration record.

## The footgun this kills

The 2D draw primitives encoded **opposite anchors under the SAME field names** `x,y`:

- `render:rect` — `x,y` = **top-left corner** (+ `w,h`).
- `render:sprite` — `x,y` = **center** (+ `scaleX,scaleY`). Same `x,y`, opposite meaning.
- `render:sector` — `cx,cy` = **center** (already named explicitly!).

A caller passes a corner to a center-anchored sprite → every icon shifts by half a footprint, **no
error, invisible to tests**. This actually bit a consumer (drifterra). Ground truth was
`SceneCollector.cpp` `parseSprite` (x,y read as center) vs `parseFilledRect`
(`sprite.x = x + w*0.5f; // top-left -> center`).

## The law (uniform, self-documenting)

Encode the anchor **in the field name**, everywhere. This generalizes what `render:sector` already did
(`cx,cy`) — not an invention.

- **`x, y` = top-left CORNER**, everywhere it appears. This is what an AI assumes by default
  (screen / CSS / grid — the bulk of gameplay code: HUD, grids, layouts).
- **`cx, cy` = CENTER**, everywhere a center is the natural anchor (sprite, particle, sector). The
  **name carries the semantics** → the AI reads `x`→corner, `cx`→center, without guessing or spelunking
  `SceneCollector`.
- **`rotation` pivots around the box CENTER**, whether the position was given by corner or by center
  (position ⟂ rotation pivot). This is *already* how the engine behaves (sprite rotates about its
  position = its center; camera rolls about screen-center — `parseCamera`, locked by `SceneCollectorTest`).

**Why name-encode over an explicit `anchor`/`pivot` field (Unity-style):** an `anchor` field has an
**invisible default** (failure mode #2) and must be remembered on every call; `cx` vs `x` is
self-documenting **at the call site**. Name-encode buys ~90% of the value for ~10% of the complexity.
An arbitrary pivot (0.3, 0.7) is YAGNI — add an explicit `anchor` field *then*, additively, if ever needed.

## (a) Audit — every 2D draw topic, its position fields, its anchor

| Topic | Position fields | Anchor (before) | Verdict |
|---|---|---|---|
| `render:sprite` | `x,y` (+`scaleX,scaleY`) | **CENTER** | → `cx,cy` |
| `render:sprite:add` / `:update` | `x,y` | **CENTER** | → `cx,cy` |
| `render:sprite:batch` (packed blob, stride 8) | `x,y` floats (slots 0,1) | **CENTER** | → doc only (positional blob, no field names) |
| `render:particle` | `x,y` (+`size`) | **CENTER** (point) | → `cx,cy` (law consistency) |
| `render:rect` | `x,y,w,h` | top-left CORNER | ✅ conforms |
| `render:debug:rect` | `x,y` + `w`/`width`,`h`/`height` | top-left CORNER | ✅ (but dual `w`/`width` — see footguns) |
| `render:tilemap(:add)` | `x,y` (+`width,height`) | top-left CORNER (chunk origin) | ✅ conforms |
| `render:text(:add/:update)` | `x,y` | CORNER (pen origin, top-left) | ✅ (baseline-vs-top to confirm in TextPass) |
| `render:sector` | `cx,cy` (+`r0,r1,a0,a1`) | **CENTER** | ✅ **THE MODEL** |
| `render:camera` | `x,y` | world coord @ viewport top-left | ✅ (camera domain, not an entity) |

**Only offenders: `x,y`-means-center → sprite (+add/update/batch) + particle.** Everything else is
already `x,y`=corner or `cx,cy`=center. The change is **surgical**, not "the whole API".

## Rollout — hard-reject (Alexi's call, 2026-07-06), phased so every commit stays green

`cx,cy` on sprite/particle carries the SAME center semantics as today's `x,y` — the migration is a
**field RENAME with identical numbers** (zero position shift). The legacy form is made a **loud error**,
not a silent fallback (doctrine: échec franc > fallback qui masque).

1. **Additive** — accept `cx,cy` (center) on sprite(+add/update)+particle; `x,y` still works (center)
   for now. Red-first test locks `cx,cy`→center per primitive. *(Suite stays green — old callers
   unaffected.)*
2. **Migrate in-repo** — all groveengine callers (`modules/UIModule` UIRenderer, tests, tests/visual)
   `x,y`→`cx,cy`. *(Green.)*
3. **Flip to hard-reject** — a sprite/particle with `x,y` present but NO `cx,cy` → the primitive is
   **skipped** + a **one-shot loud log per topic** (`render:sprite: 'x,y' is retired; use 'cx,cy'
   (center). See DEVELOPER_GUIDE anchor table.`). **One-shot per topic, never per-call** — `parseSprite`/
   `parseSpriteAdd` are the render hot path. Test locks the reject. *(Green — no in-repo caller left on
   `x,y`.)* Corner-on-sprite (`x,y`=corner) is a possible FUTURE additive; not built now.

## (b) External-consumer migration note (drifterra / fractax — separate repos, their own Claude)

I do not touch those repos; this is the coordinated-migration checklist for their owner. **drifterra
static-links groveengine at HEAD → the break lands at its next build** (its sprites are skipped +
logged until migrated — loud, by design).

**What breaks (rename `x,y`→`cx,cy`, keep the same numbers):**
- `render:sprite` — `{x,y}` → `{cx,cy}` (center).
- `render:sprite:add` / `render:sprite:update` — `{x,y}` → `{cx,cy}`.
- `render:particle` — `{x,y}` → `{cx,cy}`.

**What does NOT break:**
- `render:sprite:batch` — packed blob, slots 0,1 stay center (positional, no field names). No change,
  just know slots 0,1 = center.
- `render:rect`, `render:debug:rect`, `render:tilemap`, `render:text`, `render:sector`, `render:camera`
  — unchanged.

Grep drifterra/fractax for `"render:sprite"`, `"render:sprite:add"`, `"render:sprite:update"`,
`"render:particle"` publishers carrying `x`/`y` and swap to `cx`/`cy`.

## (c) Other AI footguns in the render API (coherence review — reported, fix only on validation)

1. **Dimension fields named inconsistently** — `render:rect`=`w,h` · `render:tilemap`=`width,height` ·
   `render:debug:rect` accepts BOTH. An AI passing `width` to a rect → ignored (defaults 0 → invisible
   rect). *Same family as the anchor footgun.* Fix: pick `w,h` everywhere (or accept both everywhere,
   loud if they disagree).
2. **`space:"screen"` partial coverage** — honored by sprite/rect/text, **NOT by particle** (always
   world); sector to confirm. A HUD particle fails silently.
3. **Color decode** — sprite/rect decode `0xRRGGBBAA`→floats *in* SceneCollector; text/particle/sector
   pass the raw `uint32` to their pass. Confirm TextPass/ParticlePass decode RRGGBBAA (not ABGR) — a
   subtle silent-mismatch risk.
4. **`layer`** — present on sprite/rect/text/sector; **absent on particle** (no inter-particle
   ordering); debug is always-on-top.
5. **`asset` (streamed string id)** — sprite yes; **particle no** (numeric `textureId` only).
6. **`rotation`** — sprite only; rect/particle/text silently ignore it if passed.

These are a backlog, not this change's scope. Each is a candidate for the same name/coverage
uniformization once validated.

## Status

- [x] (a) audit + convention decided + rollout locked (this doc) — **increment 1** (`62178cf`).
- [x] additive `cx,cy` + red-first test — **increment 2** (`28d1e28`; `SceneCollectorTest [anchor]`).
- [x] in-repo caller migration (2 production + 17 test files; miss-detector 0, suite 151/151) — **increment 3** (`c330ad7`).
- [x] flip to hard-reject + test (`SceneCollectorTest [reject]`; suite 151/151 with reject live = completeness proof) — **increment 4** (`4e8204e`).
- [x] doc-law: DEVELOPER_GUIDE anchor table + note + CLAUDE.md anchor bullet + "new primitives follow this" rule — **increment 5**.

**Shipped.** In-repo is on `cx,cy`; legacy `x,y` on sprite/particle is a hard reject. External consumers
(drifterra/fractax) must migrate per the section (b) checklist — that break lands at their next build (loud).
