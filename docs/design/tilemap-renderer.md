# Tilemap renderer — design

High-perf tilemap for **tens of thousands of tiles** with **seamless zoom** (tactical ↔ system),
pan, occasional updates (fog/terrain), 60 fps. CPU/frame ≈ 0 by design — all the work is on the
GPU (shader) and in the RHI. Not yet implemented; this is the agreed design.

## Architecture (GPU-resident, index-texture)

**Detail band (zoomed in).**
- Tile grid = **index texture `R16UI`** (1 texel/tile), uploaded once; `updateTexture` on changed
  texels. 360k tiles = 600×600 = 720 KB. One index texture caps at ~16384² = 268M tiles, so a
  single texture covers our scale with huge margin.
- Draw **1 quad per chunk**. Fragment shader, per pixel:
  - `ivec2 cell = ivec2(floor(worldPos / tileSize)); uint idx = texelFetch(indexTex, cell, 0).r;`
    — **integer texelFetch, POINT, no mip, no normalized-UV filter** (avoids the tile-boundary
    off-by-one).
  - sample the atlas at `vec3(frac(worldPos/tileSize), idx)`.

**Atlas = `texture2DArray` (1 tile = 1 layer).** This is the bleeding fix: sub-UV is `[0,1]`
*within* a layer, so there is no neighbor cell to bleed into, and **mips are per-tile/correct**
under minification (a packed atlas bleeds across cell borders, worst at zoom-out). Also makes
animated tiles trivial (frame = layer offset). `BGFX_CAPS_TEXTURE_2D_ARRAY` is universal.

**Per-pixel LOD via screen-space derivatives** (NOT a global zoom uniform). Metric = tiles per
pixel = `length(fwidth(tileCoord))`:
- atlas mip via `textureGrad(atlasArray, uv, ddx, ddy)` → crisp big, properly minified small, no
  aliasing/bleeding;
- detail↔LOD crossfade = `smoothstep` over tiles/pixel (~1→4 tpx), per pixel.

**LOD band (zoomed out) — color, because indices can't be mipped.**
- Game-provided **palette `tileIndex → color`** (art-directable: terrain/faction tint). Engine
  bakes `LOD[texel] = palette[index]`, RGBA8 ~1.9 MB + **CPU box-filter mips**. GPU trilinear =
  automatic LOD; blends with the detail band → seamless.
- Detail (index→atlas) and LOD (color) are two genuinely-distinct representations (the index path
  can't be the mipped color path). But the TRANSITION — smoothstep crossfade vs a hard threshold
  switch — is a **tuning detail, not a dogma**; no production source confirms a band crossfade
  (Factorio's mip selection is hardware-continuous within one atlas). Don't over-engineer it; tune
  at implementation. **Point/NEAREST sampling does NOT substitute for the mipped color band** —
  it kills interpolation bleed at one mip but does nothing for minification aliasing at zoom-out,
  so the separate mipped color representation is mandatory, not optional.

**Fog / per-tile state = mipped `R8`**, sampled in BOTH detail and LOD. **Encode it as a SCALAR
visibility (0..1), NOT discrete enum states** — mip box-filtering a scalar is meaningful (gives
partial-reveal edges when zoomed out), whereas averaging discrete state *indices* is nonsense (same
trap as the tile indices). So fog dims correctly at every zoom; `updateTexture` a few texels on
reveal. *(Refinement from the SOTA research — see below.)*

**Layers** (terrain / decals / objects) = N index textures, N blended draws (each ~0 CPU).
**Animated tiles** (water/lava) = a per-tile-type table (`animated?`, frames, fps) in a tiny 1D
texture/uniform → time-based layer offset. Shares the Flipbook timing concept.

## Scope (Drifterra: tens of thousands + seamless zoom)
- **Must:** index `R16UI` + texelFetch, atlas `texture2DArray`, derivative LOD + crossfade,
  palette LOD, mipped `R8` state for fog.
- **Probable:** multi-layer (terrain + overlay), animated tiles.
- **Skip:** chunked/streamed multi-index-textures — only needed beyond ~16384² (multi-million-tile
  worlds). Note the threshold; don't build it.

## Implementation reality — this is an RHI capability bump, not a weekend
The current RHI/`TextureDesc` exposes only basic RGBA8 sprite textures. This design needs the RHI
to add: **integer texture formats (R16UI), texture-array textures, POINT/CLAMP sampler flags**, a
`*_tilemap` shader using `texelFetch` / `textureGrad` / `sampler2DArray`, and CPU mip generation +
upload for the index/LOD/state textures. bgfx supports all of it single-threaded; our abstraction
doesn't surface it yet. Real, bounded engine work.

## Slicing
- **A — detail:** RHI extensions (R16UI, array, sampler flags) + `*_tilemap` shader + retained
  index grid (`render:tilemap:add/update/remove`) + atlas-array + texelFetch. Kills the CPU cost.
- **B — LOD:** palette → mipped color + derivative crossfade.
- **Then as needed:** mipped R8 fog/state, multi-layer, animated tiles.

## Validated against the state of the art (deep-research, 2026-06-17)
A multi-source, adversarially-verified survey confirmed this design matches current best practice
on **every major axis**:
- **Index-texture + texelFetch, 1 draw/chunk** — multiply attested (Tristeon, paavo.me,
  WebGLFundamentals; + Phaser/Godot/PixiJS). Draw/vertex cost is world-size-independent.
- **Index can't be mipped → separate mippable COLOR for LOD** — exactly what **Factorio** does
  (mips its color/sprite atlas, streams the mip per zoom — FFF-264/227). Our palette→mipped RGBA8
  is the small-engine analogue.
- **Derivative-driven LOD** (`length(fwidth)` → mip = log2(ρ)) — canonical, OpenGL spec §8.14.
- **`textureGrad` with explicit gradients** — the right call *because* tilemap UVs jump at tile
  boundaries where implicit ddx/ddy misbehave (0fps.net, Ben Golus). array **+** textureGrad =
  correct.
- **`texture2DArray` atlas** — the recommended bleeding fix (never samples across layers; Khronos,
  0fps.net). Packed-atlas bleeding under minification is real & unavoidable (0fps.net, Halladay,
  Polycount, US patent US10839590B2).
- **Chunk/stream beyond ~16384²** = SVT / tiled-resources (Sean Barrett, id Tech 5); our
  chunk-per-quad already aligns. **Not needed at our scale** (one index texture ≈ 268M tiles).

**Honest caveats from the research:**
- DON'T cite the "1M tiles in 0.05 ms / O(1)" benchmark (Tristeon) — refuted; the qualitative
  scaling holds, the number doesn't.
- The exact **R16UI + texelFetch path is attested by indie/web blogs + educational refs**, not a
  AAA primary source. AAA (Factorio) is **sprite/RTT-based**, not index-texture. Our path is the
  well-known indie/web approach — a legitimate choice, just not the AAA one.
- **Animated tiles & multi-layer compositing** on the index path are reasonable design items but
  **not literature-validated** here — treat as unproven until a running visual test exists.

## Known scaling lever (deferred unless measured)
**Factorio's persistent terrain buffer + scroll-reuse** (FFF-333): render terrain to a buffer kept
across frames; on camera move, adjust the buffer offset and only re-render newly-exposed edge
strips → re-render cost ∝ scroll distance, not screen area. **Only pays off if per-pixel
composition is expensive** (complex tile-transition/blend rules). Our texelFetch + 1-quad path has
near-free composition, so this is optional — a known lever IF we later add costly terrain
transitions. Backlog.

## Status
**This is the next major project.** Design is pinned + SOTA-validated. Implementation = an RHI
capability bump (R16UI / array textures / POINT-CLAMP samplers / `*_tilemap` shader / CPU mip
upload), sliced A (detail) → B (LOD) → fog/multi-layer/animated as needed. Not a weekend.

## Out (over-engineering here)
GPU-driven / compute-culled / `multiDrawIndirect` — pointless for a tilemap (index-texture is
already 1 draw) and blocked by our single-threaded bgfx config.
