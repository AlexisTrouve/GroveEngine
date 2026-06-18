/**
 * Mock RHI Device for Unit Tests
 *
 * Provides a stub implementation of IRHIDevice that tracks all calls
 * without requiring actual GPU/bgfx initialization.
 *
 * Usage:
 *   MockRHIDevice device;
 *   auto handle = device.createTexture(desc);
 *   REQUIRE(device.textureCreateCount == 1);
 */

#pragma once

#include "../../modules/BgfxRenderer/RHI/RHIDevice.h"
#include "../../modules/BgfxRenderer/RHI/RHITypes.h"

#include <atomic>
#include <vector>
#include <string>

namespace grove {
namespace test {

class MockRHIDevice : public rhi::IRHIDevice {
public:
    // ========================================
    // Counters (thread-safe)
    // ========================================
    std::atomic<int> textureCreateCount{0};
    std::atomic<int> bufferCreateCount{0};
    std::atomic<int> shaderCreateCount{0};
    std::atomic<int> uniformCreateCount{0};

    std::atomic<int> textureDestroyCount{0};
    std::atomic<int> bufferDestroyCount{0};
    std::atomic<int> shaderDestroyCount{0};
    std::atomic<int> uniformDestroyCount{0};

    std::atomic<int> updateBufferCount{0};
    std::atomic<int> updateTextureCount{0};

    // Largest byte size ever passed to updateBuffer — lets tests assert a pass never
    // uploads more than the buffer's capacity (overflow detection).
    std::atomic<uint32_t> lastUpdateBufferSize{0};
    std::atomic<uint32_t> maxUpdateBufferSize{0};

    std::atomic<int> setViewClearCount{0};
    std::atomic<int> setViewRectCount{0};
    std::atomic<int> setViewTransformCount{0};

    std::atomic<int> frameCount{0};

    // ========================================
    // Stored handles
    // ========================================
    std::vector<rhi::TextureHandle> textures;
    std::vector<rhi::TextureDesc> textureDescs;  // every desc handed to createTexture, in order

    // Region updates (Slice A1): each sub-rectangle patch handed to the updateTexture overload,
    // so tests can assert the renderer patches a window instead of re-uploading the whole image.
    struct TextureRegionUpdate { uint16_t x, y, w, h; uint32_t size; };
    std::vector<TextureRegionUpdate> textureRegionUpdates;

    std::vector<rhi::BufferHandle> buffers;
    std::vector<rhi::ShaderHandle> shaders;
    std::vector<rhi::UniformHandle> uniforms;

    // ========================================
    // Configuration
    // ========================================
    bool initShouldSucceed = true;
    std::string mockRendererName = "MockRenderer";
    std::string mockGpuName = "MockGPU";
    uint16_t mockMaxTextureSize = 4096;

    // ========================================
    // IRHIDevice Implementation
    // ========================================

    bool init(void* /*nativeWindowHandle*/, void* /*nativeDisplayHandle*/, uint16_t /*width*/, uint16_t /*height*/) override {
        return initShouldSucceed;
    }

    void shutdown() override {
        // Stub
    }

    void reset(uint16_t /*width*/, uint16_t /*height*/) override {
        // Stub
    }

    rhi::DeviceCapabilities getCapabilities() const override {
        rhi::DeviceCapabilities caps;
        caps.maxTextureSize = mockMaxTextureSize;
        caps.maxViews = 256;
        caps.maxDrawCalls = 100000;
        caps.instancingSupported = true;
        caps.computeSupported = false;
        caps.rendererName = mockRendererName;
        caps.gpuName = mockGpuName;
        return caps;
    }

    rhi::TextureHandle createTexture(const rhi::TextureDesc& desc) override {
        rhi::TextureHandle h;
        h.id = static_cast<uint16_t>(textureCreateCount.fetch_add(1) + 1);
        textures.push_back(h);
        textureDescs.push_back(desc);  // record so tests can assert format/filter/wrap round-trip
        return h;
    }

    rhi::BufferHandle createBuffer(const rhi::BufferDesc& /*desc*/) override {
        rhi::BufferHandle h;
        h.id = static_cast<uint16_t>(bufferCreateCount.fetch_add(1) + 1);
        buffers.push_back(h);
        return h;
    }

    rhi::ShaderHandle createShader(const rhi::ShaderDesc& /*desc*/) override {
        rhi::ShaderHandle h;
        h.id = static_cast<uint16_t>(shaderCreateCount.fetch_add(1) + 1);
        shaders.push_back(h);
        return h;
    }

    rhi::UniformHandle createUniform(const char* /*name*/, uint8_t /*numVec4s*/) override {
        rhi::UniformHandle h;
        h.id = static_cast<uint16_t>(uniformCreateCount.fetch_add(1) + 1);
        uniforms.push_back(h);
        return h;
    }

    void destroy(rhi::TextureHandle /*handle*/) override {
        textureDestroyCount++;
    }

    void destroy(rhi::BufferHandle /*handle*/) override {
        bufferDestroyCount++;
    }

    void destroy(rhi::ShaderHandle /*handle*/) override {
        shaderDestroyCount++;
    }

    void destroy(rhi::UniformHandle /*handle*/) override {
        uniformDestroyCount++;
    }

    void updateBuffer(rhi::BufferHandle /*handle*/, const void* /*data*/, uint32_t size) override {
        updateBufferCount++;
        lastUpdateBufferSize.store(size);
        uint32_t prev = maxUpdateBufferSize.load();
        while (size > prev && !maxUpdateBufferSize.compare_exchange_weak(prev, size)) {}
    }

    void updateTexture(rhi::TextureHandle /*handle*/, const void* /*data*/, uint32_t /*size*/) override {
        updateTextureCount++;
    }

    void updateTexture(rhi::TextureHandle /*handle*/, const void* /*data*/, uint32_t size,
                       uint16_t x, uint16_t y, uint16_t w, uint16_t h) override {
        textureRegionUpdates.push_back(TextureRegionUpdate{x, y, w, h, size});
    }

    rhi::TransientInstanceBuffer allocTransientInstanceBuffer(uint32_t count) override {
        rhi::TransientInstanceBuffer buffer{};
        buffer.data = nullptr;
        buffer.size = 0;
        buffer.count = count;
        buffer.stride = 0;
        buffer.poolIndex = 0;
        return buffer;
    }

    void setViewClear(rhi::ViewId /*id*/, uint32_t /*rgba*/, float /*depth*/) override {
        setViewClearCount++;
    }

    void setViewRect(rhi::ViewId /*id*/, uint16_t /*x*/, uint16_t /*y*/, uint16_t /*w*/, uint16_t /*h*/) override {
        setViewRectCount++;
    }

    void setViewTransform(rhi::ViewId /*id*/, const float* /*view*/, const float* /*proj*/) override {
        setViewTransformCount++;
    }

    void frame() override {
        frameCount++;
    }

    void executeCommandBuffer(const rhi::RHICommandBuffer& /*cmdBuffer*/) override {
        // Stub - just counts execution
    }

    // ========================================
    // Test Helpers
    // ========================================

    void reset() {
        textureCreateCount = 0;
        bufferCreateCount = 0;
        shaderCreateCount = 0;
        uniformCreateCount = 0;
        textureDestroyCount = 0;
        bufferDestroyCount = 0;
        shaderDestroyCount = 0;
        uniformDestroyCount = 0;
        updateBufferCount = 0;
        updateTextureCount = 0;
        lastUpdateBufferSize = 0;
        maxUpdateBufferSize = 0;
        setViewClearCount = 0;
        setViewRectCount = 0;
        setViewTransformCount = 0;
        frameCount = 0;

        textures.clear();
        textureDescs.clear();
        textureRegionUpdates.clear();
        buffers.clear();
        shaders.clear();
        uniforms.clear();
    }
};

} // namespace test
} // namespace grove
