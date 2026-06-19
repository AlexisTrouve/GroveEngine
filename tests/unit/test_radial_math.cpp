// ============================================================================
// test_radial_math.cpp — objective unit test for the action-wheel geometry.
//
// QUOI   : verrouille selectIndex()/itemOffset() (RadialMath.h) avec des oracles
//   analytiques calculés à la main — pas de GPU, pas de widget, pas d'IIO.
// POURQUOI : la sélection d'une roue d'action est ANGULAIRE ; un bug d'angle/sens
//   donnerait un menu qui choisit le mauvais segment. On teste les 4 cardinales, la
//   dead-zone, le hors-bande, le snap hors-axe, le cas 8 segments, et le placement.
// COMMENT : convention item 0 EN HAUT, sens HORAIRE, repère écran (y vers le bas).
// ============================================================================

#include <catch2/catch_test_macros.hpp>
#include "Widgets/RadialMath.h"
#include <cmath>

using namespace grove::radial;

TEST_CASE("radial selectIndex: cardinals map to wedges (top=0, clockwise)", "[unit][radial]") {
    // 4 segments, bande [20,100]. Item 0 en haut, puis horaire : 1=droite, 2=bas, 3=gauche.
    REQUIRE(selectIndex(  0.0f, -50.0f, 20.0f, 100.0f, 4) == 0);  // haut
    REQUIRE(selectIndex( 50.0f,   0.0f, 20.0f, 100.0f, 4) == 1);  // droite
    REQUIRE(selectIndex(  0.0f,  50.0f, 20.0f, 100.0f, 4) == 2);  // bas
    REQUIRE(selectIndex(-50.0f,   0.0f, 20.0f, 100.0f, 4) == 3);  // gauche
}

TEST_CASE("radial selectIndex: dead-zone and out-of-band return -1", "[unit][radial]") {
    REQUIRE(selectIndex(0.0f,  -10.0f, 20.0f, 100.0f, 4) == -1);  // dans le rayon interne (annuler)
    REQUIRE(selectIndex(0.0f, -200.0f, 20.0f, 100.0f, 4) == -1);  // au-delà du rayon externe
    REQUIRE(selectIndex(0.0f,    0.0f, 20.0f, 100.0f, 4) == -1);  // centre exact
}

TEST_CASE("radial selectIndex: off-axis points snap to the nearest wedge", "[unit][radial]") {
    // Plus proche du haut que de la droite -> 0 ; plus proche de la droite -> 1.
    REQUIRE(selectIndex(15.0f, -40.0f, 20.0f, 100.0f, 4) == 0);
    REQUIRE(selectIndex(40.0f, -15.0f, 20.0f, 100.0f, 4) == 1);
}

TEST_CASE("radial selectIndex: 8 wedges, cardinals land on even indices", "[unit][radial]") {
    // 8 segments, secteur = 45°. Haut=0 ; horaire : droite=90°=2, bas=180°=4, gauche=270°=6.
    REQUIRE(selectIndex(  0.0f, -50.0f, 20.0f, 100.0f, 8) == 0);
    REQUIRE(selectIndex( 50.0f,   0.0f, 20.0f, 100.0f, 8) == 2);
    REQUIRE(selectIndex(  0.0f,  50.0f, 20.0f, 100.0f, 8) == 4);
    REQUIRE(selectIndex(-50.0f,   0.0f, 20.0f, 100.0f, 8) == 6);
}

TEST_CASE("radial selectIndex: n<=0 is safe", "[unit][radial]") {
    REQUIRE(selectIndex(50.0f, 0.0f, 20.0f, 100.0f, 0) == -1);
}

TEST_CASE("radial itemOffset: item 0 up, then clockwise (right, bottom, left)", "[unit][radial]") {
    float ox, oy;
    itemOffset(0, 4, 100.0f, ox, oy);
    REQUIRE(std::abs(ox -   0.0f) < 0.01f);  REQUIRE(std::abs(oy - (-100.0f)) < 0.01f);  // haut
    itemOffset(1, 4, 100.0f, ox, oy);
    REQUIRE(std::abs(ox - 100.0f) < 0.01f);  REQUIRE(std::abs(oy -    0.0f)   < 0.01f);  // droite
    itemOffset(2, 4, 100.0f, ox, oy);
    REQUIRE(std::abs(ox -   0.0f) < 0.01f);  REQUIRE(std::abs(oy -  100.0f)   < 0.01f);  // bas
    itemOffset(3, 4, 100.0f, ox, oy);
    REQUIRE(std::abs(ox - (-100.0f)) < 0.01f);  REQUIRE(std::abs(oy - 0.0f)   < 0.01f);  // gauche
}
