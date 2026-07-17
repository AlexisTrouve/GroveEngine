#pragma once
// =============================================================================
// MapViewHypsometry — terrain colour stops CALIBRATED FROM THE DOCUMENT'S OWN ELEVATION.
//
// WHY   : `terrainStops()` in MapViewDemoScene.h is tuned for the synthetic demo world — a toy where
//         sea level sits near 330 and peaks reach 1000. A real world-document exports elevation
//         RELATIVE TO SEA LEVEL (0 = shore, <0 ocean, >0 land), and its land can run to thousands of
//         metres. Feeding one to the other silently clips: measured on a Theomen world (seed 42),
//         land median = 2214 m against a ramp that goes white at 1000 -> 69% OF ALL LAND RENDERED
//         WHITE, only the 14% below 380 m carrying any colour. The map looked like a snowball and
//         hid every ridge, cordillera and trench the generator had built. Not a taste problem: the
//         palette was measuring a different world than the one on screen.
//
// WHAT  : sample the document's `elevation` field, split at 0 (the shore — the datum the exporter
//         already guarantees), and place the colour stops on PERCENTILES of each side. The ramp
//         therefore stretches to whatever relief the world actually has, with no magic constants:
//         a flat world and an alpine one both use their full colour range.
//
// HOW   : stride-sample chunks through IChunkProvider (has/load) -> collect finite `elevation`
//         values -> sort -> percentile lookup. Ocean and land are calibrated INDEPENDENTLY: they are
//         two different distributions and sharing one scale would let a 20 km peak flatten the whole
//         seabed into one blue. Falls back to the demo stops when the doc has no usable elevation
//         (empty/absent field) — a viewer that renders SOMETHING beats one that throws.
//
// NOTE  : percentiles, not min/max. A single freak peak (or an abyssal spike) would otherwise
//         hijack the whole ramp — the classic outlier trap. p99/p1 keep the ends meaningful and let
//         the extremes clamp to snow / abyss, which is what you want them to look like anyway.
// =============================================================================
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "grove/mapview/ChunkProvider.h"
#include "grove/mapview/Field.h"
#include "grove/mapview/Palette.h"

namespace grove {
namespace mvdemo {

namespace mapview = grove::mapview; // meme alias que MapViewDemoScene.h

// Percentile of a SORTED vector. p in [0,1]. Empty -> 0.
inline double hypsoPct(const std::vector<double>& sorted, double p) {
    if (sorted.empty()) return 0.0;
    const std::size_t i = static_cast<std::size_t>(p * static_cast<double>(sorted.size() - 1) + 0.5);
    return sorted[std::min(i, sorted.size() - 1)];
}

// Sample `elevation` across the document. `stride` skips chunks (1 = all): a world-overview ramp does
//   not need every cell, and loading every chunk of a big doc just to pick 8 colours is wasteful.
inline std::vector<double> sampleElevation(mapview::IChunkProvider& prov,
                                           const std::vector<mapview::FieldDecl>& schema,
                                           int cx0, int cy0, int cx1, int cy1, int stride = 1) {
    const mapview::FieldDecl* decl = nullptr;
    for (const auto& f : schema)
        if (f.name == "elevation") { decl = &f; break; }
    std::vector<double> out;
    if (!decl) return out;
    for (int cy = cy0; cy <= cy1; cy += stride)
        for (int cx = cx0; cx <= cx1; cx += stride) {
            const mapview::ChunkCoord c{cx, cy, 0};
            if (!prov.has(c)) continue;
            const mapview::ChunkData d = prov.load(c);
            const std::vector<std::uint32_t>* raw = d.get("elevation");
            if (!raw) continue;
            out.reserve(out.size() + raw->size());
            for (std::uint32_t v : *raw) {
                const double phys = mapview::decodePhysical(*decl, v);
                if (std::isfinite(phys)) out.push_back(phys);
            }
        }
    return out;
}

// Build terrain stops from sampled elevations. `fallback` is returned when there is nothing usable.
// The stop LADDER (why these percentiles): the ocean gets 4 stops because most of a world is ocean and
//   its structure (ridges, trenches, shelves) is the part that was invisible; land gets 5 so the bulk
//   of the hypsometric curve — not just the coast — carries colour.
inline std::vector<std::pair<double, mapview::Rgba>>
hypsometricStops(std::vector<double> samples,
                 const std::vector<std::pair<double, mapview::Rgba>>& fallback) {
    std::vector<double> sea, land;
    for (double v : samples) (v <= 0.0 ? sea : land).push_back(v);
    if (land.size() < 8 || sea.size() < 8) return fallback; // doc dégénéré -> on rend quand même
    std::sort(sea.begin(), sea.end());
    std::sort(land.begin(), land.end());

    using mapview::Rgba;
    std::vector<std::pair<double, Rgba>> s;
    // OCÉAN — calibré sur SA propre distribution (indépendante de la terre : partager l'échelle
    //   laisserait un pic de 20 km aplatir tout le plancher en un seul bleu).
    s.emplace_back(hypsoPct(sea, 0.01), Rgba{0.02f, 0.05f, 0.20f, 1});  // abysses / fosses
    s.emplace_back(hypsoPct(sea, 0.25), Rgba{0.04f, 0.12f, 0.38f, 1});  // plaine abyssale
    s.emplace_back(hypsoPct(sea, 0.65), Rgba{0.10f, 0.32f, 0.62f, 1});  // dorsales / bas-fonds
    s.emplace_back(hypsoPct(sea, 0.95), Rgba{0.24f, 0.56f, 0.80f, 1});  // plateau continental
    // RIVAGE — le datum que l'export garantit déjà (elevation_m − seaLevel).
    s.emplace_back(0.0, Rgba{0.85f, 0.80f, 0.55f, 1});                  // sable
    // TERRE — percentiles : la médiane du relief doit être VERTE, pas blanche.
    s.emplace_back(hypsoPct(land, 0.10), Rgba{0.27f, 0.55f, 0.22f, 1}); // plaines
    s.emplace_back(hypsoPct(land, 0.40), Rgba{0.20f, 0.42f, 0.18f, 1}); // collines boisées
    s.emplace_back(hypsoPct(land, 0.70), Rgba{0.45f, 0.36f, 0.22f, 1}); // roche
    s.emplace_back(hypsoPct(land, 0.90), Rgba{0.55f, 0.52f, 0.48f, 1}); // haute montagne
    s.emplace_back(hypsoPct(land, 0.99), Rgba{0.98f, 0.98f, 1.00f, 1}); // neige (le 1% le plus haut)
    // Les stops doivent être STRICTEMENT croissants (la rampe interpole entre voisins) : un monde
    //   plat peut écraser deux percentiles sur la même valeur -> on écarte d'un epsilon.
    for (std::size_t i = 1; i < s.size(); ++i)
        if (s[i].first <= s[i - 1].first) s[i].first = s[i - 1].first + 1e-3;
    return s;
}

// Log ce que la calibration a décidé — sans ça, une palette auto est une boîte noire : on ne peut
//   pas distinguer « le monde est plat » de « la calibration a raté ».
inline void logHypsometry(const std::vector<std::pair<double, mapview::Rgba>>& s, bool calibrated) {
    if (!calibrated) { std::fprintf(stdout, "hypsometry: pas d'elevation exploitable -> stops de demo\n"); return; }
    std::fprintf(stdout, "hypsometry: rampe calibree sur le document — %zu stops, %.0f m (abysses) -> %.0f m (neige)\n",
                 s.size(), s.front().first, s.back().first);
}

} // namespace mvdemo
} // namespace grove
