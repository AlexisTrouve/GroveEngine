#include "TextureLoader.h"
#include "AtlasSlice.h"
#include "MipChain.h"
#include "../RHI/RHIDevice.h"

#define STB_IMAGE_IMPLEMENTATION
#include <stb/stb_image.h>

#include <fstream>
#include <spdlog/spdlog.h>

namespace grove {

TextureLoader::LoadResult TextureLoader::loadFromFile(rhi::IRHIDevice& device, const std::string& path) {
    LoadResult result;

    spdlog::info("📂 TextureLoader: Loading texture from '{}'", path);

    // Read file into memory
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        result.error = "Failed to open file: " + path;
        spdlog::error("❌ TextureLoader: FAILED to open file '{}'", path);
        return result;
    }

    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> buffer(static_cast<size_t>(size));
    if (!file.read(reinterpret_cast<char*>(buffer.data()), size)) {
        result.error = "Failed to read file: " + path;
        spdlog::error("❌ TextureLoader: FAILED to read file '{}'", path);
        return result;
    }

    spdlog::info("✅ TextureLoader: File '{}' read successfully ({} bytes)", path, size);
    auto loadResult = loadFromMemory(device, buffer.data(), buffer.size());

    if (loadResult.success) {
        spdlog::info("✅ TextureLoader: Texture '{}' loaded successfully ({}x{}, handle={})",
            path, loadResult.width, loadResult.height, loadResult.handle.id);
    } else {
        spdlog::error("❌ TextureLoader: FAILED to load texture '{}': {}", path, loadResult.error);
    }

    return loadResult;
}

TextureLoader::LoadResult TextureLoader::loadFromMemory(rhi::IRHIDevice& device, const uint8_t* data, size_t size) {
    LoadResult result;

    // Decode image with stb_image
    int width, height, channels;
    // Force RGBA output (4 channels)
    stbi_uc* pixels = stbi_load_from_memory(data, static_cast<int>(size), &width, &height, &channels, 4);

    if (!pixels) {
        result.error = "stb_image failed: ";
        result.error += stbi_failure_reason();
        return result;
    }

    // Log decoded image info
    spdlog::info("TextureLoader: Decoded image {}x{} (original {} channels, converted to RGBA)",
        width, height, channels);
    if (width > 0 && height > 0) {
        spdlog::info("  First pixel: R={}, G={}, B={}, A={}",
            pixels[0], pixels[1], pixels[2], pixels[3]);
        spdlog::info("  Last pixel: R={}, G={}, B={}, A={}",
            pixels[(width*height-1)*4 + 0], pixels[(width*height-1)*4 + 1],
            pixels[(width*height-1)*4 + 2], pixels[(width*height-1)*4 + 3]);
    }

    // Build a MIPPED RGBA8 chain so the GPU trilinear-filters the sprite when minified — without mips
    // free unit sprites shimmer/alias under strong zoom-out. Box-filter is pure + unit-tested
    // (grove::tex, see test_lod_color [mip]); createTexture uploads the contiguous chain (hasMips) and
    // the default sampler (Linear) trilinears it — same proven path as the tilemap LOD color.
    int mips = 0;
    std::vector<uint32_t> mipChain = tex::buildRgba8MipChain(
        reinterpret_cast<const uint32_t*>(pixels), width, height, mips);

    // Create texture via RHI
    rhi::TextureDesc desc;
    desc.width = static_cast<uint16_t>(width);
    desc.height = static_cast<uint16_t>(height);
    desc.format = rhi::TextureDesc::RGBA8;
    desc.mipLevels = static_cast<uint8_t>(mips);
    desc.data = mipChain.data();
    desc.dataSize = static_cast<uint32_t>(mipChain.size() * sizeof(uint32_t));

    result.handle = device.createTexture(desc);   // bgfx::copy duplicates the chain
    result.width = desc.width;
    result.height = desc.height;
    result.success = result.handle.isValid();

    if (result.success) {
        spdlog::info("✅ TextureLoader: GPU texture created successfully (handle={})", result.handle.id);
    } else {
        result.error = "Failed to create GPU texture";
        spdlog::error("❌ TextureLoader: FAILED to create GPU texture (handle invalid)");
    }

    // Free stb_image memory
    stbi_image_free(pixels);

    return result;
}

TextureLoader::LoadResult TextureLoader::loadArrayFromFile(rhi::IRHIDevice& device,
                                                           const std::string& path, int tileW, int tileH) {
    LoadResult result;
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        result.error = "Failed to open file: " + path;
        spdlog::error("❌ TextureLoader: FAILED to open atlas '{}'", path);
        return result;
    }
    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<uint8_t> buffer(static_cast<size_t>(size));
    if (!file.read(reinterpret_cast<char*>(buffer.data()), size)) {
        result.error = "Failed to read file: " + path;
        return result;
    }
    return loadArrayFromMemory(device, buffer.data(), buffer.size(), tileW, tileH);
}

TextureLoader::LoadResult TextureLoader::loadArrayFromMemory(rhi::IRHIDevice& device,
                                                             const uint8_t* data, size_t size,
                                                             int tileW, int tileH) {
    LoadResult result;
    if (tileW <= 0 || tileH <= 0) {
        result.error = "invalid tile size";
        return result;
    }

    int width = 0, height = 0, channels = 0;
    stbi_uc* pixels = stbi_load_from_memory(data, static_cast<int>(size), &width, &height, &channels, 4);
    if (!pixels) {
        result.error = std::string("stb_image failed: ") + stbi_failure_reason();
        return result;
    }

    // Slice the grid into row-major per-tile layers (RGBA8 texel == one uint32; no byte swap, just a
    // block copy, so bgfx RGBA8 reads R,G,B,A correctly).
    int layers = 0;
    std::vector<uint32_t> arr = atlas::sliceToArray(
        reinterpret_cast<const uint32_t*>(pixels), width, height, tileW, tileH, layers);
    stbi_image_free(pixels);

    if (layers < 1) {
        result.error = "tile size larger than image (0 layers)";
        return result;
    }

    rhi::TextureDesc desc;
    desc.width  = static_cast<uint16_t>(tileW);
    desc.height = static_cast<uint16_t>(tileH);
    desc.layers = static_cast<uint16_t>(layers);
    desc.format = rhi::TextureDesc::RGBA8;
    desc.data = arr.data();
    desc.dataSize = static_cast<uint32_t>(arr.size() * sizeof(uint32_t));

    result.handle = device.createTexture(desc);
    result.width  = static_cast<uint16_t>(tileW);
    result.height = static_cast<uint16_t>(tileH);
    result.layers = static_cast<uint16_t>(layers);
    result.success = result.handle.isValid();
    if (result.success) {
        spdlog::info("✅ TextureLoader: atlas array {}x{} px image -> {} layers of {}x{}",
                     width, height, layers, tileW, tileH);
    } else {
        result.error = "Failed to create array texture";
    }
    return result;
}

} // namespace grove
