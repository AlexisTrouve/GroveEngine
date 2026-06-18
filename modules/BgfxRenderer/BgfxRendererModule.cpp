#include "BgfxRendererModule.h"
#include "RHI/RHIDevice.h"
#include "Frame/FrameAllocator.h"
#include "Frame/FramePacket.h"
#include "Shaders/ShaderManager.h"
#include "RenderGraph/RenderGraph.h"
#include "Scene/SceneCollector.h"
#include "Resources/ResourceCache.h"
#include "Debug/DebugOverlay.h"
#include "Passes/ClearPass.h"
#include "Passes/TilemapPass.h"
#include "Passes/SpritePass.h"
#include "Passes/TextPass.h"
#include "Passes/ParticlePass.h"
#include "Passes/DebugPass.h"

#include <grove/JsonDataNode.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

namespace grove {

BgfxRendererModule::BgfxRendererModule() = default;
BgfxRendererModule::~BgfxRendererModule() = default;

ResourceCache* BgfxRendererModule::getResourceCache() const {
    return m_resourceCache.get();
}

rhi::IRHIDevice* BgfxRendererModule::getDevice() const {
    return m_device.get();
}

void BgfxRendererModule::setConfiguration(const IDataNode& config, IIO* io, ITaskScheduler* scheduler) {
    m_io = io;

    // Setup logger
    m_logger = spdlog::get("BgfxRenderer");
    if (!m_logger) {
        m_logger = spdlog::stdout_color_mt("BgfxRenderer");
    }

    // Read static config via IDataNode
    m_width = static_cast<uint16_t>(config.getInt("windowWidth", 1280));
    m_height = static_cast<uint16_t>(config.getInt("windowHeight", 720));
    m_backend = config.getString("backend", "opengl");
    m_shaderPath = config.getString("shaderPath", "./shaders");
    m_vsync = config.getBool("vsync", true);
    m_maxSprites = config.getInt("maxSpritesPerBatch", 10000);
    size_t allocatorSize = static_cast<size_t>(config.getInt("frameAllocatorSizeMB", 16)) * 1024 * 1024;

    // Window handle (passed via config or 0 if separate WindowModule)
    // Use double to preserve 64-bit pointer values
    // Also try getInt as fallback for compatibility with older code that uses setInt
    void* windowHandle = nullptr;
    double handleDouble = config.getDouble("nativeWindowHandle", 0.0);
    if (handleDouble != 0.0) {
        windowHandle = reinterpret_cast<void*>(static_cast<uintptr_t>(handleDouble));
    } else {
        // Fallback: try reading as int (for 32-bit handles or compatibility)
        int handleInt = config.getInt("nativeWindowHandle", 0);
        if (handleInt != 0) {
            windowHandle = reinterpret_cast<void*>(static_cast<uintptr_t>(static_cast<uint32_t>(handleInt)));
            m_logger->warn("nativeWindowHandle passed as int - consider using setDouble for 64-bit handles");
        }
    }

    // Display handle (X11 Display* on Linux, 0/nullptr on Windows)
    void* displayHandle = nullptr;
    double displayDouble = config.getDouble("nativeDisplayHandle", 0.0);
    if (displayDouble != 0.0) {
        displayHandle = reinterpret_cast<void*>(static_cast<uintptr_t>(displayDouble));
    }

    m_logger->info("Initializing BgfxRenderer: {}x{} backend={}", m_width, m_height, m_backend);

    // Initialize subsystems
    m_frameAllocator = std::make_unique<FrameAllocator>(allocatorSize);

    m_device = rhi::IRHIDevice::create();
    if (!m_device->init(windowHandle, displayHandle, m_width, m_height)) {
        m_logger->error("Failed to initialize RHI device");
        return;
    }

    // Log device capabilities
    auto caps = m_device->getCapabilities();
    m_logger->info("GPU: {} ({})", caps.gpuName, caps.rendererName);
    m_logger->info("Max texture size: {}, Max draw calls: {}", caps.maxTextureSize, caps.maxDrawCalls);

    // Initialize shader manager
    m_shaderManager = std::make_unique<ShaderManager>();
    m_shaderManager->init(*m_device, caps.rendererName);
    m_logger->info("ShaderManager initialized with {} programs", m_shaderManager->getProgramCount());

    // Get shader handles for passes
    rhi::ShaderHandle spriteShader = m_shaderManager->getProgram("sprite");
    rhi::ShaderHandle debugShader = m_shaderManager->getProgram("debug");
    rhi::ShaderHandle tilemapShader = m_shaderManager->getProgram("tilemap");

    if (!spriteShader.isValid()) {
        m_logger->error("Failed to load sprite shader");
        return;
    }
    if (!tilemapShader.isValid()) {
        m_logger->error("Failed to load tilemap shader");
        return;
    }

    // Setup render graph with passes (inject shaders via constructors)
    m_logger->info("Creating RenderGraph...");
    m_renderGraph = std::make_unique<RenderGraph>();
    m_renderGraph->addPass(std::make_unique<ClearPass>());
    m_logger->info("Added ClearPass");

    // Setup resource cache first (needed by passes)
    m_resourceCache = std::make_unique<ResourceCache>();

    // Create TilemapPass (renders before sprites) — uses the dedicated GPU tilemap shader
    auto tilemapPass = std::make_unique<TilemapPass>(tilemapShader);
    tilemapPass->setResourceCache(m_resourceCache.get());
    m_renderGraph->addPass(std::move(tilemapPass));
    m_logger->info("Added TilemapPass");

    // Create SpritePass and keep reference for texture binding
    auto spritePass = std::make_unique<SpritePass>(spriteShader);
    m_spritePass = spritePass.get();  // Non-owning reference
    m_spritePass->setResourceCache(m_resourceCache.get());
    m_renderGraph->addPass(std::move(spritePass));
    m_logger->info("Added SpritePass");

    // Create TextPass (uses sprite shader for glyph quads)
    m_renderGraph->addPass(std::make_unique<TextPass>(spriteShader));
    m_logger->info("Added TextPass");

    // Create ParticlePass (uses sprite shader, renders after sprites with additive blending)
    auto particlePass = std::make_unique<ParticlePass>(spriteShader);
    particlePass->setResourceCache(m_resourceCache.get());
    m_renderGraph->addPass(std::move(particlePass));
    m_logger->info("Added ParticlePass");

    m_renderGraph->addPass(std::make_unique<DebugPass>(debugShader));
    m_logger->info("Added DebugPass");
    m_renderGraph->setup(*m_device);
    m_logger->info("RenderGraph setup complete");
    m_renderGraph->compile();
    m_logger->info("RenderGraph compiled");

    // Setup scene collector with IIO subscriptions and correct dimensions
    m_sceneCollector = std::make_unique<SceneCollector>();
    m_sceneCollector->setup(io, m_width, m_height);
    m_logger->info("SceneCollector setup complete with dimensions {}x{}", m_width, m_height);

    // Setup debug overlay
    m_debugOverlay = std::make_unique<DebugOverlay>();
    bool debugEnabled = config.getBool("debugOverlay", false);
    m_debugOverlay->setEnabled(debugEnabled);
    if (debugEnabled) {
        m_logger->info("Debug overlay enabled");
    }

    // Load default texture if specified in config
    std::string defaultTexturePath = config.getString("defaultTexture", "");
    if (!defaultTexturePath.empty()) {
        uint16_t texId = m_resourceCache->loadTextureWithId(*m_device, defaultTexturePath);
        if (texId > 0) {
            rhi::TextureHandle tex = m_resourceCache->getTextureById(texId);
            m_spritePass->setTexture(tex);
            m_logger->info("Loaded default texture: {} (id={})", defaultTexturePath, texId);
        } else {
            m_logger->warn("Failed to load default texture: {}", defaultTexturePath);
        }
    }

    // Load additional textures (texture1, texture2, etc.)
    for (int i = 1; i <= 10; ++i) {
        std::string key = "texture" + std::to_string(i);
        std::string path = config.getString(key, "");
        if (!path.empty()) {
            uint16_t texId = m_resourceCache->loadTextureWithId(*m_device, path);
            if (texId > 0) {
                m_logger->info("Loaded texture: {} (id={})", path, texId);
            } else {
                m_logger->warn("Failed to load texture: {}", path);
            }
        }
    }

    m_logger->info("BgfxRenderer initialized successfully");
}

void BgfxRendererModule::process(const IDataNode& input) {
    if (!m_device) {
        m_logger->error("Device not initialized");
        return;
    }

    // Reset frame allocator for this frame
    if (m_frameAllocator) {
        m_frameAllocator->reset();
    }

    // Collect scene data from IIO messages and prepare frame packet
    if (m_sceneCollector && m_renderGraph && m_frameAllocator && m_io) {
        // Get delta time from input (or default to 16ms)
        float deltaTime = static_cast<float>(input.getDouble("deltaTime", 0.016));

        // Collect all IIO messages for this frame
        m_sceneCollector->collect(m_io, deltaTime);

        // Generate immutable FramePacket for render passes
        FramePacket packet = m_sceneCollector->finalize(*m_frameAllocator);

        // Apply view transform (projection matrix for 2D rendering)
        m_device->setViewClear(0, packet.clearColor, 1.0f);
        m_device->setViewRect(0, packet.mainView.viewportX, packet.mainView.viewportY,
                               packet.mainView.viewportW, packet.mainView.viewportH);
        m_device->setViewTransform(0, packet.mainView.viewMatrix, packet.mainView.projMatrix);

        // HUD overlay (view 1): a FIXED screen-space transform so sprites/text published with
        // space:"screen" do NOT zoom/pan with the world. No setViewClear here on purpose —
        // view 1 must overlay view 0 on the same backbuffer. bgfx submits views in ascending
        // id order, so view 1 renders after (on top of) view 0.
        m_device->setViewRect(1, packet.hudView.viewportX, packet.hudView.viewportY,
                               packet.hudView.viewportW, packet.hudView.viewportH);
        m_device->setViewTransform(1, packet.hudView.viewMatrix, packet.hudView.projMatrix);

        // Execute render graph with collected scene data
        m_renderGraph->execute(packet, *m_device);

        // Clear staging buffers for next frame
        m_sceneCollector->clear();
    }

    // Present frame
    m_device->frame();

    m_frameCount++;
}
void BgfxRendererModule::shutdown() {
    m_logger->info("BgfxRenderer shutting down, {} frames rendered", m_frameCount);

    if (m_renderGraph && m_device) {
        m_renderGraph->shutdown(*m_device);
    }

    if (m_resourceCache && m_device) {
        m_resourceCache->clear(*m_device);
    }

    if (m_shaderManager && m_device) {
        m_shaderManager->shutdown(*m_device);
    }

    if (m_device) {
        m_device->shutdown();
    }

    m_renderGraph.reset();
    m_resourceCache.reset();
    m_shaderManager.reset();
    m_sceneCollector.reset();
    m_frameAllocator.reset();
    m_device.reset();
}

std::unique_ptr<IDataNode> BgfxRendererModule::getState() {
    // Minimal state for hot-reload (renderer is stateless gameplay-wise)
    auto state = std::make_unique<JsonDataNode>("state");
    state->setInt("frameCount", static_cast<int>(m_frameCount));
    // GPU resources are recreated on reload
    return state;
}

void BgfxRendererModule::setState(const IDataNode& state) {
    m_frameCount = static_cast<uint64_t>(state.getInt("frameCount", 0));
    m_logger->info("State restored: frameCount={}", m_frameCount);
}

const IDataNode& BgfxRendererModule::getConfiguration() {
    if (!m_configCache) {
        m_configCache = std::make_unique<JsonDataNode>("config");
        m_configCache->setInt("windowWidth", m_width);
        m_configCache->setInt("windowHeight", m_height);
        m_configCache->setString("backend", m_backend);
        m_configCache->setString("shaderPath", m_shaderPath);
        m_configCache->setBool("vsync", m_vsync);
        m_configCache->setInt("maxSpritesPerBatch", m_maxSprites);
    }
    return *m_configCache;
}

std::unique_ptr<IDataNode> BgfxRendererModule::getHealthStatus() {
    auto health = std::make_unique<JsonDataNode>("health");
    health->setString("status", "running");
    health->setInt("frameCount", static_cast<int>(m_frameCount));
    health->setInt("allocatorUsedBytes", static_cast<int>(m_frameAllocator ? m_frameAllocator->getUsed() : 0));
    health->setInt("textureCount", static_cast<int>(m_resourceCache ? m_resourceCache->getTextureCount() : 0));
    health->setInt("shaderCount", static_cast<int>(m_resourceCache ? m_resourceCache->getShaderCount() : 0));
    return health;
}

} // namespace grove

// ============================================================================
// C Export (required for dlopen/LoadLibrary)
// Skip when building as static library to avoid multiple definition errors
// ============================================================================

#ifndef GROVE_MODULE_STATIC

#ifdef _WIN32
#define GROVE_MODULE_EXPORT __declspec(dllexport)
#else
#define GROVE_MODULE_EXPORT
#endif

extern "C" {

GROVE_MODULE_EXPORT grove::IModule* createModule() {
    return new grove::BgfxRendererModule();
}

GROVE_MODULE_EXPORT void destroyModule(grove::IModule* module) {
    delete module;
}

}

#endif // GROVE_MODULE_STATIC
