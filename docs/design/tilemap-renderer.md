# Tilemap renderer — design (WIP)

High-perf tilemap for **tens of thousands of tiles** with **seamless zoom** (tactical ↔ system),
pan, occasional updates (fog/terrain), 60 fps. Design in progress — not yet implemented.

## Decided so far

**Detail (zoomed in) — GPU index-texture, not per-tile instances.**
- Tile grid = an **index texture** (`R16U`, 1 texel/tile). 360k tiles = 600×600 = 720 KB, uploaded
  once; `updateTexture` on changed texels.
- Draw **1 quad per chunk**. Fragment shader: `tileCoord = floor(worldUV·mapDim)` → fetch index
  (POINT/`texelFetch`) → atlas cell → sub-UV → sample atlas.
- CPU/frame ≈ 0, upload/frame = 0 (except on change), 1 draw call. Obsoletes per-tile
  `SpriteInstance` generation + per-frame regen/windowing.

**LOD (zoomed out) — color, because indices can't be mipped.**
- Game-provided **palette** `tileIndex → color` (art-directable: terrain/faction tint, not just
  atlas average). Engine bakes `LOD[texel] = palette[index]`, RGBA8 ~1.9 MB + CPU box-filter mips.
- Zoom-out draws this mipped color quad (GPU trilinear = automatic LOD).
- **Crossfade** detail↔LOD in a transition zoom band → seamless, no pop.

**Slicing:** A = index-texture detail (retained grid + shader). B = LOD (palette → mipped color +
crossfade). Design A so B slots in. Optional: per-tile **state texture** (R8) for fog/lighting/tint
sampled in the shader — `updateTexture` a few texels on reveal, ~free.

## Deeper considerations (being worked through — see conversation)
- Atlas **bleeding/minification** at tile edges → **texture-array atlas** (1 layer/tile).
- **Derivative-based** per-pixel LOD/mip (`fwidth`/ddx of tile-space), not a global zoom uniform.
- **texelFetch**-exact index lookup (tile-boundary off-by-one).
- Scaling ceiling: max texture size → **chunked index textures + streaming** beyond VRAM.
- **Multiple layers** (terrain/overlay/objects) + **animated tiles**.
- Fog/state must also dim the **LOD** representation.

## Out (over-engineering for this)
GPU-driven / compute-culled / `multiDrawIndirect` — pointless for a tilemap (index-texture is
already 1 draw) and blocked by our single-threaded bgfx config. Not pursued.
