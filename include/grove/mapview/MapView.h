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
    // LOD (niveau de détail) par zoom pour le chemin per-cellule. QUOI : `pxPerWorld` = pixels écran par unité
    //   monde (= le zoom caméra). POURQUOI : en vue d'ensemble une cellule fait <1 px -> émettre 1 sprite/cellule
    //   (décode + palette + hillshade 4-samples + projection, CHAQUE frame) sur des cellules invisibles est du
    //   pur gaspillage (le coût per-cellule domine, mesuré). COMMENT : update() en déduit un STRIDE (on ne garde
    //   qu'une cellule sur stride en x/y, agrandie pour couvrir le bloc) de sorte qu'une cellule émise ≈
    //   `lodTargetPx_` pixels -> le nombre de cellules émises est borné par la résolution écran, PAS par la
    //   taille du monde. Zoomé (cellule ≥ target px) -> stride 1 = grain fin intégral. pxPerWorld<=0 (défaut) ->
    //   stride 1 partout = comportement historique byte-identique (les autres hôtes Grove ne changent pas).
    void setLod(double pxPerWorld) { pxPerWorld_ = pxPerWorld; }

    // Enable/disable the per-frame per-cell compile (the CellDraw grid). QUOI : off -> update() n'émet PLUS de
    //   cellules ni ne stream de chunks (les overlays continuent). POURQUOI : le mode "carte bakée" du viewer
    //   dessine la grille comme UNE texture bakée + un quad (cf. bakeLensRGBA) ; le compile per-cellule par frame
    //   devient inutile et c'est justement le coût O(cellules) qu'on veut supprimer. COMMENT : simple flag lu en
    //   tête d'update() ; défaut true = comportement historique (aucun autre hôte n'est affecté tant qu'il ne
    //   l'appelle pas). Orthogonal au LOD (qui, lui, borne le compile ; ici on le supprime).
    void setCompileCells(bool on) { compileCells_ = on; }

    // Run the per-frame pipeline; results are queryable via drainCells()/cells().
    void update() {
        cells_.clear();
        regionDraws_.clear();
        markerDraws_.clear();
        tileChunks_.clear();

        const int W = grid_.chunkW, H = grid_.chunkH, D = grid_.chunkD;
        if (W <= 0 || H <= 0 || D <= 0 || grid_.cellW <= 0.0 || grid_.cellH <= 0.0) return;
        const int chunkZ = static_cast<int>(std::floor(static_cast<double>(zSlice_) / D));

        // LOD : stride d'échantillonnage per-cellule dérivé du zoom (cf. setLod). stride=1 -> tout (défaut/off).
        //   pxPerCell = taille apparente d'une cellule à l'écran ; on vise lodTargetPx_ px/cellule -> stride =
        //   ceil(target/pxPerCell). Zoomé (pxPerCell >= target) -> stride 1 (grain fin). On borne le stride à la
        //   taille d'un chunk pour ne jamais tout sauter. Le stride s'ALIGNE sur une grille absolue (gx % stride)
        //   -> stable au pan (pas de scintillement des cellules gardées).
        int stride = 1;
        if (pxPerWorld_ > 0.0) {
            const double pxPerCell = grid_.cellW * pxPerWorld_;
            if (pxPerCell > 0.0 && pxPerCell < lodTargetPx_) {
                stride = static_cast<int>(std::ceil(lodTargetPx_ / pxPerCell));
                if (stride < 1) stride = 1;
                if (stride > W) stride = W;   // jamais plus grossier qu'un chunk
            }
        }
        const double lodCellW = grid_.cellW * static_cast<double>(stride);  // cellule agrandie -> couvre le bloc
        const double lodCellH = grid_.cellH * static_cast<double>(stride);

        // 1-3. cull (viewport + prefetch margin) -> visible chunks, stream them in, evict LRU beyond budget.
        //   POURQUOI conditionnel : en mode "carte bakée" (compileCells_==false) la grille de cellules n'est plus
        //   émise par frame — une texture bakée UNE fois la remplace (cf. bakeLensRGBA), et SEULS les overlays
        //   (§5, quelques marqueurs/régions) tournent, qui n'ont pas besoin des chunks. On saute donc tout le
        //   streaming quand ni cellules ni tuiles ne sont demandées -> coût par frame O(1) en cellules (le but
        //   "gen once"). compileCells_ défaut = true -> chemin/comportement historique inchangé (autres hôtes Grove).
        const bool wantCells = compileCells_;
        const bool wantTiles = !lens_.tileLayers.empty();
        std::vector<ChunkCoord> visible;
        if (wantCells || wantTiles) {
            visible = chunksInViewport(viewport_.minX - margin_, viewport_.minY - margin_,
                                       viewport_.maxX + margin_, viewport_.maxY + margin_,
                                       grid_.cellW, grid_.cellH, W, H, chunkZ);
            cache_.ensureResident(visible);
        }

        // 4. compile each visible cell through the active lens into CellDraws (skipped in baked-map mode: the
        //   `if (wantCells)` guards the whole streamed loop, so an O(1) overview does zero per-cell work).
        if (wantCells) for (const ChunkCoord& cc : visible) {
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
                    // LOD : ne garder qu'une cellule sur `stride` (grille absolue -> stable au pan). Le skip est
                    //   AVANT le décode/palette/hillshade/projection -> c'est là qu'on économise le gros du coût.
                    if (stride > 1 && ((gx % stride) != 0 || (gy % stride) != 0)) continue;
                    const double phys = decodePhysical(fd, (*vals)[idx]);

                    // Colour this cell through the layer (filter + palette + opacity + hillshade). Factored into
                    //   shadeLayer() — the SHARED colour kernel — so the sprite path here and bakeLensRGBA() below
                    //   produce byte-identical colours (the parity contract). Returns false when the layer's Filter
                    //   drops the cell -> nothing drawn.
                    Rgba color;
                    if (!shadeLayer(layer, phys, gx, gy, gz, color)) continue;

                    const WorldPos wp = layout_.cellToWorld(CellCoord{gx, gy, static_cast<int16_t>(gz)});
                    const RenderPos rp = projection_.project(wp);
                    cells_.push_back(CellDraw{rp.x, rp.y, lodCellW, lodCellH, 0.0, layer.layerZ, color});
                }
            }
        }

        // 4b. compile the tile layers: each visible chunk -> one TileChunkDraw per tile layer (the retained-
        // tilemap path). Independent of the per-cell colour path above — a lens may use either or both. This
        // path is TopDown / axis-aligned only (a rectangular tilemap can't follow a rotated/iso projection),
        // so it reads the chunk's world corner straight from the grid rather than through the projection.
        if (wantTiles) {
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

    // Bake the ACTIVE lens's WHOLE-WORLD cell grid into one row-major RGBA8 buffer — the "gen once" static-map
    //   path. This is the O(1)-per-frame enabler: a host compiles the planet ONCE into a texture, then draws a
    //   single world-space quad each frame (the camera does pan/zoom) instead of recompiling every visible cell.
    //
    // WHAT : fills `out` with worldW*worldH RGBA8 texels (texel (gx,gy) row-major = cell (gx,gy) at the active
    //        z-slice), and reports the world-space AABB the texture covers so the host can place the quad exactly
    //        where the sprite path would have drawn the cells. Returns false when the world has no known finite
    //        extent (GridSpec.worldW/worldH == 0) — the host must then keep the per-frame sprite path.
    // WHY  : a static map only needs the CAMERA to move; the per-cell compile per frame is pure waste. Baking it
    //        once makes the per-frame cost independent of the world size (the whole point of the brief).
    // HOW  : iterate the ENTIRE grid (no viewport cull, no LOD). Per cell, composite every colour layer in layerZ
    //        order with PREMULTIPLIED-alpha "over" accumulation, then un-premultiply -> a straight-alpha texel
    //        that, drawn ONCE with the renderer's standard alpha blend, reproduces the EXACT pixel the stack of
    //        per-layer sprites produced (parity proof, derived: the C-independent term of the sprite stack over
    //        any clear colour C equals premult accumulation). Colours come from shadeLayer() — the same kernel
    //        the sprite path uses — so a texel and a sprite are byte-identical. A cell with no covering layer
    //        stays fully transparent (0,0,0,0) so the clear shows through, exactly like an unemitted sprite cell.
    //        All world chunks are made resident up front so cross-chunk hillshade sampling sees real neighbours
    //        (a full-res overview world is ~tens of chunks < the cache budget; a world with more chunks than the
    //        budget degrades gracefully at seams — the documented flat-centre hillshade fallback).
    bool bakeLensRGBA(std::vector<uint8_t>& out, int& w, int& h,
                      double& worldMinX, double& worldMinY, double& worldSpanX, double& worldSpanY) {
        const int W = grid_.chunkW, H = grid_.chunkH, D = grid_.chunkD;
        if (W <= 0 || H <= 0 || D <= 0 || grid_.cellW <= 0.0 || grid_.cellH <= 0.0) return false;
        if (grid_.worldW <= 0 || grid_.worldH <= 0) return false;   // unbounded world -> caller keeps sprite path

        w = grid_.worldW; h = grid_.worldH;
        // World-space AABB of the full grid. SquareLayout puts cell (0,0)'s CENTRE at (0.5*cellW, 0.5*cellH), so
        //   the grid's top-left CORNER is world (0,0) and it spans worldW*cellW × worldH*cellH (TopDown = identity).
        worldMinX = 0.0; worldMinY = 0.0;
        worldSpanX = static_cast<double>(w) * grid_.cellW;
        worldSpanY = static_cast<double>(h) * grid_.cellH;

        // Make ALL world chunks at the active z-slice's chunk layer resident so hillshade's cross-chunk gradient
        //   sampling reads real neighbours instead of falling back to the centre at every chunk seam.
        const int chunkZ = static_cast<int>(std::floor(static_cast<double>(zSlice_) / D));
        const int nchX = (w + W - 1) / W, nchY = (h + H - 1) / H;
        std::vector<ChunkCoord> all;
        all.reserve(static_cast<size_t>(nchX) * static_cast<size_t>(nchY));
        for (int cy = 0; cy < nchY; ++cy)
            for (int cx = 0; cx < nchX; ++cx)
                all.push_back(ChunkCoord{cx, cy, static_cast<int16_t>(chunkZ)});
        cache_.ensureResident(all);

        // Composite order = the GPU's back-to-front order = ascending layerZ (stable, so equal-z ties keep the
        //   lens's declared order — exactly how the renderer resolves same-layer sprites by submit order).
        std::vector<const Layer*> ordered;
        ordered.reserve(lens_.layers.size());
        for (const Layer& l : lens_.layers) ordered.push_back(&l);
        std::stable_sort(ordered.begin(), ordered.end(),
                         [](const Layer* a, const Layer* b) { return a->layerZ < b->layerZ; });

        // 8-bit quantise with round-to-nearest + clamp (same rounding as the sprite path's packRGBA8 helper).
        const auto to8 = [](float v) -> uint8_t {
            const int i = static_cast<int>(v * 255.0f + 0.5f);
            return static_cast<uint8_t>(i < 0 ? 0 : (i > 255 ? 255 : i));
        };

        out.assign(static_cast<size_t>(w) * static_cast<size_t>(h) * 4u, 0u);   // default transparent

        for (int gy = 0; gy < h; ++gy) {
            for (int gx = 0; gx < w; ++gx) {
                float pr = 0.0f, pg = 0.0f, pb = 0.0f, pa = 0.0f;   // premultiplied accumulator
                for (const Layer* lp : ordered) {
                    const Layer& layer = *lp;
                    if (fieldByName_.find(layer.field) == fieldByName_.end()) continue;  // field not in schema
                    double phys;
                    if (!sampleField(layer.field, gx, gy, zSlice_, phys)) continue;      // field absent here
                    Rgba c;
                    if (!shadeLayer(layer, phys, gx, gy, zSlice_, c)) continue;          // filter rejected
                    float a = c.a < 0.0f ? 0.0f : (c.a > 1.0f ? 1.0f : c.a);
                    // Premultiplied "over": acc = src(premult) + acc*(1-src.a).
                    pr = c.r * a + pr * (1.0f - a);
                    pg = c.g * a + pg * (1.0f - a);
                    pb = c.b * a + pb * (1.0f - a);
                    pa = a       + pa * (1.0f - a);
                }
                uint8_t* px = &out[(static_cast<size_t>(gy) * static_cast<size_t>(w) + static_cast<size_t>(gx)) * 4u];
                if (pa > 0.0f) {
                    const float inv = 1.0f / pa;   // un-premultiply -> straight-alpha texel
                    px[0] = to8(pr * inv); px[1] = to8(pg * inv); px[2] = to8(pb * inv); px[3] = to8(pa);
                }   // else leave (0,0,0,0): no covering layer -> transparent, clear shows through
            }
        }
        return true;
    }

private:
    // The SHARED per-cell colour kernel: given a layer and the cell's already-decoded physical value, apply the
    //   layer's Filter (may reference other fields via the sampler), Palette, opacity, and optional hillshade,
    //   writing the final tint to `out`. Returns false when the Filter drops the cell (no contribution).
    // WHY : factored so update()'s sprite path AND bakeLensRGBA() run the EXACT same maths in the EXACT same
    //   order -> a baked texel and a live sprite are byte-identical (the parity contract of the "gen once" path).
    // HOW : identical to the block update() ran inline before the factoring — hillshade is a central-difference
    //   gradient of hillshadeField (cross-chunk; a missing neighbour falls back to the centre so seams degrade
    //   gracefully), fed to Hillshade::factor and multiplied into the RGB (alpha untouched).
    bool shadeLayer(const Layer& layer, double phys, int gx, int gy, int gz, Rgba& out) const {
        // Filter — a leaf may reference OTHER fields, resolved at this cell via the sampler.
        const auto sampler = [&](const std::string& name, double& o) {
            return sampleField(name, gx, gy, gz, o);
        };
        if (!layer.filter.eval(sampler, phys)) return false;

        Rgba color = layer.palette.eval(phys);
        color.a *= layer.opacity;

        // Optional relief shading from a (possibly different) field's gradient (central difference, cross-chunk;
        // a missing neighbour falls back to the centre so edges degrade gracefully).
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
        out = color;
        return true;
    }

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
    double pxPerWorld_{0.0};    // LOD : px écran/unité monde (=zoom) ; 0 -> LOD off (stride 1, byte-identique)
    double lodTargetPx_{3.5};   // LOD : taille apparente visée d'une cellule émise (px) — sous ça, on stride
    bool   compileCells_{true}; // baked-map : false -> update() n'émet plus la grille per-cellule (cf. setCompileCells)
    std::vector<CellDraw> cells_;
    std::vector<Region> regions_;       // global region overlay set (host-provided)
    std::vector<Marker> markers_;       // global marker overlay set (host-provided)
    std::vector<RegionDraw> regionDraws_;
    std::vector<MarkerDraw> markerDraws_;
    std::vector<TileChunkDraw> tileChunks_;  // compiled tile-grid chunks (tiling path); empty if no tileLayers
};

} // namespace mapview
} // namespace grove
