#pragma once

#include "../RHI/RHITypes.h"
#include <string>
#include <vector>
#include <cstdint>

namespace grove {

namespace rhi { class IRHIDevice; }

/**
 * @brief Loads textures from files (PNG, JPG, etc.)
 *
 * Uses stb_image for decoding. All textures are loaded as RGBA8.
 */
class TextureLoader {
public:
    struct LoadResult {
        bool success = false;
        rhi::TextureHandle handle;
        uint16_t width = 0;     // for an array load, this is the per-tile width
        uint16_t height = 0;    // ...and per-tile height
        uint16_t layers = 1;    // >1 for an array load (tiles sliced from the grid)
        std::string error;
    };

    /**
     * @brief Load texture from file
     * @param device RHI device for texture creation
     * @param path Path to image file (PNG, JPG, BMP, TGA, etc.)
     * @return LoadResult with handle and dimensions on success
     */
    static LoadResult loadFromFile(rhi::IRHIDevice& device, const std::string& path);

    /**
     * @brief Load texture from memory
     * @param device RHI device for texture creation
     * @param data Raw file data (PNG/JPG bytes, not decoded pixels)
     * @param size Size of data in bytes
     * @return LoadResult with handle and dimensions on success
     */
    static LoadResult loadFromMemory(rhi::IRHIDevice& device, const uint8_t* data, size_t size);

    /**
     * @brief Load a tileset GRID image as a texture2DArray (one tile per layer, Slice A3.3).
     *        The image is decoded, sliced into tileW x tileH tiles (row-major: layer = row*cols+col,
     *        so tile id 1 = top-left), and uploaded as an array. Pair with TilemapPass::setTileset.
     * @return LoadResult with handle + per-tile width/height + layer count.
     */
    static LoadResult loadArrayFromFile(rhi::IRHIDevice& device, const std::string& path,
                                        int tileW, int tileH);
    static LoadResult loadArrayFromMemory(rhi::IRHIDevice& device, const uint8_t* data, size_t size,
                                          int tileW, int tileH);

    /**
     * @brief Decode an image file to RGBA8 pixels in memory (NO GPU upload). For runtime atlas packing —
     *        the packer decodes several images, assembles them into one buffer, then uploads once.
     * @return true on success; outPixels = w*h*4 bytes (RGBA8), outW/outH = dimensions.
     */
    static bool decodeRgba(const std::string& path, std::vector<uint8_t>& outPixels, int& outW, int& outH);
};

} // namespace grove
