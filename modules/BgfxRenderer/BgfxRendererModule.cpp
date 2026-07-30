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
#include "Passes/LodColor.h"      // lod::paletteFromBytes — render:tilemap:palette payload decode
#include "Passes/SpritePass.h"
#include "Passes/TextPass.h"
#include "Passes/ParticlePass.h"
#include "Passes/DebugPass.h"
#include "Passes/SectorPass.h"
#include "Passes/CompositePass.h"
#include "Passes/BloomPass.h"
#include "Passes/PresentPass.h"
#include <grove/light/Bloom.h>
#include "Passes/LightPass.h"
#include "Passes/OcclusionPass.h"
#include "Passes/NebulaPass.h"

#include <grove/JsonDataNode.h>
#include <grove/IIO.h>           // IIO subscribe + Message (render:tilemap:anim handler)
#include <grove/text/TextMetricsWire.h>  // push the font's advances to whoever must MEASURE text
#include <nlohmann/json.hpp>     // parse the declarative asset manifest
#include <fstream>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

namespace grove {

namespace {

// QUOI : publie la table d'avances de la police courante sur `render:font:metrics`.
//
// POURQUOI : le renderer POSSÈDE la police, mais ce n'est pas lui qui a besoin de mesurer. Un champ
//   de saisie doit savoir où poser son curseur, quel morceau de texte surligner, et sur quel caractère
//   un clic est tombé — le tout dans la frame même du clic. L'UIModule est délibérément découplé du
//   renderer, donc il ne peut pas interroger la police ; et un aller-retour par topic serait
//   asynchrone sur un chemin qui doit être synchrone. On POUSSE donc la table, une fois par
//   changement de police : un seul sens, pas de requête, et le consommateur mesure localement.
//
// COMMENT : plage dense ASCII + Latin-1 (32..255) — ce que loadTTF cuit, et ce qui couvre les accents
//   français. L'encodage passe par grove::text::MetricsWire (chaîne compacte) parce qu'IIO ne
//   transporte que le JSON propre du nœud, pas les enfants assemblés par setChild().
//
//   Publiée AUSSI pour la police 8x8 intégrée : ses avances valent toutes 8, donc le consommateur
//   obtient exactement son repli monospace historique. Mesurer devient ainsi un invariant ("l'UI
//   mesure ce que le renderer dessine") plutôt qu'un cas particulier réservé aux hôtes avec TTF.
void publishFontMetrics(IIO* io, const BitmapFont& font) {
    if (!io) return;

    constexpr uint32_t kFirst = 32;   // espace
    constexpr uint32_t kLast  = 255;  // fin de Latin-1

    text::Metrics m;
    m.baseSize = font.getBaseSize();
    m.lineHeight = font.getLineHeight();
    for (uint32_t cp = kFirst; cp <= kLast; ++cp) {
        m.advances[cp] = font.getGlyph(cp).advance;
    }

    const text::MetricsWire wire = text::encodeDense(m, kFirst, kLast);
    auto payload = std::make_unique<JsonDataNode>("fontMetrics");
    payload->setDouble("baseSize", wire.baseSize);
    payload->setDouble("lineHeight", wire.lineHeight);
    payload->setInt("firstCodepoint", static_cast<int>(wire.firstCodepoint));
    payload->setString("advances", wire.advances);
    io->publish("render:font:metrics", std::move(payload));
}

}  // namespace

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

void BgfxRendererModule::submitSpriteBatch(const SpriteInstance* data, size_t count) {
    // Forward to the collector's bulk feed. Called between frames (before the next process()
    // finalize()); the instances live in m_sprites until this frame is drawn and cleared.
    if (m_sceneCollector) m_sceneCollector->addSpritesBulk(data, count);
}

void BgfxRendererModule::submitParticleBatch(const ParticleInstance* data, size_t count) {
    // Same contract as submitSpriteBatch, for particles (ParticlePass consumes them this frame).
    if (m_sceneCollector) m_sceneCollector->addParticlesBulk(data, count);
}

void BgfxRendererModule::submitTextBatch(const TextCommand* items, size_t count) {
    // Same contract as submitSpriteBatch, for text labels (the collector copies each string into the
    // frame staging; TextPass batches the glyphs this frame).
    if (m_sceneCollector) m_sceneCollector->addTextsBulk(items, count);
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
    if (!m_device->init(windowHandle, displayHandle, m_width, m_height, m_vsync)) {
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

        // Runtime topic: load a PNG tileset GRID as a texture2DArray (one tile per layer) and bind it to a
        // tileset id, so render:tilemap:add {textureId} can draw with real tile images. Handled here (the
        // pass + device live here, like render:tilemap:anim). TextureHandle is a POD id — the device owns
        // the texture and frees it on shutdown, so no per-tileset lifetime bookkeeping is needed here.
        m_io->subscribe("render:tilemap:tileset", [this](const Message& msg) {
            if (!msg.data || !m_tilemapPass || !m_device) return;
            const IDataNode& d = *msg.data;
            const std::string path = d.getString("path", "");
            const uint16_t texId = static_cast<uint16_t>(d.getInt("textureId", 0));
            const int tw = static_cast<int>(d.getInt("tileW", 16));
            const int th = static_cast<int>(d.getInt("tileH", 16));
            if (texId == 0) return;                  // 0 is reserved for the procedural colour atlas

            // Two sources, same handling downstream: a PNG on disk (`path`) or ALREADY-DECODED RGBA8
            // pixels riding beside the json (`pixels` blob + imgW/imgH). The blob spares a game that
            // GENERATES its tileset at startup from encoding a PNG and writing it to disk just so we
            // can read and decode it back. Note imgW/imgH rather than render:texture:upload's w/h:
            // tileW/tileH already live on this topic, so bare w/h would read ambiguously here.
            const auto* blob = d.getBlob("pixels");
            TextureLoader::LoadResult res;
            if (blob != nullptr && !blob->empty()) {
                // Explicit data beats an indirection: if BOTH are given the pixels win, loudly.
                if (!path.empty()) {
                    m_logger->warn("Tileset {}: both 'pixels' and 'path' given — using 'pixels', ignoring '{}'",
                                   texId, path);
                }
                const int iw = static_cast<int>(d.getInt("imgW", 0));
                const int ih = static_cast<int>(d.getInt("imgH", 0));
                res = TextureLoader::loadArrayFromPixels(*m_device, blob->data(), blob->size(), iw, ih, tw, th);
            } else if (!path.empty()) {
                res = TextureLoader::loadArrayFromFile(*m_device, path, tw, th);
            } else {
                m_logger->warn("Tileset {} ignored: neither 'path' nor a 'pixels' blob", texId);
                return;
            }
            if (res.success) {
                m_tilemapPass->setTileset(texId, res.handle);
                // Also feed the zoom-out band: each layer's average colour becomes this tileset's LOD
                // colour table, so dezoomed tiles show the tileset's own colours instead of the
                // built-in 8-colour palette. Free (computed during the decode) and needs no topic.
                m_tilemapPass->setTilesetLodColors(texId, std::move(res.layerColors));
                m_logger->info("Tileset {} <- {} ({} layers of {}x{})", texId,
                               path.empty() ? std::string("<pixels blob>") : path,
                               res.layers, res.width, res.height);
            } else {
                m_logger->warn("Tileset {} load failed from {}: {}", texId,
                               path.empty() ? std::string("<pixels blob>") : path, res.error);
            }
        });

        // Runtime topic: capture du backbuffer pour le devlog. La scene publie
        // render:screenshot {path} ; on relaie au device (qui demande la capture a
        // bgfx, ecrite au prochain frame()). Ce handler tourne pendant collect()
        // (avant le frame() de process()) -> la capture sort sur la frame courante.
        // ------------------------------------------------------------------
        // Runtime topic: l'APPARENCE du brouillard — `render:tilemap:fog:style {path?, scale?, offsetX?, offsetY?}`
        //
        // QUOI : change la texture de brouillard, sa taille MONDE, et son décalage d'échantillonnage,
        //   en cours de partie.
        //
        // POURQUOI : tout cela n'était réglable qu'au BOOT (`fogTexture`/`fogScale` dans la config),
        //   et l'échelle ne l'était pas du tout (constante `64.0` dans le shader). Un jeu ne pouvait
        //   donc ni changer de brouillard selon le biome ou la scène, ni le faire dériver. Les
        //   polices avaient déjà reçu ce traitement (`render:font`) ; le brouillard le méritait pour
        //   la même raison : un asset chargé une fois pour toutes n'est pas un asset, c'est une
        //   constante.
        //
        // COMMENT : chaque champ est OPTIONNEL et ne s'applique que s'il est fourni — publier
        //   `{offsetX, offsetY}` chaque frame fait dériver le nuage sans retoucher la texture ni
        //   l'échelle. ⚠️ Seul le NUAGE bouge : le masque de révélation (`render:tilemap:fog`) n'est
        //   pas touché, donc une dérive ne peut pas re-cacher ce que le joueur a exploré.
        //   Un chargement raté conserve la texture courante (échec franc + log, jamais de brouillard
        //   noir surprise).
        m_io->subscribe("render:tilemap:fog:style", [this](const Message& msg) {
            if (!msg.data || !m_tilemapPass || !m_device) return;
            const IDataNode& d = *msg.data;

            const std::string path = d.getString("path", "");
            if (!path.empty()) {
                // Même chemin que le boot : loadFromFile (PAS loadTextureWithId) — la texture de
                // brouillard est liée directement par setFogTexture et ne doit pas consommer un
                // textureId de sprite, sous peine de décaler texture1/texture2.
                TextureLoader::LoadResult fog = TextureLoader::loadFromFile(*m_device, path);
                if (fog.success) {
                    m_tilemapPass->setFogTexture(fog.handle);
                    m_logger->info("render:tilemap:fog:style — texture '{}' ({}x{})", path, fog.width, fog.height);
                } else {
                    m_logger->warn("render:tilemap:fog:style — '{}' non chargee ({}), on garde l'actuelle",
                                   path, fog.error);
                }
            }

            const double scale = d.getDouble("scale", 0.0);
            if (scale > 0.0) m_tilemapPass->setFogScale(static_cast<float>(scale));

            // `edge` accepte 0 (= remettre un bord droit), on relit donc la valeur courante comme
            // défaut plutôt que de traiter 0 comme « absent ».
            const double edge = d.getDouble("edge", static_cast<double>(m_tilemapPass->fogEdge()));
            m_tilemapPass->setFogEdge(static_cast<float>(edge));

            // Le décalage n'a pas de sentinelle « absent » utilisable (0 est une valeur légitime), on
            // relit donc la valeur courante comme défaut : publier l'un sans l'autre est sans effet
            // de bord.
            const double offX = d.getDouble("offsetX", static_cast<double>(m_tilemapPass->fogOffsetX()));
            const double offY = d.getDouble("offsetY", static_cast<double>(m_tilemapPass->fogOffsetY()));
            m_tilemapPass->setFogOffset(static_cast<float>(offX), static_cast<float>(offY));
        });

        // SceneCollector ignore ce topic (pas une primitive) ; on le traite ici, ou
        // vit le device -- comme render:tilemap:anim.
        // Runtime topic: an EXPLICIT zoom-out colour table pushed by the game, overriding the one
        // derived from the tileset. For a game whose tile colours are pure data with no art to
        // average (the tileset is the normal path — this is the escape hatch). `colors` is a raw
        // RGBA8 blob, 4 bytes per entry, entry i = tile id i+1 (the atlas layer convention).
        // textureId defaults to 0 = the procedural-atlas path. Publishing nothing changes nothing.
        m_io->subscribe("render:tilemap:palette", [this](const Message& msg) {
            if (!msg.data || !m_tilemapPass) return;
            const IDataNode& d = *msg.data;
            const auto* blob = d.getBlob("colors");
            if (!blob || blob->empty()) {
                m_logger->warn("render:tilemap:palette ignored: no 'colors' blob");
                return;
            }
            std::vector<uint32_t> table = lod::paletteFromBytes(blob->data(), blob->size());
            if (table.empty()) {
                m_logger->warn("render:tilemap:palette ignored: 'colors' blob too short ({} bytes)",
                               blob->size());
                return;
            }
            const uint16_t texId = static_cast<uint16_t>(d.getInt("textureId", 0));
            m_logger->info("LOD palette for tileset {} <- {} colours", texId, table.size());
            m_tilemapPass->setLodPalette(texId, std::move(table));
        });

        // Runtime topic: swap the glyph atlas for a REAL TrueType face.
        //   render:font {path, size?}
        // The built-in 8x8 bitmap stays the default, so publishing nothing changes nothing. No font is
        // vendored with the engine: shipping a face is a content/licensing decision for the game, the
        // same posture as the audio stems and the 9-slice art. A failed load keeps the current font.
        m_io->subscribe("render:font", [this](const Message& msg) {
            if (!msg.data || !m_textPass || !m_device) return;
            const std::string path = msg.data->getString("path", "");
            if (path.empty()) { m_logger->warn("render:font ignored: no 'path'"); return; }
            const float size = static_cast<float>(msg.data->getDouble("size", 32.0));
            // `bold: true` targets the BOLD face; anything else the regular one.
            const bool asBold = msg.data->getBool("bold", false);
            BitmapFont& target = asBold ? m_textPass->getFontBold() : m_textPass->getFont();
            if (!target.loadTTF(*m_device, path, size)) {
                m_logger->warn("render:font: '{}' not loaded — keeping the current font", path);
                return;
            }
            // La police a changé : rediffuser ses avances, sinon tout consommateur qui MESURE du texte
            // (curseur d'un champ de saisie, surlignage de sélection) continuerait de mesurer avec
            // l'ancienne — le curseur dériverait du texte réellement dessiné. On ne pousse que la
            // face REGULAR : le gras est une seconde table, à traiter le jour où l'UI en aura besoin.
            if (!asBold) publishFontMetrics(m_io, m_textPass->getFont());
        });

        m_io->subscribe("render:screenshot", [this](const Message& msg) {
            if (!msg.data || !m_device) return;
            const std::string path = msg.data->getString("path", "");
            if (!path.empty()) m_device->requestScreenShot(path);
        });
    }

    // Create SpritePass and keep reference for texture binding
    auto spritePass = std::make_unique<SpritePass>(spriteShader);
    m_spritePass = spritePass.get();  // Non-owning reference
    m_spritePass->setResourceCache(m_resourceCache.get());
    m_renderGraph->addPass(std::move(spritePass));
    m_logger->info("Added SpritePass");

    // Create TextPass (uses sprite shader for glyph quads)
    auto textPass = std::make_unique<TextPass>(spriteShader);
    m_textPass = textPass.get();   // non-owning ref, so render:font can rebake the glyph atlas
    m_renderGraph->addPass(std::move(textPass));
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

    // Lighting composite (L1). Registered unconditionally: it costs nothing per frame until a game
    // publishes render:ambient (the pass returns immediately), and building it lazily would mean
    // creating GPU resources in the middle of the first lit frame. We keep a raw pointer so the
    // module can hand it the offscreen textures each frame; the graph owns the pass.
    // Occluders -> the occlusion map the light march samples. Reuses the "color" program: they are
    // flat black quads, exactly what DebugPass and SectorPass already draw with it.
    m_renderGraph->addPass(std::make_unique<OcclusionPass>(debugShader));
    m_logger->info("Added OcclusionPass");

    // Soft radial media, into the SAME map and with the same multiplicative blend. Its own pass
    // because it needs its own shader and one draw per volume, where OcclusionPass batches flat
    // quads into one buffer.
    m_renderGraph->addPass(std::make_unique<NebulaPass>(m_shaderManager->getProgram("nebula")));
    m_logger->info("Added NebulaPass");

    {
        // The occlusion map the light march samples. WHITE = vacuum, so with nothing writing into
        // it the march multiplies by 1 and the render is unchanged — that neutrality IS the proof
        // this slice ships. 1x1 + clamp is enough while nothing draws occluders.
        rhi::TextureDesc od;
        od.width = 1; od.height = 1; od.format = rhi::TextureDesc::RGBA8;
        const uint8_t whitePixel[4] = { 255, 255, 255, 255 };
        od.data = whitePixel;
        od.dataSize = sizeof(whitePixel);
        m_occlusionTex = m_device->createTexture(od);

        // ...et son PENDANT pour l'accumulation de lumière : BLACK = aucune lumière ajoutée.
        //
        // ⚠️ Même piège, même remède, et il a fallu une capture pour le voir. Une vue qui ne reçoit
        //    AUCUN draw est sautée par bgfx, et une vue sautée n'exécute jamais son effacement. Une
        //    frame qui ne publie aucune lampe laissait donc la cible d'accumulation garder le contenu
        //    de la dernière frame qui en avait — et le composite l'ajoutait à l'ambiant. Symptôme : un
        //    fantôme de lumière FIGÉ, qu'un jeu chercherait dans son propre code.
        //
        //    C'est un état parfaitement légitime : toutes les lampes cullées hors écran, une
        //    transition de scène, un interrupteur coupé. Personne ne l'avait vu parce que tous les
        //    tests et toutes les planches publiaient au moins une lampe par frame.
        //
        //    Comme pour l'occultation, on ne CONSULTE pas la cible quand personne n'a rien écrit,
        //    plutôt que de compter sur une sémantique d'effacement-au-toucher. Verrouillé par
        //    LightingGpu [stale].
        rhi::TextureDesc bd;
        bd.width = 1; bd.height = 1; bd.format = rhi::TextureDesc::RGBA8;
        const uint8_t blackPixel[4] = { 0, 0, 0, 255 };
        bd.data = blackPixel;
        bd.dataSize = sizeof(blackPixel);
        m_blackLightTex = m_device->createTexture(bd);

        auto lightPass = std::make_unique<LightPass>(m_shaderManager->getProgram("light"));
        lightPass->setOcclusionTexture(m_occlusionTex);
        m_lightPass = lightPass.get();
        m_renderGraph->addPass(std::move(lightPass));
        m_logger->info("Added LightPass");
    }

    {
        auto compositePass = std::make_unique<CompositePass>(m_shaderManager->getProgram("composite"));
        m_compositePass = compositePass.get();
        m_renderGraph->addPass(std::move(compositePass));
        m_logger->info("Added CompositePass");
    }

    {
        // Post-traitement (plan B). Les deux passes sont enregistrées inconditionnellement et sortent
        // immédiatement quand `bloom.intensity == 0` — comme CompositePass avec l'ambiant. Enregistrer
        // conditionnellement obligerait à reconstruire le graphe quand un jeu allume le bloom en cours
        // de partie, ce qui est exactement le genre de mutation qu'un graphe topologique n'aime pas.
        auto bloomPass = std::make_unique<BloomPass>(m_shaderManager->getProgram("bloom_extract"),
                                                    m_shaderManager->getProgram("bloom_blur"));
        m_bloomPass = bloomPass.get();
        m_renderGraph->addPass(std::move(bloomPass));

        auto presentPass = std::make_unique<PresentPass>(m_shaderManager->getProgram("present"));
        m_presentPass = presentPass.get();
        m_renderGraph->addPass(std::move(presentPass));
        m_logger->info("Added BloomPass + PresentPass");
    }

    m_renderGraph->setup(*m_device);
    m_logger->info("RenderGraph setup complete");

    // REAL FONT (optional). TextPass::setup() just installed the built-in 8x8 bitmap fallback; if the
    // host asked for a TrueType face, bake it over the top NOW — after setup, or initDefault would
    // overwrite it. Absent `fontPath`, the 8x8 stays: no existing host changes behaviour.
    // The engine ships assets/fonts/roboto-regular.ttf (Apache 2.0) as a sane starting point; a game
    // points this at its own face instead. Same loader as the runtime `render:font` topic.
    const std::string fontPath = config.getString("fontPath", "");
    if (!fontPath.empty() && m_textPass) {
        const float fontBakeSize = static_cast<float>(config.getDouble("fontSize", 32.0));
        if (m_textPass->getFont().loadTTF(*m_device, fontPath, fontBakeSize)) {
            m_logger->info("Font: baked '{}' at {}px", fontPath, fontBakeSize);
        } else {
            m_logger->warn("Font: '{}' not loaded — keeping the built-in 8x8 bitmap", fontPath);
        }
        // Optional REAL BOLD face. Without it, bold text stays the synthetic double-draw — legible,
        // but a smear rather than a weight. With it, bold gets its own glyphs AND its own (wider)
        // advances, which is what actually distinguishes a real weight from a fattened regular.
        const std::string fontPathBold = config.getString("fontPathBold", "");
        if (!fontPathBold.empty()) {
            if (m_textPass->getFontBold().loadTTF(*m_device, fontPathBold, fontBakeSize)) {
                m_logger->info("Font (bold): baked '{}' at {}px", fontPathBold, fontBakeSize);
            } else {
                m_logger->warn("Font (bold): '{}' not loaded — bold stays synthetic", fontPathBold);
            }
        }
    }

    // Diffuser les avances de la police REGULAR, quelle qu'elle soit (TTF fraîchement cuite ou 8x8
    // intégrée). C'est ce qui permet à l'UI de placer un curseur là où le glyphe est réellement
    // dessiné. Avec la 8x8, toutes les avances valent 8 : le consommateur retrouve exactement son
    // repli monospace, donc aucun hôte existant ne change de comportement.
    if (m_textPass) publishFontMetrics(m_io, m_textPass->getFont());

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
                // Read the sprites array straight off the payload json (const). We deliberately do
                // NOT use getChildReadOnly here: it lazily MATERIALIZES child nodes (mutates the
                // node), which on a shared, const, zero-copy payload would be both ill-formed and a
                // data race across subscribers. The raw const json read is mutation-free + thread-safe.
                if (auto* jn = dynamic_cast<const JsonDataNode*>(m.data.get())) {
                    const auto& j = jn->getJsonData();
                    auto it = j.find("sprites");
                    if (it != j.end() && it->is_array()) {
                        for (const auto& e : *it) {
                            entries.push_back({ e.value("id", std::string{}), e.value("path", std::string{}) });
                        }
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
            // render:texture:upload {id, w, h, +blob "pixels"} — replace the WHOLE texture's pixels from a
            // raw RGBA8 blob (the arbitrary-pixel upload path — video frames, procedural images). Same GPU
            // region-update as paint, but full-texture with real pixels instead of a solid colour. The blob
            // rides beside the json (setBlob/getBlob, zero-copy in-process) — no base64/UTF-8 abuse.
            m_io->subscribe("render:texture:upload", [this](const Message& m) {
                if (!m.data || !m_device || !m_resourceCache || !m_assetManager) return;
                const std::string id = m.data->getString("id", "");
                const int w = m.data->getInt("w", 0), h = m.data->getInt("h", 0);
                if (id.empty() || w <= 0 || h <= 0) return;
                const auto* blob = m.data->getBlob("pixels");
                const size_t need = static_cast<size_t>(w) * static_cast<size_t>(h) * 4u;   // RGBA8
                if (!blob || blob->size() < need) return;
                float u0, v0, u1, v1;
                const uint32_t texId = m_assetManager->resolveSprite(id, u0, v0, u1, v1);   // string id -> texId
                if (texId == 0) return;
                rhi::TextureHandle handle = m_resourceCache->getTextureById(static_cast<uint16_t>(texId));
                if (!handle.isValid()) return;
                m_device->updateTexture(handle, blob->data(), static_cast<uint32_t>(need),
                                        0, 0, static_cast<uint16_t>(w), static_cast<uint16_t>(h));
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

    // Échelle MONDE du brouillard : combien d'unités monde couvre une tuile de la texture. Elle était
    // écrite en dur dans le shader (`/64.0`), donc « mettre un asset plus grand » ne voulait rien dire.
    // Le défaut vaut cette constante historique → un hôte qui ne configure rien rend à l'identique.
    if (m_tilemapPass) {
        const double fogScale = config.getDouble("fogScale", 0.0);
        if (fogScale > 0.0) {
            m_tilemapPass->setFogScale(static_cast<float>(fogScale));
            m_logger->info("Fog scale: {} world units per fog tile", fogScale);
        }
        // Ondulation du bord, en tuiles. 0 (défaut) = bord droit = rendu historique.
        const double fogEdge = config.getDouble("fogEdge", 0.0);
        if (fogEdge > 0.0) {
            m_tilemapPass->setFogEdge(static_cast<float>(fogEdge));
            m_logger->info("Fog edge: {} tiles of wobble", fogEdge);
        }
    }

    // Load additional textures (texture1, texture2, etc.)
    // NOTE (Drifterra tile system, 2026-07-30) : releve de 10 -> 128. Un ship rendu par TUILES peut referer beaucoup
    //   d'atomes (module/composant) -> >10 textures ; a 10 les ids au-dela ne chargeaient pas -> sprites BLANCS. Les
    //   cles absentes sont ignorees (getString defaut ""), donc sans surcout pour les scenes a peu de textures.
    for (int i = 1; i <= 128; ++i) {
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

        // ---- Lighting (L1) --------------------------------------------------------------------
        // ambientColor == 0 means NO game asked for lighting. Everything below is then skipped:
        // no targets are built, view 0 keeps drawing to the backbuffer, no submission order is
        // imposed, and CompositePass returns immediately. That is the whole zero-cost guarantee —
        // Drifterra, DAOS and Fractax must keep paying exactly what they paid before.
        const bool lightingActive = (packet.ambientColor != 0);
        if (lightingActive) {
            ensureLightingTargets(packet.mainView.viewportW, packet.mainView.viewportH);

            // The world now renders into the scene target instead of the backbuffer, and the light
            // accumulation gets its own view + target (cleared to BLACK: it is a sum, so it must
            // start at "no light", not at the scene's clear colour).
            m_device->setViewFramebuffer(0, m_sceneFB);
            m_device->setViewFramebuffer(CompositePass::kLightView, m_lightFB);
            m_device->setViewClear(CompositePass::kLightView, 0x000000FF, 1.0f);
            m_device->setViewRect(CompositePass::kLightView, 0, 0, m_lightingWidth, m_lightingHeight);
            // SAME camera as the world view, and this is load-bearing: lights are published in WORLD
            // coordinates (render:light {cx,cy}), and the composite samples the two targets pixel for
            // pixel. If this view carried any other transform, a lamp at a sprite's position would
            // land somewhere else on screen — and the error would scale with zoom and pan, so it
            // would look like a physics or camera bug rather than a missing matrix.
            m_device->setViewTransform(CompositePass::kLightView,
                                       packet.mainView.viewMatrix, packet.mainView.projMatrix);

            // The composite draws to the BACKBUFFER (no framebuffer bound) over the full viewport.
            m_device->setViewRect(CompositePass::kCompositeView, 0, 0, m_lightingWidth, m_lightingHeight);

            // Submission order, the reason setViewOrder had to exist: world -> lights -> composite
            // -> HUD. Ascending ids would put the HUD (1) before the composite (3) and the HUD would
            // be overwritten by the composited frame.
            // Occlusion map — ONLY when something actually occludes.
            //
            // ⚠️ A view with no draw is SKIPPED by bgfx, and a skipped view never runs its clear.
            // Binding the occlusion target on a frame with no occluders would therefore hand the
            // light march a target holding whatever was in it last — and the march would multiply
            // by that garbage instead of by white. Measured: every lit pixel collapsed to the
            // ambient. The frame LOOKED like a lighting bug, not like a missing clear.
            //
            // So an empty frame keeps the 1x1 white placeholder: the map is not merely cleared to
            // vacuum, it is not consulted at all. That removes the dependency on clear-on-touch
            // semantics rather than working around it.
            // Filters write into the SAME map as walls (F1), so either one alone is reason enough
            // to bind and clear the target. Testing only for occluders would leave a scene whose
            // matter is entirely stained glass sampling the 1x1 white placeholder — no tint at all.
            const bool hasOccluders = (packet.occluders != nullptr && packet.occluderCount > 0)
                                   || (packet.filters   != nullptr && packet.filterCount   > 0)
                                   || (packet.fogs      != nullptr && packet.fogCount      > 0)
                                   || (packet.nebulae   != nullptr && packet.nebulaCount   > 0);
            if (hasOccluders) {
                // Drawn with the WORLD camera, because occluders are published in world coordinates
                // — the same reason the light view carries it.
                m_device->setViewFramebuffer(OcclusionPass::kOcclusionView, m_occlusionFB);
                m_device->setViewClear(OcclusionPass::kOcclusionView, 0xFFFFFFFF, 1.0f);
                m_device->setViewRect(OcclusionPass::kOcclusionView, 0, 0, m_lightingWidth, m_lightingHeight);
                m_device->setViewTransform(OcclusionPass::kOcclusionView,
                                           packet.mainView.viewMatrix, packet.mainView.projMatrix);
            }

            // ---- Post-traitement / bloom (plan B) --------------------------------------------
            // Le bloom EXIGE l'éclairage, et c'est structurel : sa source est la frame COMPOSÉE, or
            // sans éclairage il n'y a pas de composite (la scène va au backbuffer, qui ne
            // s'échantillonne pas). D'où la place de ce bloc, à l'intérieur de `lightingActive`.
            // DEUX réglages indépendants activent cette chaîne, et ils ne demandent pas la même
            // chose : le bloom a besoin des cibles de flou, le tonemapping seulement de la cible HDR
            // et de la passe de présentation. Les traiter en bloc ferait payer deux cibles réduites à
            // un jeu qui ne veut qu'une courbe d'exposition.
            const bool bloomActive   = (packet.bloom.intensity > 0.0f);
            const bool tonemapActive = (packet.tonemap.mode != light::TonemapMode::None);
            const bool postActive    = bloomActive || tonemapActive;

            if (postActive) {
                ensureHdrTarget(m_lightingWidth, m_lightingHeight);

                // LE changement de forme du pipeline : le composite n'écrit plus au backbuffer mais
                // dans une cible HDR, que l'extraction du bloom et la présentation échantillonnent.
                m_device->setViewFramebuffer(CompositePass::kCompositeView, m_hdrFB);

                // La présentation va au BACKBUFFER (aucun framebuffer attaché), plein viewport.
                m_device->setViewRect(PresentPass::kPresentView, 0, 0, m_lightingWidth, m_lightingHeight);

                // La lueur à ajouter : la cible de flou si le bloom tourne, sinon le placeholder 1x1
                // NOIR — celui-là même que le correctif de la lampe fantôme a introduit. Ajouter zéro
                // est un no-op, et on ne consulte pas une cible que personne n'a écrite.
                rhi::TextureHandle bloomTex = m_blackLightTex;

                if (bloomActive) {
                    // Le facteur de réduction suit le RAYON demandé (tranche B4). La règle vit dans
                    // grove::light pour être testable au CPU : les 9 taps tombent aux mêmes positions
                    // écran quel que soit le facteur, mais l'EMPREINTE d'un tap vaut un texel — trop
                    // petite, elle laisse des trous entre les taps, et ces trous sont un feston.
                    const int bloomDown = light::bloomDownsample(packet.bloom.radius);
                    ensureBloomTargets(m_lightingWidth, m_lightingHeight, bloomDown);

                    // Pas de setViewClear : chaque étape REMPLACE toute sa cible (blend None sur un
                    // quad plein écran), donc un effacement serait un remplissage payé pour rien.
                    // ⚠️ Ça vaut UNIQUEMENT parce que la couverture est totale — la remarque inverse
                    // de la carte d'occultation, dont la vue est sautée quand personne ne dessine et
                    // qui a donc BESOIN de son effacement.
                    m_device->setViewFramebuffer(BloomPass::kExtractView, m_bloomFB[0]);
                    m_device->setViewRect(BloomPass::kExtractView, 0, 0, m_bloomSmallW, m_bloomSmallH);
                    m_device->setViewFramebuffer(BloomPass::kBlurHView, m_bloomFB[1]);
                    m_device->setViewRect(BloomPass::kBlurHView, 0, 0, m_bloomSmallW, m_bloomSmallH);
                    m_device->setViewFramebuffer(BloomPass::kBlurVView, m_bloomFB[0]);
                    m_device->setViewRect(BloomPass::kBlurVView, 0, 0, m_bloomSmallW, m_bloomSmallH);

                    if (m_bloomPass) {
                        m_bloomPass->setTargets(m_device->getFramebufferTexture(m_hdrFB),
                                                m_device->getFramebufferTexture(m_bloomFB[0]),
                                                m_device->getFramebufferTexture(m_bloomFB[1]));
                        m_bloomPass->setSizes(m_lightingWidth, m_lightingHeight,
                                              m_bloomSmallW, m_bloomSmallH, m_bloomDownsample);
                    }
                    // La cible A : le ping-pong est agencé pour que le flou vertical y termine.
                    bloomTex = m_device->getFramebufferTexture(m_bloomFB[0]);
                } else if (m_bloomWidth != 0) {
                    // Le bloom seul s'est éteint, le tonemapping reste : on rend les cibles de flou
                    // sans toucher à la cible HDR, que la présentation utilise encore.
                    releaseBloomTargets();
                }

                if (m_presentPass) {
                    m_presentPass->setTargets(m_device->getFramebufferTexture(m_hdrFB), bloomTex);
                }
            } else if (m_hdrWidth != 0) {
                // Tout le post-traitement vient d'être ÉTEINT (il était allumé). Il faut rendre la vue
                // du composite au backbuffer AVANT de détruire la cible HDR, sinon la vue reste
                // attachée à un framebuffer mort.
                //
                // ⚠️ C'est précisément ce chemin qui a exposé le défaut de `setViewFramebuffer` : sur
                //    un handle invalide, il ne détachait RIEN. Corrigé côté RHI, verrouillé par
                //    RhiReadbackGpu [unbind]. Sans ce correctif, éteindre le post-traitement laissait
                //    la frame composée dans une cible que plus personne ne présentait — un écran noir.
                m_device->setViewFramebuffer(CompositePass::kCompositeView, rhi::FramebufferHandle{});
                releaseBloomTargets();
                releaseHdrTarget();
            }

            // Submission order. The occlusion map must be FILLED before the lights march through it
            // - with ascending ids it would be written after being read, and the shadows would lag
            // one frame behind the walls that cast them.
            //
            // Avec le bloom, quatre vues s'insèrent ENTRE le composite et le HUD, et l'ordre relatif
            // porte tout : extraction et flous lisent ce que le composite vient d'écrire, la
            // présentation lit le résultat des flous, et le HUD passe EN DERNIER — donc il ne brille
            // pas et n'est pas écrasé par la frame présentée. Interface nette au-dessus d'un monde qui
            // éblouit : un choix, pas un oubli.
            if (bloomActive) {
                const rhi::ViewId order[] = { OcclusionPass::kOcclusionView, 0,
                                              CompositePass::kLightView, CompositePass::kCompositeView,
                                              BloomPass::kExtractView, BloomPass::kBlurHView,
                                              BloomPass::kBlurVView, PresentPass::kPresentView, 1 };
                m_device->setViewOrder(order, 9);
            } else if (postActive) {
                // Tonemapping SANS bloom : les trois vues de flou n'ont rien à faire dans l'ordre,
                // leur passe sortant immédiatement. La présentation, elle, reste indispensable —
                // c'est elle qui amène la cible HDR à l'écran.
                const rhi::ViewId order[] = { OcclusionPass::kOcclusionView, 0,
                                              CompositePass::kLightView, CompositePass::kCompositeView,
                                              PresentPass::kPresentView, 1 };
                m_device->setViewOrder(order, 6);
            } else {
                const rhi::ViewId order[] = { OcclusionPass::kOcclusionView, 0,
                                              CompositePass::kLightView, CompositePass::kCompositeView, 1 };
                m_device->setViewOrder(order, 5);
            }

            // ONE texture, TWO readers: the march samples its RGB (transmittance), the composite
            // samples its ALPHA (scattering). Resolved once here so the two can never disagree about
            // which map they are looking at — and so the no-matter case hands BOTH the white
            // placeholder, which reads as vacuum and as zero scattering.
            const rhi::TextureHandle occlusionTex =
                hasOccluders ? m_device->getFramebufferTexture(m_occlusionFB) : m_occlusionTex;

            // MÊME raisonnement pour l'accumulation de lumière, et le même piège : une frame sans
            // aucune lampe ne fait dessiner personne dans cette vue, bgfx la SAUTE, son effacement ne
            // tourne pas — et la cible rejouerait la dernière frame éclairée. On sert donc le
            // placeholder 1×1 NOIR au lieu de consulter une cible que personne n'a écrite.
            //
            // ⚠️ Trouvé sur une CAPTURE (une planche sans lampe montrait le halo de la planche
            //    précédente), pas en relisant le code — et le défaut est antérieur au bloom.
            //    Verrouillé par LightingGpu [stale].
            const bool hasLights = (packet.lights != nullptr && packet.lightCount > 0);
            const rhi::TextureHandle lightTex =
                hasLights ? m_device->getFramebufferTexture(m_lightFB) : m_blackLightTex;
            if (m_compositePass) {
                m_compositePass->setTargets(m_device->getFramebufferTexture(m_sceneFB),
                                            lightTex,
                                            occlusionTex);
            }
            if (m_lightPass) {
                m_lightPass->setOcclusionTexture(occlusionTex);
            }
        } else if (m_lightingWidth != 0) {
            // Lighting was on and has just been turned off: give the targets back and restore the
            // default ascending-id submission order, or view 0 would stay bound to a dead target.
            m_device->setViewFramebuffer(0, rhi::FramebufferHandle{});
            // Idem pour la vue du composite si le bloom l'avait redirigée vers la cible HDR : éteindre
            // l'éclairage éteint le bloom avec lui (il en dépend), et releaseLightingTargets ci-dessous
            // détruit les deux familles de cibles.
            m_device->setViewFramebuffer(CompositePass::kCompositeView, rhi::FramebufferHandle{});
            m_device->setViewOrder(nullptr, 0);
            // Hand the 1x1 white placeholder back BEFORE the targets go: the pass would otherwise
            // hold a texture from a destroyed framebuffer until lighting is switched on again.
            if (m_lightPass) m_lightPass->setOcclusionTexture(m_occlusionTex);
            releaseLightingTargets();
        }

        // Execute render graph with collected scene data
        m_renderGraph->execute(packet, *m_device);

        // Clear staging buffers for next frame
        m_sceneCollector->clear();
    }

    // Present frame
    m_device->frame();

    m_frameCount++;
}
void BgfxRendererModule::releaseBloomTargets() {
    if (!m_device) return;
    if (m_bloomWidth != 0) {
        // ⚠️ La cible HDR n'est PAS libérée ici : elle appartient au post-traitement en général, pas au
        //    bloom. Le tonemapping seul continue de s'en servir quand le bloom s'éteint.
        m_device->destroy(m_bloomFB[0]);
        m_device->destroy(m_bloomFB[1]);
        m_bloomFB[0] = rhi::FramebufferHandle{};
        m_bloomFB[1] = rhi::FramebufferHandle{};
        m_bloomWidth = 0;
        m_bloomHeight = 0;
        m_bloomSmallW = 0;
        m_bloomSmallH = 0;
        m_bloomDownsample = 0;
    }
}

void BgfxRendererModule::ensureBloomTargets(uint16_t width, uint16_t height, int downsample) {
    if (width == 0 || height == 0 || downsample <= 0) return;
    // Le facteur fait partie de l'identité des cibles : un rayon qui change de palier change la TAILLE
    // des cibles de flou, donc il faut les rebâtir. Trois paliers seulement (4/8/16), et le rayon est un
    // réglage persistant — un jeu qui rampe son rayon pour un fondu paie au pire deux reconstructions
    // sur toute la course, pas une par frame. C'est la raison d'être des paliers.
    if (m_bloomWidth == width && m_bloomHeight == height && m_bloomDownsample == downsample) return;

    releaseBloomTargets();

    // La cible de flou, réduite du facteur demandé, avec un plancher à 1 : un viewport minuscule
    // donnerait 0, donc une cible de dimension nulle et une division par zéro dans la conversion
    // pixels -> UV.
    const int sw = width / downsample;
    const int sh = height / downsample;
    m_bloomSmallW = static_cast<uint16_t>(sw > 0 ? sw : 1);
    m_bloomSmallH = static_cast<uint16_t>(sh > 0 ? sh : 1);
    m_bloomFB[0] = m_device->createFramebuffer(m_bloomSmallW, m_bloomSmallH, rhi::TargetFormat::RGBA16F);
    m_bloomFB[1] = m_device->createFramebuffer(m_bloomSmallW, m_bloomSmallH, rhi::TargetFormat::RGBA16F);

    m_bloomWidth = width;
    m_bloomHeight = height;
    m_bloomDownsample = downsample;

    m_logger->info("Bloom blur targets built (2x {}x{} RGBA16F, 1/{})",
                   m_bloomSmallW, m_bloomSmallH, downsample);
}

void BgfxRendererModule::releaseHdrTarget() {
    if (!m_device) return;
    if (m_hdrWidth != 0) {
        m_device->destroy(m_hdrFB);
        m_hdrFB = rhi::FramebufferHandle{};
        m_hdrWidth = 0;
        m_hdrHeight = 0;
    }
}

void BgfxRendererModule::ensureHdrTarget(uint16_t width, uint16_t height) {
    if (width == 0 || height == 0) return;
    if (m_hdrWidth == width && m_hdrHeight == height) return;

    releaseHdrTarget();

    // RGBA16F comme les cibles d'éclairage, et pour la même raison : tout le post-traitement travaille
    // sur ce qui DÉPASSE 1. Écrêter ici en RGBA8 rendrait un seuil de bloom au-dessus de 1
    // inatteignable ET priverait le tonemapping de la plage dynamique qu'il existe pour comprimer —
    // deux effets réduits à néant par un choix de format.
    m_hdrFB = m_device->createFramebuffer(width, height, rhi::TargetFormat::RGBA16F);
    m_hdrWidth = width;
    m_hdrHeight = height;

    m_logger->info("HDR present target built ({}x{} RGBA16F)", width, height);
}

void BgfxRendererModule::releaseLightingTargets() {
    if (!m_device) return;
    // Les cibles bloom sont FILLES de celles-ci : toutes sont dimensionnées à l'écran, donc un
    // changement de taille les invalide ensemble. Les libérer ici évite qu'un redimensionnement laisse
    // une cible HDR à l'ancienne taille échantillonnée par une présentation à la nouvelle.
    releaseBloomTargets();
    releaseHdrTarget();
    if (m_lightingWidth != 0) {
        m_device->destroy(m_sceneFB);
        m_device->destroy(m_lightFB);
        m_device->destroy(m_occlusionFB);
        m_sceneFB = rhi::FramebufferHandle{};
        m_lightFB = rhi::FramebufferHandle{};
        m_occlusionFB = rhi::FramebufferHandle{};
        m_lightingWidth = 0;
        m_lightingHeight = 0;
    }
}

void BgfxRendererModule::ensureLightingTargets(uint16_t width, uint16_t height) {
    if (width == 0 || height == 0) return;                       // degenerate viewport: nothing to build
    if (m_lightingWidth == width && m_lightingHeight == height) return;   // already the right size

    // A resize invalidates both targets: they are screen-sized by definition, and sampling a stale
    // one would composite last size's picture. Rebuild rather than scale.
    releaseLightingTargets();

    // BOTH targets are RGBA16F. The light buffer needs it (additive lamps overshoot 1.0 and the
    // bloom pass will feed on that overbright); the scene target matches so the composite reads two
    // textures of one format and post-processing later inherits an HDR scene instead of an LDR one
    // that already threw its highlights away.
    m_sceneFB = m_device->createFramebuffer(width, height, rhi::TargetFormat::RGBA16F);
    m_lightFB = m_device->createFramebuffer(width, height, rhi::TargetFormat::RGBA16F);
    // The occlusion map stores TRANSMITTANCE, which lives in 0..1 by definition - RGBA8 is exact
    // enough and half the bandwidth of the two HDR targets beside it.
    m_occlusionFB = m_device->createFramebuffer(width, height, rhi::TargetFormat::RGBA8);
    m_lightingWidth = width;
    m_lightingHeight = height;

    m_logger->info("Lighting targets built ({}x{}, RGBA16F)", width, height);
}

void BgfxRendererModule::shutdown() {
    m_logger->info("BgfxRenderer shutting down, {} frames rendered", m_frameCount);

    // Lighting targets first: they are plain device resources, and the graph's shutdown destroys the
    // pass that borrows their textures. Releasing them after would leave the pass holding handles to
    // freed targets for the length of the teardown.
    releaseLightingTargets();
    if (m_device && m_blackLightTex.isValid()) {
        m_device->destroy(m_blackLightTex);
        m_blackLightTex = rhi::TextureHandle{};
    }
    if (m_device && m_occlusionTex.isValid()) {
        m_device->destroy(m_occlusionTex);
        m_occlusionTex = rhi::TextureHandle{};
    }
    m_compositePass = nullptr;   // the graph owns it and is about to destroy it
    m_lightPass = nullptr;
    m_bloomPass = nullptr;
    m_presentPass = nullptr;

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
