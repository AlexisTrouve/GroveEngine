#include "SceneCollector.h"
#include "grove/IIO.h"
#include "grove/IDataNode.h"
#include "../Frame/FrameAllocator.h"
#include "../Assets/AssetManager.h"   // resolve a sprite's "asset" id -> texture id
#include "grove/ui/NineSlice.h"        // pure 9-slice geometry (render:nineslice expansion)
#include "grove/light/Transmittance.h" // perUnitForTint: author tint -> per-unit transmittance (F1)
#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <spdlog/spdlog.h>

namespace grove {

// An "asset" string id (resolved through the streaming AssetManager) wins over a raw numeric "textureId".
// For an asset, resolveSprite also yields the UV rect (full [0,1] for a standalone, the sub-rect for an
// atlas sub-sprite) which we write into the sprite's UVs — so an atlas icon renders its region with the
// existing sprite shader. `fallback` is used when neither is present (e.g. keep the current id on update).
int SceneCollector::resolveSpriteTexture(const IDataNode& data, SpriteInstance& sprite, int fallback) const {
    const std::string asset = data.getString("asset", "");
    if (!asset.empty() && m_assetMgr) {
        float u0, v0, u1, v1;
        const uint32_t tex = m_assetMgr->resolveSprite(asset, u0, v0, u1, v1);
        sprite.u0 = u0; sprite.v0 = v0; sprite.u1 = u1; sprite.v1 = v1;   // atlas sub-rect overrides data UVs
        return static_cast<int>(tex);
    }
    return data.getInt("textureId", fallback);
}

// QUOI : construit une vue orthographique screen-space (1px = 1 unité monde, origine
//   haut-gauche), zoom 1, sans translation.
// POURQUOI : c'est la vue du HUD (et la vue monde par défaut avant toute caméra). Le HUD
//   doit rester fixe quand le monde zoome → sa projection ne dépend QUE du viewport, jamais
//   du zoom/pan de render:camera. Même matrice que initDefaultView, factorisée pour servir
//   m_mainView (défaut) ET m_hudView.
// COMMENT : view = identité ; proj ortho mappant (0,0)-(w,h) vers (-1,-1)-(1,1) avec Y-flip.
static void buildScreenSpaceView(ViewInfo& v, uint16_t width, uint16_t height) {
    v.positionX = 0.0f;
    v.positionY = 0.0f;
    v.zoom = 1.0f;
    v.viewportX = 0;
    v.viewportY = 0;
    v.viewportW = width;
    v.viewportH = height;

    for (int i = 0; i < 16; ++i) v.viewMatrix[i] = (i % 5 == 0) ? 1.0f : 0.0f;

    std::memset(v.projMatrix, 0, sizeof(v.projMatrix));
    v.projMatrix[0] = 2.0f / width;
    v.projMatrix[5] = -2.0f / height;  // Y-flip for top-left origin
    v.projMatrix[10] = 1.0f;
    v.projMatrix[12] = -1.0f;
    v.projMatrix[13] = 1.0f;
    v.projMatrix[15] = 1.0f;
}

// Un render:* est-il destiné au HUD (espace écran, fixe) plutôt qu'au monde (zoomable) ?
// Opt-in explicite via le champ "space":"screen" ; tout le reste reste monde par défaut.
static bool isScreenSpace(const IDataNode& data) {
    return data.getString("space", "") == "screen";
}

// Mirror a sprite horizontally/vertically by SWAPPING its UV range.
//
// WHY a UV swap rather than a negative scale or a new instance field: it costs nothing at runtime
// (no shader change, no extra per-instance data on a path that is deliberately lean), it mirrors
// WITHIN an atlas sub-rect instead of assuming the sprite owns the whole texture, and it composes
// with `rotation` in the order a paper-doll needs — the picture is mirrored inside its quad first,
// then the quad turns. A negative scaleX would instead flip the rotation's apparent direction.
//
// ⚠️ A sprite with textureId 0 is a flat tinted quad: there is no image to mirror, so a flip on it
// is visually a no-op. That is inherent, not an oversight.
inline void applySpriteFlip(const IDataNode& data, SpriteInstance& sprite) {
    if (data.getBool("flipX", false)) std::swap(sprite.u0, sprite.u1);
    if (data.getBool("flipY", false)) std::swap(sprite.v0, sprite.v1);
}

void SceneCollector::setup(IIO* io, uint16_t width, uint16_t height) {
    // Subscribe to all render topics with callback handler
    io->subscribe("render:.*", [this](const Message& msg) {
        if (!msg.data) return;

        // Route message based on topic.
        // NB: NO per-message logging here — this callback runs on the render hot path
        // (once per command, per frame). The previous spdlog::info() calls flooded the
        // log at 60fps×N sprites and cost formatting/mutex on every frame.
        //
        // Retained mode: sprites persist across frames, keyed by renderId
        // (add/update/remove). For static / long-lived sprites.
        if (msg.topic == "render:sprite:add") {
            parseSpriteAdd(*msg.data);
        }
        else if (msg.topic == "render:sprite:update") {
            parseSpriteUpdate(*msg.data);
        }
        else if (msg.topic == "render:sprite:remove") {
            parseSpriteRemove(*msg.data);
        }
        // Retained mode - 9-slice (nine-patch) frame: ONE message describes a bordered box; we expand it
        // into up to 9 retained sprites (corners/edges/centre) so the whole retained pipeline (HUD bucket,
        // clip, tint) draws it with no new pass. See parseNineSliceAdd.
        else if (msg.topic == "render:nineslice:add") {
            parseNineSliceAdd(*msg.data);
        }
        else if (msg.topic == "render:nineslice:update") {
            parseNineSliceUpdate(*msg.data);
        }
        else if (msg.topic == "render:nineslice:remove") {
            parseNineSliceRemove(*msg.data);
        }
        // Retained mode - text
        else if (msg.topic == "render:text:add") {
            parseTextAdd(*msg.data);
        }
        else if (msg.topic == "render:text:update") {
            parseTextUpdate(*msg.data);
        }
        else if (msg.topic == "render:text:remove") {
            parseTextRemove(*msg.data);
        }
        // Ephemeral / immediate mode: the sprite is drawn for THIS frame only (the
        // publisher re-sends it every frame; cleared in clear()). This is a LEGITIMATE,
        // supported path for dynamic per-frame entities — NOT an error. The old
        // "should not happen in retained mode" warning was wrong; both modes coexist
        // and are merged in finalize().
        else if (msg.topic == "render:sprite") {
            parseSprite(*msg.data);
        }
        else if (msg.topic == "render:sprite:batch") {
            parseSpriteBatch(*msg.data);
        }
        else if (msg.topic == "render:tilemap") {
            parseTilemap(*msg.data);
        }
        else if (msg.topic == "render:tilemap:add") {
            parseTilemapAdd(*msg.data);
        }
        else if (msg.topic == "render:tilemap:update") {
            parseTilemapUpdate(*msg.data);
        }
        else if (msg.topic == "render:tilemap:remove") {
            parseTilemapRemove(*msg.data);
        }
        else if (msg.topic == "render:tilemap:fog") {
            parseTilemapFog(*msg.data);
        }
        else if (msg.topic == "render:text") {
            parseText(*msg.data);
        }
        else if (msg.topic == "render:particle") {
            parseParticle(*msg.data);
        }
        else if (msg.topic == "render:camera") {
            parseCamera(*msg.data);
        }
        else if (msg.topic == "render:clear") {
            parseClear(*msg.data);
        }
        else if (msg.topic == "render:ambient") {
            parseAmbient(*msg.data);
        }
        else if (msg.topic == "render:bloom") {
            parseBloom(*msg.data);
        }
        else if (msg.topic == "render:tonemap") {
            parseTonemap(*msg.data);
        }
        else if (msg.topic == "render:fade") {
            parseFade(*msg.data);
        }
        else if (msg.topic == "render:grade") {
            parseGrade(*msg.data);
        }
        else if (msg.topic == "render:light") {
            parseLight(*msg.data);
        }
        else if (msg.topic == "render:occluder") {
            parseOccluder(*msg.data);
        }
        else if (msg.topic == "render:occluder:add") {
            parseOccluderAdd(*msg.data);
        }
        else if (msg.topic == "render:occluder:update") {
            parseOccluderUpdate(*msg.data);
        }
        else if (msg.topic == "render:filter") {
            parseFilter(*msg.data);
        }
        else if (msg.topic == "render:filter:add") {
            parseFilterAdd(*msg.data);
        }
        else if (msg.topic == "render:filter:update") {
            parseFilterUpdate(*msg.data);
        }
        else if (msg.topic == "render:filter:remove") {
            parseFilterRemove(*msg.data);
        }
        else if (msg.topic == "render:fog") {
            parseFog(*msg.data);
        }
        else if (msg.topic == "render:nebula") {
            parseNebula(*msg.data);
        }
        else if (msg.topic == "render:nebula:add") {
            parseNebulaAdd(*msg.data);
        }
        else if (msg.topic == "render:nebula:update") {
            parseNebulaUpdate(*msg.data);
        }
        else if (msg.topic == "render:nebula:remove") {
            parseNebulaRemove(*msg.data);
        }
        else if (msg.topic == "render:fog:add") {
            parseFogAdd(*msg.data);
        }
        else if (msg.topic == "render:fog:update") {
            parseFogUpdate(*msg.data);
        }
        else if (msg.topic == "render:fog:remove") {
            parseFogRemove(*msg.data);
        }
        else if (msg.topic == "render:occluder:remove") {
            parseOccluderRemove(*msg.data);
        }
        else if (msg.topic == "render:debug:line") {
            parseDebugLine(*msg.data);
        }
        else if (msg.topic == "render:debug:rect") {
            parseDebugRect(*msg.data);
        }
        else if (msg.topic == "render:sector") {
            parseSector(*msg.data);
        }
        // Filled rect via the LAYERED sprite path (engine help A2): unlike debug:rect
        // (always-on-top, no layer), render:rect honors `layer` and draws before text —
        // so HUD backgrounds sit under their labels.
        else if (msg.topic == "render:rect") {
            parseRect(*msg.data);
        }
    });

    // Initialize default view with provided dimensions (will be overridden by camera messages)
    initDefaultView(width > 0 ? width : 1280, height > 0 ? height : 720);
}

void SceneCollector::collect(IIO* io, float deltaTime) {
    m_deltaTime = deltaTime;
    m_elapsedTime += deltaTime;   // running clock for time-based shaders (animated tiles)
    m_frameNumber++;

    // Pull and dispatch all pending messages (callbacks invoked automatically)
    while (io->hasMessages() > 0) {
        io->pullAndDispatch();
    }
}

namespace {

// ============================================================================
// COPIE D'UN TABLEAU EPHEMERE DANS L'ARENE DE FRAME
// ----------------------------------------------------------------------------
// QUOI     : alloue une tranche exactement dimensionnee dans l'arene, y recopie le
//            vecteur ephemere, et publie le couple (pointeur, compte) dans le paquet.
//
// POURQUOI : `finalize` contenait QUINZE blocs de copie de primitives, dont six
//            strictement identiques a ces quelques lignes pres. Le probleme n'est pas
//            la longueur : c'est qu'un lecteur ne peut pas voir LEQUEL differe ni
//            POURQUOI. Regrouper l'identique fait ressortir la variation au site
//            d'appel, au lieu de la noyer.
//
// COMMENT  : ⚠️ COMPORTEMENT PRESERVE A L'IDENTIQUE, y compris son bord rugueux — si
//            l'allocation ECHOUE, on ne touche NI le pointeur NI le compte (ils gardent
//            leur valeur d'initialisation du paquet). C'est ce que faisaient les quinze
//            blocs d'origine. Un refactor ne corrige pas en douce ; si ce bord doit
//            changer, c'est un commit separe avec son test.
//            ⚠️ DEUX fonctions plutot qu'un drapeau `sortByLayer` : le corps d'un
//            gabarit est instancie meme quand le drapeau est faux, or LightCommand,
//            ParticleInstance, DebugLine et DebugRect n'ont PAS de champ `layer` — un
//            booleen ne compilerait tout simplement pas. Le nom au site d'appel dit
//            donc la difference, ce qu'un `true` nu n'aurait pas fait de toute facon.
// ============================================================================
template <typename T>
void packEphemeral(FrameAllocator& allocator, const std::vector<T>& src,
                   const T*& outPtr, size_t& outCount) {
    if (!src.empty()) {
        T* dst = allocator.allocateArray<T>(src.size());
        if (dst) {
            std::memcpy(dst, src.data(), src.size() * sizeof(T));
            outPtr = dst;
            outCount = src.size();
        }
    } else {
        outPtr = nullptr;
        outCount = 0;
    }
}

// Idem, suivi d'un tri par COUCHE croissante. Le tri est **stable** : a couche egale,
// l'ordre de publication est conserve — c'est ce qui rend le z-order deterministe d'une
// frame a l'autre pour un jeu qui republie ses secteurs dans le meme ordre.
// Reserve aux types qui portent un champ `layer` (SectorCommand).
template <typename T>
void packEphemeralSortedByLayer(FrameAllocator& allocator, const std::vector<T>& src,
                                const T*& outPtr, size_t& outCount) {
    packEphemeral(allocator, src, outPtr, outCount);
    if (outPtr && outCount > 1) {
        T* dst = const_cast<T*>(outPtr);   // l'arene nous appartient : le const est celui du PAQUET
        std::stable_sort(dst, dst + outCount,
            [](const T& a, const T& b) { return a.layer < b.layer; });
    }
}

} // namespace

FramePacket SceneCollector::finalize(FrameAllocator& allocator) {
    FramePacket packet;

    packet.frameNumber = m_frameNumber;
    packet.deltaTime = m_deltaTime;
    packet.elapsedTime = m_elapsedTime;
    packet.clearColor = m_clearColor;
    packet.ambientColor = m_ambientColor;   // global state, not cleared at the frame boundary
    packet.bloom = m_bloom;                 // idem: a setting, published once, honoured every frame
    packet.tonemap = m_tonemap;             // idem — et INDEPENDANT du bloom, voir FramePacket
    packet.fade = m_fade;                   // idem — et n'exige ni eclairage ni cible HDR
    packet.grade = m_grade;                 // idem — mais sur la presentation, donc il EPARGNE le HUD
    packet.mainView = m_mainView;
    packet.allocator = &allocator;

    // Copy sprites to frame allocator (merge retained + ephemeral)
    size_t totalSprites = m_retainedSprites.size() + m_sprites.size();
    if (totalSprites > 0) {
        SpriteInstance* sprites = allocator.allocateArray<SpriteInstance>(totalSprites);
        if (sprites) {
            size_t idx = 0;
            // Copy retained sprites first
            for (const auto& [renderId, sprite] : m_retainedSprites) {
                sprites[idx++] = sprite;
            }
            // Copy ephemeral sprites
            if (!m_sprites.empty()) {
                std::memcpy(&sprites[idx], m_sprites.data(), m_sprites.size() * sizeof(SpriteInstance));
            }
            // FIX #4 : trier par layer croissant → ordre de dessin = z-order.
            // POURQUOI : les sprites étaient émis "retained (ordre de hash unordered_map)
            //   puis ephemeral", sans tri ; comme la passe submit dans l'ordre du paquet,
            //   le z-order était non déterministe (un fond pouvait passer devant son texte).
            // COMMENT : stable_sort pour que, à layer ÉGAL, l'ordre d'insertion (retained
            //   avant ephemeral) soit préservé — pas de scintillement entre éléments du
            //   même plan.
            // Ne trier que si nécessaire. QUOI : is_sorted O(n) avant le stable_sort O(n log n). POURQUOI : les
            //   sprites d'une carte monde entier (émis layer par layer par MapView) arrivent DÉJÀ triés par
            //   layer -> un stable_sort sur ~300k+ éléments déjà triés coûtait ~30 ms/frame pour rien. COMMENT :
            //   clé IDENTIQUE (a.layer < b.layer) ; déjà trié -> on garde l'ordre (retained avant ephemeral à
            //   layer égal, exactement ce que stable_sort préservait) et on saute le tri.
            if (!std::is_sorted(sprites, sprites + totalSprites,
                    [](const SpriteInstance& a, const SpriteInstance& b) { return a.layer < b.layer; })) {
                std::stable_sort(sprites, sprites + totalSprites,
                    [](const SpriteInstance& a, const SpriteInstance& b) { return a.layer < b.layer; });
            }
            packet.sprites = sprites;
            packet.spriteCount = totalSprites;
        }
    } else {
        packet.sprites = nullptr;
        packet.spriteCount = 0;
    }

    // Copy tilemaps (retained + ephemeral) into the frame allocator. Retained chunks come first;
    // each carries its `dirty` flag so the pass uploads only changed grids. After a retained chunk
    // is copied into a frame, its store dirty is cleared — it stays false until the next update,
    // which is what makes a static retained chunk upload exactly once.
    {
        const size_t totalTilemaps = m_retainedTilemaps.size() + m_tilemaps.size();
        if (totalTilemaps > 0) {
            TilemapChunk* tilemaps = allocator.allocateArray<TilemapChunk>(totalTilemaps);
            if (tilemaps) {
                size_t idx = 0;

                // Retained chunks (by id).
                for (auto& [id, rt] : m_retainedTilemaps) {
                    TilemapChunk chunk = rt.chunk;  // meta (incl. id + dirty)
                    if (!rt.tiles.empty()) {
                        uint16_t* tilesCopy = static_cast<uint16_t*>(
                            allocator.allocate(rt.tiles.size() * sizeof(uint16_t), alignof(uint16_t)));
                        if (tilesCopy) {
                            std::memcpy(tilesCopy, rt.tiles.data(), rt.tiles.size() * sizeof(uint16_t));
                            chunk.tiles = tilesCopy;
                            chunk.tileCount = rt.tiles.size();
                        }
                    }
                    if (!rt.fog.empty()) {
                        uint8_t* fogCopy = static_cast<uint8_t*>(
                            allocator.allocate(rt.fog.size(), alignof(uint8_t)));
                        if (fogCopy) {
                            std::memcpy(fogCopy, rt.fog.data(), rt.fog.size());
                            chunk.fog = fogCopy;
                        }
                    }
                    // Multi-layer (Strategy A): copy each layer's grid into frame memory + build the
                    // TilemapLayer array (layer 0 == chunk.tiles above; layers[1..] = the overlays).
                    if (!rt.layerTiles.empty()) {
                        TilemapLayer* layerArr = allocator.allocateArray<TilemapLayer>(rt.layerTiles.size());
                        if (layerArr) {
                            for (size_t li = 0; li < rt.layerTiles.size(); ++li) {
                                const std::vector<uint16_t>& lt = rt.layerTiles[li];
                                uint16_t* copy = static_cast<uint16_t*>(
                                    allocator.allocate(lt.size() * sizeof(uint16_t), alignof(uint16_t)));
                                if (copy && !lt.empty()) std::memcpy(copy, lt.data(), lt.size() * sizeof(uint16_t));
                                layerArr[li].tiles = copy;
                                layerArr[li].tileCount = lt.size();
                                layerArr[li].textureId = (li < rt.layerTexIds.size()) ? rt.layerTexIds[li] : 0;
                            }
                            chunk.layers = layerArr;
                            chunk.layerCount = rt.layerTiles.size();
                        }
                    }
                    tilemaps[idx++] = chunk;
                    // Consumed this frame -> clean until the next update (clears the dirty rects too).
                    rt.chunk.dirty = false;
                    rt.chunk.dirtyX = 0; rt.chunk.dirtyY = 0;
                    rt.chunk.dirtyW = 0; rt.chunk.dirtyH = 0;
                    rt.chunk.fogDirty = false;
                    rt.chunk.fogDirtyX = 0; rt.chunk.fogDirtyY = 0;
                    rt.chunk.fogDirtyW = 0; rt.chunk.fogDirtyH = 0;
                }

                // Ephemeral chunks (re-sent every frame).
                for (size_t i = 0; i < m_tilemaps.size(); ++i) {
                    TilemapChunk chunk = m_tilemaps[i];
                    if (i < m_tilemapTiles.size() && !m_tilemapTiles[i].empty()) {
                        const std::vector<uint16_t>& tiles = m_tilemapTiles[i];
                        uint16_t* tilesCopy = static_cast<uint16_t*>(
                            allocator.allocate(tiles.size() * sizeof(uint16_t), alignof(uint16_t)));
                        if (tilesCopy) {
                            std::memcpy(tilesCopy, tiles.data(), tiles.size() * sizeof(uint16_t));
                            chunk.tiles = tilesCopy;
                            chunk.tileCount = tiles.size();
                        }
                    }
                    tilemaps[idx++] = chunk;
                }

                packet.tilemaps = tilemaps;
                packet.tilemapCount = idx;
            }
        } else {
            packet.tilemaps = nullptr;
            packet.tilemapCount = 0;
        }
    }

    // Copy texts (with string data) - merge retained + ephemeral
    size_t totalTexts = m_retainedTexts.size() + m_texts.size();
    if (totalTexts > 0) {
        TextCommand* texts = allocator.allocateArray<TextCommand>(totalTexts);
        if (texts) {
            size_t idx = 0;

            // Copy retained texts first
            for (const auto& [renderId, textCmd] : m_retainedTexts) {
                texts[idx] = textCmd;
                // Copy string data
                auto strIt = m_retainedTextStrings.find(renderId);
                if (strIt != m_retainedTextStrings.end() && !strIt->second.empty()) {
                    const std::string& str = strIt->second;
                    char* textCopy = static_cast<char*>(allocator.allocate(str.size() + 1, 1));
                    if (textCopy) {
                        std::memcpy(textCopy, str.c_str(), str.size() + 1);
                        texts[idx].text = textCopy;
                    }
                }
                idx++;
            }

            // Copy ephemeral texts
            for (size_t i = 0; i < m_texts.size(); ++i) {
                texts[idx] = m_texts[i];
                if (i < m_textStrings.size() && !m_textStrings[i].empty()) {
                    const std::string& str = m_textStrings[i];
                    char* textCopy = static_cast<char*>(allocator.allocate(str.size() + 1, 1));
                    if (textCopy) {
                        std::memcpy(textCopy, str.c_str(), str.size() + 1);
                        texts[idx].text = textCopy;
                    }
                }
                idx++;
            }

            // FIX #4 : même tri stable par layer que les sprites (z-order déterministe).
            // Le texte des labels UI doit passer DEVANT les fonds (layer supérieur).
            std::stable_sort(texts, texts + totalTexts,
                [](const TextCommand& a, const TextCommand& b) { return a.layer < b.layer; });
            packet.texts = texts;
            packet.textCount = totalTexts;
        }
    } else {
        packet.texts = nullptr;
        packet.textCount = 0;
    }

    // Copy occluders: RETAINED (static level geometry) then EPHEMERAL (a moving shutter). Both
    // modes coexist -- neither is an error -- so a scene mixing them occludes with both. Order is
    // irrelevant here: occlusion is a product, and a product does not care which factor came first.
    {
        const size_t totalOccluders = m_retainedOccluders.size() + m_occluders.size();
        if (totalOccluders > 0) {
            OccluderCommand* occ = allocator.allocateArray<OccluderCommand>(totalOccluders);
            if (occ) {
                size_t idx = 0;
                for (const auto& kv : m_retainedOccluders) occ[idx++] = kv.second;
                if (!m_occluders.empty()) {
                    std::memcpy(&occ[idx], m_occluders.data(),
                                m_occluders.size() * sizeof(OccluderCommand));
                }
                packet.occluders = occ;
                packet.occluderCount = totalOccluders;
            }
        } else {
            packet.occluders = nullptr;
            packet.occluderCount = 0;
        }
    }

    // Copy filters: RETAINED (static stained glass) then EPHEMERAL. Same shape as the occluders
    // above, and for the same reason: no array at all when nobody published one, so a game without
    // stained glass claims no arena slice.
    //
    // The retained ones are CONVERTED here rather than at merge time, because the conversion depends
    // on the pane's thickness and an update may have changed it. Deriving from the stored author
    // values every frame is what keeps a resized window honest — the cost is three pow() per pane,
    // against a whole level's worth of messages saved by being retained at all.
    {
        const size_t totalFilters = m_retainedFilters.size() + m_filters.size();
        if (totalFilters > 0) {
            FilterCommand* flt = allocator.allocateArray<FilterCommand>(totalFilters);
            if (flt) {
                size_t idx = 0;
                for (const auto& kv : m_retainedFilters) flt[idx++] = buildFilter(kv.second);
                if (!m_filters.empty()) {
                    std::memcpy(&flt[idx], m_filters.data(),
                                m_filters.size() * sizeof(FilterCommand));
                }
                packet.filters = flt;
                packet.filterCount = totalFilters;
            }
        } else {
            packet.filters = nullptr;
            packet.filterCount = 0;
        }
    }

    // Copy nebulae: RETAINED (a cloud, which does not move — and is usually SEVERAL overlapping
    // volumes, so the saving is per-volume) then EPHEMERAL. Same no-array-when-unused rule as the rest.
    {
        const size_t totalNebulae = m_retainedNebulae.size() + m_nebulae.size();
        if (totalNebulae > 0) {
            NebulaCommand* nb = allocator.allocateArray<NebulaCommand>(totalNebulae);
            if (nb) {
                size_t idx = 0;
                for (const auto& kv : m_retainedNebulae) nb[idx++] = kv.second;
                if (!m_nebulae.empty()) {
                    std::memcpy(&nb[idx], m_nebulae.data(), m_nebulae.size() * sizeof(NebulaCommand));
                }
                packet.nebulae = nb;
                packet.nebulaCount = totalNebulae;
            }
        } else {
            packet.nebulae = nullptr;
            packet.nebulaCount = 0;
        }
    }

    // Copy fog volumes: RETAINED (a nebula, which does not move) then EPHEMERAL. Same
    // no-array-when-unused rule as the rest; retained ones are derived here from the author's
    // numbers so a partial update naming only one field stays coherent.
    {
        const size_t totalFogs = m_retainedFogs.size() + m_fogs.size();
        if (totalFogs > 0) {
            FogCommand* fg = allocator.allocateArray<FogCommand>(totalFogs);
            if (fg) {
                size_t idx = 0;
                for (const auto& kv : m_retainedFogs) fg[idx++] = buildFog(kv.second);
                if (!m_fogs.empty()) {
                    std::memcpy(&fg[idx], m_fogs.data(), m_fogs.size() * sizeof(FogCommand));
                }
                packet.fogs = fg;
                packet.fogCount = totalFogs;
            }
        } else {
            packet.fogs = nullptr;
            packet.fogCount = 0;
        }
    }

    // Lumieres et particules : ephemeres, aucune contrainte d'ordre (l'eclairage est une
    // somme, les particules sont melangees en additif — deux operations commutatives).
    packEphemeral(allocator, m_lights, packet.lights, packet.lightCount);
    packEphemeral(allocator, m_particles, packet.particles, packet.particleCount);

    // Primitives de debogage : ephemeres, dessinees par-dessus, sans ordre significatif.
    packEphemeral(allocator, m_debugLines, packet.debugLines, packet.debugLineCount);
    packEphemeral(allocator, m_debugRects, packet.debugRects, packet.debugRectCount);

    // Secteurs (parts de disque), ephemeres. Bucket MONDE ici, trie par couche ; le bucket
    // HUD est traite plus bas avec les autres primitives en espace ecran.
    packEphemeralSortedByLayer(allocator, m_sectors, packet.sectors, packet.sectorCount);

    // HUD sprites (screen-space): retained widgets (m_retainedHudSprites) + ephemeral (m_hudSprites),
    // both drawn on the fixed m_hudView (camera-immune). Retained first (matches the world bucket order),
    // then a per-layer stable_sort so HUD z-order is deterministic too.
    {
        const size_t totalHud = m_retainedHudSprites.size() + m_hudSprites.size();
        if (totalHud > 0) {
            SpriteInstance* hud = allocator.allocateArray<SpriteInstance>(totalHud);
            if (hud) {
                size_t idx = 0;
                for (const auto& [renderId, sprite] : m_retainedHudSprites) hud[idx++] = sprite;
                if (!m_hudSprites.empty())
                    std::memcpy(&hud[idx], m_hudSprites.data(), m_hudSprites.size() * sizeof(SpriteInstance));
                std::stable_sort(hud, hud + totalHud,
                    [](const SpriteInstance& a, const SpriteInstance& b) { return a.layer < b.layer; });
                packet.hudSprites = hud;
                packet.hudSpriteCount = totalHud;
            }
        } else {
            packet.hudSprites = nullptr;
            packet.hudSpriteCount = 0;
        }
    }

    // HUD texts (screen-space): retained (m_retainedHudTexts, strings from m_retainedHudTextStrings) +
    // ephemeral (m_hudTexts, strings by index). Retained first, then a per-layer stable_sort. Strings are
    // strdup'd into the frame arena (same as the world-text path).
    {
        const size_t totalHud = m_retainedHudTexts.size() + m_hudTexts.size();
        if (totalHud > 0) {
            TextCommand* hud = allocator.allocateArray<TextCommand>(totalHud);
            if (hud) {
                size_t idx = 0;
                for (const auto& [renderId, textCmd] : m_retainedHudTexts) {
                    hud[idx] = textCmd;
                    auto strIt = m_retainedHudTextStrings.find(renderId);
                    if (strIt != m_retainedHudTextStrings.end() && !strIt->second.empty()) {
                        const std::string& str = strIt->second;
                        char* textCopy = static_cast<char*>(allocator.allocate(str.size() + 1, 1));
                        if (textCopy) { std::memcpy(textCopy, str.c_str(), str.size() + 1); hud[idx].text = textCopy; }
                    }
                    idx++;
                }
                for (size_t i = 0; i < m_hudTexts.size(); ++i) {
                    hud[idx] = m_hudTexts[i];
                    if (i < m_hudTextStrings.size() && !m_hudTextStrings[i].empty()) {
                        const std::string& str = m_hudTextStrings[i];
                        char* textCopy = static_cast<char*>(allocator.allocate(str.size() + 1, 1));
                        if (textCopy) { std::memcpy(textCopy, str.c_str(), str.size() + 1); hud[idx].text = textCopy; }
                    }
                    idx++;
                }
                std::stable_sort(hud, hud + totalHud,
                    [](const TextCommand& a, const TextCommand& b) { return a.layer < b.layer; });
                packet.hudTexts = hud;
                packet.hudTextCount = totalHud;
            }
        } else {
            packet.hudTexts = nullptr;
            packet.hudTextCount = 0;
        }
    }

    // Secteurs HUD (parts de disque en espace ecran, ex. la roue d'action). Meme tri par
    // couche que le bucket monde, et c'est voulu : le z-order de l'interface doit etre
    // deterministe au meme titre que celui de la scene.
    packEphemeralSortedByLayer(allocator, m_hudSectors, packet.hudSectors, packet.hudSectorCount);

    packet.hudView = m_hudView;

    return packet;
}

void SceneCollector::addSpritesBulk(const SpriteInstance* data, size_t count) {
    // One bulk insert — no per-sprite JSON parse, no IIO. The instances are already in the
    // exact GPU layout finalize() expects, so this is the whole cost of feeding `count` sprites.
    if (data && count) m_sprites.insert(m_sprites.end(), data, data + count);
}

void SceneCollector::addParticlesBulk(const ParticleInstance* data, size_t count) {
    // Same as addSpritesBulk: ParticleInstance is already the POD ParticlePass consumes -> one insert,
    // no per-particle JSON/IIO. Merges with any render:particle from this frame (same ephemeral list).
    if (data && count) m_particles.insert(m_particles.end(), data, data + count);
}

void SceneCollector::addTextsBulk(const TextCommand* items, size_t count) {
    // N labels in one call. The command + its string must stay INDEX-ALIGNED across m_texts /
    // m_textStrings (finalize pairs them by index and strdups the string into the frame arena). We
    // COPY each caller string now (its buffer needn't survive) and null the pointer — set in finalize.
    if (!items || !count) return;
    for (size_t i = 0; i < count; ++i) {
        TextCommand t = items[i];
        m_textStrings.emplace_back(t.text ? t.text : "");   // own the string until finalize
        t.text = nullptr;                                   // pointer set in finalize (index-aligned)
        m_texts.push_back(t);
    }
}

void SceneCollector::clear() {
    m_sprites.clear();
    m_tilemaps.clear();
    m_tilemapTiles.clear();
    m_texts.clear();
    m_textStrings.clear();
    m_occluders.clear();
    m_filters.clear();
    m_fogs.clear();
    m_nebulae.clear();
    m_lights.clear();
    m_particles.clear();
    m_debugLines.clear();
    m_debugRects.clear();
    m_sectors.clear();
    m_hudSprites.clear();
    m_hudTexts.clear();
    m_hudTextStrings.clear();
    m_hudSectors.clear();
}

// ============================================================================
// Message Parsing
// ============================================================================

// Read a CENTER coordinate under the anchor convention (docs/design/render-anchor-convention.md):
// prefer the canonical `cx`/`cy`; accept legacy `x`/`y` (same center semantics) during the additive
// migration phase; else `fallback` (0 for a fresh instance, the current value for an update).
static float centerCoord(const IDataNode& d, const char* cxKey, const char* xKey, float fallback) {
    if (d.hasProperty(cxKey)) return static_cast<float>(d.getDouble(cxKey, fallback));
    if (d.hasProperty(xKey))  return static_cast<float>(d.getDouble(xKey, fallback));
    return fallback;
}

// Reject the RETIRED legacy anchor on a center primitive: x/y present WITHOUT cx/cy is the old center
// convention (docs/design/render-anchor-convention.md). Returns true -> the caller DROPS the primitive
// (echec franc: a silent half-footprint shift is exactly the footgun this convention kills). Logs ONCE
// per topic via `warned` -- parseSprite/parseSpriteAdd are the render hot path, so never log per-call.
static bool rejectsLegacyAnchor(const IDataNode& d, const char* topic, bool& warned) {
    if ((d.hasProperty("x") || d.hasProperty("y")) && !(d.hasProperty("cx") || d.hasProperty("cy"))) {
        if (!warned) {
            warned = true;
            spdlog::error("[SceneCollector] {}: 'x,y' is retired - use 'cx,cy' (center). "
                          "See docs/design/render-anchor-convention.md. Primitive dropped.", topic);
        }
        return true;
    }
    return false;
}

namespace {
// Optional `blend` field on a sprite: "additive" turns the quad into a glowing one, anything else
// (including absent) keeps the historical ALPHA behaviour bit for bit. Parsed as a STRING rather
// than a bool so a future "multiply" needs no new field and no migration.
inline float parseSpriteBlend(const IDataNode& data) {
    const std::string mode = data.getString("blend", "");
    return (mode == "additive") ? 1.0f : 0.0f;
}
} // namespace

void SceneCollector::parseSprite(const IDataNode& data) {
    static bool warnedLegacy = false;
    if (rejectsLegacyAnchor(data, "render:sprite", warnedLegacy)) return;
    SpriteInstance sprite;
    // i_data0 -- cx,cy = CENTER (anchor convention).
    sprite.x = centerCoord(data, "cx", "x", 0.0f);
    sprite.y = centerCoord(data, "cy", "y", 0.0f);
    sprite.scaleX = static_cast<float>(data.getDouble("scaleX", 1.0));
    sprite.scaleY = static_cast<float>(data.getDouble("scaleY", 1.0));

    // i_data1
    sprite.rotation = static_cast<float>(data.getDouble("rotation", 0.0));
    sprite.u0 = static_cast<float>(data.getDouble("u0", 0.0));
    sprite.v0 = static_cast<float>(data.getDouble("v0", 0.0));
    sprite.u1 = static_cast<float>(data.getDouble("u1", 1.0));
    // i_data2
    sprite.v1 = static_cast<float>(data.getDouble("v1", 1.0));
    applySpriteFlip(data, sprite);   // optional mirror; absent -> UVs untouched
    sprite.textureId = static_cast<float>(resolveSpriteTexture(data, sprite));
    sprite.layer = static_cast<float>(data.getInt("layer", 0));
    sprite.padding0 = parseSpriteBlend(data);   // 0 = alpha (default), 1 = additive
    // i_data3 (reserved)
    sprite.reserved[0] = 0.0f;
    sprite.reserved[1] = 0.0f;
    sprite.reserved[2] = 0.0f;
    sprite.reserved[3] = 0.0f;
    // i_data4 (color as floats)
    uint32_t color = static_cast<uint32_t>(data.getInt("color", 0xFFFFFFFF));
    sprite.r = static_cast<float>((color >> 24) & 0xFF) / 255.0f;
    sprite.g = static_cast<float>((color >> 16) & 0xFF) / 255.0f;
    sprite.b = static_cast<float>((color >> 8) & 0xFF) / 255.0f;
    sprite.a = static_cast<float>(color & 0xFF) / 255.0f;

    // Route to the HUD bucket if screen-space, else the world bucket (default).
    (isScreenSpace(data) ? m_hudSprites : m_sprites).push_back(sprite);
}

void SceneCollector::parseRect(const IDataNode& data) {
    // QUOI : un rectangle plein coloré, posé comme un sprite teinté (texture 0 = blanc).
    // POURQUOI : donne aux jeux HUD-lourds (Drifterra) un rect qui RESPECTE le `layer`
    //   et passe par le SpritePass (avant le texte) — contrairement à render:debug:rect
    //   (dernière passe, toujours au-dessus, sans layer). Cf. aide moteur A2.
    // COMMENT : coords coin haut-gauche (x,y,w,h) comme debug:rect ; on les centre pour
    //   le sprite (x+w/2, y+h/2), scale = (w,h), textureId=0, UV plein quad, couleur teintée.
    SpriteInstance sprite;
    const float x = static_cast<float>(data.getDouble("x", 0.0));
    const float y = static_cast<float>(data.getDouble("y", 0.0));
    const float w = static_cast<float>(data.getDouble("w", 0.0));
    const float h = static_cast<float>(data.getDouble("h", 0.0));

    sprite.x = x + w * 0.5f;   // top-left -> center (same convention as render:sprite)
    sprite.y = y + h * 0.5f;
    sprite.scaleX = w;
    sprite.scaleY = h;

    sprite.rotation = 0.0f;
    sprite.u0 = 0.0f; sprite.v0 = 0.0f; sprite.u1 = 1.0f; sprite.v1 = 1.0f;  // full white texel
    sprite.textureId = 0.0f;  // 0 => default white texture in SpritePass => solid color
    sprite.layer = static_cast<float>(data.getInt("layer", 0));
    sprite.padding0 = 0.0f;
    sprite.reserved[0] = 0.0f;
    sprite.reserved[1] = 0.0f;
    sprite.reserved[2] = 0.0f;
    sprite.reserved[3] = 0.0f;

    const uint32_t color = static_cast<uint32_t>(data.getInt("color", 0xFFFFFFFF));
    sprite.r = static_cast<float>((color >> 24) & 0xFF) / 255.0f;
    sprite.g = static_cast<float>((color >> 16) & 0xFF) / 255.0f;
    sprite.b = static_cast<float>((color >> 8) & 0xFF) / 255.0f;
    sprite.a = static_cast<float>(color & 0xFF) / 255.0f;

    // HUD rect (space:"screen") → fixed overlay bucket; else world (zoomable). Cf. vue HUD.
    (isScreenSpace(data) ? m_hudSprites : m_sprites).push_back(sprite);
}

void SceneCollector::parseSpriteBatch(const IDataNode& data) {
    // FAST PATH -- packed float blob "spriteData", stride 8 per sprite (x, y, scaleX, scaleY, rotation,
    //   textureId, layer, colorBits[uint32 reinterpreted]). ZERO node per sprite -> no alloc / no JSON
    //   materialisation: O(N) memcpy. The bulk PERF path (thousands of sprites/frame). Two sources, in order:
    //   (1) a first-class BINARY blob (getBlob -- raw bytes BESIDE the json: no UTF-8 abuse, replay-safe);
    //   (2) LEGACY -- the same bytes smuggled in a JSON string (getString), kept for back-compat.
    //   textureId 0 => tinted white quad (solid); colour = 0xRRGGBBAA; full-quad UVs.
    const uint8_t* bytes = nullptr;
    size_t byteLen = 0;
    std::string legacy;                                             // owns the bytes IFF the string path is taken
    if (const std::vector<uint8_t>* blob = data.getBlob("spriteData")) {
        bytes = blob->data(); byteLen = blob->size();
    } else {
        legacy = data.getString("spriteData", "");
        bytes = reinterpret_cast<const uint8_t*>(legacy.data()); byteLen = legacy.size();
    }
    if (byteLen > 0) {
        constexpr size_t STRIDE = 8;
        const size_t count = byteLen / (STRIDE * sizeof(float));
        std::vector<float> f(count * STRIDE);                       // ALIGNED copy (blob not guaranteed 4-byte aligned)
        std::memcpy(f.data(), bytes, count * STRIDE * sizeof(float));
        auto& bucket = isScreenSpace(data) ? m_hudSprites : m_sprites;
        bucket.reserve(bucket.size() + count);
        for (size_t i = 0; i < count; ++i) {
            const float* s = f.data() + i * STRIDE;
            SpriteInstance sp;
            sp.x = s[0]; sp.y = s[1]; sp.scaleX = s[2]; sp.scaleY = s[3];
            sp.rotation = s[4]; sp.u0 = 0.0f; sp.v0 = 0.0f; sp.u1 = 1.0f; sp.v1 = 1.0f;
            sp.textureId = s[5]; sp.layer = s[6]; sp.padding0 = 0.0f;
            sp.reserved[0] = sp.reserved[1] = sp.reserved[2] = sp.reserved[3] = 0.0f;
            uint32_t color; std::memcpy(&color, &s[7], sizeof(uint32_t));   // bits, pas cast numérique
            sp.r = static_cast<float>((color >> 24) & 0xFF) / 255.0f;
            sp.g = static_cast<float>((color >> 16) & 0xFF) / 255.0f;
            sp.b = static_cast<float>((color >> 8) & 0xFF) / 255.0f;
            sp.a = static_cast<float>(color & 0xFF) / 255.0f;
            bucket.push_back(sp);
        }
        return;
    }

    // FALLBACK -- child-nodes : un sous-nœud "sprites/<i>" par sprite (compat ; plus lourd que le blob).
    IDataNode* spritesNode = const_cast<IDataNode&>(data).getChildReadOnly("sprites");
    if (!spritesNode) return;
    for (const auto& name : spritesNode->getChildNames()) {
        IDataNode* spriteData = spritesNode->getChildReadOnly(name);
        if (spriteData) parseSprite(*spriteData);
    }
}

// Read chunk metadata (position, dims, tileset) from a data node. Tiles are handled separately.
static void readTilemapMeta(const IDataNode& data, TilemapChunk& chunk) {
    chunk.x = static_cast<float>(data.getDouble("x", 0.0));
    chunk.y = static_cast<float>(data.getDouble("y", 0.0));
    chunk.width = static_cast<uint16_t>(data.getInt("width", 0));
    chunk.height = static_cast<uint16_t>(data.getInt("height", 0));
    chunk.tileWidth = static_cast<uint16_t>(data.getInt("tileW", 16));
    chunk.tileHeight = static_cast<uint16_t>(data.getInt("tileH", 16));
    chunk.textureId = static_cast<uint16_t>(data.getInt("textureId", 0));
}

// Parse per-tile fog visibility (0..255) from a comma-separated "fogData" string. Empty = no fog.
static std::vector<uint8_t> parseFogData(const IDataNode& data) {
    std::vector<uint8_t> fog;
    std::string s = data.getString("fogData", "");
    size_t pos = 0;
    while (pos < s.size()) {
        size_t end = s.find(',', pos);
        if (end == std::string::npos) end = s.size();
        std::string n = s.substr(pos, end - pos);
        if (!n.empty()) fog.push_back(static_cast<uint8_t>(std::stoi(n)));
        pos = end + 1;
    }
    return fog;
}

// Shared tile-array parser: "tiles" child node (each child has int "v") OR a comma-separated
// "tileData" string. Used by both the ephemeral and retained tilemap paths.
std::vector<uint16_t> SceneCollector::parseTileArray(const IDataNode& data) {
    std::vector<uint16_t> tiles;

    IDataNode* tilesNode = const_cast<IDataNode&>(data).getChildReadOnly("tiles");
    if (tilesNode) {
        for (const auto& name : tilesNode->getChildNames()) {
            IDataNode* tileNode = tilesNode->getChildReadOnly(name);
            if (tileNode) {
                tiles.push_back(static_cast<uint16_t>(tileNode->getInt("v", 0)));
            }
        }
    }

    if (tiles.empty()) {
        std::string tileData = data.getString("tileData", "");
        size_t pos = 0;
        while (pos < tileData.size()) {
            size_t end = tileData.find(',', pos);
            if (end == std::string::npos) end = tileData.size();
            std::string numStr = tileData.substr(pos, end - pos);
            if (!numStr.empty()) {
                tiles.push_back(static_cast<uint16_t>(std::stoi(numStr)));
            }
            pos = end + 1;
        }
    }

    return tiles;
}

void SceneCollector::parseTilemap(const IDataNode& data) {
    TilemapChunk chunk{};
    readTilemapMeta(data, chunk);
    chunk.id = 0;        // ephemeral: re-sent every frame, always uploaded
    chunk.dirty = true;

    // Store tiles - pointer will be fixed in finalize
    m_tilemapTiles.push_back(parseTileArray(data));
    chunk.tiles = nullptr;
    chunk.tileCount = 0;

    m_tilemaps.push_back(chunk);
}

void SceneCollector::parseTilemapAdd(const IDataNode& data) {
    const uint32_t id = static_cast<uint32_t>(data.getInt("id", 0));
    if (id == 0) {
        return;  // retained chunks require a non-zero id (id 0 is the ephemeral sentinel)
    }
    RetainedTilemap rt;
    readTilemapMeta(data, rt.chunk);
    rt.chunk.id = id;
    rt.chunk.dirty = true;            // upload on the next finalize
    rt.tiles = parseTileArray(data);
    rt.fog = parseFogData(data);      // optional per-tile visibility (empty = no fog)
    // Multi-layer (Strategy A): a "layers" array of {tileData/tiles, textureId?}, read BY INDEX (order =
    // compositing order, 0 = base). layer 0 also drives the legacy `tiles`/textureId/LOD path.
    IDataNode* layersNode = const_cast<IDataNode&>(data).getChildReadOnly("layers");
    if (layersNode) {
        for (int i = 0; ; ++i) {
            IDataNode* L = layersNode->getChildReadOnly(std::to_string(i));
            if (!L) break;
            rt.layerTiles.push_back(parseTileArray(*L));
            rt.layerTexIds.push_back(static_cast<uint16_t>(L->getInt("textureId", 0)));
        }
    }
    if (!rt.layerTiles.empty()) {
        rt.tiles = rt.layerTiles[0];              // layer 0 = the legacy single grid
        rt.chunk.textureId = rt.layerTexIds[0];
    }
    rt.chunk.tiles = nullptr;         // pointers fixed in finalize
    rt.chunk.tileCount = rt.tiles.size();
    m_retainedTilemaps[id] = std::move(rt);
}

// Grow a chunk's dirty rect to also cover [x,y,w,h] (Slice A4.2). A pending full-grid upload
// (dirty && dirtyW==0) subsumes any patch; a clean chunk takes the rect as-is; otherwise union.
static void unionDirtyRect(TilemapChunk& c, int x, int y, int w, int h) {
    if (!c.dirty) {
        c.dirtyX = static_cast<uint16_t>(x); c.dirtyY = static_cast<uint16_t>(y);
        c.dirtyW = static_cast<uint16_t>(w); c.dirtyH = static_cast<uint16_t>(h);
        return;
    }
    if (c.dirtyW == 0) return;  // full-grid upload already pending — it covers everything
    const int x0 = (c.dirtyX < x) ? c.dirtyX : x;
    const int y0 = (c.dirtyY < y) ? c.dirtyY : y;
    const int x1 = (c.dirtyX + c.dirtyW > x + w) ? c.dirtyX + c.dirtyW : x + w;
    const int y1 = (c.dirtyY + c.dirtyH > y + h) ? c.dirtyY + c.dirtyH : y + h;
    c.dirtyX = static_cast<uint16_t>(x0); c.dirtyY = static_cast<uint16_t>(y0);
    c.dirtyW = static_cast<uint16_t>(x1 - x0); c.dirtyH = static_cast<uint16_t>(y1 - y0);
}

void SceneCollector::parseTilemapUpdate(const IDataNode& data) {
    const uint32_t id = static_cast<uint32_t>(data.getInt("id", 0));
    auto it = m_retainedTilemaps.find(id);
    if (id == 0 || it == m_retainedTilemaps.end()) {
        parseTilemapAdd(data);        // unknown id -> treat as add (mirrors sprite update)
        return;
    }
    RetainedTilemap& rt = it->second;

    const int w = data.getInt("w", 0);
    const int h = data.getInt("h", 0);
    if (w > 0 && h > 0) {
        // PARTIAL patch (Slice A4.2): write a w*h block of ids at (x,y) into the stored grid, and
        // grow the dirty rect — only those texels get re-uploaded, not the whole grid.
        const int x = data.getInt("x", 0);
        const int y = data.getInt("y", 0);
        const std::vector<uint16_t> patch = parseTileArray(data);   // w*h tile ids, row-major
        const int gw = rt.chunk.width;
        for (int ty = 0; ty < h; ++ty) {
            for (int tx = 0; tx < w; ++tx) {
                const size_t pi = static_cast<size_t>(ty) * w + tx;
                const size_t gi = static_cast<size_t>(y + ty) * gw + (x + tx);
                if (pi < patch.size() && gi < rt.tiles.size()) {
                    rt.tiles[gi] = patch[pi];
                }
            }
        }
        unionDirtyRect(rt.chunk, x, y, w, h);
        rt.chunk.dirty = true;
    } else {
        // FULL replace (same geometry — to change dims, remove + add). dirtyW=0 => full upload.
        rt.tiles = parseTileArray(data);
        std::vector<uint8_t> f = parseFogData(data);
        if (!f.empty()) rt.fog = std::move(f);   // replace fog only if provided
        rt.chunk.tileCount = rt.tiles.size();
        rt.chunk.dirty = true;
        rt.chunk.dirtyW = 0;
        rt.chunk.dirtyH = 0;
    }
}

void SceneCollector::parseTilemapRemove(const IDataNode& data) {
    const uint32_t id = static_cast<uint32_t>(data.getInt("id", 0));
    m_retainedTilemaps.erase(id);
}

// Fog-ONLY partial reveal: render:tilemap:fog {id, x, y, w, h, fogData} patches a w*h visibility block at
// (x,y) into the retained chunk's fog grid and grows the FOG dirty rect — WITHOUT touching the tile `dirty`,
// so the pass region-updates only that fog sub-rect (mip 0) and never re-uploads tiles or re-bakes the LOD.
void SceneCollector::parseTilemapFog(const IDataNode& data) {
    const uint32_t id = static_cast<uint32_t>(data.getInt("id", 0));
    auto it = m_retainedTilemaps.find(id);
    if (id == 0 || it == m_retainedTilemaps.end()) return;   // a reveal targets an existing retained chunk
    RetainedTilemap& rt = it->second;

    const int w = data.getInt("w", 0);
    const int h = data.getInt("h", 0);
    if (w <= 0 || h <= 0) return;
    const int x = data.getInt("x", 0);
    const int y = data.getInt("y", 0);
    const std::vector<uint8_t> patch = parseFogData(data);   // w*h visibility bytes, row-major
    if (patch.empty()) return;

    const int gw = rt.chunk.width, gh = rt.chunk.height;
    // First fog on this chunk -> start fully VISIBLE (255) so the patch only reveals/hides its own rect.
    if (rt.fog.empty()) rt.fog.assign(static_cast<size_t>(gw) * gh, 255);
    for (int ty = 0; ty < h; ++ty) {
        for (int tx = 0; tx < w; ++tx) {
            const size_t pi = static_cast<size_t>(ty) * w + tx;
            const size_t gi = static_cast<size_t>(y + ty) * gw + (x + tx);
            if (pi < patch.size() && gi < rt.fog.size()) rt.fog[gi] = patch[pi];
        }
    }

    // Union the FOG dirty rect (independent of the tile dirty rect).
    TilemapChunk& c = rt.chunk;
    if (!c.fogDirty) {
        c.fogDirtyX = static_cast<uint16_t>(x); c.fogDirtyY = static_cast<uint16_t>(y);
        c.fogDirtyW = static_cast<uint16_t>(w); c.fogDirtyH = static_cast<uint16_t>(h);
    } else {
        const int x0 = (c.fogDirtyX < x) ? c.fogDirtyX : x;
        const int y0 = (c.fogDirtyY < y) ? c.fogDirtyY : y;
        const int x1 = (c.fogDirtyX + c.fogDirtyW > x + w) ? c.fogDirtyX + c.fogDirtyW : x + w;
        const int y1 = (c.fogDirtyY + c.fogDirtyH > y + h) ? c.fogDirtyY + c.fogDirtyH : y + h;
        c.fogDirtyX = static_cast<uint16_t>(x0); c.fogDirtyY = static_cast<uint16_t>(y0);
        c.fogDirtyW = static_cast<uint16_t>(x1 - x0); c.fogDirtyH = static_cast<uint16_t>(y1 - y0);
    }
    c.fogDirty = true;
}

void SceneCollector::parseText(const IDataNode& data) {
    TextCommand text;
    text.x = static_cast<float>(data.getDouble("x", 0.0));
    text.y = static_cast<float>(data.getDouble("y", 0.0));
    text.fontId = static_cast<uint16_t>(data.getInt("fontId", 0));
    text.fontSize = static_cast<uint16_t>(data.getInt("fontSize", 16));
    text.color = static_cast<uint32_t>(data.getInt("color", 0xFFFFFFFF));
    text.layer = static_cast<uint16_t>(data.getInt("layer", 0));
    text.align = static_cast<uint8_t>(data.getInt("align", 0));           // 0 left / 1 center / 2 right
    text.bold  = data.getBool("bold", false) ? 1 : 0;

    // Store text string - pointer will be fixed up in finalize()
    std::string textStr = data.getString("text", "");
    text.text = nullptr;  // Will be set in finalize()

    // HUD text (space:"screen") goes to the fixed overlay bucket; the string and the command
    // must stay index-aligned in their respective vectors (finalize pairs them by index).
    if (isScreenSpace(data)) {
        m_hudTextStrings.push_back(std::move(textStr));
        m_hudTexts.push_back(text);
    } else {
        m_textStrings.push_back(std::move(textStr));
        m_texts.push_back(text);
    }
}

void SceneCollector::parseParticle(const IDataNode& data) {
    static bool warnedLegacy = false;
    if (rejectsLegacyAnchor(data, "render:particle", warnedLegacy)) return;
    ParticleInstance particle;
    particle.x = centerCoord(data, "cx", "x", 0.0f);   // cx,cy = CENTER (anchor convention)
    particle.y = centerCoord(data, "cy", "y", 0.0f);
    particle.vx = static_cast<float>(data.getDouble("vx", 0.0));
    particle.vy = static_cast<float>(data.getDouble("vy", 0.0));
    particle.size = static_cast<float>(data.getDouble("size", 1.0));
    particle.life = static_cast<float>(data.getDouble("life", 1.0));
    particle.color = static_cast<uint32_t>(data.getInt("color", 0xFFFFFFFF));
    particle.textureId = static_cast<uint16_t>(data.getInt("textureId", 0));

    m_particles.push_back(particle);
}

void SceneCollector::parseCamera(const IDataNode& data) {
    m_mainView.positionX = static_cast<float>(data.getDouble("x", 0.0));
    m_mainView.positionY = static_cast<float>(data.getDouble("y", 0.0));
    m_mainView.zoom = static_cast<float>(data.getDouble("zoom", 1.0));
    m_mainView.viewportX = static_cast<uint16_t>(data.getInt("viewportX", 0));
    m_mainView.viewportY = static_cast<uint16_t>(data.getInt("viewportY", 0));
    m_mainView.viewportW = static_cast<uint16_t>(data.getInt("viewportW", 1280));
    m_mainView.viewportH = static_cast<uint16_t>(data.getInt("viewportH", 720));
    m_mainView.rotation = static_cast<float>(data.getDouble("rotation", 0.0));

    // Compute the view matrix. QUOI : translate la caméra à l'origine ET tourne le monde autour du
    //   PIVOT centre-écran par `rotation` (la caméra peut "rouler"). POURQUOI : une caméra qui suit
    //   le cap d'une entité doit faire pivoter le monde autour du point montré au centre, pas autour
    //   du coin haut-gauche. COMMENT : eye' = R*(world - pivot) + eyeCentre, avec pivot = le point
    //   monde au centre écran = (x + vpW/(2*zoom), y + vpH/(2*zoom)) et eyeCentre = (vpW/(2*zoom),
    //   vpH/(2*zoom)). À rotation 0 ça retombe sur l'ancien translate(-x,-y) (verrouillé par
    //   SceneCollectorTest). Column-major ; R = [[c,-s],[s,c]].
    {
        const float c = std::cos(m_mainView.rotation);
        const float s = std::sin(m_mainView.rotation);
        const float halfW = static_cast<float>(m_mainView.viewportW) / (2.0f * m_mainView.zoom);
        const float halfH = static_cast<float>(m_mainView.viewportH) / (2.0f * m_mainView.zoom);
        const float pivotX = m_mainView.positionX + halfW;
        const float pivotY = m_mainView.positionY + halfH;
        std::memset(m_mainView.viewMatrix, 0, sizeof(m_mainView.viewMatrix));
        m_mainView.viewMatrix[0] = c;    m_mainView.viewMatrix[1] = s;     // col0 = (R00, R10)
        m_mainView.viewMatrix[4] = -s;   m_mainView.viewMatrix[5] = c;     // col1 = (R01, R11)
        m_mainView.viewMatrix[10] = 1.0f;
        m_mainView.viewMatrix[12] = halfW - (c * pivotX - s * pivotY);     // eyeCentre.x - R*pivot
        m_mainView.viewMatrix[13] = halfH - (s * pivotX + c * pivotY);     // eyeCentre.y - R*pivot
        m_mainView.viewMatrix[15] = 1.0f;
    }

    // Compute orthographic projection matrix with zoom
    float width = static_cast<float>(m_mainView.viewportW) / m_mainView.zoom;
    float height = static_cast<float>(m_mainView.viewportH) / m_mainView.zoom;

    std::memset(m_mainView.projMatrix, 0, sizeof(m_mainView.projMatrix));
    m_mainView.projMatrix[0] = 2.0f / width;
    m_mainView.projMatrix[5] = -2.0f / height;  // Y-flip for top-left origin
    m_mainView.projMatrix[10] = 1.0f;
    m_mainView.projMatrix[12] = -1.0f;
    m_mainView.projMatrix[13] = 1.0f;
    m_mainView.projMatrix[15] = 1.0f;

    // Keep the HUD overlay spanning the live viewport, but NEVER inherit the camera's zoom or
    // pan — that invariance is the whole point of the screen-space view.
    buildScreenSpaceView(m_hudView, m_mainView.viewportW, m_mainView.viewportH);
}

void SceneCollector::parseClear(const IDataNode& data) {
    m_clearColor = static_cast<uint32_t>(data.getInt("color", 0x303030FF));
}

void SceneCollector::parseOccluderAdd(const IDataNode& data) {
    const uint32_t renderId = static_cast<uint32_t>(data.getInt("renderId", 0));
    // 0 is the "no id" value: accepting it would give every unidentified wall the SAME slot, each
    // add silently replacing the last. Same guard as every other retained primitive.
    if (renderId == 0) return;

    OccluderCommand o;
    o.x = static_cast<float>(data.getDouble("x", 0.0));
    o.y = static_cast<float>(data.getDouble("y", 0.0));
    o.w = static_cast<float>(data.getDouble("w", 0.0));
    o.h = static_cast<float>(data.getDouble("h", 0.0));
    if (o.w <= 0.0f || o.h <= 0.0f) return;   // degenerate, same rule as the ephemeral path

    m_retainedOccluders[renderId] = o;
}

void SceneCollector::parseOccluderUpdate(const IDataNode& data) {
    const uint32_t renderId = static_cast<uint32_t>(data.getInt("renderId", 0));
    if (renderId == 0) return;

    auto it = m_retainedOccluders.find(renderId);
    if (it == m_retainedOccluders.end()) return;   // updating something absent is a no-op, not an add

    // PARTIAL merge: each field keeps its current value when the message omits it. A sliding door
    // must be able to move without restating its extent -- and an update that reset the omitted
    // fields to zero would DELETE the wall while looking like a move.
    OccluderCommand& o = it->second;
    o.x = static_cast<float>(data.getDouble("x", static_cast<double>(o.x)));
    o.y = static_cast<float>(data.getDouble("y", static_cast<double>(o.y)));
    o.w = static_cast<float>(data.getDouble("w", static_cast<double>(o.w)));
    o.h = static_cast<float>(data.getDouble("h", static_cast<double>(o.h)));
}

void SceneCollector::parseOccluderRemove(const IDataNode& data) {
    const uint32_t renderId = static_cast<uint32_t>(data.getInt("renderId", 0));
    if (renderId == 0) return;
    // A destroyed wall must stop casting its shadow -- leaving it would be the mirror of the
    // orphaned-sprite hazard the FX layer had to solve at hot-reload.
    m_retainedOccluders.erase(renderId);
}

void SceneCollector::parseOccluder(const IDataNode& data) {
    OccluderCommand o;
    // x,y = top-left CORNER: a rect's anchor is its corner, a light's is its centre, and the field
    // NAME is what says which - no guessing at the call site.
    o.x = static_cast<float>(data.getDouble("x", 0.0));
    o.y = static_cast<float>(data.getDouble("y", 0.0));
    o.w = static_cast<float>(data.getDouble("w", 0.0));
    o.h = static_cast<float>(data.getDouble("h", 0.0));

    // A degenerate rect occludes nothing. Dropping it here spares a quad per frame that draws
    // nothing, and stops a caller's uninitialised struct from reaching the GPU.
    if (o.w > 0.0f && o.h > 0.0f) m_occluders.push_back(o);
}

// Author's tint -> the per-unit transmittance the packet carries. ONE implementation, shared by the
// ephemeral and retained paths so the two can never disagree on what a colour means.
FilterCommand SceneCollector::buildFilter(const RetainedFilter& src) {
    FilterCommand f;
    f.x = src.x; f.y = src.y; f.w = src.w; f.h = src.h;

    // `opacity` is the author's guard rail against a multiplicative model's brutality (three panes
    // at 0.3 leave 2.7% of the light). It blends the stated tint back towards vacuum, so 0 is a TRUE
    // no-op -- a filter dialled to nothing must leave the scene exactly as bright as no filter.
    const float o = src.opacity;
    const float tint[3] = {
        1.0f - o * (1.0f - src.tintR),
        1.0f - o * (1.0f - src.tintG),
        1.0f - o * (1.0f - src.tintB),
    };

    // THE conversion. `color` is the tint after ONE perpendicular crossing; the map stores per-unit
    // transmittance because it is shared with fog, where a longer traversal must absorb more. The
    // reference thickness is the pane's THIN axis: a window is crossed through its narrow side, and
    // a ray entering at an angle travels further inside it and comes out darker -- which is right.
    const float thickness = (src.w < src.h) ? src.w : src.h;
    f.r = grove::light::perUnitForTint(tint[0], thickness);
    f.g = grove::light::perUnitForTint(tint[1], thickness);
    f.b = grove::light::perUnitForTint(tint[2], thickness);
    return f;
}

// Reads x/y/w/h/color/opacity into a RetainedFilter, each field DEFAULTING TO ITS CURRENT VALUE.
// That default is what makes a partial update partial: a sliding pane must be able to move without
// restating its extent, and an update that reset the omitted fields would DELETE the window while
// looking like a move.
void SceneCollector::readFilterFields(const IDataNode& data, RetainedFilter& out) {
    out.x = static_cast<float>(data.getDouble("x", static_cast<double>(out.x)));
    out.y = static_cast<float>(data.getDouble("y", static_cast<double>(out.y)));
    out.w = static_cast<float>(data.getDouble("w", static_cast<double>(out.w)));
    out.h = static_cast<float>(data.getDouble("h", static_cast<double>(out.h)));

    // The colour's alpha byte is ignored on purpose: "how much of this tint applies" is `opacity`,
    // a separate knob, and reading it from two places would make one of them silently lose.
    if (data.hasProperty("color")) {
        const uint32_t c = static_cast<uint32_t>(data.getInt("color", static_cast<int>(0xFFFFFFFFu)));
        out.tintR = static_cast<float>((c >> 24) & 0xFF) / 255.0f;
        out.tintG = static_cast<float>((c >> 16) & 0xFF) / 255.0f;
        out.tintB = static_cast<float>((c >>  8) & 0xFF) / 255.0f;
    }
    out.opacity = std::min(1.0f, std::max(0.0f,
        static_cast<float>(data.getDouble("opacity", static_cast<double>(out.opacity)))));
}

void SceneCollector::parseFilter(const IDataNode& data) {
    RetainedFilter src;   // fresh: an ephemeral message states everything, so the defaults are vacuum
    readFilterFields(data, src);

    // A degenerate rect filters nothing -- AND its thin axis is 0, which is exactly the thickness
    // the conversion cannot invert. Dropping it here is what keeps that division safe.
    if (src.w <= 0.0f || src.h <= 0.0f) return;

    m_filters.push_back(buildFilter(src));
}

// Author's numbers -> the per-unit transmittance the packet carries. ONE implementation, shared by
// the ephemeral and retained paths.
FogCommand SceneCollector::buildFog(const RetainedFog& src) {
    FogCommand f;
    f.x = src.x; f.y = src.y; f.w = src.w; f.h = src.h;

    // `density` is the Beer-Lambert ALPHA, and `color` makes the absorption SELECTIVE (a channel with
    // a lower colour extinguishes faster) — sunsets and tinted nebulae. White is neutral.
    f.r = grove::light::fogPerUnit(src.density, src.tintR);
    f.g = grove::light::fogPerUnit(src.density, src.tintG);
    f.b = grove::light::fogPerUnit(src.density, src.tintB);
    f.scatter = src.scatter;
    return f;
}

// Each field DEFAULTS TO ITS CURRENT VALUE, which is what makes a partial update partial.
void SceneCollector::readFogFields(const IDataNode& data, RetainedFog& out) {
    out.x = static_cast<float>(data.getDouble("x", static_cast<double>(out.x)));
    out.y = static_cast<float>(data.getDouble("y", static_cast<double>(out.y)));
    out.w = static_cast<float>(data.getDouble("w", static_cast<double>(out.w)));
    out.h = static_cast<float>(data.getDouble("h", static_cast<double>(out.h)));

    // ⚠️ NOT called "opacity" on purpose: it is the Beer-Lambert alpha, it has no upper bound, and
    // doubling the distance travelled doubles its effect in the exponent. The name "opacity" would
    // guarantee someone sets it to 1 believing they had saturated it.
    out.density = static_cast<float>(data.getDouble("density", static_cast<double>(out.density)));

    if (data.hasProperty("color")) {
        const uint32_t c = static_cast<uint32_t>(data.getInt("color", static_cast<int>(0xFFFFFFFFu)));
        out.tintR = static_cast<float>((c >> 24) & 0xFF) / 255.0f;
        out.tintG = static_cast<float>((c >> 16) & 0xFF) / 255.0f;
        out.tintB = static_cast<float>((c >>  8) & 0xFF) / 255.0f;
    }

    // `scatter` (A2) is what makes the medium VISIBLE rather than merely darkening. Clamped to 0..1
    // because it is a FRACTION of the arriving light re-emitted towards the viewer, not a coefficient
    // like `density` — the two are deliberately different kinds of number.
    out.scatter = std::min(1.0f, std::max(0.0f,
        static_cast<float>(data.getDouble("scatter", static_cast<double>(out.scatter)))));
}

void SceneCollector::parseFog(const IDataNode& data) {
    RetainedFog src;   // fresh: an ephemeral message states everything
    readFogFields(data, src);
    // No extent, or no density: either way there is nothing to absorb, and nothing worth a quad.
    if (src.w <= 0.0f || src.h <= 0.0f || src.density <= 0.0f) return;
    m_fogs.push_back(buildFog(src));
}

// Merges the message's fields into `out`, each DEFAULTING TO ITS CURRENT VALUE. That default is
// what makes a partial update partial: a drifting cloud must move without restating its radius,
// density, colour or scattering, and an update that reset the omitted fields would DELETE it while
// looking like a move.
void SceneCollector::readNebulaFields(const IDataNode& data, NebulaCommand& out) {
    // cx,cy = CENTRE. Unlike the rect media beside it, this primitive is a DISC, and the field name
    // is what says so (render-anchor-convention.md).
    out.cx     = static_cast<float>(data.getDouble("cx", static_cast<double>(out.cx)));
    out.cy     = static_cast<float>(data.getDouble("cy", static_cast<double>(out.cy)));
    out.radius = static_cast<float>(data.getDouble("radius", static_cast<double>(out.radius)));

    // `density` is the PEAK Beer-Lambert alpha, reached at the core; it falls to exactly zero at the
    // rim. Same units as render:fog, so a value tuned on one transfers to the other.
    out.density = static_cast<float>(data.getDouble("density", static_cast<double>(out.density)));

    if (data.hasProperty("color")) {
        const uint32_t c = static_cast<uint32_t>(data.getInt("color", static_cast<int>(0xFFFFFFFFu)));
        out.r = static_cast<float>((c >> 24) & 0xFF) / 255.0f;
        out.g = static_cast<float>((c >> 16) & 0xFF) / 255.0f;
        out.b = static_cast<float>((c >>  8) & 0xFF) / 255.0f;
    }
    // NOT converted, unlike every other matter: the density varies across the volume, so the
    // Beer-Lambert conversion has to happen per pixel. The collector only validates.

    out.scatter = std::min(1.0f, std::max(0.0f,
        static_cast<float>(data.getDouble("scatter", static_cast<double>(out.scatter)))));
}

void SceneCollector::parseNebula(const IDataNode& data) {
    NebulaCommand n{};   // fresh: an ephemeral message states everything
    n.r = n.g = n.b = 1.0f;   // white = neutral, the default when no colour is named
    readNebulaFields(data, n);
    if (n.radius <= 0.0f || n.density <= 0.0f) return;   // nothing to absorb
    m_nebulae.push_back(n);
}

void SceneCollector::parseNebulaAdd(const IDataNode& data) {
    const uint32_t renderId = static_cast<uint32_t>(data.getInt("renderId", 0));
    // 0 is the "no id" value: accepting it would give every unidentified volume the SAME slot, each
    // add silently replacing the last. Same guard as every other retained primitive.
    if (renderId == 0) return;

    NebulaCommand n{};
    n.r = n.g = n.b = 1.0f;
    readNebulaFields(data, n);
    if (n.radius <= 0.0f || n.density <= 0.0f) return;

    m_retainedNebulae[renderId] = n;
}

void SceneCollector::parseNebulaUpdate(const IDataNode& data) {
    const uint32_t renderId = static_cast<uint32_t>(data.getInt("renderId", 0));
    if (renderId == 0) return;

    auto it = m_retainedNebulae.find(renderId);
    if (it == m_retainedNebulae.end()) return;   // updating something absent is a no-op, not an add

    NebulaCommand merged = it->second;
    readNebulaFields(data, merged);
    if (merged.radius <= 0.0f || merged.density <= 0.0f) return;   // a shrink to nothing is refused
    it->second = merged;
}

void SceneCollector::parseNebulaRemove(const IDataNode& data) {
    const uint32_t renderId = static_cast<uint32_t>(data.getInt("renderId", 0));
    if (renderId == 0) return;
    m_retainedNebulae.erase(renderId);
}

void SceneCollector::parseFogAdd(const IDataNode& data) {
    const uint32_t renderId = static_cast<uint32_t>(data.getInt("renderId", 0));
    if (renderId == 0) return;   // 0 = "no id": every unidentified volume would share one slot
    RetainedFog src;
    readFogFields(data, src);
    if (src.w <= 0.0f || src.h <= 0.0f || src.density <= 0.0f) return;
    m_retainedFogs[renderId] = src;
}

void SceneCollector::parseFogUpdate(const IDataNode& data) {
    const uint32_t renderId = static_cast<uint32_t>(data.getInt("renderId", 0));
    if (renderId == 0) return;
    auto it = m_retainedFogs.find(renderId);
    if (it == m_retainedFogs.end()) return;   // updating something absent is a no-op, not an add

    RetainedFog merged = it->second;
    readFogFields(data, merged);
    if (merged.w <= 0.0f || merged.h <= 0.0f || merged.density <= 0.0f) return;
    it->second = merged;
}

void SceneCollector::parseFogRemove(const IDataNode& data) {
    const uint32_t renderId = static_cast<uint32_t>(data.getInt("renderId", 0));
    if (renderId == 0) return;
    m_retainedFogs.erase(renderId);
}

void SceneCollector::parseFilterAdd(const IDataNode& data) {
    const uint32_t renderId = static_cast<uint32_t>(data.getInt("renderId", 0));
    // 0 is the "no id" value: accepting it would give every unidentified pane the SAME slot, each
    // add silently replacing the last. Same guard as every other retained primitive.
    if (renderId == 0) return;

    RetainedFilter src;
    readFilterFields(data, src);
    if (src.w <= 0.0f || src.h <= 0.0f) return;

    m_retainedFilters[renderId] = src;
}

void SceneCollector::parseFilterUpdate(const IDataNode& data) {
    const uint32_t renderId = static_cast<uint32_t>(data.getInt("renderId", 0));
    if (renderId == 0) return;

    auto it = m_retainedFilters.find(renderId);
    if (it == m_retainedFilters.end()) return;   // updating something absent is a no-op, not an add

    // Merge onto the AUTHOR's values, then let finalize re-derive. Storing only the converted
    // per-unit figure would make a resize silently wrong: the pane would keep a value computed for
    // its old thickness, and widening a window would darken it by a power.
    RetainedFilter merged = it->second;
    readFilterFields(data, merged);
    if (merged.w <= 0.0f || merged.h <= 0.0f) return;   // a resize to nothing is refused, not stored
    it->second = merged;
}

void SceneCollector::parseFilterRemove(const IDataNode& data) {
    const uint32_t renderId = static_cast<uint32_t>(data.getInt("renderId", 0));
    if (renderId == 0) return;
    // A destroyed window must stop tinting -- the mirror of the orphaned-sprite hazard the FX layer
    // had to solve at hot-reload.
    m_retainedFilters.erase(renderId);
}

void SceneCollector::parseLight(const IDataNode& data) {
    LightCommand l;
    // cx,cy = CENTRE. No legacy x,y accepted: this primitive is NEW, so it starts life on the right
    // side of the anchor convention instead of earning an exception to it.
    l.cx     = static_cast<float>(data.getDouble("cx", 0.0));
    l.cy     = static_cast<float>(data.getDouble("cy", 0.0));
    l.radius = static_cast<float>(data.getDouble("radius", 0.0));

    const uint32_t color = static_cast<uint32_t>(data.getInt("color", static_cast<int>(0xFFFFFFFFu)));
    l.r = static_cast<float>((color >> 24) & 0xFF) / 255.0f;
    l.g = static_cast<float>((color >> 16) & 0xFF) / 255.0f;
    l.b = static_cast<float>((color >>  8) & 0xFF) / 255.0f;
    // The colour's alpha byte is ignored: a light ADDS, it does not blend, so "how opaque" has no
    // meaning here. Brightness is `intensity`, which is deliberately unclamped — the accumulation
    // target is RGBA16F and overlapping lamps are supposed to overshoot 1.0.
    l.intensity = static_cast<float>(data.getDouble("intensity", 1.0));

    // Cone (L3). spreadDeg defaults to 360 = omni, so a light published before cones existed stays
    // a full disc — a default of 0 would have switched every one of them off.
    l.dirDeg    = static_cast<float>(data.getDouble("dirDeg", 0.0));
    l.spreadDeg = static_cast<float>(data.getDouble("spreadDeg", 360.0));

    // A light with no extent lights nothing; dropping it here spares the pass a degenerate quad.
    if (l.radius > 0.0f) m_lights.push_back(l);
}

void SceneCollector::parseAmbient(const IDataNode& data) {
    // Global ambient (lighting L1). Default 0 = UNSET: publishing `render:ambient` with no colour
    // TURNS LIGHTING OFF again rather than silently picking a value — the topic is the on/off switch
    // as much as it is the value, and a game dimming to black must be able to say so.
    m_ambientColor = static_cast<uint32_t>(data.getInt("color", 0));
}

void SceneCollector::parseBloom(const IDataNode& data) {
    // Bloom settings (plan B). Comme l'ambiant : un RÉGLAGE global persistant, et `intensity` est
    // l'interrupteur autant que la valeur — publier `render:bloom {intensity:0}` doit ÉTEINDRE, sinon
    // le bloom serait allumable et pas éteignable.
    //
    // COMMENT — les trois bornes, et pourquoi on borne au lieu de retomber sur le défaut :
    //   - intensity < 0 : une lueur négative SOUSTRAIRAIT de la frame. Zéro (éteint) est le seul sens
    //     qu'on puisse donner à « moins que rien ».
    //   - threshold < 0 : casse la courbe du genou. Zéro est la valeur limite déjà documentée : tout
    //     brille, le voile.
    //   - radius < 0 : donnerait un sigma négatif au noyau. Zéro = aucun étalement, donc une lueur
    //     nette — explicable et MONOTONE (plus le rayon est petit, plus la lueur est serrée).
    //
    // Remettre le défaut à la place d'une borne serait pire qu'un fallback : un rayon de 16 sorti de
    // nulle part là où l'auteur a écrit -50 masque sa faute de frappe au lieu d'en montrer l'effet.
    m_bloom.intensity = static_cast<float>(data.getDouble("intensity", 0.0));
    m_bloom.threshold = static_cast<float>(data.getDouble("threshold", 1.0));
    m_bloom.radius    = static_cast<float>(data.getDouble("radius", 16.0));

    if (!(m_bloom.intensity > 0.0f)) m_bloom.intensity = 0.0f;   // couvre aussi un NaN entrant
    if (!(m_bloom.threshold > 0.0f)) m_bloom.threshold = 0.0f;
    if (!(m_bloom.radius    > 0.0f)) m_bloom.radius    = 0.0f;
}

void SceneCollector::parseTonemap(const IDataNode& data) {
    // Tonemapping (plan T). Reglage global persistant, SEPARE du bloom : le tonemapping change
    // l'image, donc l'agrafer au bloom ferait qu'activer une lueur modifierait l'exposition de tout le
    // rendu.
    //
    // COMMENT: le mode est une CHAINE et pas un nombre, parce que « pas de tonemapping » n'est pas
    //         « un tonemapping d'intensite nulle » : une courbe n'a pas de reglage continu vers
    //         l'identite. Deux modes nommes, et un troisieme nom pour l'extinction.
    //
    // ⚠️ Un mode INCONNU eteint, il ne retombe pas sur une courbe. Deviner appliquerait au rendu une
    //    transformation que l'auteur n'a pas demandee -- et il la chercherait dans son propre code. Ca
    //    couvre aussi bien la faute de frappe que le mode d'une version future lu par un vieux moteur.
    const std::string mode = data.getString("mode", "none");
    if (mode == "reinhard")   m_tonemap.mode = light::TonemapMode::Reinhard;
    else if (mode == "aces")  m_tonemap.mode = light::TonemapMode::ACES;
    else                      m_tonemap.mode = light::TonemapMode::None;

    // L'exposition est portee par le MESSAGE et pas accumulee : `render:tonemap {mode:"aces"}` remet
    // l'exposition a 1, comme `render:ambient {}` remet l'ambiant a 0. Le message decrit l'etat complet
    // du reglage, ce qui evite un etat cache que personne ne peut relire.
    m_tonemap.exposure = static_cast<float>(data.getDouble("exposure", 1.0));
    // Une exposition negative inverserait l'image ; 0 est le seul sens qu'on puisse donner a moins que
    // rien, et il est explicable (noir).
    if (!(m_tonemap.exposure > 0.0f)) m_tonemap.exposure = 0.0f;
}

void SceneCollector::parseFade(const IDataNode& data) {
    // Fondu plein ecran (plan F2). Reglage global persistant, comme l'ambiant et le tonemapping.
    //
    // La couleur par defaut est le NOIR : la transition au noir est de loin le cas courant, donc
    // `render:fade {amount: 1}` doit suffire a l'ecrire.
    m_fade.color  = static_cast<uint32_t>(data.getInt("color", 0x000000FF));
    m_fade.amount = static_cast<float>(data.getDouble("amount", 0.0));

    // ⚠️ BORNE a [0,1], et ce n'est pas de la prudence : au-dela de 1 un `mix` EXTRAPOLE, donc la
    //    couleur depasserait ses propres canaux et produirait des artefacts la ou l'auteur attendait un
    //    ecran plein. En dessous de 0, il extrapolerait dans l'autre sens.
    if (!(m_fade.amount > 0.0f)) m_fade.amount = 0.0f;   // couvre aussi un NaN entrant
    if (m_fade.amount > 1.0f)    m_fade.amount = 1.0f;
}

void SceneCollector::parseGrade(const IDataNode& data) {
    // Colorimetrie (plan G). Reglage global persistant, applique a l'image FINIE.
    //
    // ⚠️ PAS de bouton « luminosite », et c'est un refus argumente : il existe deja, c'est `exposure`
    //    du tonemapping, et il est du BON COTE de la courbe. Un gain applique apres la compression ne
    //    ferait que saturer plus tot, en re-ecretant ce que le tonemapping venait de sauver. Deux
    //    boutons pour une idee, dont le plus accessible serait le pire.
    m_grade.saturation = static_cast<float>(data.getDouble("saturation", 1.0));
    m_grade.contrast   = static_cast<float>(data.getDouble("contrast", 1.0));

    // La teinte est publiee comme une COULEUR (0xRRGGBBAA) et non comme trois flottants : c'est la
    // convention du moteur pour tout ce qui est une couleur, et un auteur pense « bleu nuit », pas
    // « (0.5, 0.7, 1.2) ». Le blanc (0xFFFFFF) est donc le neutre, ce qui tombe juste.
    //
    // ⚠️ Consequence assumee : une teinte ne peut qu'ASSOMBRIR un canal (un octet vaut au plus 255,
    //    donc au plus 1.0). Pour eclaircir, on monte `exposure`. C'est coherent avec le refus ci-dessus
    //    et ca evite un second chemin vers la meme chose.
    const uint32_t tint = static_cast<uint32_t>(data.getInt("tint", 0xFFFFFFFF));
    m_grade.tintR = static_cast<float>((tint >> 24) & 0xFF) / 255.0f;
    m_grade.tintG = static_cast<float>((tint >> 16) & 0xFF) / 255.0f;
    m_grade.tintB = static_cast<float>((tint >>  8) & 0xFF) / 255.0f;

    // Bornes basses seulement. `saturation` n'est PAS bornee en haut (>1 = couleurs criardes, un effet
    // legitime) ni `contrast` (un contraste extreme est un effet aussi) : la borne finale de sortie,
    // dans l'oracle, s'occupe du depassement par canal.
    if (!(m_grade.saturation > 0.0f)) m_grade.saturation = 0.0f;   // couvre aussi un NaN entrant
    if (!(m_grade.contrast   > 0.0f)) m_grade.contrast   = 0.0f;
}

void SceneCollector::parseDebugLine(const IDataNode& data) {
    DebugLine line;
    line.x1 = static_cast<float>(data.getDouble("x1", 0.0));
    line.y1 = static_cast<float>(data.getDouble("y1", 0.0));
    line.x2 = static_cast<float>(data.getDouble("x2", 0.0));
    line.y2 = static_cast<float>(data.getDouble("y2", 0.0));
    line.color = static_cast<uint32_t>(data.getInt("color", 0xFF0000FF));

    m_debugLines.push_back(line);
}

void SceneCollector::parseDebugRect(const IDataNode& data) {
    DebugRect rect;
    rect.x = static_cast<float>(data.getDouble("x", 0.0));
    rect.y = static_cast<float>(data.getDouble("y", 0.0));
    // Accept both "w"/"h" and "width"/"height" for convenience
    double w = data.getDouble("w", 0.0);
    if (w == 0.0) w = data.getDouble("width", 0.0);
    double h = data.getDouble("h", 0.0);
    if (h == 0.0) h = data.getDouble("height", 0.0);
    rect.w = static_cast<float>(w);
    rect.h = static_cast<float>(h);
    rect.color = static_cast<uint32_t>(data.getInt("color", 0x00FF00FF));
    rect.filled = data.getBool("filled", false);

    m_debugRects.push_back(rect);
}

void SceneCollector::parseSector(const IDataNode& data) {
    SectorCommand s;
    s.cx = static_cast<float>(data.getDouble("cx", 0.0));
    s.cy = static_cast<float>(data.getDouble("cy", 0.0));
    s.r0 = static_cast<float>(data.getDouble("r0", 0.0));
    s.r1 = static_cast<float>(data.getDouble("r1", 1.0));
    s.a0 = static_cast<float>(data.getDouble("a0", 0.0));
    s.a1 = static_cast<float>(data.getDouble("a1", 6.2831853));   // default = full circle
    s.color = static_cast<uint32_t>(data.getInt("color", 0xFFFFFFFF));
    s.layer = static_cast<uint16_t>(data.getInt("layer", 0));
    // space:"screen" -> HUD (fixed view 1); else world (view 0). Same split as render:rect.
    (isScreenSpace(data) ? m_hudSectors : m_sectors).push_back(s);
}

void SceneCollector::initDefaultView(uint16_t width, uint16_t height) {
    // World view defaults to screen-space (identity camera) until a render:camera arrives.
    buildScreenSpaceView(m_mainView, width, height);
    // HUD view is ALWAYS screen-space — initialized here and only ever updated to track the
    // viewport size (never the camera's zoom/pan), see parseCamera.
    buildScreenSpaceView(m_hudView, width, height);
}

// ============================================================================
// Retained Mode Parsing (sprites persist across frames)
// ============================================================================

void SceneCollector::parseSpriteAdd(const IDataNode& data) {
    static bool warnedLegacy = false;
    if (rejectsLegacyAnchor(data, "render:sprite:add", warnedLegacy)) return;
    uint32_t renderId = static_cast<uint32_t>(data.getInt("renderId", 0));
    if (renderId == 0) return;

    SpriteInstance sprite;
    sprite.x = centerCoord(data, "cx", "x", 0.0f);   // cx,cy = CENTER (anchor convention)
    sprite.y = centerCoord(data, "cy", "y", 0.0f);
    sprite.scaleX = static_cast<float>(data.getDouble("scaleX", 1.0));
    sprite.scaleY = static_cast<float>(data.getDouble("scaleY", 1.0));
    sprite.rotation = static_cast<float>(data.getDouble("rotation", 0.0));
    sprite.u0 = static_cast<float>(data.getDouble("u0", 0.0));
    sprite.v0 = static_cast<float>(data.getDouble("v0", 0.0));
    sprite.u1 = static_cast<float>(data.getDouble("u1", 1.0));
    sprite.v1 = static_cast<float>(data.getDouble("v1", 1.0));
    applySpriteFlip(data, sprite);   // optional mirror; absent -> UVs untouched
    sprite.textureId = static_cast<float>(resolveSpriteTexture(data, sprite));
    sprite.layer = static_cast<float>(data.getInt("layer", 0));
    sprite.padding0 = parseSpriteBlend(data);   // retained sprites glow too (same field)
    // Optional UI clip rect rides in reserved[] (SpritePass reads it -> bgfx scissor). Absent = 0 = none.
    sprite.reserved[0] = static_cast<float>(data.getDouble("clipX", 0.0));
    sprite.reserved[1] = static_cast<float>(data.getDouble("clipY", 0.0));
    sprite.reserved[2] = static_cast<float>(data.getDouble("clipW", 0.0));
    sprite.reserved[3] = static_cast<float>(data.getDouble("clipH", 0.0));

    uint32_t color = static_cast<uint32_t>(data.getInt("color", 0xFFFFFFFF));
    sprite.r = static_cast<float>((color >> 24) & 0xFF) / 255.0f;
    sprite.g = static_cast<float>((color >> 16) & 0xFF) / 255.0f;
    sprite.b = static_cast<float>((color >> 8) & 0xFF) / 255.0f;
    sprite.a = static_cast<float>(color & 0xFF) / 255.0f;

    // Route by screen-space: a retained widget tagged space:"screen" (the UIModule tags all its widgets)
    // goes to the fixed HUD bucket (m_hudView, camera-immune); everything else stays world-space. Without
    // this a retained UI widget would pan/zoom with the terrain under a live render:camera.
    (isScreenSpace(data) ? m_retainedHudSprites : m_retainedSprites)[renderId] = sprite;
    // (no per-add log: render hot path — same reason as the routing callback.)
}

void SceneCollector::parseSpriteUpdate(const IDataNode& data) {
    static bool warnedLegacy = false;
    if (rejectsLegacyAnchor(data, "render:sprite:update", warnedLegacy)) return;
    uint32_t renderId = static_cast<uint32_t>(data.getInt("renderId", 0));
    if (renderId == 0) return;

    // Screen-space selects the bucket; a renderId lives in exactly one map. Not in the target -> it's new
    // or it changed space -> drop any stale twin in the other bucket + re-add (routes by space).
    const bool hud = isScreenSpace(data);
    auto& target = hud ? m_retainedHudSprites : m_retainedSprites;
    auto it = target.find(renderId);
    if (it == target.end()) {
        (hud ? m_retainedSprites : m_retainedHudSprites).erase(renderId);
        parseSpriteAdd(data);
        return;
    }

    // Update existing sprite
    SpriteInstance& sprite = it->second;
    sprite.x = centerCoord(data, "cx", "x", sprite.x);   // cx,cy = CENTER; omitted -> keep current
    sprite.y = centerCoord(data, "cy", "y", sprite.y);
    sprite.scaleX = static_cast<float>(data.getDouble("scaleX", sprite.scaleX));
    sprite.scaleY = static_cast<float>(data.getDouble("scaleY", sprite.scaleY));
    sprite.rotation = static_cast<float>(data.getDouble("rotation", sprite.rotation));
    sprite.textureId = static_cast<float>(resolveSpriteTexture(data, sprite, static_cast<int>(sprite.textureId)));
    sprite.layer = static_cast<float>(data.getInt("layer", static_cast<int>(sprite.layer)));

    // Re-resolve the clip every update (full snapshot): absent -> 0 -> clip cleared. The UI
    // includes it whenever a container clip is active, so a still-clipped sprite keeps its scissor.
    sprite.reserved[0] = static_cast<float>(data.getDouble("clipX", 0.0));
    sprite.reserved[1] = static_cast<float>(data.getDouble("clipY", 0.0));
    sprite.reserved[2] = static_cast<float>(data.getDouble("clipW", 0.0));
    sprite.reserved[3] = static_cast<float>(data.getDouble("clipH", 0.0));

    // Preserve the existing color when the update omits "color" — exactly like x/y/
    // scale/layer above, which default to the sprite's current value. The old code
    // defaulted to 0xFFFFFFFF, so a color-less update silently RESET the sprite to
    // white (a retained-mode bug that no test exercised).
    if (data.hasProperty("color")) {
        uint32_t color = static_cast<uint32_t>(data.getInt("color", 0xFFFFFFFF));
        sprite.r = static_cast<float>((color >> 24) & 0xFF) / 255.0f;
        sprite.g = static_cast<float>((color >> 16) & 0xFF) / 255.0f;
        sprite.b = static_cast<float>((color >> 8) & 0xFF) / 255.0f;
        sprite.a = static_cast<float>(color & 0xFF) / 255.0f;
    }
}

void SceneCollector::parseSpriteRemove(const IDataNode& data) {
    uint32_t renderId = static_cast<uint32_t>(data.getInt("renderId", 0));
    if (renderId == 0) return;

    // The widget lives in exactly one bucket (renderId is globally unique); erase from both — the other
    // is a no-op. render:sprite:remove carries no "space", so we can't know which without this.
    m_retainedSprites.erase(renderId);
    m_retainedHudSprites.erase(renderId);
}

// ============================================================================
// 9-slice (nine-patch) frame — retained. See the declarations in SceneCollector.h.
// ============================================================================

void SceneCollector::parseNineSliceAdd(const IDataNode& data)    { expandNineSlice(data); }
void SceneCollector::parseNineSliceUpdate(const IDataNode& data) { expandNineSlice(data); }

void SceneCollector::parseNineSliceRemove(const IDataNode& data) {
    uint32_t renderId = static_cast<uint32_t>(data.getInt("renderId", 0));
    if (renderId == 0) return;
    // Drop every possible child (0..8) from BOTH buckets — the parent may have expanded to fewer than 9
    // quads, and remove carries no "space", so erase the full superset in both (each erase is a no-op miss).
    for (int i = 0; i < 9; ++i) {
        const uint32_t cid = nineSliceChildId(renderId, i);
        m_retainedSprites.erase(cid);
        m_retainedHudSprites.erase(cid);
    }
}

// QUOI : (ré)expanse un nine-slice en jusqu'à 9 sprites retained. POURQUOI : porter le bord continu sur le
//   pipeline sprite existant (bucket HUD, clip, teinte) sans nouveau pass — 1 message -> N quads. COMMENT :
//   1. lire la cible (x,y = coin haut-gauche, w,h) + la NinePatch (dims source + marges) ; 2. résoudre la
//   texture (asset streamé -> texId + sous-rect UV d'atlas ; sinon textureId numérique, UV plein [0,1]) ;
//   3. calculer les quads (grove::ui::computeNineSlice) ; 4. purger les 9 enfants des DEUX buckets (état
//   propre : le nombre de quads a pu changer, ou l'espace a basculé) ; 5. pour chaque quad, composer
//   l'UV d'atlas avec l'UV du quad (atlasUV ∘ sliceUV), fabriquer la SpriteInstance (centre = coin+½taille)
//   et l'insérer dans le bon bucket (screen -> HUD). Le clip/teinte rident dans reserved[]/r,g,b,a comme un
//   sprite normal, donc finalize() les traite à l'identique.
void SceneCollector::expandNineSlice(const IDataNode& data) {
    const uint32_t renderId = static_cast<uint32_t>(data.getInt("renderId", 0));
    if (renderId == 0) return;

    // 1. Cible + NinePatch. x,y = coin haut-gauche (convention render:rect/text, PAS de centre ici).
    const float dx = static_cast<float>(data.getDouble("x", 0.0));
    const float dy = static_cast<float>(data.getDouble("y", 0.0));
    const float dw = static_cast<float>(data.getDouble("w", 0.0));
    const float dh = static_cast<float>(data.getDouble("h", 0.0));
    grove::ui::NinePatch np;
    np.srcW   = static_cast<float>(data.getDouble("srcW", 0.0));
    np.srcH   = static_cast<float>(data.getDouble("srcH", 0.0));
    np.left   = static_cast<float>(data.getDouble("left", 0.0));
    np.right  = static_cast<float>(data.getDouble("right", 0.0));
    np.top    = static_cast<float>(data.getDouble("top", 0.0));
    np.bottom = static_cast<float>(data.getDouble("bottom", 0.0));

    // 2. Texture + UV d'atlas de base. asset streamé (atlas-aware) l'emporte sur textureId numérique.
    int texId = 0;
    float au0 = 0.0f, av0 = 0.0f, au1 = 1.0f, av1 = 1.0f;
    const std::string asset = data.getString("asset", "");
    if (!asset.empty() && m_assetMgr) {
        texId = static_cast<int>(m_assetMgr->resolveSprite(asset, au0, av0, au1, av1));
    } else {
        texId = data.getInt("textureId", 0);
    }

    // 3. Géométrie des 9 quads (maths pures, headless-testées).
    grove::ui::NinePatchQuad quads[9];
    const int n = grove::ui::computeNineSlice(np, dx, dy, dw, dh, quads);

    // Couleur (teinte de tout le cadre) + clip container, décodés une fois pour les N enfants.
    const uint32_t color = static_cast<uint32_t>(data.getInt("color", 0xFFFFFFFF));
    const float cr = static_cast<float>((color >> 24) & 0xFF) / 255.0f;
    const float cg = static_cast<float>((color >> 16) & 0xFF) / 255.0f;
    const float cb = static_cast<float>((color >> 8)  & 0xFF) / 255.0f;
    const float ca = static_cast<float>( color        & 0xFF) / 255.0f;
    const float layer = static_cast<float>(data.getInt("layer", 0));
    const float clipX = static_cast<float>(data.getDouble("clipX", 0.0));
    const float clipY = static_cast<float>(data.getDouble("clipY", 0.0));
    const float clipW = static_cast<float>(data.getDouble("clipW", 0.0));
    const float clipH = static_cast<float>(data.getDouble("clipH", 0.0));

    // 4. Purge des enfants dans LES DEUX buckets (état propre avant réinsertion).
    for (int i = 0; i < 9; ++i) {
        const uint32_t cid = nineSliceChildId(renderId, i);
        m_retainedSprites.erase(cid);
        m_retainedHudSprites.erase(cid);
    }
    auto& bucket = isScreenSpace(data) ? m_retainedHudSprites : m_retainedSprites;

    // 5. Une SpriteInstance par quad : centre = coin + ½taille ; UV = atlasUV ∘ sliceUV.
    const float aw = au1 - au0, ah = av1 - av0;
    for (int i = 0; i < n; ++i) {
        const grove::ui::NinePatchQuad& q = quads[i];
        SpriteInstance sprite;
        sprite.x = q.x + q.w * 0.5f;     // le pipeline sprite attend un CENTRE
        sprite.y = q.y + q.h * 0.5f;
        sprite.scaleX = q.w;
        sprite.scaleY = q.h;
        sprite.rotation = 0.0f;
        sprite.u0 = au0 + q.u0 * aw;      // composer le sous-rect du quad DANS le sous-rect d'atlas
        sprite.v0 = av0 + q.v0 * ah;
        sprite.u1 = au0 + q.u1 * aw;
        sprite.v1 = av0 + q.v1 * ah;
        sprite.textureId = static_cast<float>(texId);
        sprite.layer = layer;
        sprite.padding0 = 0.0f;
        sprite.reserved[0] = clipX; sprite.reserved[1] = clipY;
        sprite.reserved[2] = clipW; sprite.reserved[3] = clipH;
        sprite.r = cr; sprite.g = cg; sprite.b = cb; sprite.a = ca;
        bucket[nineSliceChildId(renderId, i)] = sprite;
    }
}

void SceneCollector::parseTextAdd(const IDataNode& data) {
    uint32_t renderId = static_cast<uint32_t>(data.getInt("renderId", 0));
    if (renderId == 0) return;

    TextCommand text;
    text.x = static_cast<float>(data.getDouble("x", 0.0));
    text.y = static_cast<float>(data.getDouble("y", 0.0));
    text.fontId = static_cast<uint16_t>(data.getInt("fontId", 0));
    text.fontSize = static_cast<uint16_t>(data.getInt("fontSize", 16));
    text.color = static_cast<uint32_t>(data.getInt("color", 0xFFFFFFFF));
    text.layer = static_cast<uint16_t>(data.getInt("layer", 0));
    text.clipX = static_cast<float>(data.getDouble("clipX", 0.0));
    text.clipY = static_cast<float>(data.getDouble("clipY", 0.0));
    text.clipW = static_cast<float>(data.getDouble("clipW", 0.0));
    // Optional width budget: a longer line is truncated with an ellipsis (0 = unlimited).
    text.maxWidth = static_cast<float>(data.getDouble("maxWidth", 0.0));
    text.clipH = static_cast<float>(data.getDouble("clipH", 0.0));
    text.align = static_cast<uint8_t>(data.getInt("align", 0));           // 0 left / 1 center / 2 right
    text.bold  = data.getBool("bold", false) ? 1 : 0;
    text.text = nullptr;  // Will be set from the matching strings map in finalize

    // Route by screen-space (same as sprites): a tagged retained widget text goes to the fixed HUD bucket.
    if (isScreenSpace(data)) {
        m_retainedHudTexts[renderId] = text;
        m_retainedHudTextStrings[renderId] = data.getString("text", "");
    } else {
        m_retainedTexts[renderId] = text;
        m_retainedTextStrings[renderId] = data.getString("text", "");
    }
}

void SceneCollector::parseTextUpdate(const IDataNode& data) {
    uint32_t renderId = static_cast<uint32_t>(data.getInt("renderId", 0));
    if (renderId == 0) return;

    // Screen-space selects the bucket (like sprites); not in the target -> new or space changed -> drop
    // any stale twin (command + its string) in the other bucket + re-add.
    const bool hud = isScreenSpace(data);
    auto& target = hud ? m_retainedHudTexts : m_retainedTexts;
    auto& targetStr = hud ? m_retainedHudTextStrings : m_retainedTextStrings;
    auto it = target.find(renderId);
    if (it == target.end()) {
        if (hud) { m_retainedTexts.erase(renderId); m_retainedTextStrings.erase(renderId); }
        else     { m_retainedHudTexts.erase(renderId); m_retainedHudTextStrings.erase(renderId); }
        parseTextAdd(data);
        return;
    }

    // Update existing text
    TextCommand& text = it->second;
    text.x = static_cast<float>(data.getDouble("x", text.x));
    text.y = static_cast<float>(data.getDouble("y", text.y));
    text.fontSize = static_cast<uint16_t>(data.getInt("fontSize", text.fontSize));
    text.color = static_cast<uint32_t>(data.getInt("color", text.color));
    text.layer = static_cast<uint16_t>(data.getInt("layer", text.layer));
    text.clipX = static_cast<float>(data.getDouble("clipX", 0.0));   // full snapshot: absent -> cleared
    text.clipY = static_cast<float>(data.getDouble("clipY", 0.0));
    text.clipW = static_cast<float>(data.getDouble("clipW", 0.0));
    // Optional width budget: a longer line is truncated with an ellipsis (0 = unlimited).
    text.maxWidth = static_cast<float>(data.getDouble("maxWidth", 0.0));
    text.clipH = static_cast<float>(data.getDouble("clipH", 0.0));
    text.align = static_cast<uint8_t>(data.getInt("align", text.align));   // keep current when omitted
    text.bold  = data.getBool("bold", text.bold != 0) ? 1 : 0;

    // Update text string if provided (into the SAME bucket's string map).
    std::string newText = data.getString("text", "");
    if (!newText.empty()) {
        targetStr[renderId] = newText;
    }
}

void SceneCollector::parseTextRemove(const IDataNode& data) {
    uint32_t renderId = static_cast<uint32_t>(data.getInt("renderId", 0));
    if (renderId == 0) return;

    // Erase from both buckets (unique renderId; the other pair is a no-op) — remove carries no "space".
    m_retainedTexts.erase(renderId);
    m_retainedTextStrings.erase(renderId);
    m_retainedHudTexts.erase(renderId);
    m_retainedHudTextStrings.erase(renderId);
}

} // namespace grove
