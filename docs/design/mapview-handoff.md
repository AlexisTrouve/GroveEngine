# Map View — handoff

Resume-from-here for `grove::mapview`, the generic header-only map-viewer engine. **Design is in
[`mapview.md`](mapview.md)** — this doc is the operational state + how to start building + the
**cross-Claude coordination**. Don't re-read the spec to know *what* to do next; read this.

---

## Status (2026-06-30) — RESUME HERE

**SPEC ✅ locked. S0 ✅ DONE (format, frozen). S1 ✅ DONE (pure `MapView` core). 11 MapView ctests green.
Resume at S2 (the viewer app) — and/or the BgfxRenderer CellDraw→SpriteInstance adapter.**

**S1 (pure core, headless TDD)** — `include/grove/mapview/`, commits `9c1fbb4`→`ee0702e`→`3be5f7c`→`5b969b9`:
- **S1a** geometry — `Geometry.h` (WorldPos/RenderPos/CellQuad), `GridLayout.h` (IGridLayout + SquareLayout:
  cell↔world, floor pick, quad, neighbours), `Projection.h` (IProjection + TopDownProjection = identity;
  the renderer's camera does pan/zoom, mapview emits world-space). Interfaces day-one → hex/iso plug in later.
- **S1b** streaming — `ChunkProvider.h` (IChunkProvider, host-injected), `Cull.h` (chunksInViewport → bounds
  cost to screen×zoom), `ChunkCache.h` (LRU eviction under a budget; never reload resident / never evict visible).
- **S1c** recipe — `Color.h` (Rgba), `Palette.h` (ramp/banded/categorical), `Filter.h` (composable predicate).
- **S1d** orchestrator — `CellDraw.h` (neutral emit), `Lens.h` (Layer/Lens), `MapView.h` (cull→stream→compile
  →drainCells). Decoupled from Manifest/JSON (takes a plain schema + GridSpec) → no nlohmann in the core.

The chosen emit boundary is the neutral **CellDraw** (a thin BgfxRenderer-side adapter maps it to
SpriteInstance) — grove::mapview is 100% renderer-independent.

**S0 (format + reader, frozen)** — all green via ctest (`MapViewFormatUnit`/`CompressUnit`/`DiskUnit`/
`RoundtripUnit`), header-only + std-only so it builds/tests on WSL too:
- **S0a** — pure bit codec + sparse chunk (de)serialize (`Coord.h`, `Field.h`, `ChunkCodec.h`,
  `WorldDocument.h`). Portable LSB-first/little-endian; `absent ≠ 0` proven red via mutation; loud
  negative controls (wrong bit-width at codec + document level, out-of-range value, corrupt/truncated blob).
- **S0b** — optional per-chunk zlib compression, INJECTED (`Compression.h`, vendored miniz) so the core
  stays dependency-free; compressed round-trips identically to raw; fails franc with no decompressor / on corruption.
- **S0c** — JSON manifest (`Manifest.h`, vendored nlohmann) + reference disk transport
  (`WorldDocumentDisk.h`, kept separate from the pure core); full document round-trips through a temp dir.
- root `CMakeLists.txt` now `LANGUAGES CXX C` (miniz.c is C).

| Piece | Status |
|---|---|
| Design spec (`mapview.md`) | ✅ locked, §9 decisions, §8 slice plan |
| world-document format + reader (S0) | ✅ **DONE** — frozen contract, `include/grove/mapview/` |
| `MapView` pure core (S1) | ✅ **DONE** — `include/grove/mapview/`, 8 ctests green |
| CellDraw→SpriteInstance adapter (P1) | ✅ **DONE** — `modules/BgfxRenderer/MapView/SpriteAdapter.h`, `MapViewAdapterUnit` |
| render proof (P2) | ✅ **DONE** — `tests/visual/capture_mapview.cpp` renders a synthetic world to a PNG (first pixels) |
| viewer app (S2 — interactive) | ❌ not started (in groveengine) — **resume here**: camera pan/zoom + lens/z-slice UI |
| Theomen adapter (S3) | ❌ not started (Theomen-side, its Claude) — **UNBLOCKED** (format frozen) |

> **Format frozen → S3 can start NOW.** Theomen static-links GroveEngine at HEAD, so its adapter just
> `#include`s `grove/mapview/{WorldDocument,Manifest,WorldDocumentDisk}.h` and calls
> `writeWorldDocument(dir, manifest, chunks)` — the headers ARE the contract (the mp4/VLC shared spec), no
> separate byte-layout doc needed. The chunk blob format and manifest schema are frozen.
>
> **Repo decision (2026-06-30):** Alexi chose NOT to spin a separate repo for now — the viewer app (S2)
> will live inside groveengine (e.g. a `tests/visual/` demo or an `examples/`/`tools/` target) rather than a
> new neutral-named project. Revisit if/when it grows into a standalone product.

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

## Slice S0 — DONE (format + reader, headless TDD). As built:

`include/grove/mapview/` (header-only, pure unless noted):
- `Coord.h` — Z-aware `CellCoord`/`ChunkCoord` (flat producer pins z=0).
- `Field.h` — self-describing `FieldDecl` (encoding + bits + scale/offset) + `decodePhysical`. Encodings:
  `bit`, `uint{N}`, `int{N}`, `unorm8/16`, `float32`. **`float16` deliberately deferred** (no native type;
  Theomen doesn't need it) — add a conversion routine when a producer asks.
- `ChunkCodec.h` — pure LSB-first/little-endian bit packer (`packBits`/`unpackBits`). Portability is won here:
  defined byte+bit order, never a memcpy of a wider int → endianness can't leak in.
- `WorldDocument.h` — sparse `ChunkData` (`get()` returns `nullptr` for absent, never a zero vector) +
  `serializeChunk`/`deserializeChunk`. Optional **injected** `Compressor*` (default = raw).
- `Compression.h` — `zlibCompressor()` over the vendored miniz (kept OUT of the core so the format has zero
  deps; only a consumer that compresses links `miniz.c`). `MINIZ_NO_ZLIB_COMPATIBLE_NAMES` set to dodge
  miniz's `compress`/`uncompress` macros.
- `Manifest.h` — pure string↔`Manifest` via vendored nlohmann (coordinate + ordered field schema).
- `WorldDocumentDisk.h` — reference disk transport (`std::filesystem`), SEPARATE from the pure core (the I/O
  the S1 `ChunkProvider` will inject). `writeWorldDocument`/`readManifest`/`readChunk`/`hasChunk`.

Tests (`tests/unit/`, all headless, registered in ctest): `test_mapview_format.cpp` (`MapViewFormatUnit`),
`test_mapview_compress.cpp` (`MapViewCompressUnit`, links miniz.c), `test_mapview_disk.cpp` (`MapViewDiskUnit`,
links miniz.c + nlohmann include). Build: `cmake --build build --target test_mapview_format test_mapview_compress test_mapview_disk`.

**Lessons banked:** (1) the project root was `LANGUAGES CXX` only — a vendored `.c` needs `LANGUAGES CXX C`.
(2) miniz exposes zlib-compat `#define compress mz_compress` macros → name injected members `*Fn`, not `compress`.
(3) compression as an injected `Compressor` (not a hardcoded dep) is what keeps the format core's "zero
dependency / builds on a bare toolchain" property — and keeps the S0a test linking nothing.

## Slice S1 — DONE (pure `MapView` core, headless TDD). As built:

`include/grove/mapview/` (header-only, pure, renderer-independent — NO bgfx, NO nlohmann):
- `Geometry.h` — `WorldPos` / `RenderPos` / `CellQuad` (double precision; cast to float only at emit).
- `GridLayout.h` — `IGridLayout` (① topology) + `SquareLayout` (cellToWorld=centre, worldToCell=floor pick,
  cellQuad=4 CCW corners, neighbours=4-orthogonal). Non-square via unequal cellSize.
- `Projection.h` — `IProjection` (② projection) + `TopDownProjection` = identity in XY + depthKey 0. KEY:
  projection is camera-INDEPENDENT — the renderer's `render:camera` (grove::camera/ZoneNavigator) does
  pan/zoom; mapview emits world-space. Iso = a future non-identity projection + real depthKey.
- `ChunkProvider.h` — `IChunkProvider` (③, host-injected: disk/generator/network). `Cull.h` — `chunksInViewport`
  (bounds cost to screen×zoom). `ChunkCache.h` — LRU eviction under a budget (never reload resident, never evict visible).
- `Color.h`/`Palette.h`/`Filter.h`/`Hillshade.h` — recipe (deepened S1e–h): Rgba + multiplyRgb; Palette
  ramp/banded/categorical/**diverging**/**stepped**; `Filter` composable AND/OR/NOT **+ cross-field**
  (`cmpField`, sampler-resolved, absent named field fails franc); **Hillshade** (Lambertian relief) wired as
  a Layer modifier (`hillshadeField` + light) with **cross-chunk** gradient sampling in `MapView`.
- `Overlays.h` — Region/Marker DATA PODs (split from styling so Manifest stays recipe-independent, S1j).
- `Region.h`/`Marker.h` — overlay layers (S1i): RegionLayer (circles by type/value, disc/ring) + MarkerLayer
  (points by kind, scaled/rotated), global vector sets culled by viewport, emitting RegionDraw/MarkerDraw.
  **Overlay format (S1j):** AREAS = a categorical field (already works, exact boundaries); only points/circles
  are overlays, carried as inline JSON lists in the manifest (`regions`/`markers`) — no vector blob. Host reads
  the manifest → `setRegions`/`setMarkers`.
- `CellDraw.h` — the neutral emit unit. `Lens.h` — Layer/Lens (+ regionLayers/markerLayers). `MapView.h` — the orchestrator
  (cull→stream→compile→`drainCells`), **decoupled from Manifest/JSON** (takes a plain `vector<FieldDecl>` +
  `GridSpec`), so no nlohmann leaks into the core. Only active z-slice emitted; absent field = no draw (fail-franc).

Tests (`tests/unit/`, headless, ctest): `MapViewLayoutUnit`/`StreamingUnit`/`RecipeUnit`/`CoreUnit`/
`HillshadeUnit` (S1) + the four S0 locks + `MapViewHardeningUnit`. `ctest -R MapView` = 10/10 green.

**Adversarial review done (S1 hardening, commit `05c0a01`).** A 4-way parallel review fixed 7 real bugs,
each reproduced red then locked in `test_mapview_hardening.cpp`: cull `reserve` overflow/UB on zoom-out +
missing cellW/H guard (Cull.h); `ChunkCache` exception-safety (load-before-mutate); `MapView` dangling
schema pointer → owns a copy now; `Hillshade` NaN/Inf leak; `Palette` NaN→fallback (ramp/banded gained an
optional fallback); `worldToCell` fractional-boundary round-trip (snapFloor). Minor invalid-input findings
(zero light, stepped reversed, Eq epsilon at >4.5M, z>int16, get() lifetime) were consciously left as
by-design/invalid-input, not real defects.

**Resolved fork (was "core↔SpriteInstance"):** the core emits **neutral `CellDraw`**, NOT `SpriteInstance`.
So ALL of mapview lives in `include/grove/mapview/` (renderer-independent); the only renderer-coupled piece
left is a tiny **CellDraw→SpriteInstance adapter** (TODO, `modules/BgfxRenderer/MapView/`, couples to `Frame/FramePacket.h`).

## Resume: S2 (viewer app) + the CellDraw→SpriteInstance adapter

The pure core is done and locked. To see actual pixels:
1. **Adapter (engine, small):** `modules/BgfxRenderer/MapView/` — map `CellDraw[]` → `SpriteInstance[]` (x,y←centre,
   scaleX/Y←w/h, rotation, layer, rgba, + a white 1×1 texture / default UV for solid colour), then `submitSpriteBatch`.
2. **App (S2, decided to live IN groveengine):** load a world-document (`WorldDocumentDisk`), build a `DiskChunkProvider`
   (wrap `readChunk`), wire `grove::camera`/`ZoneNavigator` → feed `MapView.setViewport` from `visibleWorldBounds`,
   `update()` → adapter → `submitSpriteBatch`. Lens/layer/z-slice UI later. A `tests/visual/` demo is the natural home.
3. **Theomen S3 (parallel, its Claude):** `World` → `writeWorldDocument` using the frozen headers.

**Conventions (unchanged):** header-only like `include/grove/anim/`; 3-level comments (QUOI/POURQUOI/COMMENT);
TDD red→green→commit per increment; build/test from `build/`. The pure core builds & tests on WSL too (fold
mapview into the quality-hardening lenses, [[quality-hardening]]); the app/adapter need GPU.

---

## Engine facts that shaped the design (gotchas for whoever builds it)

- **`SpriteInstance` (the bulk-path format, `modules/BgfxRenderer/Frame/FramePacket.h`) already carries
  per-cell** `x,y,scaleX,scaleY,rotation` + `layer` (z-order) + **`r,g,b,a`** + UVs/textureId. That's why the
  bulk path is topology/projection/color/layer-agnostic and **continuous color is free** (no shader, no banding).
  The CellDraw→SpriteInstance adapter (TODO) fills `SpriteInstance[]` from the core's `CellDraw[]` and the app
  calls `submitSpriteBatch` (~100k–400k/frame, benchmarked). The core itself never touches `SpriteInstance`.
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

**One-line resume:** *S0 + S1 done (pure core, 12 ctest locks incl. the adapter). P1 adapter + P2 render proof
DONE — `capture_mapview` renders a synthetic world to a PNG (first pixels). Next = the interactive S2 app
(camera pan/zoom + lens/z-slice UI, in groveengine) and/or wiring real Theomen data (S3). Lesson banked:
hillshade wants a finely-encoded field (coarse → gradient quantization contours, caught only by the real render).*
