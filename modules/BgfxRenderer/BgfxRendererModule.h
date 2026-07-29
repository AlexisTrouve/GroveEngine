#pragma once

#include <grove/IModule.h>
#include <grove/IDataNode.h>
#include <grove/IIO.h>
#include "RHI/RHITypes.h"   // FramebufferHandle held by value for the lighting targets
#include <memory>
#include <string>

namespace spdlog { class logger; }

namespace grove {

namespace rhi { class IRHIDevice; }
class FrameAllocator;
class RenderGraph;
class SceneCollector;
class ResourceCache;
class ShaderManager;
class SpritePass;
class TilemapPass;
class DebugOverlay;
struct SpriteInstance;    // POD GPU instance (Frame/FramePacket.h) — submitSpriteBatch takes a pointer
struct ParticleInstance;  // POD (Frame/FramePacket.h) — submitParticleBatch takes a pointer
struct TextCommand;       // (Frame/FramePacket.h) — submitTextBatch takes a pointer (carries its string)
namespace assets { class AssetManager; class BgfxTextureProvider; class ThreadedDecoder; }   // streaming texture assets

// ============================================================================
// BgfxRenderer Module - 2D rendering via bgfx
// ============================================================================

class BgfxRendererModule : public IModule {
public:
    BgfxRendererModule();
    ~BgfxRendererModule() override;

    // ========================================
    // IModule Interface
    // ========================================

    void setConfiguration(const IDataNode& config, IIO* io, ITaskScheduler* scheduler) override;
    void process(const IDataNode& input) override;
    void shutdown() override;

    std::unique_ptr<IDataNode> getState() override;
    void setState(const IDataNode& state) override;

    const IDataNode& getConfiguration() override;
    std::unique_ptr<IDataNode> getHealthStatus() override;

    std::string getType() const override { return "bgfx_renderer"; }
    bool isIdle() const override { return true; }

    // ========================================
    // Public API for external access
    // ========================================

    ResourceCache* getResourceCache() const;
    rhi::IRHIDevice* getDevice() const;
    assets::AssetManager* getAssetManager() const;   // streaming texture assets (string id -> texture)

    // BULK sprite submission — direct, IIO/JSON-free. A statically-linked host that already
    // holds packed SpriteInstances feeds them straight to this frame's scene (call between
    // frames, before the next process()). This is the high-throughput path: render:sprite
    // sends one JSON message per sprite (deep-copied by IIO, ~10µs each); this is ~ns/sprite.
    void submitSpriteBatch(const SpriteInstance* data, size_t count);

    // BULK particle submission — same direct, IIO/JSON-free path as submitSpriteBatch, for a crowd's
    // per-agent particles (thruster trails, impacts). render:particle sends one JSON message per
    // particle (~10µs); this is ~ns/particle. World-space; feed between frames before the next process().
    void submitParticleBatch(const ParticleInstance* data, size_t count);

    // BULK text submission — N labels in one IIO/JSON-free call (render:text = one message per label).
    // Each item carries its string via TextCommand.text (null-terminated); it is copied into the frame,
    // so the caller's buffers need not outlive the call. World-space; feed between frames.
    void submitTextBatch(const TextCommand* items, size_t count);

private:
    // Logger
    std::shared_ptr<spdlog::logger> m_logger;

    // Core systems
    std::unique_ptr<rhi::IRHIDevice> m_device;
    std::unique_ptr<FrameAllocator> m_frameAllocator;

    // ---- Lighting (L1) ------------------------------------------------------------------------
    // Offscreen targets, created ONLY once a game publishes render:ambient and destroyed when the
    // window size changes. A frame with no ambient never touches any of this: no target, no view
    // redirection, no composite draw — the world goes straight to the backbuffer exactly as it did
    // before lighting existed. See docs/design/lighting-2d.md §3.
    class CompositePass* m_compositePass = nullptr;   // owned by the render graph, borrowed here
    class LightPass*     m_lightPass = nullptr;       // idem — fed the occlusion map each frame
    // Placeholder occlusion map (core C2): a 1x1 WHITE texel = vacuum everywhere, sampled with
    // clamp so the march reads white wherever it looks. Nothing writes occluders yet; plan W
    // replaces this with a real screen-space target. Created once, destroyed at shutdown.
    rhi::TextureHandle m_occlusionTex;
    rhi::FramebufferHandle m_sceneFB;
    rhi::FramebufferHandle m_lightFB;
    rhi::FramebufferHandle m_occlusionFB;
    uint16_t m_lightingWidth = 0;      // size the targets were built for (0 = none yet)
    uint16_t m_lightingHeight = 0;

    // Create/resize the offscreen targets to WxH. No-op when they already match.
    void ensureLightingTargets(uint16_t width, uint16_t height);
    // Release them (window resize, or shutdown).
    void releaseLightingTargets();
    std::unique_ptr<ShaderManager> m_shaderManager;
    std::unique_ptr<RenderGraph> m_renderGraph;
    std::unique_ptr<SceneCollector> m_sceneCollector;
    std::unique_ptr<ResourceCache> m_resourceCache;
    std::unique_ptr<DebugOverlay> m_debugOverlay;
    std::unique_ptr<assets::BgfxTextureProvider> m_textureProvider;   // GPU side of the asset system
    std::unique_ptr<assets::AssetManager> m_assetManager;             // string id -> resident texture
    std::unique_ptr<assets::ThreadedDecoder> m_asyncDecoder;          // phase 3: off-thread decode (opt-in)

    // Pass references (non-owning, owned by RenderGraph)
    SpritePass* m_spritePass = nullptr;
    class TextPass* m_textPass = nullptr;   // non-owning — render:font rebakes its atlas
    TilemapPass* m_tilemapPass = nullptr;   // non-owning (setTileset / setFogTexture)

    // IIO (non-owning)
    IIO* m_io = nullptr;

    // Config (from IDataNode)
    uint16_t m_width = 1280;
    uint16_t m_height = 720;
    std::string m_backend = "opengl";
    std::string m_shaderPath = "./shaders";
    bool m_vsync = true;
    int m_maxSprites = 10000;
    std::unique_ptr<IDataNode> m_configCache;

    // Stats
    uint64_t m_frameCount = 0;
};

} // namespace grove

// ============================================================================
// C Export (required for dlopen)
// ============================================================================

#ifdef _WIN32
#define GROVE_MODULE_EXPORT __declspec(dllexport)
#else
#define GROVE_MODULE_EXPORT
#endif

extern "C" {
    GROVE_MODULE_EXPORT grove::IModule* createModule();
    GROVE_MODULE_EXPORT void destroyModule(grove::IModule* module);
}
