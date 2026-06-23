# Asset System (streaming textures)

Streaming texture assets for the BgfxRenderer: **thousands of assets *available*, only a budget's worth
*resident*.** A game references a texture by a stable **string `assetId`**; the engine loads it on demand (or
on preload), caches it under a configurable **VRAM budget**, and evicts by **priority + LRU**. Small sprites
can share **atlas** sheets (pre-baked or packed at runtime).

> Status: **phases 1, 2, 2b shipped.** Phase 3 (async load) is the only remaining refinement.

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

## Atlases

Many small sprites share **one** resident sheet texture (fewer handles, batchable).

- **A sub-sprite** (`registerAtlasSprite(id, sheetId, u0,v0,u1,v1)`) has no texture of its own — it points at a
  **sheet** asset + a UV rect. `resolveSprite(id, &uv)` resolves the *sheet* (one shared resident texture) and
  yields the sub-rect UVs.
- **Pre-baked** (phase 2): the sheet PNG + UV rects are authored offline (TexturePacker-style) and declared in
  the manifest's `atlases` section.
- **Runtime** (phase 2b): `packAtlas(...)` / the `asset:pack` topic decode N *separate* PNGs, shelf-pack them
  into one sheet at load time, upload once, and register the sub-sprites. The packed sheet is **pinned**.

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

## Tests (regression locks)
- `AssetManagerUnit` — registry, on-demand+cache, budget eviction, priority protection, LRU, dynamic
  setPriority, preloadGroup, unload, **atlas sub-sprites share one sheet + UVs**. (headless, no GPU)
- `ShelfPackerUnit` — packed rects in-bounds, never overlap, over-wide fails. (headless)
- `AssetProviderGpu` — real PNGs load/evict with priority on a real device.
- `AssetSpriteGpu` — `render:sprite{asset}` streams the texture in.
- `AssetTopicsGpu` — manifest (assets + atlas) at boot + `asset:*` topics.
- `AtlasPackerGpu` — `packAtlas` + `asset:pack` build one shared sheet with distinct UVs.

## Phases
1. ✅ Registry + cache (budget/priority/LRU) + manifest + topics + `render:sprite` by id.
2. ✅ Pre-baked atlas (sheet + UV sub-sprites).
2b. ✅ Runtime atlas packing (`ShelfPacker` + `AtlasPacker` + `asset:pack`).
3. ⏳ Async load — decode on a worker thread (the slow part), upload on the render thread (bgfx stays
   single-threaded). Removes the first-touch hitch. Not yet built.
