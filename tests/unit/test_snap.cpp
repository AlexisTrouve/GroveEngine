// ============================================================================
// test_snap.cpp — objective unit test for grove::snap::directionalDetent (generic).
//
// QUOI   : verrouille le snap directionnel pur (grove/snap.h) avec des oracles — pas d'engine.
// POURQUOI : c'est le primitif réutilisable derrière le snap de zoom du ZoneNavigator (et d'autres
//   usages : cardinales de rotation, butées de scroll…). La règle clé : on snappe vers un détent
//   DANS LE SENS du dernier mouvement (jamais en arrière), et seulement si on est dans `range`.
// COMMENT : espace LOG (zoom multiplicatif) ou LINÉAIRE (angles), dir +1 haut / -1 bas / 0 plus proche.
// ============================================================================

#include <catch2/catch_test_macros.hpp>
#include <grove/snap.h>

using namespace grove::snap;

TEST_CASE("directionalDetent: moving UP snaps to the nearest detent above (within range)", "[unit][snap]") {
    const float dets[] = {1.0f, 5.0f, 25.0f};
    REQUIRE(directionalDetent(4.3f, dets, 3, +1, 0.3f, true) == 5.0f);   // 4.3 -> 5 (log dist 0.15 < 0.3)
    REQUIRE(directionalDetent(2.2f, dets, 3, +1, 0.3f, true) == 2.2f);   // far below 5 -> free (no snap)
}

TEST_CASE("directionalDetent: moving DOWN snaps to the nearest detent below", "[unit][snap]") {
    const float dets[] = {1.0f, 5.0f, 25.0f};
    REQUIRE(directionalDetent(1.2f, dets, 3, -1, 0.3f, true) == 1.0f);   // 1.2 -> 1 (within range)
    REQUIRE(directionalDetent(4.0f, dets, 3, -1, 0.3f, true) == 4.0f);   // 1 is far below -> free
}

TEST_CASE("directionalDetent: NEVER reverses — moving up ignores detents below", "[unit][snap]") {
    const float dets[] = {1.0f, 5.0f};
    // 5.3 moving up: only detents above 5.3 -> none -> stays (does NOT snap back DOWN to 5).
    REQUIRE(directionalDetent(5.3f, dets, 2, +1, 0.3f, true) == 5.3f);
    // 4.8 moving down: only detents below -> 1 is far, 5 excluded (above) -> stays.
    REQUIRE(directionalDetent(4.8f, dets, 2, -1, 0.3f, true) == 4.8f);
}

TEST_CASE("directionalDetent: dir 0 snaps to the nearest on either side", "[unit][snap]") {
    const float dets[] = {1.0f, 5.0f};
    REQUIRE(directionalDetent(4.3f, dets, 2, 0, 0.3f, true) == 5.0f);    // nearest = 5
    REQUIRE(directionalDetent(2.2f, dets, 2, 0, 0.3f, true) == 2.2f);    // both far -> free
}

TEST_CASE("directionalDetent: linear space (e.g. rotation cardinals in degrees)", "[unit][snap]") {
    const float cards[] = {0.0f, 90.0f, 180.0f, 270.0f};
    REQUIRE(directionalDetent(85.0f, cards, 4, +1, 10.0f, false) == 90.0f);  // up to 90 (dist 5 < 10)
    REQUIRE(directionalDetent(50.0f, cards, 4, +1, 10.0f, false) == 50.0f);  // far -> free
}

TEST_CASE("directionalDetent: empty / zero range are safe", "[unit][snap]") {
    const float dets[] = {5.0f};
    REQUIRE(directionalDetent(4.3f, nullptr, 0, +1, 0.3f, true) == 4.3f);    // no detents
    REQUIRE(directionalDetent(4.3f, dets, 1, +1, 0.0f, true) == 4.3f);       // range 0 -> no snap
}
