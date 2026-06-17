#pragma once

/**
 * grove::anim::SpriteSheet — uniform-grid atlas -> UV mapping (flipbook slice F-a).
 *
 * WHAT  : A sprite sheet is a columns x rows grid of equal cells in one texture. frameUV(index)
 *         returns the [0,1] UV rectangle of cell `index` (row-major: left-to-right, top-to-
 *         bottom). The pixel-free foundation under frame-by-frame (flipbook) animation.
 *
 * WHY   : Spritesheet/flipbook animation = cycling which cell is displayed. Keeping the
 *         atlas->UV math pure and standalone keeps it headless-testable and reusable (a static
 *         sprite can pick a cell too); the Flipbook layer then deals only with timing and stays
 *         oblivious to atlas geometry.
 *
 * HOW   : UV origin top-left, v increasing downward — same convention as TilemapPass and the
 *         default render:sprite UVs. `count` optionally caps a partial last row (0 => full
 *         grid). Indices are clamped to [0, frameCount-1] so a bad index never reads out of the
 *         atlas (it just shows the nearest valid cell).
 *
 * LATER : irregular/packed atlases (explicit per-frame rects) can be added as a second variant
 *         without touching the Flipbook layer.
 */

namespace grove {
namespace anim {

struct SpriteSheet {
    int columns = 1;
    int rows = 1;
    int count = 0;   // 0 => columns*rows; otherwise caps the usable frame count (partial sheet)

    // Number of usable frames.
    int frameCount() const {
        const int grid = (columns > 0 ? columns : 1) * (rows > 0 ? rows : 1);
        return (count > 0 && count < grid) ? count : grid;
    }

    // UV rectangle of cell `index` (row-major). Index is clamped into the valid range.
    void frameUV(int index, float& u0, float& v0, float& u1, float& v1) const {
        const int cols = (columns > 0) ? columns : 1;
        const int rws = (rows > 0) ? rows : 1;

        const int last = frameCount() - 1;
        if (index < 0) index = 0;
        else if (index > last) index = last;

        const int col = index % cols;
        const int row = index / cols;

        const float uw = 1.0f / static_cast<float>(cols);
        const float vh = 1.0f / static_cast<float>(rws);

        u0 = static_cast<float>(col) * uw;
        v0 = static_cast<float>(row) * vh;
        u1 = u0 + uw;
        v1 = v0 + vh;
    }
};

} // namespace anim
} // namespace grove
