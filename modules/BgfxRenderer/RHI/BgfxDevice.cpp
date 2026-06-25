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

        // Draw in SUBMIT order on the 2D views (world 0 + HUD 1). Default bgfx sorting groups draws by
        // state/program to cut state changes — which reorders our manually-layered passes (e.g. the
        // sector pass's wedges sank behind the sprite-pass UI). Sequential makes the pass order + the
        // per-pass layer sort authoritative, as a 2D layered renderer needs.
        bgfx::setViewMode(0, bgfx::ViewMode::Sequential);
        bgfx::setViewMode(1, bgfx::ViewMode::Sequential);

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
            desc.layers, // 1 = plain 2D; >1 = texture2DArray (tilemap atlas: one layer per tile)
            format,
            toSamplerFlags(desc),  // Point/Clamp for the index texture; 0 (legacy) by default
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
        // Detect sampler uniforms by name prefix (bgfx convention: s_*)
        bool isSampler = (name[0] == 's' && name[1] == '_');

        // numVec4s = the array size for a `vec4 name[N]` uniform (1 = a plain vec4). MUST be passed to
        // bgfx as `_num`, and the type stays Vec4 — a vec4[N] array, NOT a Mat4. (The old code dropped
        // _num and mis-typed N>1 as Mat4; it was never exercised, since every uniform was num==1, and a
        // mismatched array uniform corrupts/no-ops. The first real array uniform is u_tileAnim[4].)
        bgfx::UniformHandle uniform = bgfx::createUniform(
            name,
            isSampler ? bgfx::UniformType::Sampler : bgfx::UniformType::Vec4,
            isSampler ? 1 : numVec4s);

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
        // FLAG (pre-existing bug, not fixed here — out of A1 scope): this uses m_width/m_height,
        // the DEVICE/backbuffer size, not the texture's own size. It is only correct for a
        // full-screen-sized texture. The tilemap deliberately uses the region overload below
        // (explicit w/h), which sidesteps this. A real fix means TextureDesc dims stored per handle.
        bgfx::updateTexture2D(h, 0, 0, 0, 0, m_width, m_height, bgfx::copy(data, size));
    }

    void updateTexture(TextureHandle handle, const void* data, uint32_t size,
                       uint16_t x, uint16_t y, uint16_t w, uint16_t h) override {
        if (!handle.isValid()) return;

        bgfx::TextureHandle th = { handle.id };
        // Patch exactly the [x,y, x+w,y+h] rectangle of mip 0. w/h are explicit (no m_width/m_height
        // dependency) so this is correct for any texture size — the retained index grid included.
        bgfx::updateTexture2D(th, 0, 0, x, y, w, h, bgfx::copy(data, size));
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

    void requestScreenShot(const std::string& filePath) override {
        // QUOI : demande a bgfx une capture du BACKBUFFER (handle invalide = backbuffer).
        // POURQUOI : sert le devlog (cf. RHIDevice.h). bgfx lit les pixels au prochain
        //            bgfx::frame() puis appelle CallbackI::screenShot -> le callback PAR
        //            DEFAUT ecrit "<filePath>.tga" (bgfx.cpp, CallbackStub). Aucune init
        //            de callback custom requise -> changement minimal et sans risque.
        // COMMENT : non-bloquant ; la capture est prete apres le frame() qui suit l'appel.
        bgfx::requestScreenShot(BGFX_INVALID_HANDLE, filePath.c_str());
    }

    // ========================================
    // Offscreen framebuffers (test / readback)
    // ========================================

    FramebufferHandle createFramebuffer(uint16_t width, uint16_t height) override {
        // Color render target we draw into...
        bgfx::TextureHandle rt = bgfx::createTexture2D(
            width, height, false, 1, bgfx::TextureFormat::RGBA8,
            BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
        // ...and a CPU-readable copy the GPU blits into and we read back.
        bgfx::TextureHandle readback = bgfx::createTexture2D(
            width, height, false, 1, bgfx::TextureFormat::RGBA8,
            BGFX_TEXTURE_BLIT_DST | BGFX_TEXTURE_READ_BACK
            | BGFX_SAMPLER_POINT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
        bgfx::FrameBufferHandle fb = bgfx::createFrameBuffer(1, &rt, false);  // we own `rt`

        FramebufferHandle result;
        result.id = static_cast<uint16_t>(m_framebuffers.size());
        m_framebuffers.push_back({fb, rt, readback, width, height});
        return result;
    }

    void setViewFramebuffer(ViewId id, FramebufferHandle handle) override {
        if (handle.id >= m_framebuffers.size()) return;
        bgfx::setViewFrameBuffer(id, m_framebuffers[handle.id].fb);
    }

    bool readFramebuffer(FramebufferHandle handle, void* out, uint32_t outSize) override {
        if (handle.id >= m_framebuffers.size() || out == nullptr) return false;
        const FramebufferRecord& rec = m_framebuffers[handle.id];
        if (outSize < static_cast<uint32_t>(rec.w) * rec.h * 4u) return false;

        // Copy the render target into the read-back texture, then read it to the CPU. The blit goes
        // on a dedicated high view id so it stays out of the scene's view ordering. readTexture's
        // result is ready at frame `frameAvail`; pump frames until then (blocking, fine for a test).
        const ViewId kBlitView = 250;
        bgfx::blit(kBlitView, rec.readback, 0, 0, rec.rt, 0, 0, rec.w, rec.h);
        const uint32_t frameAvail = bgfx::readTexture(rec.readback, out);
        uint32_t f = bgfx::frame();
        while (f < frameAvail) { f = bgfx::frame(); }
        return true;
    }

    void destroy(FramebufferHandle handle) override {
        if (handle.id >= m_framebuffers.size()) return;
        FramebufferRecord& rec = m_framebuffers[handle.id];
        if (bgfx::isValid(rec.fb)) bgfx::destroy(rec.fb);
        if (bgfx::isValid(rec.rt)) bgfx::destroy(rec.rt);
        if (bgfx::isValid(rec.readback)) bgfx::destroy(rec.readback);
        rec.fb = BGFX_INVALID_HANDLE;
        rec.rt = BGFX_INVALID_HANDLE;
        rec.readback = BGFX_INVALID_HANDLE;
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

        // Store texture state to apply at draw time (not immediately). One pending binding PER
        // slot — the tilemap binds TWO textures (index in slot 0 + atlas in slot 1) for a single
        // submit, so a single global pending would let the second setTexture clobber the first.
        static constexpr uint8_t MAX_TEXTURE_SLOTS = 8;
        struct PendingTex { TextureHandle texture; UniformHandle sampler; bool has = false; };
        PendingTex pendingTex[MAX_TEXTURE_SLOTS];

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
                    // Store texture state per slot - apply at draw time, not immediately, so the
                    // texture is set after all other state is configured.
                    uint8_t slot = cmd.setTexture.slot;
                    if (slot < MAX_TEXTURE_SLOTS) {
                        pendingTex[slot].texture = cmd.setTexture.texture;
                        pendingTex[slot].sampler = cmd.setTexture.sampler;
                        pendingTex[slot].has = true;
                    }
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
                    // Apply every pending texture binding right before submit (index + atlas for
                    // the tilemap; a single texture for sprites/text).
                    for (uint8_t s = 0; s < MAX_TEXTURE_SLOTS; ++s) {
                        if (pendingTex[s].has) {
                            bgfx::TextureHandle tex = { pendingTex[s].texture.id };
                            bgfx::UniformHandle sampler = { pendingTex[s].sampler.id };
                            bgfx::setTexture(s, sampler, tex);
                        }
                    }
                    bgfx::ProgramHandle program = { cmd.submit.shader.id };
                    bgfx::submit(cmd.submit.view, program, cmd.submit.depth);
                    // Reset texture state after submit (consumed)
                    for (uint8_t s = 0; s < MAX_TEXTURE_SLOTS; ++s) pendingTex[s].has = false;
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

    // Offscreen framebuffers for test/readback (Slice ②). RHI FramebufferHandle.id indexes this.
    struct FramebufferRecord {
        bgfx::FrameBufferHandle fb;
        bgfx::TextureHandle rt;        // color render target (drawn into)
        bgfx::TextureHandle readback;  // CPU-readable blit copy
        uint16_t w, h;
    };
    std::vector<FramebufferRecord> m_framebuffers;

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
            case TextureDesc::R16UI: return bgfx::TextureFormat::R16U;  // integer tile-index texture
            default: return bgfx::TextureFormat::RGBA8;
        }
    }

    // Translate the RHI sampler description into bgfx sampler flags. Linear+Repeat -> 0 (== the
    // old BGFX_SAMPLER_NONE), so every pre-A0 texture is created identically. Point/Clamp are
    // opt-in for the tilemap index texture (no filtering / no wrap on integer tile ids).
    static uint64_t toSamplerFlags(const TextureDesc& desc) {
        uint64_t flags = BGFX_TEXTURE_NONE;
        if (desc.filter == TextureDesc::Point) {
            flags |= BGFX_SAMPLER_POINT;  // MIN|MAG|MIP point — no bilinear blend across tile ids
        }
        if (desc.wrap == TextureDesc::Clamp) {
            flags |= BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;  // no wrap at the chunk edge
        }
        return flags;
    }
};

// ============================================================================
// Factory
// ============================================================================

std::unique_ptr<IRHIDevice> IRHIDevice::create() {
    return std::make_unique<BgfxDevice>();
}

} // namespace grove::rhi
