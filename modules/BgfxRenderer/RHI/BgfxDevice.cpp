#include "RHIDevice.h"
#include "RHICommandBuffer.h"

// CRITICAL: Force single-threaded mode BEFORE including bgfx
// This avoids TLS (Thread-Local Storage) crashes when bgfx runs in a DLL on Windows
#ifndef BGFX_CONFIG_MULTITHREADED
#define BGFX_CONFIG_MULTITHREADED 0
#endif

// bgfx includes - ONLY in this file
#include <bgfx/bgfx.h>
#include <bgfx/platform.h>
#include <bx/math.h>

#include <spdlog/spdlog.h>
#include <unordered_map>

namespace grove::rhi {

// ============================================================================
// Bgfx Device Implementation
// ============================================================================

class BgfxDevice : public IRHIDevice {
public:
    BgfxDevice() = default;
    ~BgfxDevice() override = default;

    bool init(void* nativeWindowHandle, void* nativeDisplayHandle, uint16_t width, uint16_t height) override {
        m_width = width;
        m_height = height;

        // IMPORTANT: On Windows, we MUST call bgfx::renderFrame() before bgfx::init() to force
        // single-threaded mode. This is required because:
        // 1. In multi-threaded mode, bgfx starts a render thread that pumps Windows message queue
        // 2. This conflicts with SDL_PollEvent which also pumps the message queue
        // 3. The conflict causes crashes on frame 2
        // With single-threaded mode, bgfx::frame() does all the work synchronously.
#ifdef _WIN32
        // bgfx::renderFrame();  // Disabled - test_bgfx_minimal_win works without it
#endif

        bgfx::Init init;
        init.type = bgfx::RendererType::Direct3D11;
        init.resolution.width = width;
        init.resolution.height = height;
        init.resolution.reset = BGFX_RESET_VSYNC;

        // Set platform data
        init.platformData.nwh = nativeWindowHandle;
        init.platformData.ndt = nativeDisplayHandle; // X11 Display* on Linux, nullptr on Windows

        if (!bgfx::init(init)) {
            return false;
        }

        // Note: Debug text is enabled only when DebugOverlay is active
        // Don't enable it by default as it can cause issues on some platforms
        // bgfx::setDebug(BGFX_DEBUG_TEXT);

        // Set default view clear - BRIGHT RED for debugging
        bgfx::setViewClear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0xFF0000FF, 1.0f, 0);
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
            // Build vertex layout based on layout type
            bgfx::VertexLayout layout;
            if (desc.layout == BufferDesc::PosColor) {
                // vec3 position + vec4 color (7 floats = 28 bytes per vertex)
                layout.begin()
                    .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
                    .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Float, true)  // normalized
                    .end();
            } else {
                // Raw bytes - use 1-byte layout
                layout.begin().add(bgfx::Attrib::Position, 1, bgfx::AttribType::Uint8).end();
            }

            if (desc.dynamic) {
                bgfx::DynamicVertexBufferHandle dvb = bgfx::createDynamicVertexBuffer(
                    desc.layout == BufferDesc::PosColor ? desc.size / layout.getStride() : desc.size,
                    layout,
                    BGFX_BUFFER_ALLOW_RESIZE
                );
                result.id = dvb.idx | 0x8000;
            } else {
                // Use bgfx::copy instead of bgfx::makeRef to ensure data is copied
                // This avoids potential issues with DLL memory visibility on Windows
                bgfx::VertexBufferHandle vb = bgfx::createVertexBuffer(
                    desc.data ? bgfx::copy(desc.data, desc.size) : bgfx::copy(s_emptyBuffer, 1),
                    layout
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
                // Use bgfx::copy instead of bgfx::makeRef to ensure data is copied
                bgfx::IndexBufferHandle ib = bgfx::createIndexBuffer(
                    desc.data ? bgfx::copy(desc.data, desc.size) : bgfx::copy(s_emptyBuffer, 1)
                );
                result.id = ib.idx;
            }
        } else { // Instance buffer - treated as dynamic vertex buffer
            // Instance buffer layout: 5 x vec4 = 80 bytes per instance
            // i_data0 (TEXCOORD7), i_data1 (TEXCOORD6), i_data2 (TEXCOORD5),
            // i_data3 (TEXCOORD4), i_data4 (TEXCOORD3)
            bgfx::VertexLayout layout;
            layout.begin()
                .add(bgfx::Attrib::TexCoord7, 4, bgfx::AttribType::Float)  // i_data0: pos.xy, scale.xy
                .add(bgfx::Attrib::TexCoord6, 4, bgfx::AttribType::Float)  // i_data1: rotation, uv0.xy, unused
                .add(bgfx::Attrib::TexCoord5, 4, bgfx::AttribType::Float)  // i_data2: uv1.xy, unused, unused
                .add(bgfx::Attrib::TexCoord4, 4, bgfx::AttribType::Float)  // i_data3: reserved
                .add(bgfx::Attrib::TexCoord3, 4, bgfx::AttribType::Float)  // i_data4: color rgba
                .end();
            // 80 bytes per instance
            uint32_t instanceCount = desc.size / 80;
            bgfx::DynamicVertexBufferHandle dvb = bgfx::createDynamicVertexBuffer(
                instanceCount,
                layout,
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
    // Transient Instance Buffers
    // ========================================

    TransientInstanceBuffer allocTransientInstanceBuffer(uint32_t count) override {
        TransientInstanceBuffer result;

        constexpr uint16_t INSTANCE_STRIDE = 80;  // 5 x vec4

        // Check if we have space in the pool
        if (m_transientPoolCount >= MAX_TRANSIENT_BUFFERS) {
            return result;  // Pool full, return invalid
        }

        // Check if bgfx has enough transient memory
        if (bgfx::getAvailInstanceDataBuffer(count, INSTANCE_STRIDE) < count) {
            return result;  // Not enough memory
        }

        // Allocate from bgfx
        uint16_t poolIndex = m_transientPoolCount++;
        bgfx::allocInstanceDataBuffer(&m_transientPool[poolIndex], count, INSTANCE_STRIDE);

        result.data = m_transientPool[poolIndex].data;
        result.size = count * INSTANCE_STRIDE;
        result.count = count;
        result.stride = INSTANCE_STRIDE;
        result.poolIndex = poolIndex;

        return result;
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
        // Ensure view 0 is processed even if nothing was rendered to it
        bgfx::touch(0);

        // Present frame
        // Note: bgfx must be linked statically on Windows to avoid TLS/threading crashes.
        // Use BgfxRenderer_static library instead of BgfxRenderer DLL.
        bgfx::frame();

        // Reset transient pool for next frame
        m_transientPoolCount = 0;
    }

    void executeCommandBuffer(const RHICommandBuffer& cmdBuffer) override {
        // Reset transient instance state for this command buffer execution
        m_useTransientInstance = false;
        m_currentTransientIndex = UINT16_MAX;

        // Track current state for bgfx calls
        RenderState currentState;
        BufferHandle currentVB;
        BufferHandle currentIB;
        BufferHandle currentInstBuffer;
        uint32_t instStart = 0;
        uint32_t instCount = 0;

        // Store texture state to apply at draw time (not immediately)
        TextureHandle pendingTexture;
        UniformHandle pendingSampler;
        uint8_t pendingTextureSlot = 0;
        bool hasTexture = false;

        for (const Command& cmd : cmdBuffer.getCommands()) {
            switch (cmd.type) {
                case CommandType::SetState: {
                    currentState = cmd.setState.state;
                    // Build bgfx state flags
                    uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A;

                    switch (currentState.blend) {
                        case BlendMode::Alpha:
                            state |= BGFX_STATE_BLEND_ALPHA;
                            break;
                        case BlendMode::Additive:
                            state |= BGFX_STATE_BLEND_ADD;
                            break;
                        case BlendMode::Multiply:
                            state |= BGFX_STATE_BLEND_MULTIPLY;
                            break;
                        case BlendMode::None:
                        default:
                            break;
                    }

                    switch (currentState.cull) {
                        case CullMode::CW:
                            state |= BGFX_STATE_CULL_CW;
                            break;
                        case CullMode::CCW:
                            state |= BGFX_STATE_CULL_CCW;
                            break;
                        case CullMode::None:
                        default:
                            break;
                    }

                    // Primitive type
                    switch (currentState.primitive) {
                        case PrimitiveType::Lines:
                            state |= BGFX_STATE_PT_LINES;
                            break;
                        case PrimitiveType::Points:
                            state |= BGFX_STATE_PT_POINTS;
                            break;
                        case PrimitiveType::Triangles:
                        default:
                            // Triangles is default, no flag needed
                            break;
                    }

                    if (currentState.depthTest) {
                        state |= BGFX_STATE_DEPTH_TEST_LESS;
                    }
                    if (currentState.depthWrite) {
                        state |= BGFX_STATE_WRITE_Z;
                    }

                    bgfx::setState(state);
                    break;
                }

                case CommandType::SetTexture: {
                    // Store texture state - apply at draw time, not immediately
                    // This ensures texture is set after all other state is configured
                    pendingTexture = cmd.setTexture.texture;
                    pendingSampler = cmd.setTexture.sampler;
                    pendingTextureSlot = cmd.setTexture.slot;
                    hasTexture = true;
                    break;
                }

                case CommandType::SetUniform: {
                    bgfx::UniformHandle uniform = { cmd.setUniform.uniform.id };
                    bgfx::setUniform(uniform, cmd.setUniform.data, cmd.setUniform.numVec4s);
                    break;
                }

                case CommandType::SetVertexBuffer: {
                    currentVB = cmd.setVertexBuffer.buffer;
                    break;
                }

                case CommandType::SetIndexBuffer: {
                    currentIB = cmd.setIndexBuffer.buffer;
                    break;
                }

                case CommandType::SetInstanceBuffer: {
                    currentInstBuffer = cmd.setInstanceBuffer.buffer;
                    instStart = cmd.setInstanceBuffer.start;
                    instCount = cmd.setInstanceBuffer.count;
                    m_useTransientInstance = false;
                    break;
                }

                case CommandType::SetTransientInstanceBuffer: {
                    m_currentTransientIndex = cmd.setTransientInstanceBuffer.poolIndex;
                    instStart = cmd.setTransientInstanceBuffer.start;
                    instCount = cmd.setTransientInstanceBuffer.count;
                    m_useTransientInstance = true;
                    break;
                }

                case CommandType::SetScissor: {
                    bgfx::setScissor(cmd.setScissor.x, cmd.setScissor.y,
                                     cmd.setScissor.w, cmd.setScissor.h);
                    break;
                }

                case CommandType::Draw: {
                    // Set vertex buffer before draw
                    if (currentVB.isValid()) {
                        bool isDynamic = (currentVB.id & 0x8000) != 0;
                        uint16_t idx = currentVB.id & 0x7FFF;
                        if (isDynamic) {
                            bgfx::DynamicVertexBufferHandle h = { idx };
                            bgfx::setVertexBuffer(0, h, 0, cmd.draw.vertexCount);
                        } else {
                            bgfx::VertexBufferHandle h = { idx };
                            bgfx::setVertexBuffer(0, h, cmd.draw.startVertex, cmd.draw.vertexCount);
                        }
                    }
                    break;
                }

                case CommandType::DrawIndexed: {
                    // Set vertex and index buffers before draw
                    if (currentVB.isValid()) {
                        bool isDynamic = (currentVB.id & 0x8000) != 0;
                        uint16_t idx = currentVB.id & 0x7FFF;
                        if (isDynamic) {
                            bgfx::DynamicVertexBufferHandle h = { idx };
                            bgfx::setVertexBuffer(0, h);
                        } else {
                            bgfx::VertexBufferHandle h = { idx };
                            bgfx::setVertexBuffer(0, h);
                        }
                    }
                    if (currentIB.isValid()) {
                        bool isDynamic = (currentIB.id & 0x8000) != 0;
                        uint16_t idx = currentIB.id & 0x7FFF;
                        if (isDynamic) {
                            bgfx::DynamicIndexBufferHandle h = { idx };
                            bgfx::setIndexBuffer(h, cmd.drawIndexed.startIndex, cmd.drawIndexed.indexCount);
                        } else {
                            bgfx::IndexBufferHandle h = { idx };
                            bgfx::setIndexBuffer(h, cmd.drawIndexed.startIndex, cmd.drawIndexed.indexCount);
                        }
                    }
                    break;
                }

                case CommandType::DrawInstanced: {
                    // Set vertex, index, and instance buffers
                    if (currentVB.isValid()) {
                        bool isDynamic = (currentVB.id & 0x8000) != 0;
                        uint16_t idx = currentVB.id & 0x7FFF;
                        if (isDynamic) {
                            bgfx::DynamicVertexBufferHandle h = { idx };
                            bgfx::setVertexBuffer(0, h);
                        } else {
                            bgfx::VertexBufferHandle h = { idx };
                            bgfx::setVertexBuffer(0, h);
                        }
                    }
                    if (currentIB.isValid()) {
                        bool isDynamic = (currentIB.id & 0x8000) != 0;
                        uint16_t idx = currentIB.id & 0x7FFF;
                        if (isDynamic) {
                            bgfx::DynamicIndexBufferHandle h = { idx };
                            bgfx::setIndexBuffer(h, 0, cmd.drawInstanced.indexCount);
                        } else {
                            bgfx::IndexBufferHandle h = { idx };
                            bgfx::setIndexBuffer(h, 0, cmd.drawInstanced.indexCount);
                        }
                    }
                    // Set instance buffer (either dynamic or transient)
                    if (m_useTransientInstance && m_currentTransientIndex < m_transientPoolCount) {
                        // Transient instance buffer from pool
                        bgfx::setInstanceDataBuffer(&m_transientPool[m_currentTransientIndex], instStart, instCount);
                    } else if (currentInstBuffer.isValid()) {
                        bool isDynamic = (currentInstBuffer.id & 0x8000) != 0;
                        uint16_t idx = currentInstBuffer.id & 0x7FFF;
                        if (isDynamic) {
                            bgfx::DynamicVertexBufferHandle h = { idx };
                            bgfx::setInstanceDataBuffer(h, instStart, instCount);
                        }
                    }
                    break;
                }

                case CommandType::Submit: {
                    // Apply pending texture right before submit
                    if (hasTexture) {
                        bgfx::TextureHandle tex = { pendingTexture.id };
                        bgfx::UniformHandle sampler = { pendingSampler.id };
                        bgfx::setTexture(pendingTextureSlot, sampler, tex);
                    }
                    bgfx::ProgramHandle program = { cmd.submit.shader.id };
                    bgfx::submit(cmd.submit.view, program, cmd.submit.depth);
                    // Reset texture state after submit (consumed)
                    hasTexture = false;
                    break;
                }
            }
        }
    }

private:
    uint16_t m_width = 0;
    uint16_t m_height = 0;
    bool m_initialized = false;

    // Transient instance buffer pool (reset each frame)
    static constexpr uint16_t MAX_TRANSIENT_BUFFERS = 256;
    bgfx::InstanceDataBuffer m_transientPool[MAX_TRANSIENT_BUFFERS] = {};
    uint16_t m_transientPoolCount = 0;

    // Transient buffer state for command execution
    bool m_useTransientInstance = false;
    uint16_t m_currentTransientIndex = UINT16_MAX;

    // Empty buffer for null data fallback in buffer creation
    inline static const uint8_t s_emptyBuffer[1] = {0};

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
