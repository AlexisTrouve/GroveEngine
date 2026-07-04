#pragma once

/**
 * tests/visual/MapViewPoster — render the WHOLE map to ONE image by TILING + stitching.
 *
 * WHY  : `--shot` renders the whole world in a SINGLE frame, so it is capped by the sprite path's ~131k
 *        cells/frame ceiling (blank past it) and by the framebuffer/texture size — a big map can't be captured
 *        faithfully. A poster sidesteps all three walls: it renders the world in a grid of small TILES (each
 *        well under the cell ceiling AND under the fb size limit), reads each back, and composites them into
 *        one big CPU buffer. Output is `cells × pixels-per-cell` on each axis with NO cap — a huge map yields a
 *        huge PNG (the caller's explicit intent). Per tile the camera is set to exactly that tile's cell range
 *        at `ppc` px/cell, so there is no sub-pixel loss and no letterbox — the image is the map, edge to edge.
 *
 * HOW  : header-only, drives an existing ViewerApp (its cull→submit→publish-camera pipeline) + the RHI device's
 *        offscreen framebuffer/readback. One fb sized `tileCells·ppc`; for each tile set the camera (world coord
 *        at the fb top-left = the tile's top-left cell edge, matching fitCamera's corner convention), render a
 *        few settle frames, read back, and memcpy the valid sub-rect into the full image. FAIL-FRANC: if the
 *        full image can't be allocated (a truly enormous map) it returns ok=false rather than degrading silently.
 */

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <new>
#include <vector>

#include "BgfxRendererModule.h"
#include "RHI/RHIDevice.h"
#include "RHI/RHITypes.h"
#include "Scene/Camera.h"

#include "MapViewViewerApp.h"

namespace grove {
namespace mvdemo {

// The stitched result: an RGBA8 image of the whole map, `width × height` px. ok=false => allocation/readback
// failed (e.g. the map is too large to hold in RAM) — the caller reports it, never writes a partial poster.
struct PosterResult {
    std::vector<uint8_t> rgba;
    int  width  = 0;
    int  height = 0;
    bool ok     = false;
};

// Render [minCell..minCell+cells) cells (both axes) at `ppc` pixels/cell into one image, tiling by `tileCells`
// cells per side (chosen so a tile stays under the sprite ceiling + the fb size). `cellSize` = world units per
// cell (square cells assumed — uniform camera zoom). Returns the full RGBA buffer + its dimensions.
inline PosterResult renderPoster(ViewerApp& app, BgfxRendererModule* renderer,
                                 int minCellX, int minCellY, int cellsX, int cellsY,
                                 double cellSize, int ppc, int tileCells) {
    PosterResult r;
    if (cellsX <= 0 || cellsY <= 0 || ppc <= 0 || tileCells <= 0 || cellSize <= 0.0) return r;
    rhi::IRHIDevice* dev = renderer ? renderer->getDevice() : nullptr;
    if (!dev) return r;

    const int tilePx = tileCells * ppc;
    const long long W = static_cast<long long>(cellsX) * ppc;   // full image dims (NO cap: big map -> big png)
    const long long H = static_cast<long long>(cellsY) * ppc;
    r.width  = static_cast<int>(W);
    r.height = static_cast<int>(H);

    // Allocate the FULL image up front. px/cell with no size cap means a huge map is a huge buffer; if it does
    // not fit, FAIL FRANC (ok=false) instead of silently shrinking. 4 bytes/px RGBA8.
    try {
        r.rgba.assign(static_cast<size_t>(W) * static_cast<size_t>(H) * 4u, 0);
    } catch (const std::bad_alloc&) {
        return r;   // ok stays false
    }

    rhi::FramebufferHandle fb = dev->createFramebuffer(static_cast<uint16_t>(tilePx), static_cast<uint16_t>(tilePx));
    dev->setViewFramebuffer(0, fb);
    dev->setViewFramebuffer(1, fb);
    std::vector<uint8_t> tile(static_cast<size_t>(tilePx) * static_cast<size_t>(tilePx) * 4u);

    const int   nCols = (cellsX + tileCells - 1) / tileCells;
    const int   nRows = (cellsY + tileCells - 1) / tileCells;
    const float zoom  = static_cast<float>(static_cast<double>(ppc) / cellSize);   // px per world unit

    for (int row = 0; row < nRows; ++row) {
        for (int col = 0; col < nCols; ++col) {
            const int tcx = std::min(tileCells, cellsX - col * tileCells);   // valid cells this tile (edge partial)
            const int tcy = std::min(tileCells, cellsY - row * tileCells);

            camera::CameraView cam;
            cam.zoom      = zoom;
            cam.x         = static_cast<float>((minCellX + col * tileCells) * cellSize);   // world @ fb top-left
            cam.y         = static_cast<float>((minCellY + row * tileCells) * cellSize);
            cam.viewportW = static_cast<float>(tilePx);
            cam.viewportH = static_cast<float>(tilePx);
            cam.rotation  = 0.0f;
            app.setCamera(cam);
            for (int f = 0; f < 3; ++f) app.renderFrame(1.0f / 60.0f);   // settle async uploads (marker icons)

            if (!dev->readFramebuffer(fb, tile.data(), static_cast<uint32_t>(tile.size()))) {
                return r;   // ok=false (readback failed) — no partial poster
            }
            // Composite the valid sub-rect (tcx·ppc × tcy·ppc, at the tile fb's top-left) into the full image.
            const int       validW = tcx * ppc;
            const int       validH = tcy * ppc;
            const long long dstX   = static_cast<long long>(col) * tileCells * ppc;
            const long long dstY   = static_cast<long long>(row) * tileCells * ppc;
            for (int ty = 0; ty < validH; ++ty) {
                std::memcpy(&r.rgba[((dstY + ty) * W + dstX) * 4],
                            &tile[static_cast<size_t>(ty) * tilePx * 4],
                            static_cast<size_t>(validW) * 4);
            }
        }
    }
    r.ok = true;
    return r;
}

} // namespace mvdemo
} // namespace grove
