# Zone Navigation — design

**Status:** design locked; slices 1-3 shipped (logic complete); slice 4 (showcase demo) next (2026-06-20).
**One-line:** navigation as *entering nested spaces* — zoom descends into authored zones, the camera
is soft-magnetized to frame the active zone, pan is bounded to it and scales with zoom.

This extends the existing camera stack (`grove::camera` — `zoomAt`/`damp`/`clampZoom`, and the
`ZoomLadder` strata) into a full **semantic navigation** model for Drifterra's galaxy↔system↔ship↔
interior continuum (grove_integration.md "🔭 Continuum de zoom", fondation #3 macro→micro).

---

## Concept — nested zones

The world is a **hierarchy of zones** (galaxy ⊃ system ⊃ ship ⊃ room…). Each zone has world
**bounds** (an AABB) + a **zoom** level (≈ a `ZoomLadder` plateau, now made concrete and spatial).
Three coupled mechanics:

1. **Zoom = depth.** Zooming in *descends* into a child zone; zooming out *ascends* to the parent.
   The zoom range traverses the hierarchy instead of free-floating.
2. **Zoom magnet (soft).** Zoom doesn't land on an arbitrary point — the camera is *eased* toward
   **framing the target zone** (it "enters" a thing cleanly, not the void). Soft = a damped pull, not
   a hard snap.
3. **Pan locked to the active zone + speed ∝ zoom.** Inside a zone, pan is **clamped to its bounds**
   (the visible rectangle can't leave the zone — to exit, zoom out). Clamp is **elastic** (rubber-band,
   matching the soft feel), not a hard wall. Pan moves at a **constant on-screen feel**: world-space
   pan delta = screen delta ÷ zoom (fast across a big zoomed-out zone, slow/precise in a small one).

**Worked example:** galaxy view → zoom toward a system → the camera magnetizes and *frames that
system*; pan now roams only *within* that system, at a speed matched to its size → zoom toward a ship
→ enters the ship, pan bounded to it → … To reach a sibling ship: **zoom out** (back to the system)
then zoom into the other one.

---

## Decisions (locked by Alexi, 2026-06-20)

- **Zones are authored by the game** (Drifterra owns the tree: meaning + content) and **synced** into
  the engine (id, parent, bounds, zoom). They are **dynamic** — add / move / remove at runtime.
- **All changes are seamless** — the camera never jumps; it *glides* (damped) to the new target.
- **Current-zone deletion → back out.** If the active zone is deleted, ease back to its **parent**;
  if the parent is also gone, the **grandparent**; i.e. the **nearest surviving ancestor**, in glide.
- **Magnet: soft.** **Clamp: elastic.** Magnet triggers on **crossing a zone's zoom threshold** (not
  a permanent pull).

---

## Boundary — engine math, game content

| | |
|---|---|
| **Engine** (this) | the navigation *mechanics*, tested once: fit-a-zone, clamp-pan-to-bounds, pan∝1/zoom, the soft-magnet ease, the active-zone tracking, the deletion back-out. |
| **Game** (Drifterra) | the *zone hierarchy* (what zones mean, where they are, their bounds/zoom) and *when it changes*; it syncs zones into the engine and drives zoom/pan input. |

Same split as the rest of the engine: the renderer owns the projection, the tilemap owns the LOD
crossfade factor, and here the engine owns the navigation feel — the game owns the spaces.

---

## API

**Pure helpers** (`grove::camera`, header-only, in `Scene/Camera.h`, oracle-tested) — slice 1:

- `CameraView fitBounds(WorldBounds zone, viewportW, viewportH, margin=0)` — the camera that *frames*
  a zone (centered + zoom-to-fit, optional padding). The magnet target.
- `void clampPanToBounds(CameraView&, WorldBounds zone)` — keep the visible rect inside the zone
  (centers the zone if it's smaller than the view). The **hard** primitive; the elastic feel is the
  navigator damping toward this.
- `float worldPanForScreen(screenDelta, zoom)` — `screenDelta / zoom`: constant on-screen pan feel.

**`ZoneNavigator`** (`Scene/ZoneNavigator.h`, header-only logic, no bgfx) — slices 2-3, composes the
helpers:

```cpp
addZone(id, parentId, WorldBounds, zoom);   // game syncs its spatial tree
removeZone(id);                             // if active -> ease to nearest live ancestor (back out)
setActive(id);                             // explicit enter (eased)
zoom(delta);                               // descend/ascend + magnet toward nearest child/parent zone
pan(dx, dy);                               // world pan scaled by 1/zoom, then clamped to the active zone
CameraView update(dt);                     // damp toward the target (soft magnet); publish on render:camera
```

Note: **zones become the plateaus** — each carries its zoom — so `ZoneNavigator` concretizes the
abstract `ZoomLadder` into real spaces. They compose (the ladder is still fine for content-less zoom).

---

## Slices (TDD headless + an eye-validated demo)

1. **Pure helpers** — `fitBounds` / `clampPanToBounds` / `worldPanForScreen` in `Scene/Camera.h`.
   ✅ shipped — locked by `ZoneNavUnit` (6 cases / 17 assertions, analytical oracles).
2. **`ZoneNavigator` core** — tree + active zone + **soft magnet** (ease toward `fitBounds`) +
   **elastic clamp** + **pan∝zoom**. ✅ shipped (`Scene/ZoneNavigator.h`) — `zoomBy` descends/ascends,
   the magnet re-centers on enter, pan is clamped, `update(dt)` glides seamlessly. Locked by
   `ZoneNavUnit` (+5 cases).
3. **Dynamic + back-out** — add/remove at runtime; `removeZone(active)` → nearest live ancestor,
   seamless. ✅ shipped — `addZone` is idempotent (a zone that moved/resized keeps its children);
   `removeZone` drops the subtree and backs the camera out one zone (then two, …) to the nearest live
   ancestor, eased. Locked by `ZoneNavUnit` (+6 cases).
4. **Demo (showcase)** — a toy 3-level hierarchy: zoom to *enter*, pan locked+scaled, a key to delete
   the current zone → watch the back-out. Eye-validated.

---

## Status

- **Slice 1** — ✅ shipped (pure helpers, `ZoneNavUnit`).
- **Slice 2** — ✅ shipped (`ZoneNavigator` core, `ZoneNavUnit`).
- **Slice 3** — ✅ shipped (dynamic add/remove + deletion back-out, `ZoneNavUnit`).
- **Slice 4** — next (showcase demo, eye-validated).
