#pragma once

#include "AssetManager.h"
#include "ShelfPacker.h"
#include "../Resources/TextureLoader.h"
#include "../Resources/ResourceCache.h"
#include "../RHI/RHIDevice.h"
#include "../RHI/RHITypes.h"
#include <vector>
#include <string>
#include <algorithm>
#include <cstdint>

namespace grove::assets {

/// One image to pack: a stable assetId + the source file.
struct PackEntry { std::string id; std::string path; };

/**
 * @brief Pack N images into ONE atlas sheet at RUNTIME (phase 2b).
 *
 * QUOI : décode chaque image, shelf-pack les rects, assemble en UN buffer RGBA8, upload en une texture, puis
 *   enregistre la sheet (PINNED — une sheet runtime n'a pas de chemin, donc non-rechargeable -> jamais
 *   évincée) + le rect UV normalisé de chaque sous-sprite. Renvoie true si >= 1 image a été packée.
 * POURQUOI : permet de fournir N PNG SÉPARÉS (sans outil d'atlas offline) et de les batcher en une texture à
 *   l'exécution — N sprites, 1 handle, 1 draw batch. COMMENT : decodeRgba (CPU) -> shelfPack (pur) ->
 *   memcpy par lignes dans l'atlas -> createTexture (1 niveau, pas de mips pour éviter le bleed inter-sprite)
 *   -> registerTexture -> registerResident(sheet) + registerAtlasSprite(chaque sous-sprite).
 */
inline bool packAtlas(rhi::IRHIDevice& device, ResourceCache& cache, AssetManager& am,
                      const std::string& sheetId, const std::vector<PackEntry>& entries,
                      int maxWidth = 2048, int gutter = 2, int priority = 0, const std::string& group = "") {
    // 1. Decode each image (skip failures); keep its pixels + a rect (same index in both vectors).
    struct Decoded { std::string id; std::vector<uint8_t> px; int w = 0, h = 0; };
    std::vector<Decoded> imgs;
    std::vector<PackRect> rects;
    for (const auto& e : entries) {
        Decoded d; d.id = e.id;
        if (!TextureLoader::decodeRgba(e.path, d.px, d.w, d.h)) continue;
        rects.push_back(PackRect{ d.w, d.h, 0, 0 });
        imgs.push_back(std::move(d));
    }
    if (imgs.empty()) return false;

    // 2. Pack (fills each rect's x/y in place, by original index).
    const PackResult pr = shelfPack(rects, maxWidth, gutter);
    if (!pr.ok) return false;

    // 3. Assemble into one RGBA8 buffer (transparent background), row by row.
    const int W = pr.width, H = pr.height;
    std::vector<uint8_t> atlas(static_cast<size_t>(W) * static_cast<size_t>(H) * 4, 0);
    for (size_t i = 0; i < imgs.size(); ++i) {
        const Decoded& d = imgs[i];
        const PackRect& r = rects[i];
        for (int row = 0; row < d.h; ++row) {
            const uint8_t* src = d.px.data() + static_cast<size_t>(row) * d.w * 4;
            uint8_t* dst = atlas.data() + (static_cast<size_t>(r.y + row) * W + r.x) * 4;
            std::copy(src, src + static_cast<size_t>(d.w) * 4, dst);
        }
    }

    // 4. Upload as one texture (mipLevels 1: no mips -> no cross-sub-sprite bleeding).
    rhi::TextureDesc desc;
    desc.width = static_cast<uint16_t>(W);
    desc.height = static_cast<uint16_t>(H);
    desc.format = rhi::TextureDesc::RGBA8;
    desc.mipLevels = 1;
    desc.data = atlas.data();
    desc.dataSize = static_cast<uint32_t>(atlas.size());
    rhi::TextureHandle handle = device.createTexture(desc);
    if (!handle.isValid()) return false;
    const uint16_t texId = cache.registerTexture(handle);
    if (texId == 0) { device.destroy(handle); return false; }

    // 5. Register the sheet (resident + pinned) + each sub-sprite's normalized UV rect.
    am.registerResident(sheetId, texId, static_cast<uint64_t>(W) * static_cast<uint64_t>(H) * 4, priority, group);
    const float fw = static_cast<float>(W), fh = static_cast<float>(H);
    for (size_t i = 0; i < imgs.size(); ++i) {
        const PackRect& r = rects[i];
        am.registerAtlasSprite(imgs[i].id, sheetId,
                               static_cast<float>(r.x) / fw, static_cast<float>(r.y) / fh,
                               static_cast<float>(r.x + imgs[i].w) / fw, static_cast<float>(r.y + imgs[i].h) / fh,
                               priority, group);
    }
    return true;
}

} // namespace grove::assets
