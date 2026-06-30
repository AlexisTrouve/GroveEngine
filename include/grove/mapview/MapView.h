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
};

class MapView {
public:
    // `schema` (the ordered field decls) is held by reference — it must outlive this MapView.
    MapView(const std::vector<FieldDecl>& schema, GridSpec grid, IGridLayout& layout,
            IProjection& projection, IChunkProvider& provider, size_t chunkBudget)
        : grid_(grid), layout_(layout), projection_(projection), cache_(provider, chunkBudget) {
        for (const FieldDecl& f : schema) fieldByName_[f.name] = &f;
    }

    void setLens(Lens lens) { lens_ = std::move(lens); }
    void setViewport(Viewport vp) { viewport_ = vp; }
    void setZSlice(int z) { zSlice_ = z; }
    void setPrefetchMargin(double worldMargin) { margin_ = worldMargin; }

    // Run the per-frame pipeline; results are queryable via drainCells()/cells().
    void update() {
        cells_.clear();

        const int W = grid_.chunkW, H = grid_.chunkH, D = grid_.chunkD;
        if (W <= 0 || H <= 0 || D <= 0) return;
        const int cellsPerLayer = W * H;
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

                    const double phys = decodePhysical(fd, (*vals)[idx]);
                    if (!layer.filter.eval(phys)) continue;         // filtered out

                    Rgba color = layer.palette.eval(phys);
                    color.a *= layer.opacity;

                    const int gx = cc.x * W + (rem % W);
                    const int gy = cc.y * H + (rem / W);
                    const WorldPos wp = layout_.cellToWorld(CellCoord{gx, gy, static_cast<int16_t>(gz)});
                    const RenderPos rp = projection_.project(wp);
                    cells_.push_back(CellDraw{rp.x, rp.y, grid_.cellW, grid_.cellH, 0.0, layer.layerZ, color});
                }
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

private:
    GridSpec grid_;
    IGridLayout& layout_;
    IProjection& projection_;
    ChunkCache cache_;
    std::unordered_map<std::string, const FieldDecl*> fieldByName_;

    Lens lens_;
    Viewport viewport_{};
    int zSlice_{0};
    double margin_{0.0};
    std::vector<CellDraw> cells_;
};

} // namespace mapview
} // namespace grove
