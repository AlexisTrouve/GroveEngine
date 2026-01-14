#include "TextureLoader.h"
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

    // Create texture via RHI
    rhi::TextureDesc desc;
    desc.width = static_cast<uint16_t>(width);
    desc.height = static_cast<uint16_t>(height);
    desc.format = rhi::TextureDesc::RGBA8;
    desc.data = pixels;
    desc.dataSize = static_cast<uint32_t>(width * height * 4);

    result.handle = device.createTexture(desc);
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

} // namespace grove
