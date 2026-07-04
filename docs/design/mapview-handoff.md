# Map View — handoff

Resume-from-here for `grove::mapview`, the generic header-only map-viewer engine. **Design is in
[`mapview.md`](mapview.md)** — this doc is the operational state + how to start building + the
**cross-Claude coordination**. Don't re-read the spec to know *what* to do next; read this.

---

## Status (2026-07-01) — ENGINE SIDE COMPLETE; only S3 (Theomen adapter) remains

**SPEC ✅ · S0 format ✅ (frozen) · S1 pure core ✅ · S2 interactive viewer ✅ (disk-load) · S3-seam ✅
(file-backed provider) · tiling path ✅ (T2/T3 + live retained tiling) · overlays on screen ✅ (regions/
markers) — 16 MapView ctests green.** The consumer side is DONE and proven E2E: a `.world` written to disk is
loaded (`WorldDocumentProvider`) and rendered (cells + textured tiles + regions + markers) — `MapViewViewerE2E`
writes a doc + `--load`s + drives it with real input, all green in ctest. **The only remaining slice is S3:
Theomen's own adapter that WRITES a `.world`** (its Claude, cross-project). See the "Theomen: write a `.world`"
recipe below — the contract is frozen and the engine consumes any `.world` dir today.

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
| `MapView` pure core (S1) | ✅ **DONE** — `include/grove/mapview/` |
| CellDraw→SpriteInstance adapter (P1) | ✅ **DONE** — `modules/BgfxRenderer/MapView/SpriteAdapter.h`, `MapViewAdapterUnit` |
| render proof (P2) | ✅ **DONE** — `tests/visual/capture_mapview.cpp` renders a synthetic world to a PNG |
| viewer app (S2 — interactive) | ✅ **DONE** — `tests/visual/test_mapview_viewer.cpp`: live window, drag-pan + wheel-zoom + H/B/T/R keys, **`--load <dir>`** opens a `.world` from disk. Input **E2E** = `MapViewViewerE2E` |
| file-backed provider (S3-seam) | ✅ **DONE** — `WorldDocumentProvider.h` bridges a `.world` on disk → the pure `MapView` (the "file is the interface" thesis, proven E2E: write→load→render) |
| tiling path (T2/T3) | ✅ **DONE** — `render:tilemap:tileset` + `TileMapper` (value→tile id) + `MapView` tile-chunk emit + **live retained tiling** in the viewer (`TileChunkStreamer`, 'T' toggle). Locks: `MapViewTileMapperUnit`/`MapViewTileStreamerUnit` + capture |
| overlays on screen | ✅ **DONE** — regions → `render:sector` (rings) + markers → `render:sprite` (icons), drawn world-space by the viewer |
| **`worldcheck` validator** | ✅ **DONE** — headless semantic checker (`WorldCheck.h`/`WorldCheckDisk.h` + `worldcheck` CLI): the deterministic proof a `.world` is correct, so S3 has a feedback loop that is NOT "render + eyeball". Locked by `MapViewWorldCheckUnit`. See "Verify with `worldcheck`" ↓ |
| **Theomen adapter (S3)** | ❌ **not started (Theomen-side, its Claude) — UNBLOCKED**: format frozen, engine consumes any `.world` today. Recipe ↓ |

> **Format frozen + consumer proven → S3 can start NOW.** Theomen static-links GroveEngine at HEAD, so its
> adapter just `#include`s the mapview headers and calls `disk::writeWorldDocument(...)` — the **headers ARE
> the contract** (the mp4/VLC shared spec), no separate byte-layout doc. See the recipe below.
>
> **Repo decision (2026-06-30):** NO separate repo — the viewer lives inside groveengine (`tests/visual/`).

---

## Theomen: write a `.world` (S3 recipe — the ONLY thing left)

The engine reads **any** `.world` dir. Theomen's adapter builds a `Manifest` + a `vector<ChunkData>` and calls
the writer. **Use the C++ writer — do NOT hand-roll the manifest JSON** (the JSON keys/encoding names are an
internal detail; the emitter owns them, and hand-writing `"encoding":"int16"` will fail-franc — the real names
are `int`/`uint` + a separate `bits`).

**Copy the worked example verbatim:** `tests/visual/test_mapview_viewer_e2e.cpp` → `writeTestDoc()` writes a
complete multi-chunk compressed doc; `tests/visual/capture_mapview_from_disk.cpp` writes one with an island +
renders it. Both are the template.

```cpp
#include "grove/mapview/Field.h"             // FieldDecl, Encoding
#include "grove/mapview/WorldDocument.h"      // ChunkData
#include "grove/mapview/Manifest.h"           // Manifest, Coordinate
#include "grove/mapview/WorldDocumentDisk.h"  // disk::writeWorldDocument
#include "grove/mapview/Compression.h"        // codec::zlibCompressor  (link deps/.../miniz.c)

using namespace grove::mapview;

Manifest m;
m.coordinate.topology  = "square";
m.coordinate.cellSize  = {{1.0, 1.0}};              // world units per cell
m.coordinate.boundsMin = {{0, 0, 0}};               // inclusive min cell (x,y,z)
m.coordinate.boundsMax = {{W-1, H-1, 0}};           // inclusive max cell (flat world: z=0)
m.coordinate.chunkDims = {{CW, CH, 1}};             // cells per chunk (aim 16k–256k cells: 128×128×1 ok)
m.fields = { FieldDecl{"elevation", Encoding::Int, 16, /*scale*/0.25, /*offset*/0.0} };  // add biome/etc.
// optional overlays (low-cardinality, inline): m.regions = {...}; m.markers = {...};

std::vector<ChunkData> chunks;
for (each chunk cc) {
    ChunkData d; d.coord = {cc.x, cc.y, 0}; d.cellCount = CW * CH;
    std::vector<uint32_t> vals(CW * CH);            // RAW encoded values, row-major (then z-major)
    for (each cell) vals[i] = (uint32_t)std::llround((phys - offset) / scale);  // inverse of decodePhysical
    d.fields.emplace_back("elevation", std::move(vals));   // omit a field entirely = absent (fail-franc, ≠ 0)
    chunks.push_back(std::move(d));
}

const Compressor z = codec::zlibCompressor();
disk::writeWorldDocument(dir, m, chunks, &z);         // pass nullptr for uncompressed
```

**Gotchas:** (1) store **raw** values (`(phys-offset)/scale` rounded to int), not physical — the engine
`decodePhysical`s them back. (2) A field absent from a chunk is **not stored** and reads back as "no data" (the
layer doesn't draw) — never write zeros to mean "missing". (3) `boundsMin/Max` are **inclusive cell indices**.
(4) Encodings: `bit`/`uint`/`int` (int/uint carry `bits`) / `unorm8`/`unorm16`/`float32` — **no `float16`**.
(5) The viewer frames the world from `bounds`; put a `terrain` field named `"elevation"` to reuse the demo lens,
or the game supplies its own lens.

### Verify with `worldcheck` (do THIS, not "render + eyeball")

**The proof a `.world` is correct is `worldcheck`, not the viewer.** Looking at pixels is not a test — the
doctrine forbids it as proof. `worldcheck` is a **headless, deterministic** validator (`build/tests/worldcheck`,
built by default): it names the exact offending declaration. Run it after every write:

```bash
./build/tests/worldcheck <yourDir>            # human report; exit 0 = ok, 1 = errors, 2 = usage
./build/tests/worldcheck <yourDir> --strict   # warnings fail too (tighten for CI)
./build/tests/worldcheck <yourDir> --json     # machine-readable, for a build script / gate
```

It catches the mistakes the **reader accepts silently** (the codec already fail-francs a corrupt blob / unknown
encoding / bit-width disagreement — `worldcheck` adds the *semantic* layer): unsupported topology, non-positive
cell/chunk size, **inverted bounds**, empty/duplicate field names, a **`uint`/`int` field with `bits=0`** (the
classic "forgot to set bits"), **`scale=0`**, a chunk whose **`cellCount` ≠ `chunkDims` product** (a fill-loop
off-by-one), a **chunk outside the bounds** (dead weight / coord bug), an **empty chunk**, and **NaN/Inf** in a
`float32` field (uninitialized floats the palette would silently hide). Engine impl: `include/grove/mapview/
WorldCheck.h` (pure checks) + `WorldCheckDisk.h` (disk driver); locked by `MapViewWorldCheckUnit`. Only AFTER
`worldcheck` is clean is `./build/tests/test_mapview_viewer --load <yourDir>` worth opening (to eyeball the lens).

### Capture the WHOLE world to a PNG — `--shot` (docs / blog / a quick look)

For a headless "here is the entire world, framed" image (not the scripted `--selftest` pan+zoom), use `--shot`:

```bash
./build/tests/test_mapview_viewer --load <yourDir> --shot out.png                 # 1280x720
./build/tests/test_mapview_viewer --load <yourDir> --shot out.png --size 1600x1600 # bigger = cells not sub-pixel
```

It renders ONE static frame at the reset/fit camera — the full extent, **letterboxed** for non-square worlds
(nothing cropped) — to an offscreen framebuffer, then writes the PNG (exit 0 ok / 2 usage). `--size WxH` drives
the output resolution (the hidden window/backbuffer grows to match, so it works above 1280x720). **Cell-count
ceiling:** the flat-colour sprite path renders up to ≈**131 k cells/frame** (the bgfx transient instance-buffer
cap); a larger world comes back blank (it falls to a 10 000-sprite fallback buffer). So **downsample the `.world`
below ~131 k cells** before shooting a whole continent, then use `--size` to enlarge — raising that ceiling is a
renderer change (bgfx transient limits + `SpritePass::MAX_SPRITES_PER_BATCH`), out of the viewer's scope.

### `--poster` — the WHOLE map at full resolution, ANY size (no ceiling)

When you want the entire map at full detail — including a huge world `--shot` can't do (blank past ~131 k
cells) — use `--poster`. It **tiles** the world and **stitches** the tiles into one big PNG, so it has NO cell
ceiling (each tile stays under it), NO texture-size limit (each tile ≤ one framebuffer), NO sub-pixel and NO
letterbox (the image is the map, edge to edge):

```bash
./build/tests/test_mapview_viewer --load <yourDir> --poster map.png            # 4 px/cell (default)
./build/tests/test_mapview_viewer --load <yourDir> --poster map.png --ppc 8    # 8 px/cell -> bigger, crisper
```

Output = `cells × ppc` on each axis, **uncapped** — a 1024² world at 4 px/cell is a 4096×4096 PNG; a 4096² world
at 8 px/cell is 32768×32768 (~4 GB in RAM → it **fails franc** with an out-of-memory message rather than degrading
silently). Impl: `tests/visual/MapViewPoster.h` (`renderPoster`, header-only) drives a poster-sized `ViewerApp` +
the RHI offscreen readback; tile = min(256 cells, 8192/ppc px) per side. Locked by `MapViewViewerE2E` (poster
render → dims == cells·ppc + the stitched image is varied terrain, not blank). Verified by eye: a 1M-cell world
that `--shot` rendered blank comes out as a seamless 4096² poster. **This is the way to export a full-res map;
`--shot` stays the quick fit-view thumbnail.**

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

**One-line resume:** *Engine side COMPLETE — S0 format + S1 core + S2 viewer (`--load`) + S3-seam provider +
tiling (T2/T3, incl. live retained tiling) + regions/markers on screen, 16 MapView ctests, write→load→render
proven E2E. The ONLY thing left is S3 = Theomen's adapter that WRITES a `.world` (its Claude, cross-project) —
see the "Theomen: write a `.world`" recipe above (copy `writeTestDoc`). Everything else (S4 timeline, S5 hex/
iso/Z/LOD) slots into S1's interfaces without rework.*
