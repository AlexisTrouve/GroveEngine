#include "BgfxRendererModule.h"
#include "RHI/RHIDevice.h"
#include "Frame/FrameAllocator.h"
#include "Frame/FramePacket.h"
#include "Shaders/ShaderManager.h"
#include "RenderGraph/RenderGraph.h"
#include "Scene/SceneCollector.h"
#include "Resources/ResourceCache.h"
#include "Resources/TextureLoader.h"
#include "Assets/AssetManager.h"          // streaming texture assets (string id -> resident texture)
#include "Assets/BgfxTextureProvider.h"
#include "Assets/AtlasPacker.h"           // runtime atlas packing (asset:pack)
#include "Assets/ThreadedDecoder.h"       // phase 3: off-thread image decode (opt-in async load)
#include "Debug/DebugOverlay.h"
#include "Passes/ClearPass.h"
#include "Passes/TilemapPass.h"
#include "Passes/SpritePass.h"
#include "Passes/TextPass.h"
#include "Passes/ParticlePass.h"
#include "Passes/DebugPass.h"
#include "Passes/SectorPass.h"

#include <grove/JsonDataNode.h>
#include <grove/IIO.h>           // IIO subscribe + Message (render:tilemap:anim handler)
#include <nlohmann/json.hpp>     // parse the declarative asset manifest
#include <fstream>
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

assets::AssetManager* BgfxRendererModule::getAssetManager() const {
    return m_assetManager.get();
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
    m_tilemapPass = tilemapPass.get();   // non-owning ref (setTileset / setFogTexture / setTileAnim)
    m_renderGraph->addPass(std::move(tilemapPass));
    m_logger->info("Added TilemapPass");

    // Runtime topic: declare an ANIMATED tile (water/lava). The game publishes render:tilemap:anim
    // {tileId, frames, fps} once; the pass then cycles that tile's atlas LAYER by time (frames<=1
    // stops it). SceneCollector's "render:.*" subscription has no case for this topic (ignored there),
    // so we handle it here, where the pass pointer lives — it's pass config, like setTileset/setFog.
    if (m_io) {
        m_io->subscribe("render:tilemap:anim", [this](const Message& msg) {
            if (!msg.data || !m_tilemapPass) return;
            const IDataNode& d = *msg.data;
            m_tilemapPass->setTileAnim(static_cast<uint16_t>(d.getInt("tileId", 0)),
                                       static_cast<uint16_t>(d.getInt("frames", 0)),
                                       static_cast<float>(d.getDouble("fps", 0.0)));
        });
    }

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

    // Filled ring-sectors / pie wedges (render:sector) — same position+colour shader as debug.
    m_renderGraph->addPass(std::make_unique<SectorPass>(debugShader));
    m_logger->info("Added SectorPass");
    m_renderGraph->setup(*m_device);
    m_logger->info("RenderGraph setup complete");
    m_renderGraph->compile();
    m_logger->info("RenderGraph compiled");

    // Setup scene collector with IIO subscriptions and correct dimensions
    m_sceneCollector = std::make_unique<SceneCollector>();
    m_sceneCollector->setup(io, m_width, m_height);
    m_logger->info("SceneCollector setup complete with dimensions {}x{}", m_width, m_height);

    // Asset system: a streaming texture cache (string assetId -> resident texture, on-demand load + LRU/priority
    // eviction under a VRAM budget). The collector resolves a sprite's "asset" id through it. Budget is
    // configurable via "assetVramBudgetMB" (default 256).
    {
        const uint64_t budget = static_cast<uint64_t>(config.getInt("assetVramBudgetMB", 256)) * 1024ull * 1024ull;
        m_textureProvider = std::make_unique<assets::BgfxTextureProvider>(m_device.get(), m_resourceCache.get());
        m_assetManager = std::make_unique<assets::AssetManager>(m_textureProvider.get(), budget);
        m_sceneCollector->setAssetManager(m_assetManager.get());

        // Async load (phase 3), OPT-IN via "assetAsyncLoad" (default off = unchanged synchronous behaviour).
        // When on, resolve() decodes off-thread and returns a placeholder for a frame or two instead of
        // blocking the render thread on stb decode -> no first-touch hitch. "assetDecodeThreads" sizes the pool.
        if (config.getBool("assetAsyncLoad", false)) {
            const int threads = config.getInt("assetDecodeThreads", 1);
            m_asyncDecoder = std::make_unique<assets::ThreadedDecoder>(threads);
            m_assetManager->setAsyncDecoder(m_asyncDecoder.get());
            m_logger->info("Asset async load ON ({} decode thread(s))", threads);
        }

        // Declarative manifest at boot: config "assetManifest" = a json file { "assets":[ {id,path,priority?,
        // group?} ] }. Registers metadata only (nothing is loaded until referenced or preloaded).
        const std::string manifestPath = config.getString("assetManifest", "");
        if (!manifestPath.empty()) {
            std::ifstream f(manifestPath);
            if (!f) {
                m_logger->warn("asset manifest not found: {}", manifestPath);
            } else {
                nlohmann::json j;
                try { f >> j; } catch (...) { j = nlohmann::json(); }
                int n = 0;
                if (j.contains("assets") && j["assets"].is_array()) {
                    for (const auto& e : j["assets"]) {
                        if (!e.contains("id") || !e.contains("path")) continue;
                        m_assetManager->registerAsset(e["id"].get<std::string>(), e["path"].get<std::string>(),
                                                      e.value("priority", 0), e.value("group", std::string("")));
                        ++n;
                    }
                }
                // Atlases (phase 2): a sheet (one texture) + its sub-sprites (UV rects). The sheet is a normal
                // asset; each sub-sprite points at it. Many sub-sprites share the one resident sheet texture.
                if (j.contains("atlases") && j["atlases"].is_array()) {
                    for (const auto& at : j["atlases"]) {
                        if (!at.contains("sheet") || !at.contains("path") || !at.contains("sprites")) continue;
                        const std::string sheetId = at["sheet"].get<std::string>();
                        m_assetManager->registerAsset(sheetId, at["path"].get<std::string>(),
                                                      at.value("priority", 0), at.value("group", std::string("")));
                        ++n;
                        for (const auto& sp : at["sprites"]) {
                            if (!sp.contains("id")) continue;
                            m_assetManager->registerAtlasSprite(sp["id"].get<std::string>(), sheetId,
                                sp.value("u0", 0.0f), sp.value("v0", 0.0f), sp.value("u1", 1.0f), sp.value("v1", 1.0f),
                                sp.value("priority", 0), sp.value("group", std::string("")));
                        }
                    }
                }
                m_logger->info("asset manifest: registered {} assets/sheets from {}", n, manifestPath);
            }
        }

        // Runtime feed — the game registers / preloads / reprioritises / drops assets by data.
        if (m_io) {
            m_io->subscribe("asset:register", [this](const Message& m) {
                if (m.data) m_assetManager->registerAsset(m.data->getString("id",""), m.data->getString("path",""),
                                                          m.data->getInt("priority",0), m.data->getString("group",""));
            });
            m_io->subscribe("asset:preload", [this](const Message& m) {
                if (m.data) m_assetManager->preloadGroup(m.data->getString("group",""));
            });
            m_io->subscribe("asset:setPriority", [this](const Message& m) {
                if (m.data) m_assetManager->setPriority(m.data->getString("id",""), m.data->getInt("priority",0));
            });
            m_io->subscribe("asset:unload", [this](const Message& m) {
                if (m.data) m_assetManager->unload(m.data->getString("id",""));
            });
            // Runtime atlas packing: pack N separate PNGs into one shared sheet + register their sub-sprites.
            // asset:pack {sheet, sprites:[{id,path}], maxWidth?, gutter?, priority?, group?}.
            m_io->subscribe("asset:pack", [this](const Message& m) {
                if (!m.data || !m_device) return;
                const std::string sheet = m.data->getString("sheet", "");
                if (sheet.empty()) return;
                std::vector<assets::PackEntry> entries;
                if (IDataNode* arr = m.data->getChildReadOnly("sprites")) {
                    int i = 0;
                    while (IDataNode* e = arr->getChildReadOnly(std::to_string(i))) {
                        entries.push_back({ e->getString("id", ""), e->getString("path", "") });
                        ++i;
                    }
                }
                assets::packAtlas(*m_device, *m_resourceCache, *m_assetManager, sheet, entries,
                                  m.data->getInt("maxWidth", 2048), m.data->getInt("gutter", 2),
                                  m.data->getInt("priority", 0), m.data->getString("group", ""));
            });

            // Runtime textures / painting. The game creates a texture by a stable STRING id (registered as a
            // RESIDENT asset, so render:sprite{asset:"id"} AND the UI `asset` prop can use it) and paints
            // colored sub-rects into it via a region update (no full re-upload). Mostly EXPOSES the existing
            // RHI create/updateTexture — the asset id is the handle the game keeps.
            // render:texture:create {id, width, height, color?}  (color = 0xRRGGBBAA, default transparent).
            m_io->subscribe("render:texture:create", [this](const Message& m) {
                if (!m.data || !m_device || !m_resourceCache || !m_assetManager) return;
                const std::string id = m.data->getString("id", "");
                const int w = m.data->getInt("width", 0), h = m.data->getInt("height", 0);
                if (id.empty() || w <= 0 || h <= 0) return;
                const uint32_t color = static_cast<uint32_t>(m.data->getInt("color", 0));   // default transparent
                std::vector<uint8_t> px(static_cast<size_t>(w) * h * 4);
                for (size_t i = 0; i < static_cast<size_t>(w) * h; ++i) {
                    px[i*4+0] = (color >> 24) & 0xFF; px[i*4+1] = (color >> 16) & 0xFF;
                    px[i*4+2] = (color >> 8)  & 0xFF; px[i*4+3] =  color        & 0xFF;
                }
                rhi::TextureDesc d;
                d.width = static_cast<uint16_t>(w); d.height = static_cast<uint16_t>(h);
                d.format = rhi::TextureDesc::RGBA8; d.mipLevels = 1;
                d.filter = rhi::TextureDesc::Point; d.wrap = rhi::TextureDesc::Clamp;   // crisp canvas, no wrap
                // IMPORTANT: create EMPTY (no initial data). bgfx makes a texture created WITH data immutable,
                // and updateTexture2D on it is ignored — so a paintable canvas MUST be created empty (mutable)
                // and then filled via a region update. Same reason the tilemap index grid is created mutable.
                d.data = nullptr; d.dataSize = 0;
                rhi::TextureHandle handle = m_device->createTexture(d);
                if (!handle.isValid()) return;
                m_device->updateTexture(handle, px.data(), static_cast<uint32_t>(px.size()),
                                        0, 0, static_cast<uint16_t>(w), static_cast<uint16_t>(h));   // fill color
                if (m_assetManager->isRegistered(id)) m_assetManager->unload(id);   // replace -> free the old texture
                const uint16_t texId = m_resourceCache->registerTexture(handle);
                if (texId == 0) { m_device->destroy(handle); return; }
                m_assetManager->registerResident(id, texId, static_cast<uint64_t>(w) * h * 4);
            });
            // render:texture:paint {id, x, y, w, h, color} — fill a sub-rect (region update of the GPU texture).
            m_io->subscribe("render:texture:paint", [this](const Message& m) {
                if (!m.data || !m_device || !m_resourceCache || !m_assetManager) return;
                const std::string id = m.data->getString("id", "");
                const int x = m.data->getInt("x", 0), y = m.data->getInt("y", 0);
                const int w = m.data->getInt("w", 0), h = m.data->getInt("h", 0);
                if (id.empty() || w <= 0 || h <= 0) return;
                float u0, v0, u1, v1;
                const uint32_t texId = m_assetManager->resolveSprite(id, u0, v0, u1, v1);   // string id -> texId
                if (texId == 0) return;
                rhi::TextureHandle handle = m_resourceCache->getTextureById(static_cast<uint16_t>(texId));
                if (!handle.isValid()) return;
                const uint32_t color = static_cast<uint32_t>(m.data->getInt("color", 0));
                std::vector<uint8_t> px(static_cast<size_t>(w) * h * 4);
                for (size_t i = 0; i < static_cast<size_t>(w) * h; ++i) {
                    px[i*4+0] = (color >> 24) & 0xFF; px[i*4+1] = (color >> 16) & 0xFF;
                    px[i*4+2] = (color >> 8)  & 0xFF; px[i*4+3] =  color        & 0xFF;
                }
                m_device->updateTexture(handle, px.data(), static_cast<uint32_t>(px.size()),
                                        static_cast<uint16_t>(x), static_cast<uint16_t>(y),
                                        static_cast<uint16_t>(w), static_cast<uint16_t>(h));
            });
        }
        m_logger->info("AssetManager ready ({} MB VRAM budget)", config.getInt("assetVramBudgetMB", 256));
    }

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

    // Load the tiled fog texture if specified (Slice fog): hidden tiles show this instead of black.
    // Loaded with the default Repeat wrap, so it tiles seamlessly across the map.
    std::string fogTexturePath = config.getString("fogTexture", "");
    if (!fogTexturePath.empty() && m_tilemapPass) {
        // Load WITHOUT a public textureId (loadFromFile, not loadTextureWithId): the fog texture is
        // bound directly via setFogTexture, so it must NOT consume a sprite textureId slot (doing so
        // shifted texture1/texture2 ids and broke sprite lookups). Default wrap is Repeat -> tiles.
        TextureLoader::LoadResult fog = TextureLoader::loadFromFile(*m_device, fogTexturePath);
        if (fog.success) {
            m_tilemapPass->setFogTexture(fog.handle);
            m_logger->info("Loaded fog texture: {} ({}x{})", fogTexturePath, fog.width, fog.height);
        } else {
            m_logger->warn("Failed to load fog texture: {} ({})", fogTexturePath, fog.error);
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

        // Async load (phase 3): upload any textures that finished decoding off-thread. Done BEFORE collect so
        // a sprite resolved this frame already sees what just became resident. No-op unless async is enabled.
        if (m_assetManager) m_assetManager->pumpAsync();

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
