#pragma once

#include <grove/IModule.h>
#include <grove/IDataNode.h>
#include <grove/IIO.h>
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

private:
    // Logger
    std::shared_ptr<spdlog::logger> m_logger;

    // Core systems
    std::unique_ptr<rhi::IRHIDevice> m_device;
    std::unique_ptr<FrameAllocator> m_frameAllocator;
    std::unique_ptr<ShaderManager> m_shaderManager;
    std::unique_ptr<RenderGraph> m_renderGraph;
    std::unique_ptr<SceneCollector> m_sceneCollector;
    std::unique_ptr<ResourceCache> m_resourceCache;

    // Pass references (non-owning, owned by RenderGraph)
    SpritePass* m_spritePass = nullptr;

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

extern "C" {
    grove::IModule* createModule();
    void destroyModule(grove::IModule* module);
}
