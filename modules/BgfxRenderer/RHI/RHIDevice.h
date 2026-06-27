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
    // nativeWindowHandle: Window handle (HWND on Windows, X11 Window on Linux)
    // nativeDisplayHandle: Display handle (nullptr on Windows, X11 Display* on Linux)
    // vsync=false uncaps the present (BGFX_RESET_NONE) — frames run as fast as the GPU+CPU
    // allow instead of locking to the monitor refresh. Default true (the safe game default).
    virtual bool init(void* nativeWindowHandle, void* nativeDisplayHandle, uint16_t width, uint16_t height, bool vsync = true) = 0;
    virtual void shutdown() = 0;
    virtual void reset(uint16_t width, uint16_t height) = 0;

    // Capabilities
    virtual DeviceCapabilities getCapabilities() const = 0;

    // Resource creation
    virtual TextureHandle createTexture(const TextureDesc& desc) = 0;
    virtual BufferHandle createBuffer(const BufferDesc& desc) = 0;
    virtual ShaderHandle createShader(const ShaderDesc& desc) = 0;
    virtual UniformHandle createUniform(const char* name, uint8_t numVec4s) = 0;

    // Offscreen render target for test/readback (Slice ②). createFramebuffer makes an RGBA8 color
    // target plus a CPU-readable copy; setViewFramebuffer redirects a view into it; readFramebuffer
    // blits + reads the pixels back to `out` (RGBA8, w*h*4 bytes), blocking until the GPU result is
    // ready. Real readback needs a GPU context (these power [gpu] tests); the mock stubs them.
    virtual FramebufferHandle createFramebuffer(uint16_t width, uint16_t height) = 0;
    virtual void setViewFramebuffer(ViewId id, FramebufferHandle fb) = 0;
    virtual bool readFramebuffer(FramebufferHandle fb, void* out, uint32_t outSize) = 0;

    // Resource destruction
    virtual void destroy(TextureHandle handle) = 0;
    virtual void destroy(BufferHandle handle) = 0;
    virtual void destroy(ShaderHandle handle) = 0;
    virtual void destroy(UniformHandle handle) = 0;
    virtual void destroy(FramebufferHandle handle) = 0;

    // Dynamic updates
    virtual void updateBuffer(BufferHandle handle, const void* data, uint32_t size) = 0;
    virtual void updateTexture(TextureHandle handle, const void* data, uint32_t size) = 0;
    // Region update (Slice A1): patch a sub-rectangle [x,y, x+w,y+h] of the texture instead of
    // re-uploading the whole image. The GPU tilemap uses it to flip a few tile ids / fog texels
    // in the retained index grid without re-sending the full grid every change.
    virtual void updateTexture(TextureHandle handle, const void* data, uint32_t size,
                               uint16_t x, uint16_t y, uint16_t w, uint16_t h) = 0;

    // Transient instance buffers (frame-local, for multi-batch rendering)
    // These are automatically freed at end of frame - no manual cleanup needed
    // Returns buffer with data pointer for CPU-side writing
    virtual TransientInstanceBuffer allocTransientInstanceBuffer(uint32_t count) = 0;

    // View setup
    virtual void setViewClear(ViewId id, uint32_t rgba, float depth) = 0;
    virtual void setViewRect(ViewId id, uint16_t x, uint16_t y, uint16_t w, uint16_t h) = 0;
    virtual void setViewTransform(ViewId id, const float* view, const float* proj) = 0;

    // Frame
    virtual void frame() = 0;

    // Screenshot of the BACKBUFFER, written to disk on the next frame.
    // QUOI    : demande une capture du backbuffer ; le fichier est ecrit (par le
    //           backend) au prochain frame() sous le chemin `filePath` (+ extension
    //           selon le backend, p.ex. ".tga" pour le callback bgfx par defaut).
    // POURQUOI: le jeu a besoin de captures pour son devlog (pipeline externe qui
    //           clone le repo) ; en faire une primitive de rendu (topic render:
    //           screenshot) garde l'isolation R1 -- la scene publie, le renderer
    //           capture, aucun acces direct a bgfx cote jeu.
    // COMMENT : impl PAR DEFAUT = no-op -> les backends sans capture (mock, autres
    //           RHI) ne sont pas forces de l'implementer ; seul BgfxDevice override.
    virtual void requestScreenShot(const std::string& filePath) { (void)filePath; }

    // Command buffer execution
    virtual void executeCommandBuffer(const class RHICommandBuffer& cmdBuffer) = 0;

    // Factory
    static std::unique_ptr<IRHIDevice> create();
};

} // namespace grove::rhi
