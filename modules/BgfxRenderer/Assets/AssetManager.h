#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <cstdint>
#include <algorithm>

namespace grove::assets {

/**
 * @brief GPU-facing side of the asset system — the thing that actually creates/destroys textures.
 *
 * QUOI : une interface minimale — charger un fichier en texture backend (id), la libérer, et dire combien
 *   de VRAM elle coûte. POURQUOI : l'AssetManager décide QUOI/QUAND charger ou évincer (logique pure,
 *   testable), le provider ne fait QUE le travail GPU + rapporte le coût. COMMENT : l'impl réelle enveloppe
 *   bgfx (TextureLoader/ResourceCache) ; un Mock rend l'AssetManager 100% headless-testable (pattern
 *   SoundManager : mixer pur + ISoundBackend).
 */
struct ITextureProvider {
    virtual ~ITextureProvider() = default;
    virtual uint32_t load(const std::string& path) = 0;   ///< -> backend texture id (0 = failed)
    virtual void     unload(uint32_t texId) = 0;
    virtual uint64_t bytes(uint32_t texId) const = 0;     ///< VRAM cost of a loaded texture (for the budget)
    /// Upload ALREADY-DECODED RGBA8 pixels -> backend texture id (0 = failed). The render-thread half of async
    /// load (phase 3): the slow decode happens off-thread, only this cheap GPU upload runs on the render thread.
    virtual uint32_t upload(const uint8_t* rgba, int w, int h) = 0;
};

/**
 * @brief One image decoded off-thread, waiting to be uploaded on the render thread (phase 3 async load).
 * QUOI : l'id d'asset visé + ses pixels RGBA8 + dimensions + un flag de succès. POURQUOI : c'est le paquet
 *   que le worker de décodage rend au thread render via IAsyncDecoder::poll — pixels prêts, plus qu'à uploader.
 */
struct DecodedImage {
    std::string id;                 ///< the assetId this decode was requested for
    std::vector<uint8_t> pixels;    ///< RGBA8, w*h*4 bytes (empty if ok == false)
    int w = 0, h = 0;
    bool ok = false;                ///< false = decode failed (missing/corrupt file)
};

/**
 * @brief Off-thread image decoder (phase 3). Decouples the SLOW decode (file read + stb) from the render thread.
 *
 * QUOI : `request(id,path)` met un job en file (décodé sur un worker thread) ; `poll(out)` (thread render)
 *   draine les décodages terminés pour que l'AssetManager fasse l'upload GPU. POURQUOI : le decode est la
 *   partie lente du first-touch — le sortir du thread render supprime le hitch ; l'upload reste côté render
 *   car bgfx est single-thread. Interface => l'AssetManager reste pur (Mock déterministe en test, pattern
 *   ITextureProvider/ISoundBackend). COMMENT : l'impl réelle = ThreadedDecoder (worker thread + decodeRgba).
 */
struct IAsyncDecoder {
    virtual ~IAsyncDecoder() = default;
    virtual void   request(const std::string& id, const std::string& path) = 0;  ///< enqueue an off-thread decode
    virtual void   poll(std::vector<DecodedImage>& out) = 0;  ///< drain finished decodes (render thread)
    virtual size_t pending() const = 0;                       ///< decodes still in flight (diagnostic)
};

/**
 * @brief string assetId -> resident GPU texture, with on-demand load + preload + a configurable VRAM budget
 *        and DYNAMIC priority + LRU eviction.
 *
 * QUOI : un registre `id -> {chemin, priorité, groupe}` (des MILLIERS d'entrées = juste des métadonnées) au
 *   dessus d'un cache de textures RÉSIDENTES borné par un budget VRAM. `resolve(id)` rend l'id de texture
 *   backend, en chargeant à la demande et en évinçant si besoin.
 * POURQUOI : on veut des milliers d'assets DISPONIBLES sans les avoir tous en VRAM — quelques dizaines sont
 *   visibles à la fois. Donc : registre (métadonnées, ~0 coût) + cache (budget) + streaming. La PRIORITÉ
 *   (modifiable à chaud) protège les assets importants : éviction = plus basse priorité, puis plus ancien.
 * COMMENT : `resolve` charge via le provider puis `evictToFit` ramène sous le budget (sans jamais évincer
 *   ce qu'on vient de charger). `preloadGroup` charge un groupe (priorité haute d'abord). `lastUsed` = un
 *   compteur monotone bumpé à chaque accès (LRU déterministe, pas d'horloge murale).
 *
 * NOTE : logique PURE (std uniquement, pas de bgfx) — c'est la phase 1. Le backend bgfx + les topics IIO
 *   (asset:register/preload/...) + le manifeste viennent dans les slices suivantes.
 */
class AssetManager {
public:
    AssetManager(ITextureProvider* provider, uint64_t vramBudgetBytes)
        : m_provider(provider), m_budget(vramBudgetBytes) {}

    // --- Registry (declarative manifest + the runtime asset:register topic). Re-registering updates meta. ---
    void registerAsset(const std::string& id, const std::string& path, int priority = 0, const std::string& group = "") {
        Asset& a = m_assets[id];
        a.path = path; a.priority = priority; a.group = group;
    }
    void setPriority(const std::string& id, int priority) {
        auto it = m_assets.find(id);
        if (it != m_assets.end()) it->second.priority = priority;
    }

    // Register a SUB-SPRITE of a pre-baked atlas/sheet (phase 2). The sub-sprite has no texture of its own —
    // it points at a SHEET asset (registerAsset'd separately, the actual texture) + a UV rect into it. Many
    // sub-sprites share one resident sheet texture (the whole point of atlasing). uv in [0,1].
    void registerAtlasSprite(const std::string& id, const std::string& sheetId,
                             float u0, float v0, float u1, float v1, int priority = 0, const std::string& group = "") {
        Asset& a = m_assets[id];
        a.atlasSprite = true; a.sheetId = sheetId;
        a.u0 = u0; a.v0 = v0; a.u1 = u1; a.v1 = v1;
        a.priority = priority; a.group = group;
    }

    // --- Async load (phase 3): decode off-thread, upload on the render thread. Opt-in via setAsyncDecoder. ---
    void setAsyncDecoder(IAsyncDecoder* d) { m_decoder = d; }       ///< null (default) = synchronous resolve
    void setPlaceholder(uint32_t texId) { m_placeholder = texId; } ///< returned by resolve while a decode is in flight

    // Resolve an id -> resident backend texture id, loading on demand and evicting to stay under budget.
    // Returns 0 if the id is unknown or the load failed.
    //
    // ASYNC: when a decoder is set and the asset isn't resident yet, resolve does NOT block — it kicks an
    // off-thread decode (once) and returns the placeholder so the caller keeps drawing this frame; the real
    // texture appears a frame or two later, the first time pumpAsync() picks up the finished decode. A decode
    // that failed is latched (a.failed) so a sprite drawn every frame doesn't re-enqueue a broken file forever.
    uint32_t resolve(const std::string& id) {
        auto it = m_assets.find(id);
        if (it == m_assets.end()) return 0;
        Asset& a = it->second;
        if (a.resident) { a.lastUsed = ++m_clock; return a.texId; }   // cache hit

        if (m_decoder && !a.path.empty()) {                          // async path (standalone asset / sheet)
            if (a.failed) return m_placeholder;                      // broken file — don't retry every frame
            if (!a.loading) { a.loading = true; m_decoder->request(id, a.path); }   // kick the decode ONCE
            return m_placeholder;                                    // not ready yet — draw the placeholder
        }

        const uint32_t tex = m_provider->load(a.path);               // synchronous path (blocks: decode+upload)
        if (tex == 0) return 0;
        a.resident = true; a.texId = tex; a.bytes = m_provider->bytes(tex); a.lastUsed = ++m_clock;
        m_residentBytes += a.bytes;
        evictToFit(id);                                              // keep `id`, trim the rest if over budget
        return tex;
    }

    // Drain finished off-thread decodes and upload them on the RENDER THREAD (call once per frame, before
    // collecting sprites so this frame can already use what just finished). bgfx is single-threaded, so the
    // GPU upload MUST happen here, not on the worker. No-op when no decoder is set.
    void pumpAsync() {
        if (!m_decoder) return;
        std::vector<DecodedImage> done;
        m_decoder->poll(done);
        for (auto& d : done) {
            auto it = m_assets.find(d.id);
            if (it == m_assets.end()) continue;                      // asset deregistered meanwhile — drop it
            Asset& a = it->second;
            a.loading = false;
            if (a.resident) continue;                                // already resident (e.g. preloaded sync) — skip
            if (!d.ok) { a.failed = true; continue; }                // decode failed — latch, never retry
            const uint32_t tex = m_provider->upload(d.pixels.data(), d.w, d.h);
            if (tex == 0) { a.failed = true; continue; }             // upload failed — latch too
            a.resident = true; a.texId = tex; a.bytes = m_provider->bytes(tex); a.lastUsed = ++m_clock;
            m_residentBytes += a.bytes;
            evictToFit(d.id);                                        // keep the freshly uploaded one
        }
    }

    // Resolve a sprite to its texture id + UV rect. A SUB-SPRITE resolves its SHEET (load/cache that one
    // texture) and yields the sub-rect's UVs; a standalone asset yields the full [0,1] UV. The sprite path
    // uses this so an atlas sub-sprite renders the right region with the existing sprite shader. 0 if unknown.
    uint32_t resolveSprite(const std::string& id, float& u0, float& v0, float& u1, float& v1) {
        auto it = m_assets.find(id);
        if (it == m_assets.end()) { u0 = v0 = 0.0f; u1 = v1 = 1.0f; return 0; }
        const Asset& a = it->second;
        if (a.atlasSprite) {
            u0 = a.u0; v0 = a.v0; u1 = a.u1; v1 = a.v1;
            return resolve(a.sheetId);   // the SHEET carries the texture (shared by all its sub-sprites)
        }
        u0 = v0 = 0.0f; u1 = v1 = 1.0f;
        return resolve(id);
    }

    // Preload a whole named group into the cache now (highest priority first, so a tight budget favours them).
    void preloadGroup(const std::string& group) {
        std::vector<std::string> ids;
        for (const auto& kv : m_assets) if (kv.second.group == group) ids.push_back(kv.first);
        std::sort(ids.begin(), ids.end(), [&](const std::string& x, const std::string& y){
            return m_assets[x].priority > m_assets[y].priority; });
        for (const auto& id : ids) resolve(id);
    }

    void unload(const std::string& id) {
        auto it = m_assets.find(id);
        if (it != m_assets.end() && it->second.resident) evictAsset(it->second);
    }

    // Register an ALREADY-RESIDENT texture (e.g. a runtime-packed atlas sheet, created by AtlasPacker). It is
    // PINNED — never evicted — since it has no path to reload from. Counts toward the budget.
    void registerResident(const std::string& id, uint32_t texId, uint64_t bytes, int priority = 0, const std::string& group = "") {
        Asset& a = m_assets[id];
        a.path.clear(); a.priority = priority; a.group = group; a.pinned = true;
        if (!a.resident) { a.resident = true; m_residentBytes += bytes; }
        a.texId = texId; a.bytes = bytes; a.lastUsed = ++m_clock;
    }

    // --- Introspection (tests / diagnostics). ---
    bool     isResident(const std::string& id) const { auto it=m_assets.find(id); return it!=m_assets.end() && it->second.resident; }
    bool     isRegistered(const std::string& id) const { return m_assets.count(id) > 0; }
    uint64_t residentBytes() const { return m_residentBytes; }
    size_t   residentCount() const { size_t n=0; for (const auto& kv:m_assets) if (kv.second.resident) ++n; return n; }
    uint64_t budget() const { return m_budget; }
    void     setBudget(uint64_t b) { m_budget = b; evictToFit(""); }

private:
    struct Asset {
        std::string path, group;
        int priority = 0;
        bool resident = false;
        uint32_t texId = 0;
        uint64_t bytes = 0;
        uint64_t lastUsed = 0;
        // Atlas sub-sprite (phase 2): no texture of its own — points at a sheet asset + a UV rect into it.
        bool atlasSprite = false;
        std::string sheetId;
        float u0 = 0.0f, v0 = 0.0f, u1 = 1.0f, v1 = 1.0f;
        // Pinned (phase 2b): a runtime-packed sheet has no path -> it can't be reloaded, so it must NEVER be
        // evicted. Pinned residents still count toward the budget but are excluded from eviction candidates.
        bool pinned = false;
        // Async load (phase 3): `loading` = a decode is in flight (so resolve doesn't enqueue duplicates);
        // `failed` = the decode/upload failed (latched so a per-frame resolve doesn't re-request forever).
        bool loading = false;
        bool failed = false;
    };

    void evictAsset(Asset& a) {
        m_provider->unload(a.texId);
        m_residentBytes -= a.bytes;
        a.resident = false; a.texId = 0; a.bytes = 0;
    }

    // Evict residents (lowest priority first, then least-recently-used) until under budget. `keep` is spared.
    void evictToFit(const std::string& keep) {
        if (m_residentBytes <= m_budget) return;
        std::vector<std::string> resident;
        for (const auto& kv : m_assets)
            if (kv.second.resident && !kv.second.pinned && kv.first != keep) resident.push_back(kv.first);
        std::sort(resident.begin(), resident.end(), [&](const std::string& x, const std::string& y){
            const Asset& ax = m_assets[x]; const Asset& ay = m_assets[y];
            if (ax.priority != ay.priority) return ax.priority < ay.priority;   // low priority evicted first
            return ax.lastUsed < ay.lastUsed;                                   // then the oldest
        });
        for (const auto& id : resident) {
            if (m_residentBytes <= m_budget) break;
            evictAsset(m_assets[id]);
        }
    }

    ITextureProvider* m_provider;
    uint64_t m_budget;
    uint64_t m_residentBytes = 0;
    uint64_t m_clock = 0;
    std::unordered_map<std::string, Asset> m_assets;
    // Async load (phase 3): optional off-thread decoder + the texture id resolve hands back while decoding.
    IAsyncDecoder* m_decoder = nullptr;   // null = synchronous (the default; zero behaviour change)
    uint32_t m_placeholder = 0;           // returned by resolve() until the real texture is uploaded
};

} // namespace grove::assets
