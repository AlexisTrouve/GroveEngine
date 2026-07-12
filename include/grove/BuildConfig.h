#pragma once

// ============================================================================
// BuildConfig.h — the debug/prod build-configuration switch for GroveEngine.
//
// QUOI  : expose ONE always-defined symbol, GROVE_DEBUG (1 or 0), plus a constexpr
//         mirror grove::kDebugBuild and a GROVE_DEBUG_ONLY(...) helper, so the whole
//         engine can strip its "debug skin" (introspection + verbose per-frame logging)
//         from a shipping build at compile time.
//
// POURQUOI: research (Unreal / Unity / Godot) is unanimous — debug vs prod is a BUILD
//         CONFIGURATION of ONE engine, not two engine classes. We keep the single engine
//         (DebugEngine) and gate the debug-only cost behind this flag. GROVE_DEBUG=ON
//         (the CMake default) = full introspection + logging; GROVE_DEBUG=OFF (a shipping
//         build, `cmake -DGROVE_DEBUG=OFF`) = the debug code compiled OUT, zero runtime
//         cost, identical prod core (threading / clock / streaming / save / IIO health).
//
// COMMENT: the CMake option GROVE_DEBUG sets the compile definition GROVE_DEBUG_BUILD
//         (1 or 0) on the grove_core INTERFACE target, which propagates to every target
//         that links it (grove_impl + tests + the game). We normalize that raw definition
//         into GROVE_DEBUG here so downstream code tests ONE stable symbol and never has
//         to reason about "is the macro even defined?". If the definition never reached a
//         translation unit (a consumer that didn't link grove_core), we default to the
//         SAFE side — debug=1 (verbose, nothing stripped) — rather than silently shipping
//         a stripped build the author didn't ask for. A broken OFF plumbing is still caught
//         by BuildConfigUnit, which is compiled under both configs and asserts the flip.
// ============================================================================

// 1. Normalize the CMake-provided definition into an always-present value.
//    (Missing → default to debug, the safe/verbose side. See COMMENT above.)
#if !defined(GROVE_DEBUG_BUILD)
    #define GROVE_DEBUG_BUILD 1
#endif

// 2. The single public switch the rest of the engine tests.
#define GROVE_DEBUG GROVE_DEBUG_BUILD

namespace grove {

// 3. constexpr mirror of GROVE_DEBUG — lets code branch with `if constexpr (kDebugBuild)`
//    at a use site where a runtime-looking branch reads cleaner than a raw macro, while the
//    dead branch is still fully eliminated by the optimizer.
inline constexpr bool kDebugBuild = (GROVE_DEBUG != 0);

} // namespace grove

// 4. GROVE_DEBUG_ONLY(...) — expands to its argument(s) in a debug build, to NOTHING in a
//    shipping build. Wrap a whole statement (e.g. a verbose log call whose arguments must
//    not even be evaluated when stripped) so the call site vanishes entirely under OFF.
//    Variadic so commas inside the wrapped expression don't break the macro.
#if GROVE_DEBUG
    #define GROVE_DEBUG_ONLY(...) __VA_ARGS__
#else
    #define GROVE_DEBUG_ONLY(...)
#endif
