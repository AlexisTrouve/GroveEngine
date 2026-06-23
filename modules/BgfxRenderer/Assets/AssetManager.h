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

    // Resolve an id -> resident backend texture id, loading on demand and evicting to stay under budget.
    // Returns 0 if the id is unknown or the load failed.
    uint32_t resolve(const std::string& id) {
        auto it = m_assets.find(id);
        if (it == m_assets.end()) return 0;
        Asset& a = it->second;
        if (a.resident) { a.lastUsed = ++m_clock; return a.texId; }   // cache hit
        const uint32_t tex = m_provider->load(a.path);
        if (tex == 0) return 0;
        a.resident = true; a.texId = tex; a.bytes = m_provider->bytes(tex); a.lastUsed = ++m_clock;
        m_residentBytes += a.bytes;
        evictToFit(id);                                              // keep `id`, trim the rest if over budget
        return tex;
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
        for (const auto& kv : m_assets) if (kv.second.resident && kv.first != keep) resident.push_back(kv.first);
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
};

} // namespace grove::assets
