// ============================================================================
// BuildConfigUnit — locks the GROVE_DEBUG build-configuration switch (A0).
//
// QUOI  : verify the debug/prod build flag plumbs through correctly: the constexpr mirror
//         grove::kDebugBuild agrees with the GROVE_DEBUG macro, and each build config sees
//         the value it should (ON → true, OFF → false).
//
// POURQUOI: this is a BUILD-configuration flag, so a single binary can only prove ONE side.
//         We follow the sanitizer-gate precedent: this test is compiled under BOTH configs.
//         The default (debug) CTest run locks GROVE_DEBUG=ON → true; a dedicated
//         `cmake -DGROVE_DEBUG=OFF` build runs the SAME test and locks OFF → false. A broken
//         CMake plumbing (definition never propagated, or the OFF branch not stripped) makes
//         one of the two runs fail — that's the prove-it-bites.
//
// COMMENT: the assertions are #if-selected on GROVE_DEBUG so the file passes in whichever
//         config it's compiled under while still asserting the correct, config-specific fact.
//         STATIC_REQUIRE checks the constexpr mirror at compile time (a mismatch won't even
//         build), catching a BuildConfig.h that derives kDebugBuild wrong.
// ============================================================================

#include <catch2/catch_test_macros.hpp>

#include <grove/BuildConfig.h>

// The constexpr mirror must always equal the macro — a divergence means BuildConfig.h is
// deriving kDebugBuild incorrectly. Checked at compile time in both configs.
TEST_CASE("BuildConfig: constexpr mirror matches the macro", "[buildconfig]") {
    STATIC_REQUIRE(grove::kDebugBuild == (GROVE_DEBUG != 0));
}

// The flag is always defined (never an "is it defined?" ambiguity) — BuildConfig.h guarantees it.
TEST_CASE("BuildConfig: GROVE_DEBUG is always defined", "[buildconfig]") {
#if !defined(GROVE_DEBUG)
    FAIL("GROVE_DEBUG must always be defined by BuildConfig.h");
#endif
    SUCCEED("GROVE_DEBUG is defined");
}

// Config-specific truth: ON (default) → debug skin present; OFF (shipping) → stripped.
TEST_CASE("BuildConfig: value reflects the active build config", "[buildconfig][config]") {
#if GROVE_DEBUG
    // Default debug build: introspection + verbose logging are compiled in.
    REQUIRE(grove::kDebugBuild == true);
    REQUIRE(GROVE_DEBUG == 1);
#else
    // Shipping build (-DGROVE_DEBUG=OFF): the debug skin is stripped.
    REQUIRE(grove::kDebugBuild == false);
    REQUIRE(GROVE_DEBUG == 0);
#endif
}

// GROVE_DEBUG_ONLY(...) expands to its argument in debug, to nothing in shipping. Prove it by
// letting the wrapped statement move a witness variable ONLY when the debug skin is present.
TEST_CASE("BuildConfig: GROVE_DEBUG_ONLY strips its body in shipping", "[buildconfig][strip]") {
    int witness = 0;
    GROVE_DEBUG_ONLY(witness = 42;)  // vanishes entirely under GROVE_DEBUG=OFF
#if GROVE_DEBUG
    REQUIRE(witness == 42);  // debug: the wrapped statement ran
#else
    REQUIRE(witness == 0);   // shipping: the wrapped statement was compiled out
#endif
}
