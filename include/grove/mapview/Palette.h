#pragma once

/**
 * grove::mapview::Palette — field value -> colour (S1c, the "colorisation" brick of §5).
 *
 * WHAT  : One data-driven mapping from a (decoded, physical) field value to an Rgba. Three kinds:
 *           - Ramp        : continuous linear interpolation between sorted colour stops (the free
 *                           continuous colour — relief, temperature gradients…).
 *           - Banded      : discrete bands by ascending upper-bound threshold (contour-style buckets).
 *           - Categorical : integer index -> colour table (biomes, region types…), with a fallback.
 *
 * WHY   : This is the modular colour the user asked for — a Layer (S1d) pairs a field with a Palette, and
 *         the lens stack composes them. Continuous colour is free on the bulk path (Color.h), so Ramp is
 *         not a special case — the old banding-vs-LUT fork only existed on the tilemap path (mapview.md §5).
 *
 * HOW   : Header-only, std-only. Built via factory functions; eval() is a pure value->Rgba. Ramp clamps to
 *         its end stops and lerps the bracketing segment; Banded returns the first band whose upper bound is
 *         strictly greater than the value (else the last colour); Categorical rounds the value to an index.
 *         Stops/bands are expected sorted by value ascending (the factory is where a producer prepares them).
 */

#include <cmath>
#include <cstddef>
#include <utility>
#include <vector>

#include "grove/mapview/Color.h"

namespace grove {
namespace mapview {

class Palette {
public:
    enum class Kind { Ramp, Banded, Categorical };

    // Continuous ramp between sorted (value, colour) stops. Needs >= 1 stop.
    static Palette ramp(std::vector<std::pair<double, Rgba>> stops) {
        Palette p;
        p.kind_ = Kind::Ramp;
        p.stops_ = std::move(stops);
        return p;
    }

    // Discrete bands: each (upperBound, colour) covers values < upperBound (ascending). Values >= the last
    // bound take the last colour.
    static Palette banded(std::vector<std::pair<double, Rgba>> bands) {
        Palette p;
        p.kind_ = Kind::Banded;
        p.stops_ = std::move(bands);
        return p;
    }

    // Index -> colour table; out-of-range indices take `fallback`.
    static Palette categorical(std::vector<Rgba> table, Rgba fallback = Rgba{}) {
        Palette p;
        p.kind_ = Kind::Categorical;
        p.table_ = std::move(table);
        p.fallback_ = fallback;
        return p;
    }

    // Diverging: a 3-stop ramp low -> mid (at the pivot) -> high. The natural palette for data read around a
    // centre — elevation about sea level, a temperature anomaly about zero (low/high clamp at the ends).
    static Palette diverging(Rgba low, Rgba mid, Rgba high, double lowVal, double pivot, double highVal) {
        return ramp({{lowVal, low}, {pivot, mid}, {highVal, high}});
    }

    // Stepped: quantize [lo,hi] into colors.size() flat, equal-width bands (contour-style). A convenience over
    // banded with evenly spaced thresholds; values below lo / at-or-above hi clamp to the first / last colour.
    static Palette stepped(double lo, double hi, std::vector<Rgba> colors) {
        if (colors.empty()) return banded({});
        std::vector<std::pair<double, Rgba>> bands;
        bands.reserve(colors.size());
        const double step = (hi - lo) / static_cast<double>(colors.size());
        for (size_t i = 0; i < colors.size(); ++i) {
            bands.emplace_back(lo + static_cast<double>(i + 1) * step, colors[i]);
        }
        return banded(std::move(bands));
    }

    Kind kind() const { return kind_; }

    // Map a (decoded) field value to a colour.
    Rgba eval(double value) const {
        switch (kind_) {
            case Kind::Ramp:        return evalRamp(value);
            case Kind::Banded:      return evalBanded(value);
            case Kind::Categorical: return evalCategorical(value);
        }
        return fallback_;
    }

private:
    Rgba evalRamp(double value) const {
        if (stops_.empty()) return fallback_;
        if (value <= stops_.front().first) return stops_.front().second;
        if (value >= stops_.back().first)  return stops_.back().second;
        // Find the segment [i, i+1] bracketing the value and lerp within it.
        for (size_t i = 0; i + 1 < stops_.size(); ++i) {
            const double v0 = stops_[i].first;
            const double v1 = stops_[i + 1].first;
            if (value >= v0 && value <= v1) {
                const double span = v1 - v0;
                const float t = span > 0.0 ? static_cast<float>((value - v0) / span) : 0.0f;
                return lerp(stops_[i].second, stops_[i + 1].second, t);
            }
        }
        return stops_.back().second;
    }

    Rgba evalBanded(double value) const {
        if (stops_.empty()) return fallback_;
        for (const auto& band : stops_) {
            if (value < band.first) return band.second;
        }
        return stops_.back().second;  // above the last upper bound
    }

    Rgba evalCategorical(double value) const {
        const long idx = std::lround(value);
        if (idx >= 0 && static_cast<size_t>(idx) < table_.size()) {
            return table_[static_cast<size_t>(idx)];
        }
        return fallback_;
    }

    Kind kind_{Kind::Ramp};
    std::vector<std::pair<double, Rgba>> stops_;  // Ramp stops / Banded (upperBound, colour)
    std::vector<Rgba> table_;                     // Categorical
    Rgba fallback_{};
};

} // namespace mapview
} // namespace grove
