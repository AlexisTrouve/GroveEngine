#pragma once

#include "RHITypes.h"
#include <memory>
#include <string>

namespace grove::rhi {

// ============================================================================
// Device Capabilities
// ============================================================================

struct DeviceCapabilities {
    uint16_t maxTextureSize = 0;
    uint16_t maxViews = 0;
    uint32_t maxDrawCalls = 0;
    bool instancingSupported = false;
    bool computeSupported = false;
    std::string rendererName;
    std::string gpuName;
};

// ============================================================================
// RHI Device Interface - Abstract GPU access
// ============================================================================

class IRHIDevice {
public:
    virtual ~IRHIDevice() = default;

    // Lifecycle
    virtual bool init(void* nativeWindowHandle, uint16_t width, uint16_t height) = 0;
    virtual void shutdown() = 0;
    virtual void reset(uint16_t width, uint16_t height) = 0;

    // Capabilities
    virtual DeviceCapabilities getCapabilities() const = 0;

    // Resource creation
    virtual TextureHandle createTexture(const TextureDesc& desc) = 0;
    virtual BufferHandle createBuffer(const BufferDesc& desc) = 0;
    virtual ShaderHandle createShader(const ShaderDesc& desc) = 0;
    virtual UniformHandle createUniform(const char* name, uint8_t numVec4s) = 0;

    // Resource destruction
    virtual void destroy(TextureHandle handle) = 0;
    virtual void destroy(BufferHandle handle) = 0;
    virtual void destroy(ShaderHandle handle) = 0;
    virtual void destroy(UniformHandle handle) = 0;

    // Dynamic updates
    virtual void updateBuffer(BufferHandle handle, const void* data, uint32_t size) = 0;
    virtual void updateTexture(TextureHandle handle, const void* data, uint32_t size) = 0;

    // View setup
    virtual void setViewClear(ViewId id, uint32_t rgba, float depth) = 0;
    virtual void setViewRect(ViewId id, uint16_t x, uint16_t y, uint16_t w, uint16_t h) = 0;
    virtual void setViewTransform(ViewId id, const float* view, const float* proj) = 0;

    // Frame
    virtual void frame() = 0;

    // Command buffer execution
    virtual void executeCommandBuffer(const class RHICommandBuffer& cmdBuffer) = 0;

    // Factory
    static std::unique_ptr<IRHIDevice> create();
};

} // namespace grove::rhi
