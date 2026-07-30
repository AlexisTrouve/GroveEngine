// ============================================================================
// AccessGuardUnit — locks the concurrency tripwire (G1).
//
// QUOI  : ScopedAccessGuard must flag a CONCURRENT overlap (a second thread enters while the first
//         is still inside) and must NOT flag a sequential handoff (thread A leaves, then thread B
//         enters). In a shipping build the guard is a no-op.
//
// POURQUOI: this tripwire is what turns the "one owning thread per instance" invariant from a
//         silent-heap-corruption footgun into a loud, actionable failure. A deterministic overlap
//         (forced via an atomic handshake — no timing luck) proves it bites exactly on the hazard.
// ============================================================================

#include <catch2/catch_test_macros.hpp>

#include <grove/detail/AccessGuard.h>

#include <atomic>
#include <thread>

TEST_CASE("ScopedAccessGuard flags concurrent overlap, not sequential handoff", "[iio][guard][config]") {
#if GROVE_DEBUG
    grove::detail::AccessState state;
    std::atomic<bool> t1Inside{false};
    std::atomic<bool> release{false};
    const auto before = grove::detail::accessViolationCount().load();

    // Thread 1 enters the guarded section and HOLDS it until released.
    std::thread t1([&] {
        grove::detail::ScopedAccessGuard g(state, "publish", "inst-X");
        t1Inside.store(true);
        while (!release.load()) std::this_thread::yield();
    });
    while (!t1Inside.load()) std::this_thread::yield();

    // t1 is provably inside → entering here is a real OVERLAP → must be flagged exactly once.
    { grove::detail::ScopedAccessGuard g2(state, "publish", "inst-X"); }
    const auto afterOverlap = grove::detail::accessViolationCount().load();
    REQUIRE(afterOverlap == before + 1);

    release.store(true);
    t1.join();
    REQUIRE(state.active.load() == 0);   // both left the section cleanly

    // Sequential entry (nobody else inside) must NOT be flagged.
    { grove::detail::ScopedAccessGuard g3(state, "publish", "inst-X"); }
    REQUIRE(grove::detail::accessViolationCount().load() == afterOverlap);
#else
    // Shipping: the guard is an empty no-op — it never counts anything.
    grove::detail::AccessState state;
    const auto before = grove::detail::accessViolationCount().load();
    { grove::detail::ScopedAccessGuard g(state, "publish", "x"); }
    REQUIRE(grove::detail::accessViolationCount().load() == before);
#endif
}

// ============================================================================
// RE-ENTRANCE PAR LE MEME FIL — le cas que le compteur seul ne sait pas distinguer.
// ============================================================================

TEST_CASE("ScopedAccessGuard ne signale PAS une re-entrance du meme fil", "[iio][guard][config]") {
#if GROVE_DEBUG
    // POURQUOI ce test : `pullAndDispatch` garde TOUTE la fonction, y compris l'invocation des
    // callbacks utilisateur -- et un callback qui republie en reponse rentre alors dans la section
    // `publish` de la MEME instance, sur le MEME fil. Le compteur voit prev != 0 et criait a la
    // course, alors qu'il n'y a qu'un seul fil. Or republier depuis un handler est un motif
    // DOCUMENTE et supporte (c'est precisement ce que la correlation `causedBy` sert a tracer).
    //
    // L'enjeu depasse le faux positif : un garde-fou qui crie au loup sur un cas normal finit
    // ignore, et une VRAIE course se cache alors dans le bruit.
    grove::detail::AccessState state;
    const auto before = grove::detail::accessViolationCount().load();

    {
        grove::detail::ScopedAccessGuard outer(state, "pullAndDispatch", "inst-reentrant");
        {
            // Le handler republie : imbrication sur le meme fil, PAS un chevauchement.
            grove::detail::ScopedAccessGuard inner(state, "publish", "inst-reentrant");
            REQUIRE(state.active.load() == 2);   // deux niveaux, un seul fil
        }
    }

    REQUIRE(state.active.load() == 0);
    // La propriete est bien relachee : un AUTRE fil peut ensuite revendiquer la section sans etre
    // pris pour un intrus (passation sequentielle apres une imbrication).
    REQUIRE(state.owner.load() == 0);
    REQUIRE(grove::detail::accessViolationCount().load() == before);   // AUCUNE violation
#endif
}
