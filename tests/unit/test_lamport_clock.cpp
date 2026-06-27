// ============================================================================
// test_lamport_clock.cpp — objective unit test for grove::LamportClock (pure, headless).
//
// QUOI   : verrouille l'horloge logique de Lamport (grove/LamportClock.h) — la primitive
//   de CAUSALITÉ de l'enveloppe (contrat IO §5). `tick()` = un événement local / un envoi ;
//   `update(received)` = une réception = `max(local, received) + 1`. C'est ce qui donne un
//   ordre total cohérent avec la causalité, reconstructible offline (replay).
// POURQUOI : Lamport n'est PAS un compteur wall-clock ni un simple ++ partagé. La règle
//   `max+1` à la réception est subtile et load-bearing : sans elle, "A cause B" peut se
//   retrouver avec stamp(B) <= stamp(A) → ordre causal cassé. Sans ce test = une affirmation.
// COMMENT : oracles purs, pas d'engine/IIO/thread. La sérialisation thread est la
//   responsabilité de l'APPELANT (le transport, sous son lock d'instance) — ici on lock la
//   LOGIQUE, mono-thread.
// ============================================================================

#include <catch2/catch_test_macros.hpp>

#include <grove/LamportClock.h>

#include <cstdint>

using namespace grove;

TEST_CASE("LamportClock - a fresh clock is at 0", "[lamport][unit]") {
    LamportClock c;
    REQUIRE(c.value() == 0u);
}

TEST_CASE("LamportClock - tick() increments by 1 and returns the new value", "[lamport][unit]") {
    LamportClock c;
    REQUIRE(c.tick() == 1u);
    REQUIRE(c.tick() == 2u);
    REQUIRE(c.tick() == 3u);
    REQUIRE(c.value() == 3u);
}

TEST_CASE("LamportClock - update() with a HIGHER received jumps to received+1", "[lamport][unit]") {
    LamportClock c;                 // value 0
    REQUIRE(c.update(5) == 6u);     // max(0,5)+1 = 6 — adopt the sender's time, then +1
    REQUIRE(c.value() == 6u);
}

TEST_CASE("LamportClock - update() with a LOWER received still advances local by 1", "[lamport][unit]") {
    LamportClock c;
    c.tick(); c.tick(); c.tick();   // value 3
    REQUIRE(c.update(1) == 4u);     // max(3,1)+1 = 4 — a stale stamp never rewinds us
    REQUIRE(c.value() == 4u);
}

TEST_CASE("LamportClock - update() with an EQUAL received yields received+1", "[lamport][unit]") {
    LamportClock c;
    c.update(7);                    // value 8
    REQUIRE(c.update(8) == 9u);     // max(8,8)+1 = 9 — tie still strictly advances
}

TEST_CASE("LamportClock - causality: a received event is always stamped strictly after its cause", "[lamport][unit]") {
    // A publishes (stamps its message), B receives it. B's logical time must end up strictly
    // greater than the message's stamp — "B happened after the cause it just consumed".
    LamportClock a, b;
    const uint64_t sent = a.tick();         // A's send stamp
    const uint64_t afterRecv = b.update(sent);
    REQUIRE(afterRecv > sent);              // effect strictly after cause

    // And a reply from B back to A is stamped after B's receive — the full chain stays ordered.
    const uint64_t reply = b.tick();
    REQUIRE(reply > afterRecv);
    REQUIRE(a.update(reply) > reply);
}

TEST_CASE("LamportClock - value is strictly monotonic across mixed tick/update", "[lamport][unit]") {
    LamportClock c;
    uint64_t prev = c.value();
    const uint64_t feeds[] = {0, 100, 3, 3, 99, 101};
    for (uint64_t f : feeds) {
        const uint64_t now = c.update(f);
        REQUIRE(now > prev);        // every operation strictly increases logical time
        prev = now;
        const uint64_t t = c.tick();
        REQUIRE(t > prev);
        prev = t;
    }
}
