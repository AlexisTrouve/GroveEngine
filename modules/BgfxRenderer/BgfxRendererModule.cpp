#include "BgfxRendererModule.h"
#include "RHI/RHIDevice.h"
#include "Frame/FrameAllocator.h"
#include "Frame/FramePacket.h"
#include "Shaders/ShaderManager.h"
#include "RenderGraph/RenderGraph.h"
#include "Scene/SceneCollector.h"
#include "Resources/ResourceCache.h"
#include "Passes/ClearPass.h"
#include "Passes/SpritePass.h"
#include "Passes/DebugPass.h"

#include <grove/JsonDataNode.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

namespace grove {

BgfxRendererModule::BgfxRendererModule() = default;
BgfxRendererModule::~BgfxRendererModule() = default;

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
    void* windowHandle = reinterpret_cast<void*>(
        static_cast<uintptr_t>(config.getDouble("nativeWindowHandle", 0.0))
    );
    // Display handle (X11 Display* on Linux, 0/nullptr on Windows)
    void* displayHandle = reinterpret_cast<void*>(
        static_cast<uintptr_t>(config.getDouble("nativeDisplayHandle", 0.0))
    );

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

    if (!spriteShader.isValid()) {
        m_logger->error("Failed to load sprite shader");
        return;
    }

    // Setup render graph with passes (inject shaders via constructors)
    m_logger->info("Creating RenderGraph...");
    m_renderGraph = std::make_unique<RenderGraph>();
    m_renderGraph->addPass(std::make_unique<ClearPass>());
    m_logger->info("Added ClearPass");
    m_renderGraph->addPass(std::make_unique<SpritePass>(spriteShader));
    m_logger->info("Added SpritePass");
    m_renderGraph->addPass(std::make_unique<DebugPass>(debugShader));
    m_logger->info("Added DebugPass");
    m_renderGraph->setup(*m_device);
    m_logger->info("RenderGraph setup complete");
    m_renderGraph->compile();
    m_logger->info("RenderGraph compiled");

    // Setup scene collector with IIO subscriptions
    m_sceneCollector = std::make_unique<SceneCollector>();
    m_sceneCollector->setup(io);
    m_logger->info("SceneCollector setup complete");

    // Setup resource cache
    m_resourceCache = std::make_unique<ResourceCache>();

    m_logger->info("BgfxRenderer initialized successfully");
}

void BgfxRendererModule::process(const IDataNode& input) {
    // Read deltaTime from input (provided by ModuleSystem)
    float deltaTime = static_cast<float>(input.getDouble("deltaTime", 0.016));

    // 1. Collect IIO messages (pull-based)
    m_sceneCollector->collect(m_io, deltaTime);

    // 2. Build immutable FramePacket
    m_frameAllocator->reset();
    FramePacket frame = m_sceneCollector->finalize(*m_frameAllocator);

    // 3. Set view clear color
    m_device->setViewClear(0, frame.clearColor, 1.0f);
    m_device->setViewRect(0, 0, 0, m_width, m_height);
    m_device->setViewTransform(0, frame.mainView.viewMatrix, frame.mainView.projMatrix);

    // 4. Execute render graph
    m_renderGraph->execute(frame, *m_device);

    // 5. Present
    m_device->frame();

    // 6. Cleanup for next frame
    m_sceneCollector->clear();
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
// C Export (required for dlopen)
// ============================================================================

extern "C" {

grove::IModule* createModule() {
    return new grove::BgfxRendererModule();
}

void destroyModule(grove::IModule* module) {
    delete module;
}

}
