#pragma once

/**
 * grove::mapview::MapView — the host-driven orchestrator that ties the core together (S1d).
 *
 * WHAT  : The stateful object the host owns. Each frame: setViewport (the world-space visible rect) → update()
 *         runs the pipeline (cull → stream/evict → compile each visible cell through the active Lens into a
 *         neutral CellDraw buffer) → drainCells() copies the result into a caller buffer for submitSpriteBatch.
 *
 * WHY   : This is the §4 pipeline made concrete, and it stays PURE compute: all I/O is the injected
 *         IChunkProvider, all geometry is the injected IGridLayout/IProjection, so the whole thing is
 *         headless-testable (no GPU/SDL/IIO). It is deliberately decoupled from Manifest/JSON — it takes a
 *         plain field schema + a GridSpec, so a consumer that never parses JSON (or a different transport)
 *         can still drive it. The ZoneNavigator host-driven shape: feed inputs, call update, drain output.
 *
 * HOW   : Header-only, std-only. For each visible chunk (resident in the cache), for each layer of the lens,
 *         for each cell whose global z equals the active slice: decode the field's raw value to physical,
 *         test the layer's Filter, evaluate its Palette, and emit a CellDraw at the cell's projected centre.
 *         A field absent from a chunk simply contributes nothing (fail-franc). Cell index -> local (x,y,z)
 *         is row-major then z-major (the chunk blob's order). The field schema is held by reference and must
 *         outlive the MapView (its FieldDecls back the decode lookup).
 */

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "grove/mapview/CellDraw.h"
#include "grove/mapview/ChunkCache.h"
#include "grove/mapview/ChunkProvider.h"
#include "grove/mapview/Coord.h"
#include "grove/mapview/Cull.h"
#include "grove/mapview/Field.h"
#include "grove/mapview/GridLayout.h"
#include "grove/mapview/Lens.h"
#include "grove/mapview/Projection.h"
#include "grove/mapview/WorldDocument.h"

namespace grove {
namespace mapview {

// The world-space rectangle currently visible (the host computes it from its camera each frame).
struct Viewport {
    double minX{0.0};
    double minY{0.0};
    double maxX{0.0};
    double maxY{0.0};
};

// Chunk dimensions + cell size — the geometry the orchestrator needs, decoupled from the JSON Manifest.
struct GridSpec {
    int    chunkW{1};
    int    chunkH{1};
    int    chunkD{1};
    double cellW{1.0};
    double cellH{1.0};
    // World size in CELLS (0 = unknown). Needed to decode PARTIAL edge chunks: a world whose width isn't a
    //   multiple of chunkW has a last column of chunks only (worldW - lastChunkX*chunkW) cells wide — their
    //   blob is row-major at THAT stride, not chunkW. Without this, MapView decodes them at the full chunkW
    //   stride and scrambles the cells into horizontal streaks (a 1536-wide world hid the bug: 1536 = 12*128
    //   exactly; a derived world like 1625 does not). 0 -> assume full chunks (back-compat).
    int    worldW{0};
    int    worldH{0};
};

class MapView {
public:
    // The schema (ordered field decls) is COPIED and owned by the MapView, so fieldByName_ never dangles —
    // a caller may pass a temporary (e.g. a function returning the schema by value) safely.
    MapView(std::vector<FieldDecl> schema, GridSpec grid, IGridLayout& layout,
            IProjection& projection, IChunkProvider& provider, size_t chunkBudget)
        : grid_(grid), layout_(layout), projection_(projection), cache_(provider, chunkBudget),
          schema_(std::move(schema)) {
        for (const FieldDecl& f : schema_) fieldByName_[f.name] = &f;  // points into the owned copy
    }

    void setLens(Lens lens) { lens_ = std::move(lens); }
    // Region & marker overlays are global vector sets (not chunked) — the host loads them and hands them over.
    void setRegions(std::vector<Region> regions) { regions_ = std::move(regions); }
    void setMarkers(std::vector<Marker> markers) { markers_ = std::move(markers); }
    void setViewport(Viewport vp) { viewport_ = vp; }
    void setZSlice(int z) { zSlice_ = z; }
    void setPrefetchMargin(double worldMargin) { margin_ = worldMargin; }

    // Run the per-frame pipeline; results are queryable via drainCells()/cells().
    void update() {
        cells_.clear();
        regionDraws_.clear();
        markerDraws_.clear();
        tileChunks_.clear();

        const int W = grid_.chunkW, H = grid_.chunkH, D = grid_.chunkD;
        if (W <= 0 || H <= 0 || D <= 0 || grid_.cellW <= 0.0 || grid_.cellH <= 0.0) return;
        const int chunkZ = static_cast<int>(std::floor(static_cast<double>(zSlice_) / D));

        // 1. cull (viewport + prefetch margin) -> visible chunks at this layer.
        const std::vector<ChunkCoord> visible =
            chunksInViewport(viewport_.minX - margin_, viewport_.minY - margin_,
                             viewport_.maxX + margin_, viewport_.maxY + margin_,
                             grid_.cellW, grid_.cellH, W, H, chunkZ);

        // 2-3. stream missing visible chunks + evict LRU beyond budget.
        cache_.ensureResident(visible);

        // 4. compile each visible cell through the active lens into CellDraws.
        for (const ChunkCoord& cc : visible) {
            const ChunkData* cd = cache_.get(cc);
            if (cd == nullptr) continue;  // absent chunk -> nothing to draw

            // ACTUAL width/height of THIS chunk (partial at the world's right/bottom edge). If the world
            //   size is known and this is a boundary chunk, the blob is only cw×ch cells (row-major at
            //   stride cw), not chunkW×chunkH -> decode at the real stride or the cells scramble (streaks).
            //   worldW/H == 0 (unknown) -> full chunk (back-compat with the demo world).
            int cw = W, ch = H;
            if (grid_.worldW > 0) { const int r = grid_.worldW - cc.x * W; if (r > 0 && r < W) cw = r; }
            if (grid_.worldH > 0) { const int r = grid_.worldH - cc.y * H; if (r > 0 && r < H) ch = r; }
            const int cellsPerLayer = cw * ch;

            for (const Layer& layer : lens_.layers) {
                const auto fit = fieldByName_.find(layer.field);
                if (fit == fieldByName_.end()) continue;            // field not in schema
                const FieldDecl& fd = *fit->second;
                const std::vector<uint32_t>* vals = cd->get(layer.field);
                if (vals == nullptr) continue;                      // field absent in this chunk (fail-franc)

                const size_t n = vals->size();
                for (size_t idx = 0; idx < n; ++idx) {
                    const int localZ = static_cast<int>(idx / static_cast<size_t>(cellsPerLayer));
                    const int rem    = static_cast<int>(idx % static_cast<size_t>(cellsPerLayer));
                    const int gz = cc.z * D + localZ;
                    if (gz != zSlice_) continue;                    // only the active z-slice

                    const int gx = cc.x * W + (rem % cw);   // stride = ACTUAL chunk width (partial-aware)
                    const int gy = cc.y * H + (rem / cw);
                    const double phys = decodePhysical(fd, (*vals)[idx]);

                    // Filter — a leaf may reference OTHER fields, resolved at this cell via the sampler.
                    const auto sampler = [&](const std::string& name, double& out) {
                        return sampleField(name, gx, gy, gz, out);
                    };
                    if (!layer.filter.eval(sampler, phys)) continue;

                    Rgba color = layer.palette.eval(phys);
                    color.a *= layer.opacity;

                    // Optional relief shading from a (possibly different) field's gradient (central difference,
                    // cross-chunk; a missing neighbour falls back to the centre so edges degrade gracefully).
                    if (!layer.hillshadeField.empty()) {
                        double center;
                        if (sampleField(layer.hillshadeField, gx, gy, gz, center)) {
                            double s;
                            const double xm = sampleField(layer.hillshadeField, gx - 1, gy, gz, s) ? s : center;
                            const double xp = sampleField(layer.hillshadeField, gx + 1, gy, gz, s) ? s : center;
                            const double ym = sampleField(layer.hillshadeField, gx, gy - 1, gz, s) ? s : center;
                            const double yp = sampleField(layer.hillshadeField, gx, gy + 1, gz, s) ? s : center;
                            const double dzdx = (xp - xm) / (2.0 * grid_.cellW);
                            const double dzdy = (yp - ym) / (2.0 * grid_.cellH);
                            color = multiplyRgb(color, static_cast<float>(layer.hillshade.factor(dzdx, dzdy)));
                        }
                    }

                    const WorldPos wp = layout_.cellToWorld(CellCoord{gx, gy, static_cast<int16_t>(gz)});
                    const RenderPos rp = projection_.project(wp);
                    cells_.push_back(CellDraw{rp.x, rp.y, grid_.cellW, grid_.cellH, 0.0, layer.layerZ, color});
                }
            }
        }

        // 4b. compile the tile layers: each visible chunk -> one TileChunkDraw per tile layer (the retained-
        // tilemap path). Independent of the per-cell colour path above — a lens may use either or both. This
        // path is TopDown / axis-aligned only (a rectangular tilemap can't follow a rotated/iso projection),
        // so it reads the chunk's world corner straight from the grid rather than through the projection.
        if (!lens_.tileLayers.empty()) {
            const int localZ = zSlice_ - chunkZ * D;                 // the z-layer within a visible chunk
            if (localZ >= 0 && localZ < D) {
                for (const ChunkCoord& cc : visible) {
                    const ChunkData* cd = cache_.get(cc);
                    if (cd == nullptr) continue;                     // absent chunk -> no tiles (fail-franc)
                    // ACTUAL chunk size (partial at the world edge) — same rule as the cell path above ; the
                    //   tilemap must be cw×ch, not chunkW×chunkH, or the edge chunk streaks/reads OOB.
                    int cw = W, ch = H;
                    if (grid_.worldW > 0) { const int r = grid_.worldW - cc.x * W; if (r > 0 && r < W) cw = r; }
                    if (grid_.worldH > 0) { const int r = grid_.worldH - cc.y * H; if (r > 0 && r < H) ch = r; }
                    const int cellsPerLayer = cw * ch;
                    const size_t sliceOff = static_cast<size_t>(localZ) * static_cast<size_t>(cellsPerLayer);
                    for (const TileLayer& tl : lens_.tileLayers) {
                        const auto fit = fieldByName_.find(tl.field);
                        if (fit == fieldByName_.end()) continue;     // field not in schema
                        const FieldDecl& fd = *fit->second;
                        const std::vector<uint32_t>* vals = cd->get(tl.field);
                        if (vals == nullptr) continue;               // field absent in this chunk (fail-franc)
                        // Guard a ragged/short chunk: the active z-slice's dense cw*ch block must fit.
                        if (sliceOff + static_cast<size_t>(cellsPerLayer) > vals->size()) continue;

                        TileChunkDraw tc;
                        tc.chunkX = cc.x;
                        tc.chunkY = cc.y;
                        tc.width  = cw;   // ACTUAL width (partial-aware)
                        tc.height = ch;
                        tc.worldX = static_cast<double>(cc.x) * W * grid_.cellW;  // chunk top-left corner
                        tc.worldY = static_cast<double>(cc.y) * H * grid_.cellH;
                        tc.tileW  = grid_.cellW;
                        tc.tileH  = grid_.cellH;
                        tc.layer  = tl.layerZ;
                        tc.tiles.resize(static_cast<size_t>(cellsPerLayer));
                        for (int i = 0; i < cellsPerLayer; ++i) {
                            const double phys = decodePhysical(fd, (*vals)[sliceOff + static_cast<size_t>(i)]);
                            tc.tiles[static_cast<size_t>(i)] = tl.mapper.map(phys);
                        }
                        tileChunks_.push_back(std::move(tc));
                    }
                }
            }
        }

        // 5. compile the visible region & marker overlays (global vector sets, culled by the viewport).
        const double vminX = viewport_.minX - margin_, vminY = viewport_.minY - margin_;
        const double vmaxX = viewport_.maxX + margin_, vmaxY = viewport_.maxY + margin_;

        for (const RegionLayer& rl : lens_.regionLayers) {
            for (const Region& rg : regions_) {
                // Cull by the circle's bounding box (conservative; regions are few). Partially-visible kept.
                if (rg.cx + rg.radius < vminX || rg.cx - rg.radius > vmaxX ||
                    rg.cy + rg.radius < vminY || rg.cy - rg.radius > vmaxY) continue;
                const double key = rl.byValue ? rg.value : static_cast<double>(rg.type);
                if (!rl.filter.eval(key)) continue;
                Rgba color = rl.palette.eval(key);
                color.a *= rl.opacity;
                const RenderPos c = projection_.project(WorldPos{rg.cx, rg.cy, 0.0});
                regionDraws_.push_back(RegionDraw{c.x, c.y, rl.innerRatio * rg.radius, rg.radius,
                                                  0.0, kTwoPi, rl.layerZ, color});
            }
        }

        for (const MarkerLayer& ml : lens_.markerLayers) {
            for (const Marker& mk : markers_) {
                if (mk.x < vminX || mk.x > vmaxX || mk.y < vminY || mk.y > vmaxY) continue;
                const double key = static_cast<double>(mk.kind);
                if (!ml.filter.eval(key)) continue;
                Rgba color = ml.palette.eval(key);
                color.a *= ml.opacity;
                const RenderPos p = projection_.project(WorldPos{mk.x, mk.y, 0.0});
                markerDraws_.push_back(MarkerDraw{p.x, p.y, mk.scale * ml.baseScale, mk.angle, ml.layerZ, color});
            }
        }
    }

    // Copy up to `cap` compiled cells into `out`; returns the number copied (no allocation).
    size_t drainCells(CellDraw* out, size_t cap) const {
        const size_t k = std::min(cap, cells_.size());
        for (size_t i = 0; i < k; ++i) out[i] = cells_[i];
        return k;
    }

    // Convenience accessors (host/tests).
    const std::vector<CellDraw>& cells() const { return cells_; }
    size_t cellCount() const { return cells_.size(); }
    size_t residentChunks() const { return cache_.residentCount(); }

    // Drain the compiled overlay draws (region ring-sectors / marker sprites) into caller buffers.
    size_t drainRegions(RegionDraw* out, size_t cap) const {
        const size_t k = std::min(cap, regionDraws_.size());
        for (size_t i = 0; i < k; ++i) out[i] = regionDraws_[i];
        return k;
    }
    size_t drainMarkers(MarkerDraw* out, size_t cap) const {
        const size_t k = std::min(cap, markerDraws_.size());
        for (size_t i = 0; i < k; ++i) out[i] = markerDraws_[i];
        return k;
    }
    const std::vector<RegionDraw>& regionDraws() const { return regionDraws_; }
    const std::vector<MarkerDraw>& markerDraws() const { return markerDraws_; }

    // The compiled tile-grid chunks (retained-tilemap path): one per visible chunk × tile layer. The host
    // turns each into a render:tilemap:add. Empty unless the active lens declares tileLayers.
    const std::vector<TileChunkDraw>& tileChunks() const { return tileChunks_; }
    size_t tileChunkCount() const { return tileChunks_.size(); }

private:
    // Decoded physical value of `field` at global cell (gx,gy,gz); false if that chunk isn't resident or the
    // field is absent there. Backs cross-field filters and cross-chunk hillshade gradient sampling.
    bool sampleField(const std::string& field, int gx, int gy, int gz, double& out) const {
        const int W = grid_.chunkW, H = grid_.chunkH, D = grid_.chunkD;
        const int cx = floorDiv(gx, W), cy = floorDiv(gy, H), cz = floorDiv(gz, D);
        const ChunkData* cd = cache_.get(ChunkCoord{cx, cy, static_cast<int16_t>(cz)});
        if (cd == nullptr) return false;
        const std::vector<uint32_t>* vals = cd->get(field);
        if (vals == nullptr) return false;
        const auto fit = fieldByName_.find(field);
        if (fit == fieldByName_.end()) return false;
        // ACTUAL chunk stride (partial at the world edge) — MUST match the blob's row-major layout, else the
        //   local index is wrong and hillshade/filter reads a scrambled cell (the residual streaks). Same rule
        //   as the cell/tile paths. A coord in the phantom part of a partial chunk (lx>=cw) is OFF-WORLD -> false
        //   (so a hillshade neighbour past the right/bottom edge falls back to the centre, as intended).
        int cw = W, ch = H;
        if (grid_.worldW > 0) { const int r = grid_.worldW - cx * W; if (r > 0 && r < W) cw = r; }
        if (grid_.worldH > 0) { const int r = grid_.worldH - cy * H; if (r > 0 && r < H) ch = r; }
        const int lx = gx - cx * W, ly = gy - cy * H, lz = gz - cz * D;
        if (lx >= cw || ly >= ch) return false;                 // phantom cell of a partial chunk -> off-world
        const size_t i = static_cast<size_t>(lz) * static_cast<size_t>(cw * ch)
                       + static_cast<size_t>(ly) * static_cast<size_t>(cw)
                       + static_cast<size_t>(lx);
        if (i >= vals->size()) return false;
        out = decodePhysical(*fit->second, (*vals)[i]);
        return true;
    }

    // Floor division (rounds toward -inf), correct for negative global coordinates.
    static int floorDiv(int a, int b) {
        int q = a / b;
        const int r = a % b;
        if ((r != 0) && ((r < 0) != (b < 0))) --q;
        return q;
    }

    GridSpec grid_;
    IGridLayout& layout_;
    IProjection& projection_;
    ChunkCache cache_;
    std::vector<FieldDecl> schema_;  // owned copy of the schema; fieldByName_ points into it
    std::unordered_map<std::string, const FieldDecl*> fieldByName_;

    Lens lens_;
    Viewport viewport_{};
    int zSlice_{0};
    double margin_{0.0};
    std::vector<CellDraw> cells_;
    std::vector<Region> regions_;       // global region overlay set (host-provided)
    std::vector<Marker> markers_;       // global marker overlay set (host-provided)
    std::vector<RegionDraw> regionDraws_;
    std::vector<MarkerDraw> markerDraws_;
    std::vector<TileChunkDraw> tileChunks_;  // compiled tile-grid chunks (tiling path); empty if no tileLayers
};

} // namespace mapview
} // namespace grove
