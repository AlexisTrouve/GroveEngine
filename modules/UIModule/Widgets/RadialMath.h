#pragma once

// ============================================================================
// RadialMath.h — pure geometry for the action-wheel (radial menu) widget.
//
// QUOI   : conversion direction->segment et position des segments sur un cercle.
// POURQUOI : aucune dépendance moteur/IIO/widget -> testable headless avec des
//   oracles analytiques (même esprit que LodColor.h / AtlasSlice.h). Surtout, la
//   sélection d'une roue d'action est ANGULAIRE (une direction depuis le centre),
//   et c'est précisément ce qui la rend input-agnostique : un delta souris, un
//   vecteur de stick manette ou un step clavier se réduisent tous au même (dx,dy).
// COMMENT : trigonométrie pure, repère écran (y vers le BAS), item 0 EN HAUT, sens
//   HORAIRE — la convention naturelle d'un pie/radial menu.
// ============================================================================

#include <cmath>

namespace grove::radial {

// Constantes locales (évite de dépendre de M_PI, non portable).
constexpr float kPi    = 3.14159265358979323846f;
constexpr float kTwoPi = 2.0f * kPi;

// ----------------------------------------------------------------------------
// selectIndex : segment visé par un offset (dx,dy) depuis le centre, ou -1.
//
// QUOI   : mappe une direction (dx,dy) sur l'un des n segments répartis en cercle.
// POURQUOI : sélection directionnelle = cœur input-agnostique (souris/stick/clavier
//   produisent tous un (dx,dy)). La dead-zone centrale sert à ANNULER (revenir au
//   centre), le hors-rayon signifie "pas sur la roue".
// COMMENT :
//   1. dist = hypot(dx,dy). Hors de la bande [inner, outer] -> -1.
//   2. angle 0 EN HAUT, sens HORAIRE : a = atan2(dx, -dy).
//      (haut: dx=0,-dy>0 -> atan2(0,+)=0 ; droite: dx>0,-dy=0 -> atan2(+,0)=+pi/2).
//      Normalisé sur [0, 2pi).
//   3. sector = 2pi/n ; index = round(a / sector) mod n (item 0 centré en haut, donc
//      on arrondit au centre de segment le plus proche). round peut tomber sur n
//      (près de 2pi) -> le mod n le ramène à 0 (l'item du haut), ce qui est correct.
inline int selectIndex(float dx, float dy, float inner, float outer, int n) {
    if (n <= 0) return -1;
    const float dist = std::sqrt(dx * dx + dy * dy);
    if (dist < inner || dist > outer) return -1;

    float a = std::atan2(dx, -dy);            // 0 = haut, horaire positif
    if (a < 0.0f) a += kTwoPi;                // normalise sur [0, 2pi)

    const float sector = kTwoPi / static_cast<float>(n);
    int idx = static_cast<int>(std::lround(a / sector));  // centre de segment le plus proche
    idx %= n;                                 // wrap (round peut atteindre n)
    return idx;
}

// ----------------------------------------------------------------------------
// itemOffset : offset écran (ox,oy) du point d'ancrage du segment i, sur l'anneau.
//
// QUOI   : où poser visuellement le segment i (icône/label) à un rayon donné.
// POURQUOI : le rendu doit placer chaque item au même endroit que la sélection le
//   teste -> on dérive les deux de la même convention (item 0 en haut, horaire).
// COMMENT : theta = i*2pi/n ; ox = r*sin(theta), oy = -r*cos(theta) (y écran vers le
//   bas). i=0 -> (0,-r) = haut ; i pour theta=pi/2 -> (r,0) = droite.
inline void itemOffset(int i, int n, float radius, float& ox, float& oy) {
    const float theta = (n > 0)
        ? (kTwoPi * static_cast<float>(i) / static_cast<float>(n))
        : 0.0f;
    ox = radius * std::sin(theta);
    oy = -radius * std::cos(theta);
}

} // namespace grove::radial
