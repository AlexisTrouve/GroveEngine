#pragma once

// ============================================================================
// Tonemap.h — les courbes de compression des hautes lumières (grove::light), header-only.
//
// QUOI  : deux courbes qui ramènent une valeur HDR non bornée dans [0,1], plus le réglage
//         d'exposition qui place la scène sur la courbe. Ni renderer, ni IIO, ni GPU — même contrat
//         que Light.h et Bloom.h.
//
// POURQUOI ce chantier existe : les cibles sont en RGBA16F depuis L1 par un arbitrage explicite, dont
//         la justification écrite était « trois lampes superposées et une lampe seule donnent le même
//         blanc ». Or l'image FINALE écrête quand même, puisqu'elle part dans un backbuffer 8 bits :
//         une lampe d'intensité 2 et une d'intensité 8 rendent toutes deux exactement 255. On payait
//         donc deux fois la bande passante pour conserver une information jetée à la dernière ligne.
//
// COMMENT: les deux courbes sont MONOTONES, valent 0 en 0 et ne franchissent jamais 1 — c'est ce qui
//         garantit que deux sur-brillances différentes restent différentes à l'écran. Le shader les
//         mime ; ce fichier est leur oracle.
//
// Plan : docs/design/lighting-tonemap.md
// ============================================================================

#include <cmath>

namespace grove {
namespace light {

// Quelle courbe appliquer. `None` est le DÉFAUT et une identité exacte : le contournement à coût nul
// exprimé jusque dans l'oracle.
enum class TonemapMode {
    None = 0,
    Reinhard,
    ACES,
};

// Reinhard : `x / (1 + x)`.
//
// La seule des deux dont la formule EST la spécification — aucune constante ajustée. Douce et
// prévisible, elle comprime les hautes lumières sans relever le contraste des tons moyens, et elle
// tend vers 1 sans jamais l'atteindre (donc jamais de réécrêtage).
//
// ⚠️ Son point remarquable, qui surprendra : `reinhard(1) = 0,5`. Un jeu qui active le tonemapping
//    sans toucher à `exposure` voit sa scène s'assombrir de moitié. Ce n'est pas un défaut, c'est ce
//    que fait une courbe de compression — mais c'est la première chose qui sera signalée comme tel.
inline float tonemapReinhard(float x) {
    if (!(x > 0.0f)) return 0.0f;         // couvre les négatifs et un NaN entrant
    return x / (1.0f + x);
}

// ACES, approximation polynomiale de Krzysztof Narkowicz (2015).
//
// ⚠️ Ces six constantes sont un AJUSTEMENT EMPIRIQUE à la courbe de référence ACES, pas une
//    dérivation. Elles sont reproduites telles quelles, avec leur provenance, et le test unitaire
//    vérifie les PROPRIÉTÉS (monotone, 0→0, bornée, injective au-dessus de 1) et non les chiffres :
//    un test qui les recopierait ne prouverait que le copier-coller.
//
// Look « filmique » : contraste relevé et épaule qui roule vers le blanc, là où Reinhard reste plat.
// C'est ce qui fait qu'un cœur de lampe blanchit *en gardant sa couleur sur les bords* au lieu de
// devenir un disque uniforme.
inline float tonemapACES(float x) {
    if (!(x > 0.0f)) return 0.0f;
    const float a = 2.51f;
    const float b = 0.03f;
    const float c = 2.43f;
    const float d = 0.59f;
    const float e = 0.14f;
    const float num = x * (a * x + b);
    const float den = x * (c * x + d) + e;
    const float y = num / den;
    // La borne haute est une propriété de l'ajustement, pas une garantie algébrique : on la force,
    // parce qu'un dépassement se réécrêterait et ramènerait le défaut qu'on corrige.
    if (y < 0.0f) return 0.0f;
    return (y > 1.0f) ? 1.0f : y;
}

// La chaîne complète : exposition PUIS courbe.
//
// POURQUOI l'exposition avant et pas après : la courbe est fixe, c'est la scène qu'on déplace dessus.
// Multiplier après compresserait puis rééchelonnerait, donc franchirait 1 et réécrêterait — ce qui
// annulerait tout l'intérêt.
//
// Une exposition nulle ou négative rend 0 : elle n'a pas d'autre sens, et laisser passer un négatif
// donnerait une image inversée là où l'auteur a fait une faute de frappe.
inline float tonemapExposed(float x, float exposure, TonemapMode mode) {
    if (mode == TonemapMode::None) return x;      // identité EXACTE, y compris au-dessus de 1
    if (!(exposure > 0.0f)) return 0.0f;
    const float scaled = x * exposure;
    return (mode == TonemapMode::ACES) ? tonemapACES(scaled) : tonemapReinhard(scaled);
}

} // namespace light
} // namespace grove
