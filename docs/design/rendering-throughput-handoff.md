# Rendering throughput — findings + handoff

Status as of the "GROS SAUVAGE" rendering benchmark work (commits `b6b167d` → `d7b71ea`,
all pushed to gitea + github + bitbucket). This is the resume point for renderer/IIO
throughput. The short version: **the GPU was never the wall — the per-sprite IIO+JSON model
was. A direct bulk path lifts the 60fps sprite ceiling ~22× (≈5k → ≈100k).**

## TL;DR

| Path | 60fps sprite ceiling | per-sprite cost | why |
|------|----------------------|-----------------|-----|
| `render:sprite` (IIO + JSON) | **~5 000** | ~10 µs | one IIO message/sprite, **deep-copied to JSON** on publish |
| `submitSpriteBatch` (direct POD) | **~100 000** | ~100–200 ns | one `vector::insert` of GPU-ready instances, no IIO/JSON |
| retained tilemap | **millions of tiles** | n/a (uploaded once) | the right model for huge *static* content |

GPU itself: 10k sprites draw in **<1 ms**. It is idle while the JSON envelope eats the frame.

## How we got here (the investigation)

1. Built `tests/visual/benchmark_render_savage.cpp` — a windowed D3D11 benchmark that ramps
   sprites/particles/text/tilemap until the frame melts, reading `bgfx::getStats()` (GPU/CPU
   timers, draw calls, prims) and finding the 60/30fps ceilings.
2. First run: `render:sprite` flatlined at ~36ms past 10k sprites, `<<DROPPED>>` (numPrims froze
   at 20000 = 10k sprites). GPU = 0.9ms, CPU submit = 0.13ms — **wall = 60-80ms upstream, not the GPU.**
3. Decomposed via `benchmark_iio_direct`: raw IIO publish ≈ 1.3 µs (1-field json); a sprite's
   7-field json + parse ≈ 10 µs. The IIO was **designed for ~100 msgs/frame** (its own comment);
   one message per sprite is ~1000× out of envelope.
4. Root cause, read in the code: **`IntraIO::publish()` requires a `JsonDataNode` and deep-copies
   its json** (`src/IntraIO.cpp:74`, to release `operationMutex` before routing — ABBA deadlock
   prevention), **then `IntraIOManager` re-wraps the json into a NEW `JsonDataNode` per delivery**
   (`src/IntraIOManager.cpp:321`). So **2+ full json deep-copies per message.** A POD payload can't
   even ride the bus (publish throws on non-`JsonDataNode`).

## The fix (shipped) — `b6b167d`

A direct bulk path that hands the renderer GPU-ready instances, bypassing IIO + JSON:

- **`SceneCollector::addSpritesBulk(const SpriteInstance*, size_t)`** — one bulk insert into the
  frame's ephemeral sprite list (`m_sprites`), merges with IIO/retained sprites in `finalize()`.
- **`BgfxRendererModule::submitSpriteBatch(const SpriteInstance*, size_t)`** — public host API.
  A statically-linked host (Drifterra) that already holds packed instances calls this **between
  frames** (after the previous frame's `clear()`, before the next `process()`/`step()` finalize).
- Locked headless by **`SceneCollectorTest [bulk]`** (`tests/integration/test_scene_collector.cpp`):
  feed N, coexist with IIO sprites, `clear()` drops them. GPU-free (SceneCollector is pure).
- **NOT via an IIO `render:sprite:batch` topic** — that was the first idea, but IIO can't carry a
  POD payload (publish throws + deep-copies json). Direct-to-renderer is the path *by necessity*.

## A + B (shipped) — `d7b71ea`

- **A. `vsync` config now honored.** `BgfxDevice` hardcoded `BGFX_RESET_VSYNC` in `init()` AND
  `reset()`, so every frame floored at ~16.6ms regardless of the (read-but-ignored) flag. Now
  threaded: `IRHIDevice::init(..., bool vsync = true)`, BgfxDevice stores reset flags
  (`BGFX_RESET_VSYNC` vs `BGFX_RESET_NONE`) and reuses them on resize. Backward-compatible (default
  true; one impl, one caller). With it OFF, the bulk path's *real* cost is visible (below).
- **B. Documented `submitSpriteBatch`** for hosts: DEVELOPER_GUIDE "Bulk Sprite Submission" (when to
  use it vs `render:sprite` vs retained tilemap) + a CLAUDE.md BgfxRenderer note.

## Benchmark numbers (final, D3D11, vsync OFF, 256MB arena)

```
POD bulk path:   2.5k = 0.57ms (1749 fps)   <- was floored at 16.5ms by vsync
                  50k = 5.95ms (168 fps)
                 100k = 15.5ms ( 64 fps)     <- true 60fps ceiling
                 200k = 46ms,  400k = melt
60fps sprite ceiling:  JSON 4817 -> POD 104041 = 22x.  ~100-200 ns/sprite, linear.
Grand finale:  80k POD sprites + 80k particles + 8k text + 4.2M tilemap tiles = 22.6 FPS, 260k tris.
```
At the top the wall is now the **GPU (fill/overdraw) + the per-frame `std::sort` in SpritePass**,
no longer delivery. `benchmark_render_savage` is a wall-clock tool (windowed, GPU), NOT a ctest.

## Adjacent findings (surfaced, not all fixed)

- **FrameAllocator** is a fixed 16MB bump arena (`frameAllocatorSizeMB`, default 16) → caps a frame
  at ~200k sprites (`allocateArray` returns null past it → 0 drawn, no crash). Configurable; can't
  grow mid-frame (pointers already handed out). A growable/double-buffered arena would remove the cap.
- **Per-batch sprite limit**: `MAX_SPRITES_PER_BATCH = 10000` (hardcoded in `SpritePass.h`) + a 10k
  dynamic fallback buffer. A SINGLE (layer,textureId) batch > 10000 overflows the fallback → crash.
  Keep batches bounded (spread across textureIds/layers). The bulk benchmark spreads over 28 fake
  texIds × 8 layers so no batch exceeds ~n/224.
- **Retained tilemap** already scales to 4.2M tiles @ 60fps (uploaded once) — the model for huge
  static content. Per-sprite is for dynamic entities.

## OPEN TASKS (priority order)

1. **[perf — the big one] Kill the IIO json deep-copies.** `IntraIO::publish` deep-copies the json
   (`src/IntraIO.cpp:61-82`) and `IntraIOManager` re-wraps it per delivery (`src/IntraIOManager.cpp:321`,
   batch path `:548`). Replace copy-delivery with a **shared immutable payload**
   (`shared_ptr<const ...>`): one alloc, ref-counted, shared across N subscribers AND across the
   lock boundary without copying (immutable → thread-safe to share). Benefits **ALL** IIO traffic
   (events/UI/state) — **NOT the sprite path** (already solved by the direct API). **Core change,
   wide blast radius**: touches publish/route/deliver + the ABBA lock-ordering the deep-copy
   currently guarantees + the batch-flush thread. **TSan-proven invariants exist — re-run the full
   WSL TSan suite after** ([[tsan-via-wsl-recipe]]). This is its own careful effort, not a drive-by.
2. **[minor] No bulk path for particles/text.** Same JSON-per-primitive wall sprites had. Add
   `submit*Batch` analogues only if they become hot (the benchmark shows particles/text also cap ~5k/60fps).
3. **[minor] FrameAllocator can't grow** — fixed arena, sized at init. A double-buffered/growable
   arena would drop the ~200k/frame ceiling.
4. **[info] SpritePass per-frame `std::sort`** of N sprites is O(n log n) CPU — at the very top of
   the POD ramp this (plus overdraw) is the wall. A sort-key cache or pre-sorted submission could help.

## Key files

- `modules/BgfxRenderer/Scene/SceneCollector.{h,cpp}` — `addSpritesBulk` (+ existing JSON `parseSpriteBatch`).
- `modules/BgfxRenderer/BgfxRendererModule.{h,cpp}` — `submitSpriteBatch` (public host API).
- `modules/BgfxRenderer/RHI/RHIDevice.h` + `RHI/BgfxDevice.cpp` — `vsync` threading (`m_resetFlags`).
- `tests/visual/benchmark_render_savage.cpp` — the savage benchmark (windowed, wall-clock).
- `tests/integration/test_scene_collector.cpp` — `SceneCollectorTest [bulk]` regression lock.
- `docs/DEVELOPER_GUIDE.md` — "Bulk Sprite Submission" section.
- **IIO speedup target**: `src/IntraIO.cpp:61-82` (publish deep-copy), `src/IntraIOManager.cpp:321,548`
  (per-delivery re-wrap). Engine consumed by [[drifterra-consumes-groveengine]].
