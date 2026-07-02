# Map View — design (`grove::mapview`)

**Status:** BUILT through S3-seam + tiling + overlays (2026-07-01). Shipped: S0 format, S1 pure core, S2
interactive viewer (`--load` a world-document from disk), the file-backed `WorldDocumentProvider` (the "file
is the interface" proven E2E), the productized **tiling path** (`TileMapper` + retained **live tiling**), and
**regions/markers on screen** — all in `include/grove/mapview/` + `tests/`, 16 MapView ctests. This doc is the
design **spec** (the three axes, the format contract, the locked decisions); the running implementation log +
resume state live in [`mapview-handoff.md`](mapview-handoff.md) and the engine memory. Redline design changes
here first.
**One-line:** a header-only, **generator-agnostic** engine that turns any chunked world of named fields
into colored cells on screen — square / hex / rect, top-down / isometric, finite / infinite, layered, Z-aware.

First consumer: a generic **world-viewer app** that displays Theomen's procedural worlds (and, later, the
games built on Theomen — Drifterra-GROUND, a DF-like). The viewer never knows what it's looking at; it
reads a **world-document** file. Theomen is one producer of those files among future many.

---

## 1. Vision & boundary — the mp4/VLC model

The load-bearing idea: **the file is the interface.** A producer writes a *world-document* to disk; the
viewer reads it. They never talk directly.

| | |
|---|---|
| **world-document** (a file/dir on disk) | = an `.mp4` |
| **the viewer app + `grove::mapview`** | = VLC (plays *any* world-document) |
| **Theomen, future games** | = the phone / camera / Blender (many producers of the format) |

VLC doesn't care who made the mp4 — it reads **the format**. Same here: the format is neutral. The
manifest names fields `"elevation"`, `"temperature"`, `"biome"` — it knows **nothing** of "carbon",
"phase 6B", "carboniferous". Each producer has a small **adapter** that translates its internal world →
this neutral format. That neutrality is *the* thing that makes the viewer reusable across games.

### Who owns what (three seams)

| Layer | Lives in | Owner | Why |
|---|---|---|---|
| **`grove::mapview`** — world-document → render commands (topology/projection/palette/filter/chunk math). Pure, header-only, **testable headless**. | groveengine (header-only helper, like `camera`/`anim`/`ZoneNavigator`) | engine-side | only place reusable by any Grove game; pure logic → TDD unit tests |
| **world-document schema** — the contract | groveengine (spec + reader) | engine-side | the real design deliverable |
| **viewer app** — loads a world-document *file*, drives camera/UI/timeline, publishes to BgfxRenderer | a **new, generator-agnostic project** (neutral name, not "theomen_viewer") | its own Claude | generator-agnostic → genuinely reusable; the I/O shell around the pure core |
| **Theomen adapter** — `World` (per phase) → world-document | **Theomen** | its Claude | Theomen's vocabulary stays in Theomen, never in the engine |

Same split as the rest of the engine: the renderer owns projection, the tilemap owns the LOD crossfade,
ZoneNavigator owns the navigation feel — here `grove::mapview` owns the *map-display math*, the producer
owns the *world*.

---

## 2. The three orthogonal axes (what makes it generic)

The whole "maximalist" wish-list collapses to **three independent axes** + the recipe system (§5). Build
*for* all three (abstraction maximalist); ship **one combo first** (impl incremental).

### ① Topology — the shape of a cell

Square / hex / rect differ by **two pure functions** + neighbour structure, nothing else:
- `cellToWorld(cellCoord) → worldPos`
- `worldToCell(worldPos) → cellCoord` (the "pick")
- `neighbours(cellCoord) → [cellCoord]`

That's the `GridLayout` interface (canonical ref: Red Blob Games hex guide; hex uses axial/cube coords —
mechanical, standard). Swapping topology swaps the transform; the renderer is unchanged.

### ② Projection — how the plane maps to the screen

**Orthogonal to topology** (the classic confusion to avoid: iso is *not* a grid shape). A *square* grid
renders top-down **or** isometric. Same data, different screen transform:
- `worldToScreen(worldPos, camera) → screenPos`
- top-down = trivial scale/translate (today's `grove::camera`)
- isometric = a 2:1 (or arbitrary dimetric) transform **+ painter's-order depth sort** + "tall" objects
  overlapping front cells. The depth-sort + tall-object overlap is iso's *only* real added cost — flagged,
  not hand-waved.

Topology × Projection **compose**: {square,hex,rect} × {top-down,iso}.

### ③ Provider — where data comes from, and the infinity trick

The world is a **sparse set of chunks** addressed by chunk-coord, loaded **on demand** around the camera:
- `ChunkProvider(chunkCoord) → chunk data` (interface; mapview defines it, the **host implements** the
  actual load — sync or async — so mapview stays pure)
- finite world (Theomen) = provider returns chunks inside bounds, "absent" outside
- infinite world (a future game) = provider **generates** chunks on demand
- **same interface** — the viewer never knows which

Because we chunk + cull, the **visible cell count is bounded by screen × zoom, never by world size**.
Infinity is free at render time.

---

## 3. The world-document format (the contract — §2 deliverable)

A **chunked, columnar, self-describing, sparse, compressed** format. Not exotic: this is essentially how
**Minecraft** stores its infinite world (region files + bit-packed block states whose bits-per-block adapt
to the palette + zlib). A purpose-built cousin of NetCDF / Minecraft region files.

Two parts: a small **readable manifest** + heavy **binary chunk blobs**.

### 3.1 Manifest (JSON, human-readable, versionable)

The "table of contents + recipe book". Describes the world; never holds bulk numbers.

```jsonc
{
  "formatVersion": 1,
  "coordinate": {
    "topology": "square",            // ① square | hex | rect
    "cellSize": [1.0, 1.0],          // world units per cell
    "bounds": { "min": [0,0,0], "max": [1000,1000,1] },  // null on an axis = infinite
    "chunkDims": [128, 128, 1]       // W×H×D, power-of-two; Theomen = 128×128×1 (flat)
  },
  "fields": [                        // ordered schema — each declares its BIT encoding (see §3.2)
    { "name": "elevation",  "encoding": "int",   "bits": 16, "scale": 0.5, "offset": -5000 },
    { "name": "biome",      "encoding": "uint",  "bits": 5 },        // categorical id per cell
    { "name": "forest",     "encoding": "unorm8" },                  // 8-bit normalized 0..1
    { "name": "is_coastal", "encoding": "bit" }                      // 1 bit/cell
  ],
  "regions":  [ /* {cx,cy,radius,type,value?} … inline, low-cardinality (S1j) */ ],
  "markers":  [ /* {x,y,kind,angle?,scale?} … inline points (S1j) */ ],
  "chunks":   "chunks"                // dir (relative to the manifest) holding the per-chunk blobs
}
```

> **As implemented** (`Manifest.h`, `emitManifest`/`parseManifest`): `int`/`uint` carry a separate **`bits`**
> (there is no `"int16"` token); `scale`/`offset`/region `value`/marker `angle`,`scale` are omitted when
> identity. **Producers should use the C++ writer** (`emitManifest`/`disk::writeWorldDocument`), not hand-roll
> this JSON. **`palettes`/`lenses`/`frames` are NOT in the manifest yet** — the §5 data-driven recipe is spec;
> today the **consumer builds the lens in C++** (e.g. `makeTerrainLens`). A producer writes fields + overlays;
> the viewer/game picks the lens.

### 3.2 Fields are bit-packed and self-describing ("bit par bit")

Each field declares **how many bits and how to decode** — no wasted width. A flag costs 1 bit, a 19-biome
category costs 5 bits, elevation 16 bits scaled to metres. The viewer reads the declaration and knows how
to unpack any field **without knowing what it means**.

Encodings (v1, **as implemented**): `bit` (1 bit), `uint`/`int` (carry a `bits` field, N≤32), `unorm8`/
`unorm16` (normalized 0..1), `float32`. A **categorical** field is just a `uint` (the id per cell; the category
→ colour table lives in the palette, not the field). Optional `scale`+`offset` maps a stored int to a physical
range (`phys = raw·scale + offset`; a producer stores `raw = round((phys − offset)/scale)`). **`float16` is
deferred** (no native type — add a conversion routine when a producer needs it).

### 3.3 Chunks are sparse — `absent ≠ zero` (fail-franc)

Each chunk blob carries a **field-presence mask**: it may contain `elevation`+`biome` but **not**
`temperature`. Absent fields are **not stored** (zero bytes, not zeroed values).

> **Doctrine:** missing data is *explicitly missing* (layer doesn't draw / transparent), **never** silently
> `0` (else "no elevation" = sea level — wrong). A generator that produces only elevation → a perfectly
> valid world-document; the viewer shows the elevation layer and greys the rest. No fallback masking a hole.

### 3.4 Chunk blob layout

```
[chunk header]  chunkCoord (x,y,z) · presenceMask (which fields) · cellCount · compression flag
[per present field, columnar]  bit-packed values, cells in row-major (then z-major) order
```
Columnar (field-major, not cell-major) → packing is uniform per field, decodes fast, compresses well.

### 3.5 Compression — on, and **off the hot path**

Two composing levels:
1. **Bit-packing** (§3.2) = free "semantic" compression (no wasted bits).
2. **Optional per-chunk** general compressor on the blob — a **per-chunk flag**. v1 ships **zlib via the
   vendored miniz** (in-tree, zero new dep); LZ4/zstd is a later swap only if a profile shows chunk-load hitching.

**Why it doesn't slow rendering:** chunks load **on demand when the camera nears**, not per frame. Decompress
cost is paid **once at chunk-load**, off the render path. LZ4 decompresses at GB/s → invisible. At the screen,
data is already decompressed.

### 3.6 Disk-packed, RAM-expanded

Bit-packing's one cost: you can't `memcpy` a bit-packed field into a float array — you unpack bit-by-bit.
**Mitigation (standard):** bit-packing is a **disk** concern. On chunk-load, **unpack once** into a fast
flat in-RAM form; the render path reads the fast arrays. Compact on disk, fast in RAM. (Infinite world →
RAM holds only visible chunks = bounded, so unpacking the working set is cheap.)

### 3.7 Z-level — present in the format, single-slice in v1 render

The coordinate is `(x, y, z)`. A chunk is a **box** `W×H×D`. **Theomen is flat** (`D=1`, `z≡0`) → the format
**degenerates exactly** to the 2D case. The driver for Z is a **named future consumer** — a DF-like digs
through stacked horizontal slices (Theomen's own doc lists it: *"consommateurs … un futur DF-like"*).

- **Format/addressing carries Z from day 1** — cheap now, a contract-rewrite to retrofit.
- **v1 render = one z-slice, top-down**, with up/down navigation (the DF model). Theomen uses `z=0` only.
- **Deferred (plug-ins, no rework):** iso height-stacking, dig-through, inter-slice occlusion, multi-slice composite.
- **Chunk-size rule:** keep **~16k–256k cells/chunk** regardless of dimensionality. `128×128×1 = 16k` ✓;
  `128×128×128 = 2M` ✗ → for deep worlds, **chunk Z thinner** (e.g. `128×128×16`, or cubic `64³`), Minecraft-style.
  `chunkDims` is a **manifest parameter**, not a hardcoded law: Theomen picks `128×128×1`; a voxel world picks its own.

---

## 4. `grove::mapview` architecture — header-only, host-driven, pure

**Header-only** (Alexi's call), like `camera`/`anim`/`ZoneNavigator`. **Not** an IModule: no IIO topics of
its own. The **host drives it** each frame (feed camera → `update` → drain render commands), exactly the
`ZoneNavigator` pattern. "Self-contained module" = `#include` and go, not an `.so`/IModule. Stateful is fine
(header-only ≠ stateless — it owns the chunk cache + active lens), but **all I/O is injected** (the
`ChunkProvider`), so the core stays pure compute → headless TDD.

### The per-frame pipeline (pure)

```
host: setCamera(view) ──▶ mapview.update(dt):
   1. cull        — visible chunks = chunksIntersecting(viewport ⊕ margin ring), at current z-slice
   2. stream      — request missing visible chunks via ChunkProvider; evict LRU beyond budget
   3. (on load)   — decompress + bit-unpack chunk → fast RAM arrays (§3.6)
   4. compile     — for the active Lens, for each visible cell: field → palette → RGBA,
                    cellCoord → (Topology) worldPos → (Projection) screen quad, assign layer/z
   5. emit        — fill caller-provided buffers: SpriteInstance[] (cells), sector specs (regions),
                    sprite specs (markers).  NO allocation in steady state (streamlined, Alexi #3)
host: renderer.submitSpriteBatch(buf, n)  +  publish region/marker IIO  ──▶ drawn
```

### Sketched types (spec, not code)

```
struct CellCoord { int32 x, y; int16 z; };
struct ChunkCoord { int32 x, y; int16 z; };

struct IGridLayout {          // ① Topology
  WorldPos cellToWorld(CellCoord);
  CellCoord worldToCell(WorldPos);          // the pick
  span<CellCoord> neighbours(CellCoord);
  CellQuad cellQuad(CellCoord);             // 4 corners in world space (shape-aware)
};
struct IProjection {          // ② Projection
  ScreenPos worldToScreen(WorldPos, Camera);
  float     depthKey(WorldPos);             // painter's order (iso); constant for top-down
};
struct IChunkProvider {       // ③ Provider — host implements the actual load
  bool   has(ChunkCoord);
  Chunk* load(ChunkCoord);                  // sync; or returns null + async fill (host's choice)
  void   release(ChunkCoord);
};

class MapView {               // the stateful header-only object the host owns
  void setProvider(IChunkProvider&);
  void setLayout(IGridLayout&);             // ① default: SquareLayout
  void setProjection(IProjection&);         // ② default: TopDownProjection
  void setLens(const Lens&);                // §5
  void setCamera(const Camera&);
  void setZSlice(int z);
  void update(float dt);                    // the pipeline above
  size_t drainCells(SpriteInstance* out, size_t cap);   // streamlined: fill caller buffer
  span<RegionDraw> drainRegions();
  span<MarkerDraw> drainMarkers();
};
```

---

## 5. The recipe system (the "filter / colorize / modular" wish)

Four composable bricks — the modular core, all **data-driven** (in the manifest, no recompile):

- **Palette** — `field → RGBA`. `{ ramp | banded | categorical }` + an optional **`hillshade`** modifier
  (derived from a field's gradient, multiplies the base — the fake-3D relief, in 2D, for free).
- **Filter** — a predicate / transform: `field op value` (`elevation > seaLevel`), `regionType == oceanic`,
  `biome == x`, composable AND/OR. **Minimal predicate set, not a Turing-complete DSL** (premature generality).
- **Layer** — one renderable: `(field × palette × filter)` **or** `(regionSet × style)` **or**
  `(markerSet × icons)` + `opacity` + `blend`.
- **Lens** — an **ordered stack of Layers** + camera + animation = one **named view**. E.g. the *tectonic
  lens* = greyed elevation-hillshade base + region circles (coloured by type) + drift-velocity arrows.

> **Implemented (S1e–h, 2026-06-30):** Palette = ramp / banded / categorical / **diverging** / **stepped**;
> **Hillshade** (Lambertian relief, `Hillshade.h`) wired as a Layer modifier (`hillshadeField` + light), with
> cross-chunk gradient sampling; **Filter** is composable AND/OR/NOT **and cross-field** (`cmpField`, resolved
> by a sampler — a named field absent at a cell fails franc). **Region & marker layers** done (S1i):
> `regionSet × style` (circles by type/value, disc or ring → `RegionDraw`/render:sector) + `markerSet × icons`
> (points by kind, scaled/rotated → `MarkerDraw`/render:sprite), global vector sets culled by viewport.
>
> **Overlay format / recentring (S1j, 2026-06-30):** AREAS (tectonic plates, biome zones) are NOT vector
> overlays — they are a per-cell **categorical field** (`plate_id` + categorical palette), already rendered
> by the chunked/streamed field pipeline with exact boundaries. So overlays need **no chunked vector format**.
> Only genuine sub-cell **points** (markers) and abstract circles fall outside fields; being low-cardinality,
> they live as **inline JSON lists in the manifest** (`regions`/`markers`), not a `regions.bin`/`markers.bin`
> blob (the blob format described earlier was over-engineering and is NOT built). The host reads the manifest
> and hands the sets to `MapView` via `setRegions`/`setMarkers`.

> **The kicker (why phase-driven views matter for Theomen):** each worldgen phase has a *protagonist*
> datum (P2 = drifting plates, P3 = the rising sea, P6B = crystallizing biomes). A `view_config` mapping
> `phase → default lens` makes **looking at a phase = verifying it** (P3 must fill to ~30% land — if it
> drowns everything, you *see* it). The lens is the worldgen's own E2E assertion, rendered. The viewer
> stays a dumb generic layer-renderer; the JSON says what to show when.

Colour is **continuous & free** on the bulk path (§6): `SpriteInstance` carries per-instance `r,g,b,a`, so
the cell colour is computed CPU-side (field → palette → RGBA) and set as the tint. The old banding-vs-LUT
fork **dissolves** — it only existed on the tilemap path (tile-id → atlas slice).

---

## 6. Rendering — bulk-sprite path (v1), per-instance everything

v1 backend = the **bulk sprite path** (`BgfxRendererModule::submitSpriteBatch`, benchmarked ~100k–400k/frame,
bypasses IIO+JSON). It is **topology/projection/colour/layer-agnostic by construction**, because
`SpriteInstance` already carries, *per cell*:

| field | drives |
|---|---|
| `x, y, scaleX, scaleY, rotation` | arbitrary quad placement → hex / iso / rect positions |
| `r, g, b, a` | continuous per-cell colour (§5) — free |
| `layer` | z-order → render layering **and** iso depth-sort |
| `u0,v0,u1,v1, textureId` | textured cells later (sprite-tiles, iso diamond art) |

- **Regions** → `render:sector` (rings/wedges) — **BUILT (2026-07-01):** `MapView::regionDraws()` compiled
  from `regionSet × style`, published by the viewer as **world-space ring-sectors** (pan/zoom with the map,
  `packRGBA8` → the renderer's `0xRRGGBBAA`). **Markers** → `render:sprite{asset}` (streamed icons) — built.
  E2E: `MapViewViewerE2E` (regions compile at the fit view) + `mapview_viewer_selftest.png` (coloured rings
  on screen over the terrain).
- **Tilemap fast-lane — BUILT & productized into the core (T2/T3, 2026-07-01):** the retained `render:tilemap`
  pass is the *specialized* optimization for the **square · top-down** case (hex/iso break its grid assumption).
  `render:tilemap:tileset` binds a PNG tileset; **`TileMapper`** (value → tile id — the tiling sibling of
  Palette, same `banded` semantics) on a **`TileLayer`** makes `MapView` emit one neutral **`TileChunkDraw`**
  per visible chunk (`tileChunks()`); the viewer streams those into the retained tilemap via
  **`TileChunkStreamer`** (add newly-visible / remove departed chunks as the camera pans — the 'T' toggle =
  **live tiling**). TopDown/axis-aligned only. Locked by `MapViewTileMapperUnit` + `MapViewTileStreamerUnit`;
  E2E in `capture_mapview_tiles` (pixels) + `MapViewViewerE2E` (live 'T' + pan lifecycle). A retained tilemap
  still beats the bulk path for huge **static** square terrain (millions of tiles).
- **Honest limit:** the bulk path decouples cost from world size *via culling*, but at **extreme zoom-out**
  the visible static cell count can blow up → needs **LOD / a downsampled overview** (tilemap LOD, or a
  mip/summary texture). **Deferred, not v1** — but the interfaces must not foreclose it (don't bake "one
  quad per cell, always").

---

## 7. Worked example — Theomen elevation, end to end

1. Theomen runs its pipeline headless → its **adapter** writes a world-document: manifest (`square`,
   `128×128×1`, fields `elevation`(int16)+`biome`(uint5)+`forest`(unorm8), a `terrain` palette, a few lenses)
   + chunk blobs (64 chunks for 1000², bit-packed, LZ4'd).
2. The viewer app loads the manifest, wires a `ChunkProvider` reading `data/chunks/`, picks the
   `elevation→terrain` lens, `SquareLayout` + `TopDownProjection`.
3. Per frame: `mapview.update` culls to the viewport, streams the ~handful of visible chunks (unpacks once),
   compiles each visible cell → coloured `SpriteInstance`, fills the host buffer.
4. Host calls `submitSpriteBatch` → the renderer draws. You **see the world**.
5. Switch lens → biome view; toggle the forest layer; scrub the phase timeline (§ frames) → watch the sea
   rise. Same generic machine throughout.

---

## 8. Slice plan (abstraction maximalist, impl incremental)

Build *for* all axes, ship **one combo first**; each later axis plugs into an interface that already exists.

**Status (2026-07-01): S0 ✅ · S1 ✅ · S2 ✅ (interactive viewer, disk-load) · S3-seam ✅ (file-backed
provider — "the file is the interface" proven E2E) · tiling path ✅ (T2/T3 + live retained tiling) · overlays
on screen ✅ (regions/markers)** — 16 MapView ctests (see [`mapview-handoff.md`](mapview-handoff.md) + memory).
Remaining: **S3 Theomen adapter** (cross-project — its Claude writes a real `.world`; the engine already
consumes any `.world` dir), then S4 timeline + S5 plug-ins.

| Slice | Delivers | New axis exercised |
|---|---|---|
| **S0 — format + reader** ✅ | world-document writer/reader (manifest + bit-packed sparse chunks + zlib/miniz), headless tests | the contract |
| **S1 — pure core** ✅ | `MapView` + `SquareLayout` + `TopDownProjection` + `ChunkProvider` + cull/stream/LRU + Palette/Filter/Layer/Lens, headless TDD. Emits neutral **CellDraw** (not SpriteInstance) → core is renderer-independent | ① square, ② top-down, ③ provider |
| **S2 — viewer app** ✅ | in-engine `test_mapview_viewer` (drag-pan / zoom-to-cursor / H·B·T·R keys), **`--load <dir>`** opens a world-document from disk, CellDraw→SpriteInstance adapter, bulk-sprite emit; real input **E2E** (`MapViewViewerE2E`, injected SDL events) | first pixels (E2E) |
| **S3-seam — file-backed provider** ✅ | `WorldDocumentProvider` bridges the on-disk world-document → the pure `MapView` (the "file is the interface" thesis, proven E2E in a unit test + a from-disk capture) | the contract, live |
| **T2/T3 — tiling + overlays on screen** ✅ | `render:tilemap:tileset` + `TileMapper` (value→tile id) + `MapView` tile-chunk emit + **live retained tiling** in the viewer (`TileChunkStreamer`: add/remove chunks on pan, 'T' toggle) + **regions/markers drawn** (`render:sector`/`render:sprite`) | textured tiles + overlays |
| **S3 — Theomen adapter** | `World` → world-document (Theomen-side); see a real generated world | real data |
| **S4 — timeline** | per-phase frames + scrub (deltas → targeted `tilemap:update`-style) | ⑥ time |
| **S5+ — plug-ins** | hex layout · iso projection + depth-sort · infinite/procedural provider · Z multi-slice render · extreme-zoom LOD · palette-LUT | the deferred axes |

S0→S3 = "see Theomen's world, generically". Everything after slots into S1's interfaces without rework.

---

## 9. Decisions (locked by Alexi, 2026-06-30)

- **Generic, not Theomen-specific.** A neutral world-viewer; Theomen is one producer. Generalize the
  data-viz primitives (Field/Palette/Filter — stable GIS domain); **do not** generalize phases/lenses (that's
  the adapter/config). Neutral project name (not "theomen_viewer").
- **Maximalist abstraction / incremental implementation.** Design for all 3 axes; ship square·top-down·bounded
  first; the rest plugs in.
- **`grove::mapview` = header-only**, host-driven (no IModule), pure core + injected I/O. Self-contained = `#include`.
- **Bulk-sprite render path** for v1 (streamlined, caller-buffer fill); tilemap fast-lane later.
- **Best-effort colour from the start** — continuous per-instance tint (free on the bulk path).
- **Iso = future-games axis** — Projection interface in day 1, iso impl + depth-sort later, no rework.
- **Z-level in the format day 1** (`(x,y,z)`, `W×H×D` chunks, manifest-declared dims), **single-slice render
  in v1**; Theomen is flat (`D=1`). Drive: the planned DF-like. Cap ~16k–256k cells/chunk (chunk Z thinner for deep worlds).
- **Format = chunked · columnar · bit-packed self-describing · sparse (absent≠zero, fail-franc) · compressed
  (bit-pack + optional zlib/miniz per chunk — LZ4/zstd later — off the hot path) · disk-packed/RAM-expanded.**

## 10. Open / deferred (don't foreclose)

- Extreme-zoom-out LOD (downsampled overview) — bulk path needs it eventually.
- Palette-LUT continuous colour on the *tilemap* fast-lane (small shader add) — only if/when we want the
  tilemap path for square·top-down.
- Hex Z (cube-coord + Z), iso inter-slice occlusion, "tall" objects overlapping front cells.
- Async streaming policy (worker decode, like `ThreadedDecoder`) — the `ChunkProvider` already allows it
  (host's impl); spec the back-pressure when we hit it.
- `view_config.json` phase→lens mapping (Theomen-side, data-driven) — the "looking = verifying" tooling.

