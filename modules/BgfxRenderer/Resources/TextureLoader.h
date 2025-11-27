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
        uint16_t width = 0;
        uint16_t height = 0;
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
};

} // namespace grove
