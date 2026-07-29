#pragma once

// ============================================================================
// Transmittance.h — the polar mapping + transmittance accumulation (grove::light), header-only.
//
// QUOI  : how a world point finds its cell in a light's polar table, and how much light SURVIVES
//         from the lamp out to a given distance in a given direction.
//
// POURQUOI: walls, coloured filters and fog are not three techniques. They are three ways of
//         answering ONE question — how much light, and of what colour, survives from A to B — and
//         the answer is a transmittance accumulated multiplicatively along the ray. A wall is the
//         degenerate case (transmittance 0); a filter is a colour; fog is exp(−α) per unit. Sharing
//         one mechanism is what stops the first chantier from being thrown away by the second.
//         Full rationale: docs/design/lighting-transmittance-core.md
//
// COMMENT: pure functions, no renderer / IIO / GPU — same contract as grove::light::attenuation.
//         The GPU will build the table with a log₂(N) prefix-product scan, which is not assertable
//         in a shader; the O(N) version here is its ORACLE, exactly as attenuation() is the oracle
//         of the light shader's falloff.
// ============================================================================

#include <cmath>

#include "Light.h"   // Light2D + the dirDeg/spreadDeg angular convention this file must match

namespace grove {
namespace light {

// Where a world point sits in a light's polar table.
//
// Both coordinates are NORMALISED to 0..1 because they index a texture: one table layout then
// serves every light whatever its radius, and the shader does no unit conversion.
struct PolarIndex {
    float angle01;    // [0,1) — 0 = +x, 0.25 = +y (screen-DOWN), matching Light2D::dirDeg
    float radius01;   // [0,1] — distance / light radius, CLAMPED at the rim
    bool  inside;     // false when the point lies beyond the light's radius
};

/// Polar index of world point (px,py) relative to light `l`.
///
/// The angular convention deliberately matches Light2D::dirDeg (and therefore grove::fx::Emitter):
/// 0 = +x, 90° = +y = screen-down. Two angular conventions inside one lighting system would
/// guarantee that a cone and its occlusion disagree by a quarter turn somewhere — and that defect
/// would stay invisible until a wall shadowed the wrong side of a spotlight.
inline PolarIndex toPolar(const Light2D& l, float px, float py) {
    constexpr float kTwoPi = 6.28318530717958647692f;

    const float dx = px - l.cx;
    const float dy = py - l.cy;
    const float d  = std::sqrt(dx * dx + dy * dy);

    PolarIndex out;

    // atan2 returns (−π, π]; shift into [0, 2π) then normalise. Past the rim the light contributes
    // nothing anyway, so the table need not cover it — clamping beats growing the table.
    float a = std::atan2(dy, dx);
    if (a < 0.0f) a += kTwoPi;
    out.angle01 = a / kTwoPi;
    if (out.angle01 >= 1.0f) out.angle01 = 0.0f;   // guard the wrap: the index must stay in [0,1)

    if (l.radius > 0.0f) {
        const float r = d / l.radius;
        out.inside   = (r <= 1.0f);
        out.radius01 = (r > 1.0f) ? 1.0f : r;
    } else {
        out.inside   = false;
        out.radius01 = 1.0f;
    }
    return out;
}

/// Inverse of toPolar: the world point at (angle01, radius01) around light `l`.
inline void fromPolar(const Light2D& l, float angle01, float radius01, float& px, float& py) {
    constexpr float kTwoPi = 6.28318530717958647692f;
    const float a = angle01 * kTwoPi;
    const float d = radius01 * l.radius;
    px = l.cx + std::cos(a) * d;
    py = l.cy + std::sin(a) * d;
}

/// Transmittance of matter whose PER-UNIT transmittance is `perUnit`, traversed over `distance`.
///
/// Per-unit rather than per-occluder is the choice that makes discrete matter and a continuous
/// medium the same thing: thicker glass tints more, and fog absorbs by how far you travel in it.
inline float transmitThrough(float perUnit, float distance) {
    if (distance <= 0.0f) return 1.0f;   // a ray that has not moved has lost nothing
    if (perUnit >= 1.0f) return 1.0f;    // vacuum — EXACTLY 1, so an empty table is truly neutral
    if (perUnit <= 0.0f) return 0.0f;    // opaque: any crossing at all leaves nothing
    return std::pow(perUnit, distance);
}

/// Inverse of transmitThrough: the per-unit value that yields `tint` after crossing `thickness`.
///
/// POURQUOI cette fonction existe (plan F) : la carte stocke du PAR UNITÉ, parce que le brouillard
/// l'exige — un nuage plus épais doit absorber davantage. Mais l'auteur d'un vitrail énonce la
/// teinte qu'il veut VOIR derrière, pas un coefficient par unité. Sans cet inverse il devrait
/// calculer 0.3^(1/40) à la main, et toute valeur tapée d'instinct (0.3) sortirait en mur opaque.
///
/// COMMENT : `perUnit = tint^(1/thickness)`, de sorte que traverser exactement `thickness` rend
/// `tint`. Traverser plus (un rayon oblique) teinte davantage — c'est la géométrie qui fait le
/// reste, et c'est physiquement juste.
inline float perUnitForTint(float tint, float thickness) {
    // Le vide et l'opacité doivent traverser la conversion EXACTEMENT, pas à epsilon près : un
    // panneau censé ne rien faire doit multiplier par 1 tout rond, sinon un empilement de panneaux
    // neutres assombrirait une scène qui ne contient aucune matière.
    if (tint >= 1.0f) return 1.0f;
    if (tint <= 0.0f) return 0.0f;
    // Une épaisseur dégénérée n'est pas inversible (1/0). Retomber sur "pas de matière" plutôt que
    // produire un NaN, qui empoisonnerait le produit courant et éteindrait le rayon entier.
    if (thickness <= 0.0f) return 1.0f;
    return std::pow(tint, 1.0f / thickness);
}

/// Per-unit transmittance of a medium of absorption coefficient `alpha` (Beer-Lambert).
///
/// transmitThrough(fromDensity(a), d) == exp(−a·d). The law is not bolted on: it is what the
/// multiplicative form already does once the per-unit value is exp(−α).
inline float fromDensity(float alpha) {
    if (alpha <= 0.0f) return 1.0f;
    return std::exp(-alpha);
}

/// Per-unit transmittance of ONE CHANNEL of a medium of density `alpha`, tinted by `channelColor`.
///
/// POURQUOI un medium n'est pas un filtre (plan A vs plan F) : une vitre annonce la teinte obtenue
/// APRES une traversee, parce que son epaisseur est fixe. Un nuage ne le peut pas — tout son interet
/// est qu'aller plus loin dedans absorbe DAVANTAGE. Sa grandeur d'auteur est donc le coefficient
/// alpha de Beer-Lambert, sans borne haute et par unite de longueur par nature.
///
/// COMMENT la couleur agit : `alpha_c = density / channelColor`, donc un canal deux fois plus sombre
/// s'eteint deux fois plus vite (ce qui survit est mis au CARRE). Blanc = neutre, et le medium
/// retombe exactement sur fromDensity ; 0 = ce canal ne passe pas du tout. C'est cette selectivite
/// qui donne les couchers de soleil (le bleu s'eteint avant le rouge) et les nebuleuses teintees.
inline float fogPerUnit(float density, float channelColor) {
    if (density <= 0.0f) return 1.0f;          // pas de matiere = vide, EXACTEMENT
    if (channelColor <= 0.0f) return 0.0f;     // ce canal ne traverse pas : un mur, pour cette longueur d'onde
    const float c = (channelColor > 1.0f) ? 1.0f : channelColor;
    return fromDensity(density / c);
}

/// Running product along one ray: `out[i]` = what survives from the lamp THROUGH sample i.
///
/// A prefix product, not a per-sample value — each cell answers "how much light reaches here",
/// which is what makes a wall shadow everything beyond it instead of only its own pixel. The GPU
/// computes the same thing in log₂(n) ping-pong passes; this O(n) version exists to be right
/// against.
inline void accumulate(const float* perUnit, int n, float step, float* out) {
    if (perUnit == nullptr || out == nullptr || n <= 0) return;
    float running = 1.0f;
    for (int i = 0; i < n; ++i) {
        running *= transmitThrough(perUnit[i], step);
        out[i] = running;
    }
}

} // namespace light
} // namespace grove
