# Map View — handoff

Resume-from-here for `grove::mapview`, the generic header-only map-viewer engine. **Design is in
[`mapview.md`](mapview.md)** — this doc is the operational state + how to start building + the
**cross-Claude coordination**. Don't re-read the spec to know *what* to do next; read this.

---

## Status (2026-06-30) — RESUME HERE

**SPEC ✅ locked. S0 ✅ DONE (format + reader, headless TDD, committed `d0ff166`→`49f9ed3`→`617a2f1`).
The world-document format is now FROZEN. Resume at slice S1 (the pure `MapView` core).**

S0 shipped the whole contract, all green via ctest (`MapViewFormatUnit` / `MapViewCompressUnit` /
`MapViewDiskUnit`), header-only + std-only so it builds/tests on WSL too:
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
| `MapView` pure core (S1) | ❌ not started — **resume here** |
| viewer app (S2) | ❌ not started (a new project, another Claude) — needs S1 |
| Theomen adapter (S3) | ❌ not started (Theomen-side, its Claude) — **UNBLOCKED** (format frozen) |

> **Format frozen → S3 can start NOW, in parallel with S1.** Theomen static-links GroveEngine at HEAD, so
> its adapter just `#include`s `grove/mapview/{WorldDocument,Manifest,WorldDocumentDisk}.h` and calls
> `writeWorldDocument(dir, manifest, chunks)` — the headers ARE the contract (the mp4/VLC shared spec), no
> separate byte-layout doc needed. The chunk blob format and manifest schema will not change under S1.

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

## Resume: slice S1 (engine, me) — the pure `MapView` core, headless TDD

**Goal (mapview.md §4, §8):** `MapView` + `SquareLayout` (① topology) + `TopDownProjection` (② projection) +
`IChunkProvider` (③, host-injected — the disk reader above is one impl) + cull→stream→LRU + the recipe system
(Palette/Filter/Layer/Lens, §5). Pure compute, host-driven (the `ZoneNavigator` shape), emits `SpriteInstance[]`
via `drainCells`. Renderer-coupled part lives in `modules/BgfxRenderer/MapView/` (couples to `Frame/FramePacket.h`);
the renderer-independent geometry/recipe math can stay in `include/grove/mapview/`. **Plan S1 with Alexi before
building** (it's a bigger slice than S0's increments) — don't pre-commit the core↔SpriteInstance boundary alone.

**Conventions (unchanged):** header-only like `include/grove/anim/`; 3-level comments (QUOI/POURQUOI/COMMENT);
TDD red→green→commit per increment; build/test from `build/`. No GPU/SDL → builds & tests on WSL too (fold
mapview into the quality-hardening lenses, [[quality-hardening]]).

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

**One-line resume:** *S0 is done & frozen (`include/grove/mapview/`, 3 ctest locks). Next = S1 (the pure
`MapView` core) — plan it with Alexi first (mapview.md §4/§8); Theomen's S3 adapter can start in parallel now.*
