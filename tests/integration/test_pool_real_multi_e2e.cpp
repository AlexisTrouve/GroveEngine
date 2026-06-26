// ============================================================================
// Phase 3 slice 2: multiple REAL modules hosted in the THREAD_POOL together via the engine.
// Same scenario as the THREADED lock (real UIModule + real SoundManager + glue + probes,
// real cross-module IIO chain), but registerStaticModule(THREAD_POOL) routes every module
// into the SHARED ThreadPoolModuleSystem (work-stealing pool). This is the proof that the
// pool's engine integration works in real conditions — not just the standalone system.
//
// RUN: ./test_pool_real_multi_e2e   (headless; UIModule_static + SoundManagerModule.cpp)
//      exit 0 = the click crossed all three pool-hosted real modules and the sound fired.
// ============================================================================

#include "real_multi_scenario.h"

int main() {
    spdlog::set_level(spdlog::level::off);
    return grove::real_multi::runRealMultiScenario(
        grove::ModuleSystemType::THREAD_POOL, "THREAD_POOL (shared work-stealing pool)");
}
