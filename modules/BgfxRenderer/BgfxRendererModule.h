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

    /**
     * @brief Redirige la SORTIE FINALE du renderer vers `fb` (handle invalide = retour a l'ecran).
     *
     * QUOI     : ce que le joueur verrait, dans une cible relisible au CPU -- de quoi asserter des
     *            pixels sans ecran.
     * POURQUOI : c'est le module, et lui seul, qui sait quelles vues composent l'image finale. Cet
     *            ensemble DEPEND des effets actifs : sans eclairage la vue 0 va au backbuffer, avec
     *            eclairage elle part dans la cible de scene et c'est le composite qui sort, et si le
     *            post-traitement tourne c'est la presentation. Un appelant qui lierait "les vues 0 et
     *            1" -- le geste naturel -- capturerait donc un monde NOIR des qu'un jeu allume
     *            l'eclairage, sans que rien ne le signale : le HUD, lui, resterait correct, donc le
     *            test passerait pour un HUD en mentant sur la scene. Mesure a l'appui, cf.
     *            docs/design/frame-capture.md.
     * COMMENT  : pose une cible ; le module y renvoie ses vues finales a chaque frame, tant qu'elle
     *            est valide. La cible appartient a l'APPELANT (il la cree et la detruit) -- poser un
     *            handle invalide rend la main a l'ecran.
     *
     * ⚠️ Ne redimensionne rien : une cible plus petite que la fenetre capture une image tronquee.
     *
     * ⚠️ PERSISTANT, et ca se paie. Le module RE-APPLIQUE la redirection a chaque frame -- c'est ce
     *    qui la rend robuste aux reconstructions de cibles, mais ca veut dire qu'elle ECRASE toute
     *    liaison de vue posee a la main tant qu'elle n'est pas relachee. Un appelant qui capture
     *    puis enchaine sur un autre rendu (un export, un poster) doit poser un handle invalide
     *    entre les deux, sinon le second rendu part dans la PREMIERE cible. Constate en une heure :
     *    convertir le test mapview a fait sortir son poster vide, sans autre symptome.
     */
    void setCaptureTarget(rhi::FramebufferHandle fb);

private:
    // Rend a l'ecran les vues detournees par la capture (cf. .cpp : sinon corruption de tas).
    void releaseCaptureBindings();

    // ------------------------------------------------------------------------
    // HANDLERS DE TOPICS — un par sujet IIO traite ICI plutot que par SceneCollector.
    // ------------------------------------------------------------------------
    // QUOI     : le corps de chaque abonnement de setConfiguration, sorti en methode nommee.
    //
    // POURQUOI : ces sujets ne sont PAS des primitives de dessin — ce sont des reglages de
    //            passe ou de peripherique (tileset, police, texture, atlas). Ils vivent donc
    //            ici, ou vivent le device et les pointeurs de passes, et pas dans le
    //            collecteur de scene. Ecrits en lambdas dans setConfiguration, ils y noyaient
    //            la sequence d'initialisation sous ~250 lignes de logique de sujet : on ne
    //            voyait plus ni ce qui etait initialise, ni ce qui etait ecoute.
    //
    // COMMENT  : chaque abonnement garde sa POSITION EXACTE dans setConfiguration — les
    //            abonnements y sont entrelaces avec la construction du graphe de rendu, et
    //            rien ne prouve cet ordre indifferent. On extrait le CORPS, jamais l'appel.
    //            Toutes les lambdas d'origine ne capturaient que `this` (verifie), d'ou une
    //            extraction sans changement de capture. Le commentaire de chaque sujet est
    //            descendu avec son handler, la ou il documente quelque chose.
    //            ⚠️ Les quatre `asset:register/preload/setPriority/unload` restent en ligne :
    //            ils font deux lignes, les extraire allongerait sans clarifier.
    void onTilemapAnim(const Message& msg);       // render:tilemap:anim
    void onTilemapTileset(const Message& msg);    // render:tilemap:tileset
    void onTilemapFogStyle(const Message& msg);   // render:tilemap:fog:style
    void onTilemapPalette(const Message& msg);    // render:tilemap:palette
    void onFontLoad(const Message& msg);          // render:font
    void onScreenshot(const Message& msg);        // render:screenshot
    void onAssetPack(const Message& msg);         // asset:pack
    void onTextureCreate(const Message& msg);     // render:texture:create
    void onTexturePaint(const Message& msg);      // render:texture:paint
    void onTextureUpload(const Message& msg);     // render:texture:upload

    // ------------------------------------------------------------------------
    // Construction du graphe de rendu — toutes les passes, puis son setup().
    // ------------------------------------------------------------------------
    // QUOI     : cree et enregistre SpritePass, TextPass, ParticlePass, DebugPass,
    //            SectorPass et toute la chaine d'eclairage/post-traitement, puis
    //            appelle m_renderGraph->setup().
    // POURQUOI : c'etait ~110 lignes au milieu de setConfiguration, entre la lecture
    //            de la config et le systeme d'assets. L'ORDRE des addPass porte la
    //            semantique du rendu (le graphe trie, mais les passes de la meme
    //            couche sortent dans l'ordre d'ajout) : le regrouper le rend lisible
    //            d'un bloc au lieu de le faire deviner.
    // COMMENT  : le bloc extrait ne dependait QUE de ces deux handles de shader
    //            (verifie) et ne contenait ni abonnement ni sortie anticipee, d'ou une
    //            extraction sans changement de flot. ClearPass et TilemapPass restent
    //            dans setConfiguration : les abonnements tilemap y sont intercales
    //            juste apres, et rien ne prouve cet ordre indifferent.
    void buildRenderGraph(rhi::ShaderHandle spriteShader, rhi::ShaderHandle debugShader);
public:
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
    // Placeholder d'accumulation de lumière : 1×1 NOIR = aucune lumière. Servi au composite quand la
    // frame ne publie AUCUNE lampe — sans quoi la cible, jamais effacée (une vue sans draw est sautée
    // par bgfx, donc son clear ne tourne pas), rejouerait la dernière frame éclairée. Voir le
    // commentaire au point de création dans initialize().
    rhi::TextureHandle m_blackLightTex;
    rhi::FramebufferHandle m_sceneFB;
    rhi::FramebufferHandle m_captureTarget{};   // sortie finale detournee (capture headless)
    rhi::FramebufferHandle m_lightFB;
    rhi::FramebufferHandle m_occlusionFB;
    uint16_t m_lightingWidth = 0;      // size the targets were built for (0 = none yet)
    uint16_t m_lightingHeight = 0;

    // Create/resize the offscreen targets to WxH. No-op when they already match.
    void ensureLightingTargets(uint16_t width, uint16_t height);
    // Release them (window resize, or shutdown). Also releases the bloom targets, which are their
    // children: both are screen-sized, so one size change invalidates all of them.
    void releaseLightingTargets();

    // ---- Post-processing / bloom (plan B) -----------------------------------------------------
    // Built ONLY once a game publishes `render:bloom` with a non-zero intensity, and released again
    // the moment it stops. Without bloom the composite writes straight to the backbuffer and none of
    // this exists — the same zero-cost contract as lighting itself, one layer up.
    //
    // ⚠️ Le bloom EXIGE l'éclairage : la source de l'extraction est la frame COMPOSÉE, et sans
    //    éclairage il n'y a pas de composite du tout (la scène va directement au backbuffer, qui ne
    //    s'échantillonne pas). Un jeu qui ne veut que du post-traitement publie un ambiant BLANC.
    class BloomPass*   m_bloomPass = nullptr;     // owned by the render graph, borrowed here
    class PresentPass* m_presentPass = nullptr;   // idem
    // La cible HDR où le composite écrit dès qu'un post-traitement est actif (au lieu du backbuffer).
    //
    // ⚠️ Elle a sa PROPRE taille, distincte de celle des cibles de flou, parce que deux réglages
    //    indépendants l'activent : le bloom ET le tonemapping. Un jeu qui ne veut qu'une courbe
    //    d'exposition doit obtenir cette cible SANS payer les deux cibles de flou.
    rhi::FramebufferHandle m_hdrFB;
    uint16_t m_hdrWidth = 0;    // taille pour laquelle la cible HDR a été bâtie (0 = aucune)
    uint16_t m_hdrHeight = 0;
    // Les deux cibles au QUART de la résolution : extraction -> A, flou H -> B, flou V -> A.
    rhi::FramebufferHandle m_bloomFB[2];
    uint16_t m_bloomWidth = 0;    // taille PLEINE pour laquelle les cibles ont été bâties (0 = aucune)
    uint16_t m_bloomHeight = 0;
    uint16_t m_bloomSmallW = 0;   // la cible de flou réduite
    uint16_t m_bloomSmallH = 0;
    // Facteur de réduction en cours (4, 8 ou 16), choisi d'après le RAYON par
    // grove::light::bloomDownsample — voir tranche B4. Il fait partie de l'identité des cibles : un
    // rayon qui change de palier change leur taille, donc il faut les rebâtir.
    int m_bloomDownsample = 0;

    void ensureBloomTargets(uint16_t width, uint16_t height, int downsample);
    void releaseBloomTargets();
    // La cible HDR, séparément : le tonemapping seul en a besoin, les cibles de flou non.
    void ensureHdrTarget(uint16_t width, uint16_t height);
    void releaseHdrTarget();
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
