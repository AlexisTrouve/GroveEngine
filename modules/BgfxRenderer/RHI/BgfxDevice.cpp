#include "RHIDevice.h"
#include "RHICommandBuffer.h"

// bgfx includes - ONLY in this file
#include <bgfx/bgfx.h>
#include <bgfx/platform.h>
#include <bx/math.h>

#include <unordered_map>

namespace grove::rhi {

// ============================================================================
// Bgfx Device Implementation
// ============================================================================

class BgfxDevice : public IRHIDevice {
public:
    BgfxDevice() = default;
    ~BgfxDevice() override = default;

    bool init(void* nativeWindowHandle, uint16_t width, uint16_t height) override {
        m_width = width;
        m_height = height;

        bgfx::Init init;
        init.type = bgfx::RendererType::Count; // Auto-select
        init.resolution.width = width;
        init.resolution.height = height;
        init.resolution.reset = BGFX_RESET_VSYNC;
        init.platformData.nwh = nativeWindowHandle;

        if (!bgfx::init(init)) {
            return false;
        }

        // Set debug flags in debug builds
#ifdef _DEBUG
        bgfx::setDebug(BGFX_DEBUG_TEXT);
#endif

        // Set default view clear
        bgfx::setViewClear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x303030FF, 1.0f, 0);
        bgfx::setViewRect(0, 0, 0, width, height);

        m_initialized = true;
        return true;
    }

    void shutdown() override {
        if (m_initialized) {
            bgfx::shutdown();
            m_initialized = false;
        }
    }

    void reset(uint16_t width, uint16_t height) override {
        m_width = width;
        m_height = height;
        bgfx::reset(width, height, BGFX_RESET_VSYNC);
        bgfx::setViewRect(0, 0, 0, width, height);
    }

    DeviceCapabilities getCapabilities() const override {
        DeviceCapabilities caps;
        const bgfx::Caps* bgfxCaps = bgfx::getCaps();

        caps.maxTextureSize = static_cast<uint16_t>(bgfxCaps->limits.maxTextureSize);
        caps.maxViews = static_cast<uint16_t>(bgfxCaps->limits.maxViews);
        caps.maxDrawCalls = bgfxCaps->limits.maxDrawCalls;
        caps.instancingSupported = bgfxCaps->supported & BGFX_CAPS_INSTANCING;
        caps.computeSupported = bgfxCaps->supported & BGFX_CAPS_COMPUTE;
        caps.rendererName = bgfx::getRendererName(bgfxCaps->rendererType);
        caps.gpuName = bgfxCaps->vendorId != BGFX_PCI_ID_NONE
            ? std::to_string(bgfxCaps->vendorId) : "Unknown";

        return caps;
    }

    // ========================================
    // Resource Creation
    // ========================================

    TextureHandle createTexture(const TextureDesc& desc) override {
        bgfx::TextureFormat::Enum format = toBgfxFormat(desc.format);

        bgfx::TextureHandle handle = bgfx::createTexture2D(
            desc.width, desc.height,
            desc.mipLevels > 1,
            1, // layers
            format,
            BGFX_TEXTURE_NONE | BGFX_SAMPLER_NONE,
            desc.data ? bgfx::copy(desc.data, desc.dataSize) : nullptr
        );

        TextureHandle result;
        result.id = handle.idx;
        return result;
    }

    BufferHandle createBuffer(const BufferDesc& desc) override {
        BufferHandle result;

        if (desc.type == BufferDesc::Vertex) {
            bgfx::VertexBufferHandle vb;
            if (desc.dynamic) {
                bgfx::DynamicVertexBufferHandle dvb = bgfx::createDynamicVertexBuffer(
                    desc.size,
                    bgfx::VertexLayout(), // Will be set at draw time
                    BGFX_BUFFER_ALLOW_RESIZE
                );
                // Store as dynamic (high bit set)
                result.id = dvb.idx | 0x8000;
            } else {
                vb = bgfx::createVertexBuffer(
                    desc.data ? bgfx::copy(desc.data, desc.size) : bgfx::makeRef(nullptr, desc.size),
                    bgfx::VertexLayout()
                );
                result.id = vb.idx;
            }
        } else if (desc.type == BufferDesc::Index) {
            if (desc.dynamic) {
                bgfx::DynamicIndexBufferHandle dib = bgfx::createDynamicIndexBuffer(
                    desc.size / sizeof(uint16_t),
                    BGFX_BUFFER_ALLOW_RESIZE
                );
                result.id = dib.idx | 0x8000;
            } else {
                bgfx::IndexBufferHandle ib = bgfx::createIndexBuffer(
                    desc.data ? bgfx::copy(desc.data, desc.size) : bgfx::makeRef(nullptr, desc.size)
                );
                result.id = ib.idx;
            }
        } else { // Instance buffer - treated as vertex buffer
            bgfx::DynamicVertexBufferHandle dvb = bgfx::createDynamicVertexBuffer(
                desc.size,
                bgfx::VertexLayout(),
                BGFX_BUFFER_ALLOW_RESIZE
            );
            result.id = dvb.idx | 0x8000;
        }

        return result;
    }

    ShaderHandle createShader(const ShaderDesc& desc) override {
        bgfx::ShaderHandle vs = bgfx::createShader(
            bgfx::copy(desc.vsData, desc.vsSize)
        );
        bgfx::ShaderHandle fs = bgfx::createShader(
            bgfx::copy(desc.fsData, desc.fsSize)
        );

        bgfx::ProgramHandle program = bgfx::createProgram(vs, fs, true);

        ShaderHandle result;
        result.id = program.idx;
        return result;
    }

    UniformHandle createUniform(const char* name, uint8_t numVec4s) override {
        bgfx::UniformHandle uniform = bgfx::createUniform(
            name,
            numVec4s == 1 ? bgfx::UniformType::Vec4 : bgfx::UniformType::Mat4
        );

        UniformHandle result;
        result.id = uniform.idx;
        return result;
    }

    // ========================================
    // Resource Destruction
    // ========================================

    void destroy(TextureHandle handle) override {
        if (handle.isValid()) {
            bgfx::TextureHandle h = { handle.id };
            bgfx::destroy(h);
        }
    }

    void destroy(BufferHandle handle) override {
        if (handle.isValid()) {
            bool isDynamic = (handle.id & 0x8000) != 0;
            uint16_t idx = handle.id & 0x7FFF;

            if (isDynamic) {
                bgfx::DynamicVertexBufferHandle h = { idx };
                bgfx::destroy(h);
            } else {
                bgfx::VertexBufferHandle h = { idx };
                bgfx::destroy(h);
            }
        }
    }

    void destroy(ShaderHandle handle) override {
        if (handle.isValid()) {
            bgfx::ProgramHandle h = { handle.id };
            bgfx::destroy(h);
        }
    }

    void destroy(UniformHandle handle) override {
        if (handle.isValid()) {
            bgfx::UniformHandle h = { handle.id };
            bgfx::destroy(h);
        }
    }

    // ========================================
    // Dynamic Updates
    // ========================================

    void updateBuffer(BufferHandle handle, const void* data, uint32_t size) override {
        if (!handle.isValid()) return;

        bool isDynamic = (handle.id & 0x8000) != 0;
        uint16_t idx = handle.id & 0x7FFF;

        if (isDynamic) {
            bgfx::DynamicVertexBufferHandle h = { idx };
            bgfx::update(h, 0, bgfx::copy(data, size));
        }
        // Static buffers cannot be updated
    }

    void updateTexture(TextureHandle handle, const void* data, uint32_t size) override {
        if (!handle.isValid()) return;

        bgfx::TextureHandle h = { handle.id };
        bgfx::updateTexture2D(h, 0, 0, 0, 0, m_width, m_height, bgfx::copy(data, size));
    }

    // ========================================
    // View Setup
    // ========================================

    void setViewClear(ViewId id, uint32_t rgba, float depth) override {
        bgfx::setViewClear(id, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, rgba, depth, 0);
    }

    void setViewRect(ViewId id, uint16_t x, uint16_t y, uint16_t w, uint16_t h) override {
        bgfx::setViewRect(id, x, y, w, h);
    }

    void setViewTransform(ViewId id, const float* view, const float* proj) override {
        bgfx::setViewTransform(id, view, proj);
    }

    // ========================================
    // Frame
    // ========================================

    void frame() override {
        bgfx::frame();
    }

private:
    uint16_t m_width = 0;
    uint16_t m_height = 0;
    bool m_initialized = false;

    static bgfx::TextureFormat::Enum toBgfxFormat(TextureDesc::Format format) {
        switch (format) {
            case TextureDesc::RGBA8: return bgfx::TextureFormat::RGBA8;
            case TextureDesc::RGB8:  return bgfx::TextureFormat::RGB8;
            case TextureDesc::R8:    return bgfx::TextureFormat::R8;
            case TextureDesc::DXT1:  return bgfx::TextureFormat::BC1;
            case TextureDesc::DXT5:  return bgfx::TextureFormat::BC3;
            default: return bgfx::TextureFormat::RGBA8;
        }
    }
};

// ============================================================================
// Factory
// ============================================================================

std::unique_ptr<IRHIDevice> IRHIDevice::create() {
    return std::make_unique<BgfxDevice>();
}

} // namespace grove::rhi
