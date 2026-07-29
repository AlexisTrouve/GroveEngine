# Tilemap renderer — design

> ✅ **SHIPPED A→B (2026-06-18/19)** — see *Implementation status* below. This page is the **agreed design, now in code**; the future-tense passages ("not yet implemented", "the RHI doesn't surface it yet") are **historical**. Topic-contract source of truth = `modules/BgfxRenderer/Scene/SceneCollector.cpp`. *(Cross-track resync by the Drifterra Claude, 2026-06-20.)*

High-perf tilemap for **tens of thousands of tiles** with **seamless zoom** (tactical ↔ system),
pan, occasional updates (fog/terrain), 60 fps. CPU/frame ≈ 0 by design — all the work is on the
GPU (shader) and in the RHI. This is the agreed design — **now implemented (shipped A→B, see status below).**

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
- ✅ **Shipped, and the colour table is resolved in three steps** (`TilemapPass::lodTableFor`):
  an explicit palette the game pushed (`render:tilemap:palette`) > the table **derived from the
  bound tileset** (each layer's alpha-weighted average, computed at load — `atlas::averageLayers`)
  > the built-in 8 colours (`lod::paletteColor`). Deriving is the DEFAULT so both bands agree
  without the game maintaining a parallel palette; a tileset's dezoomed colours are its own art.
  `textureId 0` (procedural atlas) has no derived table → historical output, byte-identical.
  Tables are baked into the LOD texture, which is cached per chunk id, so any table change bumps
  `m_lodEpoch` and forces a re-bake — otherwise the feature would silently depend on publish order.
  Details + rationale: `docs/design/tilemap-lod-palette.md`.
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

## Implementation reality — this is an RHI capability bump, not a weekend **(✅ done — see Implementation status)**
*(Historical — the RHI work below is now shipped.)* The current RHI/`TextureDesc` exposes only basic RGBA8 sprite textures. This design needs the RHI
to add: **integer texture formats (R16UI), texture-array textures, POINT/CLAMP sampler flags**, a
`*_tilemap` shader using `texelFetch` / `textureGrad` / `sampler2DArray`, and CPU mip generation +
upload for the index/LOD/state textures. bgfx supports all of it single-threaded; our abstraction
doesn't surface it yet. Real, bounded engine work.

## Slicing
- **A — detail:** ✅ RHI extensions (R16UI, array, sampler flags) + `*_tilemap` shader + retained
  index grid (`render:tilemap:add/update/remove`) + atlas-array + texelFetch. Kills the CPU cost.
- **B — LOD:** ✅ palette → mipped color + derivative crossfade.
- **Then as needed:** ✅ mipped R8 fog/state · ⬜ multi-layer, animated tiles.

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

## Implementation status — shipped A → B (2026-06-18/19)
The detail band AND the zoom-out LOD band are implemented on `master` and tested.

**Done (commit):**
- A0 — RHI `R16UI` integer format + POINT/CLAMP sampler control (`a995253`)
- A1 — `updateTexture` sub-region overload (`900029a`)
- A2 — `*_tilemap` shader (index `texelFetch`) + `TilemapPass` rewritten to **1 quad/chunk**;
  the command replay gained **multi-texture binding** (`d3532be`, `a6c915e`)
- A3 — atlas as `texture2DArray` + procedural color atlas (`dbedcb2`, `03cfcd8`)
- A4.1 — retained `render:tilemap:add/update/remove` (by `id`) → **upload-once** (`88f5dee`)
- B1 — per-chunk mipped LOD color texture (palette + CPU box-filter) (`d47402c`)
- B2 — detail↔LOD **derivative crossfade** (`a03cbd2`)
- Tests — `PassCullingUnit` (1 draw/chunk, chunk cull, upload-once), `SceneCollectorTest` (retained
  CRUD + dirty cycle), `RhiTextureDescUnit`, `LodColorUnit` (box-filter == average — the LOD oracle),
  `RhiReadbackGpu` (offscreen render → readback foundation, `7852855`), `TilemapLodGpu`
  (end-to-end: detail → tile color, zoom-out → average color, asserted analytically, `ef16cf7`)
- A3.3 — real atlas: `AtlasSlice` (grid → row-major layers) + `TextureLoader::loadArrayFromFile`
  (PNG → array) + `TilemapPass::setTileset(textureId, array)`; a chunk's `textureId` picks the real
  atlas over the procedural one (`7a6e487`, `c068f21`, `0ea9d6c`). Tests: `AtlasSliceUnit`,
  `TextureArrayUnit`, + the tileset case in `TilemapLodGpu`.
- A4.2 — partial sub-rect updates: `render:tilemap:update {id,x,y,w,h,tileData}` patches a block into
  a retained chunk; the pass uploads only the dirty rect via the A1 region overload (fog/edits).
  Tests: `SceneCollectorTest` (patch + dirty rect), `PassCullingUnit` (frame 2 region == rect) (`26b27e3`).
- Fog — fog-of-war: per-tile scalar visibility (`fogData`) baked as a mipped **R8** texture per chunk
  sampled in fs_tilemap (`s_fog`) and multiplied into the color (`a0a714d`, `6396f6d`). Tests:
  `LodColorUnit` (R8 box-filter), `TilemapLodGpu` (half-visibility -> half color).
  **Partial-fog reveal ✅ shipped**: `render:tilemap:fog {id,x,y,w,h,fogData}` patches just the R8 mask
  sub-rect (mip 0 region update) — no tile re-upload / LOD re-bake. The fog texture is now **non-mipped +
  mutable** (a with-data bgfx texture is immutable, and the RHI region update writes only mip 0, so reveals
  are exact; trilinear fog at extreme zoom-out gives way to linear mip-0 — negligible for a visibility mask).
  Locked by `TilemapLodGpu` (reveal a sub-rect) + `SceneCollectorTest`. *(`buildR8MipChain` is now unused by
  fog but kept — still oracle-tested by `LodColorUnit`.)*

**Learnings (paid for in blood — don't relearn these):**
- **bgfx HLSL profile is `s_5_0`, NOT `vs_5_0`/`ps_5_0`** — the CMake `compile_bgfx_shader` helper
  passes the wrong one, so its DX11 path is silently broken. The committed `.bin.h` are generated by
  hand via `shaderc --bin2c` (spv/glsl/dx11 real; `mtl` a placeholder — no Metal backend on this
  MinGW toolchain). The runtime forces **Direct3D11**.
- **The command replay bound only ONE texture per submit** — extended to N slots (index + atlas + LOD).
- **`bgfx::setState` is consumed per submit** → emit it per chunk, not once.
- **LOD visibility ⇔ tiles/pixel = `1/(tilePix·zoom)`**, and zoom is clamped `[0.2, 6]`: 16px tiles
  never reach the LOD in range — the demo uses **1px tiles**. `smoothstep(0.5,1.0)` hands over to the
  LOD *before* the point-sampled detail aliases (a tuning knob, not dogma).
- **Atlas**: a procedural color array (one color per id) is the default/fallback; a real grid-PNG is
  sliced into array layers via `loadArrayFromFile` + `setTileset` (A3.3, done). Both are arrays, so
  the `sampler2DArray` bind is always valid.
- **GPU readback is async** (`readTexture` returns a frame number; pump frames) and needs a real
  context → `[gpu]` tests (hidden window + bgfx), skipped on headless CI.
- Flagged pre-existing bug: full-image `updateTexture` uses device `m_width/m_height` (only correct
  for a screen-sized texture); the region overload sidesteps it.

**Multi-layer ✅ shipped** (Strategy A): `render:tilemap:add layers:[{tileData,textureId?}]` — layer 0
opaque + layers >0 alpha-blended back-to-front (state is already `BlendMode::Alpha`; tile id 0 transparent).
Each layer = its own index + LOD; fog shared. Retained only; no shader change. Locked by `TilemapLodGpu` +
`SceneCollectorTest`. **Animated tiles ✅ shipped** (`render:tilemap:anim`). **Not done (backlog):** —

## Out (over-engineering here)
GPU-driven / compute-culled / `multiDrawIndirect` — pointless for a tilemap (index-texture is
already 1 draw) and blocked by our single-threaded bgfx config.


---

## Brouillard : échelle et dérive réglables (2026-07-29)

Le brouillard mélangeait déjà les tuiles cachées avec une **texture échantillonnée en espace monde**
(wrap `Repeat`) plutôt qu'avec du noir — donc un nuage continu, plus grand qu'une tuile et sans
couture. Mais l'échelle était **écrite en dur dans le shader** :

```glsl
vec3 fogColor = texture2D(s_fognoise, worldPos / 64.0).rgb;   // avant
```

Conséquence : un jeu pouvait changer l'IMAGE, pas la taille à laquelle elle se répète. « Utiliser un
asset de brouillard plus grand » était donc littéralement inexprimable — et le brouillard était figé
au boot, sans moyen d'en changer par biome ou de le faire vivre.

**Trois réglages ouverts**, portés par un uniform `u_fogParams {worldScale, offsetX, offsetY}` :

| Réglage | Config (boot) | Topic (à chaud) | Effet |
|---|---|---|---|
| Texture | `fogTexture` | `render:tilemap:fog:style {path}` | l'asset de brouillard |
| Échelle | `fogScale` | `… {scale}` | unités monde couvertes par une tuile de l'asset |
| Décalage | — | `… {offsetX, offsetY}` | déplace l'échantillonnage (rampé par l'hôte = dérive) |

Chaque champ du topic est **optionnel** : publier `{offsetX, offsetY}` chaque frame fait dériver le
nuage sans retoucher la texture ni l'échelle.

⚠️ **Seul le nuage bouge.** Le masque de révélation (`render:tilemap:fog`) n'est pas touché par ces
réglages — un brouillard qui dérive ne doit jamais re-cacher ce que le joueur a exploré.

**Non-régression :** les défauts (`scale = 64`, décalage nul) reproduisent l'ancien rendu **au pixel
près**, donc aucun hôte existant ne bouge.

**Preuve :** `TilemapLodGpu`, au pixel. Une texture de brouillard de 2 texels (rouge/vert) et une
tuile centrale en `worldX = 4` ; les valeurs sont choisies pour tomber au CENTRE d'un texel, donc ni
le filtrage ni l'arrondi ne brouillent le verdict. Vérifié adversarialement : en neutralisant les
deux réglages, les trois rendus donnent le **pixel identique** (`ff005fa0`) et 6 assertions passent
au rouge — c'est exactement le comportement d'avant.

**Reste ouvert** (non fait, pas oublié) : la granularité de douceur du masque est d'**une tuile**
(masque R8 à 1 texel par tuile, filtré linéairement). Le nuage par-dessus le masque assez bien ; des
bords plus fins demanderaient un masque sous-tuile — un vrai chantier, à ne lancer que sur besoin
constaté (DAOS : `vision.md` fait du fog-of-war révélé par le minage un pilier).
