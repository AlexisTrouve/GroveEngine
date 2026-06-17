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
  automatic LOD; crossfade (above) blends it with the detail band → seamless, no pop.

**Fog / per-tile state = mipped `R8`**, sampled in BOTH detail and LOD (visibility averages fine
under mipping, unlike indices) → fog dims correctly at every zoom; `updateTexture` a few texels on
reveal.

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

## Out (over-engineering here)
GPU-driven / compute-culled / `multiDrawIndirect` — pointless for a tilemap (index-texture is
already 1 draw) and blocked by our single-threaded bgfx config.
