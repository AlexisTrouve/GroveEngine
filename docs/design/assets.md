# Asset System (streaming textures)

Streaming texture assets for the BgfxRenderer: **thousands of assets *available*, only a budget's worth
*resident*.** A game references a texture by a stable **string `assetId`**; the engine loads it on demand (or
on preload), caches it under a configurable **VRAM budget**, and evicts by **priority + LRU**. Small sprites
can share **atlas** sheets (pre-baked or packed at runtime).

> Status: **phases 1, 2, 2b, 3 shipped.** The async-load refinement (phase 3) is in and opt-in.

---

## Why

One texture per asset doesn't scale to thousands (handles, VRAM, draw-call texture switches). The key insight:
you only *see* a few dozen textures at once. So:

- **available** = a **registry** (`id → {path, priority, group}`) — just metadata, ~0 cost, thousands of entries fine.
- **resident** = a **cache** with a VRAM budget + eviction.
- everything else = **streamed** in on demand, evicted when cold.

## Architecture

Mirrors the SoundManager pattern — pure logic + a backend behind an interface, so the brain is unit-tested
without a GPU.

| Piece | File | Role |
|-------|------|------|
| `AssetManager` | `modules/BgfxRenderer/Assets/AssetManager.h` | **Pure logic.** Registry + priority/LRU cache under a VRAM budget. Decides *what* to load/evict. Header-only, std-only. |
| `ITextureProvider` | (same header) | GPU-facing interface: `load(path)→texId`, `unload(texId)`, `bytes(texId)`. |
| `BgfxTextureProvider` | `Assets/BgfxTextureProvider.h` | Real provider over bgfx: `TextureLoader::loadFromFile` → `ResourceCache::registerTexture` (renderable id) → `unloadById`. |
| `ShelfPacker` | `Assets/ShelfPacker.h` | Pure shelf bin-packing (`shelfPack(rects, maxWidth, gutter)`). |
| `AtlasPacker` | `Assets/AtlasPacker.h` | `packAtlas(...)` — decode N PNGs + pack + assemble + upload + register sub-sprites. |
| `IAsyncDecoder` | (in `AssetManager.h`) | Off-thread decode interface: `request(id,path)`, `poll(out)`, `pending()`. |
| `ThreadedDecoder` | `Assets/ThreadedDecoder.h` | Real decoder: worker thread(s) run `decodeRgba` (CPU/stb), `poll()` hands finished pixels to the render thread (phase 3). |

The `AssetManager` is owned by `BgfxRendererModule` (config `assetVramBudgetMB`, default 256) and handed to
the `SceneCollector`, which resolves a sprite's `asset` id at collect time.

## The cache — budget, priority, LRU

`resolve(id)` returns the resident backend texture id, loading on demand. After a load, `evictToFit` trims to
stay under budget:

- eviction order = **lowest `priority` first, then least-recently-used** (a monotonic access counter, not wall
  clock — deterministic);
- the just-loaded asset is never the victim;
- **pinned** assets (runtime-packed sheets — see below) are never evicted (they have no path to reload from).

`priority` is **dynamic** (`setPriority` / `asset:setPriority`) and also orders preloads (high first).

## Referencing a texture from a sprite

`render:sprite` (and `:add`/`:update`) accept an **`asset`** string field; it wins over a numeric `textureId`:

```jsonc
{ "asset": "icons/iron", "x": 10, "y": 10, "scaleX": 32, "scaleY": 32, "layer": 1000 }
```

`SceneCollector::resolveSpriteTexture` resolves it → loads/caches the texture → sets the sprite's `textureId`
**and** (for an atlas sub-sprite) writes its UV rect into the sprite. So atlas icons render their sub-region
with the **existing sprite shader** — no shader change (the sprite already carries `u0..v1`).

**From the UI** (sprite-as-UI): `UIButton` and `UIImage` expose an **`asset`** prop (literal or data-bound
`"asset":"{{icon}}"`). When set, the widget publishes `render:sprite:add{asset:"id"}` (instead of a numeric
`textureId`), so thousands of atlas-packed icons/parts stream into the UI by stable id — the same resolve +
budget + atlas path as world sprites. `asset` wins over `texture`. Locked by `UIAssetSpriteE2E` (IT_052).

## Atlases

Many small sprites share **one** resident sheet texture (fewer handles, batchable).

- **A sub-sprite** (`registerAtlasSprite(id, sheetId, u0,v0,u1,v1)`) has no texture of its own — it points at a
  **sheet** asset + a UV rect. `resolveSprite(id, &uv)` resolves the *sheet* (one shared resident texture) and
  yields the sub-rect UVs.
- **Pre-baked** (phase 2): the sheet PNG + UV rects are authored offline (TexturePacker-style) and declared in
  the manifest's `atlases` section.
- **Runtime** (phase 2b): `packAtlas(...)` / the `asset:pack` topic decode N *separate* PNGs, shelf-pack them
  into one sheet at load time, upload once, and register the sub-sprites. The packed sheet is **pinned**.

## Async load (phase 3) — decode off-thread, upload on the render thread

By default `resolve(id)` is **synchronous**: it decodes the PNG (stb, the slow part) *and* uploads to the GPU
on the calling/render thread → a visible **first-touch hitch** the first time an asset is referenced.

Async load splits that in two, behind the `IAsyncDecoder` interface (mirrors `ITextureProvider`):

- **decode** runs on a worker thread (`ThreadedDecoder` → `TextureLoader::decodeRgba`, pure CPU, no bgfx);
- **upload** stays on the render thread (bgfx is single-threaded) — `pumpAsync()` drains finished decodes once
  per frame and calls `ITextureProvider::upload(pixels,w,h)`.

State machine (in `AssetManager`):

- `resolve(id)` on a not-yet-resident asset **does not block** — it requests the decode **once** (`loading`
  flag prevents duplicate requests for a sprite drawn every frame) and returns a **placeholder** texid
  (`setPlaceholder`, default 0) so the caller keeps drawing this frame.
- `pumpAsync()` (called in `process()` before collect) uploads what finished → the asset becomes resident and
  the *next* `resolve` returns the real texture. Eviction (`evictToFit`) runs on the freshly uploaded one too.
- A decode/upload **failure is latched** (`failed`) so a per-frame `resolve` of a broken file doesn't re-enqueue
  it forever.

**Opt-in**, off by default (zero behaviour change for existing callers):

```jsonc
{ "assetAsyncLoad": true, "assetDecodeThreads": 1 }   // renderer config
```

Tradeoff (v1): async uploads create the texture with **mipLevels 1** (no mips) to keep the render-thread
upload as cheap as possible — same choice as `AtlasPacker`. Async-loaded standalone textures therefore forgo
trilinear minification (fine for icons/UI drawn near native scale). Building mips on the worker thread is the
natural next refinement if heavily-minified streamed textures ever need it.

## Runtime textures / painting

The game can create a texture at runtime and paint into it, addressed by the same **string id** as any asset:

| Topic | Payload | Effect |
|-------|---------|--------|
| `render:texture:create` | `{id, width, height, color?}` | create an RGBA8 texture filled with `color` (0xRRGGBBAA, default transparent), registered as a **resident** asset by `id` |
| `render:texture:paint` | `{id, x, y, w, h, color}` | fill the sub-rect `[x,y,+w,+h]` with `color` — a GPU region update, no full re-upload |

Because it's registered as a resident asset, the canvas renders like anything else: `render:sprite{asset:"id"}`
or a UI widget's `asset` prop. Use it for procedural textures, minimaps, paint/mask layers, fog overlays, etc.

> **bgfx gotcha (locked in):** a texture created **with** initial data is *immutable* — `updateTexture` on it
> is silently ignored. So a paintable canvas is created **empty** (mutable) and filled via a region update.
> Same reason the tilemap index grid is created mutable. Locked by `RuntimeTextureGpu` (create RED + paint a
> GREEN corner → a framebuffer readback shows both colours).
>
> v1 paints **solid-colour** rects (no binary pixel transport). Arbitrary-pixel upload (base64/raw) is the
> natural follow-on if a use case needs it.

## Feeding the registry — manifest + topics

Both, as designed:

### Declarative manifest (boot) — config `assetManifest` = a json file
```jsonc
{
  "assets": [
    { "id": "icons/iron", "path": "assets/textures/items/iron.png", "priority": 5, "group": "ui_icons" }
  ],
  "atlases": [
    { "sheet": "uiSheet", "path": "assets/textures/ui_sheet.png", "group": "ui",
      "sprites": [ { "id": "ui/play", "u0": 0.0, "v0": 0.0, "u1": 0.25, "v1": 0.25 } ] }
  ]
}
```
Registers metadata only — nothing loads until referenced or preloaded.

### Runtime topics
| Topic | Payload | Effect |
|-------|---------|--------|
| `asset:register` | `{id, path, priority?, group?}` | register a standalone asset |
| `asset:preload` | `{group}` | load a whole group now (high priority first) |
| `asset:setPriority` | `{id, priority}` | re-prioritise (affects eviction) |
| `asset:unload` | `{id}` | drop a resident asset |
| `asset:pack` | `{sheet, sprites:[{id,path}], maxWidth?, gutter?, priority?, group?}` | runtime-pack N PNGs into one sheet |

### Config keys (renderer)
- `assetVramBudgetMB` — resident budget (default 256).
- `assetManifest` — path to the boot manifest (optional).
- `assetAsyncLoad` — decode off-thread instead of blocking the render thread (default `false`, phase 3).
- `assetDecodeThreads` — worker threads when async is on (default 1).

## Tests (regression locks)
- `AssetManagerUnit` — registry, on-demand+cache, budget eviction, priority protection, LRU, dynamic
  setPriority, preloadGroup, unload, **atlas sub-sprites share one sheet + UVs**. (headless, no GPU)
- `ShelfPackerUnit` — packed rects in-bounds, never overlap, over-wide fails. (headless)
- `AssetProviderGpu` — real PNGs load/evict with priority on a real device.
- `AssetSpriteGpu` — `render:sprite{asset}` streams the texture in.
- `AssetTopicsGpu` — manifest (assets + atlas) at boot + `asset:*` topics.
- `AtlasPackerGpu` — `packAtlas` + `asset:pack` build one shared sheet with distinct UVs.
- `AssetAsyncUnit` — async state machine: placeholder while decoding, no duplicate requests, upload-on-pump,
  failure latch, sync mode untouched when no decoder is set. (headless, mock provider + mock decoder)
- `AssetAsyncGpu` — real `ThreadedDecoder` decodes a PNG off-thread, `pumpAsync` uploads it on a real device.
- `AssetAsyncModuleGpu` — E2E through `BgfxRendererModule`: `assetAsyncLoad` flag honored, `pumpAsync` runs in
  `process()`, a `render:sprite{asset}` streams in across frames (`isLoading` proves the async branch was taken).

## Phases
1. ✅ Registry + cache (budget/priority/LRU) + manifest + topics + `render:sprite` by id.
2. ✅ Pre-baked atlas (sheet + UV sub-sprites).
2b. ✅ Runtime atlas packing (`ShelfPacker` + `AtlasPacker` + `asset:pack`).
3. ✅ Async load — decode on a worker thread (`ThreadedDecoder`), upload on the render thread (`pumpAsync`).
   Opt-in via `assetAsyncLoad`. Removes the first-touch hitch.
