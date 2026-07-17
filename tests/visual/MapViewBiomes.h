#pragma once
/*
 * tests/visual/MapViewBiomes — read a world's biomes.json side-car into a categorical palette table.
 *
 * WHAT : loadBiomeColors(dir) -> a table where table[biomeId] is that biome's map colour, ready for
 *        Palette::categorical (see mvdemo::makeBiomeLens). Index 0 and any gap are TRANSPARENT so ocean /
 *        unclassified cells reveal the terrain layer beneath.
 * WHY  : the biome INDEX rides inside the .world grid (field "biome"), but the id -> {name, colour} mapping is
 *        NON-spatial, so the producer ships it beside the document as a side-car (same pattern as core.json).
 *        Keeping the reader here — OUTSIDE the GROVE_MAPVIEW_HUD gate — lets BOTH the plain viewer (`--lens
 *        biome`, no UIModule needed) and the HUD's "Biomes" button share one implementation.
 * HOW  : nlohmann parse of {"biomes":[{"id":N,"name":"...","color":"#RRGGBB"}, ...]} (written by Theomen's
 *        `worldscope --export-biomes`). A missing or malformed file yields an EMPTY table — every caller treats
 *        that as "this world has no biomes" and falls back (terrain lens / inert button), never a crash.
 */
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "grove/mapview/Color.h"

namespace grove {
namespace mvdemo {

inline std::vector<mapview::Rgba> loadBiomeColors(const std::string& dir) {
    std::vector<mapview::Rgba> table;
    if (dir.empty()) return table;
    const std::filesystem::path path = std::filesystem::path(dir) / "biomes.json";
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) return table;
    std::ifstream in(path, std::ios::binary);
    if (!in) return table;
    nlohmann::json j;
    try { in >> j; } catch (const std::exception&) { return table; }   // malformed -> "no biomes"
    if (!j.contains("biomes") || !j["biomes"].is_array()) return table;

    int maxId = 0;
    for (const auto& b : j["biomes"]) maxId = std::max(maxId, b.value("id", 0));
    table.assign(static_cast<size_t>(maxId) + 1, mapview::Rgba{0.0f, 0.0f, 0.0f, 0.0f}); // 0/gaps transparent
    for (const auto& b : j["biomes"]) {
        const int id = b.value("id", 0);
        if (id < 0 || id > maxId) continue;
        const std::string hex = b.value("color", std::string("#808080"));
        const char* p = hex.c_str(); if (*p == '#') ++p;
        const unsigned rgb = static_cast<unsigned>(std::strtoul(p, nullptr, 16));
        table[static_cast<size_t>(id)] = mapview::Rgba{
            static_cast<float>((rgb >> 16) & 0xFFu) / 255.0f,
            static_cast<float>((rgb >> 8)  & 0xFFu) / 255.0f,
            static_cast<float>( rgb        & 0xFFu) / 255.0f, 1.0f};
    }
    return table;
}

}  // namespace mvdemo
}  // namespace grove
