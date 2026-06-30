# Map View — handoff

Resume-from-here for `grove::mapview`, the generic header-only map-viewer engine. **Design is in
[`mapview.md`](mapview.md)** — this doc is the operational state + how to start building + the
**cross-Claude coordination**. Don't re-read the spec to know *what* to do next; read this.

---

## Status (2026-06-30) — RESUME HERE

**SPEC ✅ locked & committed (`e142354`, pushed gitea/github/bitbucket). NO code yet. Resume at slice S0.**

The full definition is validated by Alexi (the mp4/VLC model, the 3 axes, the chunked/bit-packed/sparse/Z
world-document, header-only host-driven core, bulk-sprite render). All of it is in `mapview.md`. Nothing is
implemented — the next move is the first vertical slice.

| Piece | Status |
|---|---|
| Design spec (`mapview.md`) | ✅ locked, §9 decisions, §8 slice plan |
| world-document format + reader (S0) | ❌ not started — **resume here** |
| `MapView` pure core (S1) | ❌ not started |
| viewer app (S2) | ❌ not started (a new project, another Claude) |
| Theomen adapter (S3) | ❌ not started (Theomen-side, its Claude) |

---

## Cross-Claude coordination (the thing this handoff exists for)

Three workers touch this, in **three repos**. The **world-document format is the contract** that lets them
work independently (that's the whole mp4/VLC point — VLC and the camera firmware are built by different teams,
the mp4 spec is the only thing they share):

| # | Worker | Repo | Builds | Depends on |
|---|---|---|---|---|
| 1 | **Engine Claude (me)** | groveengine | **S0** format reader/writer + **S1** `MapView` core (both headless TDD) | — |
| 2 | **Viewer-app Claude** | a **new** project (neutral name, *not* "theomen_viewer") | **S2** app: load a world-document file, `ChunkProvider` over it, drive `grove::camera`, `mapview.update` → `submitSpriteBatch`, lens/layer/timeline UI | S0 + S1 shipped |
| 3 | **Theomen Claude** | theomen | **S3** adapter `World` → world-document (writes the format) | S0 format **locked** |

**Dependency order:** `S0` unblocks **both** `S1` (engine core) **and** `S3` (Theomen adapter) — they run
**in parallel** once the format is frozen. `S2` (app) consumes `S1`. So: S0 first → (S1 ‖ S3) → S2 ties it
together. The format being a *file contract* is what makes the parallelism safe.

> ⚠️ Boundary discipline (doctrine): the engine speaks **only** generic (`field`/`region`/`marker`/`chunk`).
> "carbon"/"phase 6B"/"carboniferous" **never** enter `grove::mapview` — they live in the Theomen adapter. If
> engine code starts knowing Theomen words, it stopped being reusable. Same for the app: it loads *a file*, it
> doesn't know Theomen exists.

---

## Resume: slice S0 (engine, me) — format + reader, headless TDD

**Goal:** a tested world-document writer/reader: manifest (§3.1) + bit-packed sparse chunks (§3.2–3.4) +
optional per-chunk compression (§3.5) + disk-pack/RAM-expand (§3.6). Renderer-independent → pure, headless.

**Proposed file layout** (confirm as you go):
- `include/grove/mapview/` — the **format** (renderer-independent, pure): `WorldDocument.h`, `Manifest.h`,
  `ChunkCodec.h` (bit-pack + presence mask + compress), `Field.h`, `Coord.h`. Header-only.
- `modules/BgfxRenderer/MapView/` — **later (S1)** the render-compiler that emits `SpriteInstance` (depends on
  `Frame/FramePacket.h`), alongside `Scene/ZoneNavigator.h`. *Split rationale:* the format is reusable even
  without a renderer (a headless analyzer, a PNG dumper); only the render-compiler couples to `SpriteInstance`.
- `tests/unit/test_mapview_format.cpp` — headless (no `_gpu` suffix). Wire into `tests/CMakeLists.txt`.

**First TDD red test** (write this first, watch it fail, then implement):
> Build a `WorldDocument` in memory: one `4×4×1` chunk, two fields — `elevation` (`int16`, **present**, a few
> known values) and `temperature` (`float16`, **absent**). Write to a temp dir, read back. Assert: (a) manifest
> round-trips (topology, `chunkDims`, field decls); (b) presence mask = elevation present, temperature absent;
> (c) bit-unpacked elevation == the known values; (d) querying temperature returns **absent**, *not* `0`
> (fail-franc); (e) **negative control** — a deliberately wrong declared bit-width fails *loudly* (no silent
> garbage). Then add a compressed-chunk round-trip case.

**Compressor call for S0:** use **miniz (zlib)** — it's already vendored (`deps/bgfx/bimg/3rdparty/tinyexr/deps/miniz/`),
in-tree, zero new dependency. Compression is off the hot path (decompress once at chunk-load, §3.5) so zlib's
speed is irrelevant. **Only** vendor an LZ4 single-header later if a profile ever shows chunk-load as a hitch.

**Conventions to respect:** header-only like `include/grove/anim/`; 3-level comments (QUOI/POURQUOI/COMMENT);
TDD red→green→commit per increment; build/test from `build/`. No GPU/SDL → builds & tests on WSL too (good for
the sanitizer sweep later — fold mapview into the quality-hardening lenses, [[quality-hardening]]).

---

## Engine facts that shaped the design (gotchas for whoever builds it)

- **`SpriteInstance` (the bulk-path format, `modules/BgfxRenderer/Frame/FramePacket.h`) already carries
  per-cell** `x,y,scaleX,scaleY,rotation` + `layer` (z-order) + **`r,g,b,a`** + UVs/textureId. That's why the
  bulk path is topology/projection/color/layer-agnostic and **continuous color is free** (no shader, no banding).
  The S1 compiler fills `SpriteInstance[]` and the app calls `submitSpriteBatch` (~100k–400k/frame, benchmarked).
- **Header-only + host-driven** = the `ZoneNavigator` pattern (`modules/BgfxRenderer/Scene/ZoneNavigator.h`):
  the host feeds input/camera, calls `update(dt)`, drains output each frame. Copy that shape. Stateful is fine;
  **I/O is injected** (`ChunkProvider`) so the core stays pure → headless TDD.
- **S1 open decision:** the core emits `SpriteInstance` directly (streamlined per Alexi #3, couples to
  `FramePacket.h`) — that's what `mapview.md §4` sketched. Alternative (a neutral `CellDraw` the app translates)
  is more decoupled but adds a step. Lean: emit `SpriteInstance` directly; revisit only if a non-bgfx consumer appears.
- **Only miniz/zlib is vendored** — no LZ4/zstd (see S0 compressor call).
- **PNG write is a gap** (unrelated but adjacent): `render:screenshot` writes **`.tga`** (not PNG); the only real
  PNG path is the `svpng` snippet in `tests/visual/capture_ui_on_engine.cpp`; `stb_image_write` is **not**
  vendored (only the *read* side, via bimg). If the app wants PNG export, "productize PNG write" is a small
  separate increment. PNG **read** is solid (sync + async `ThreadedDecoder`).
- **groveengine is NOT in ProjectMind** — tracked via `docs/design/*.md` + the file-based memory
  (`memory/mapview-design.md`). Keep both current on resume.

---

## Decisions, open questions, slice plan

- **Decisions locked** → `mapview.md §9` (don't duplicate here — single source).
- **Open / deferred** → `mapview.md §10`: extreme-zoom LOD, palette-LUT on the tilemap fast-lane, hex-Z,
  iso inter-slice occlusion, async streaming back-pressure, the `view_config.json` phase→lens map (Theomen-side).
- **Slice plan** → `mapview.md §8`: **S0** format+reader → **S1** pure core (square·top-down·provider, TDD) →
  **S2** viewer app → **S3** Theomen adapter → **S4** timeline → **S5+** plug-ins (hex / iso+depth-sort /
  infinite / Z multi-slice / tilemap fast-lane / extreme-zoom LOD / palette-LUT). S0→S3 = "see Theomen's world,
  generically"; everything after slots into S1's interfaces without rework.

**One-line resume:** *write `tests/unit/test_mapview_format.cpp` (the red test above), implement
`include/grove/mapview/` to make it green, commit — that's S0 underway.*
