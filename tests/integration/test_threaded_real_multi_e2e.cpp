// ============================================================================
// Phase 2 consolidation: multiple REAL modules hosted THREADED together via the engine.
// The scenario (real UIModule + real SoundManager + glue + parallelism probes, with a
// real cross-module IIO chain) lives in real_multi_scenario.h and is shared with the
// THREAD_POOL lock so the exact same behaviour is asserted against both hosting systems.
//
// RUN: ./test_threaded_real_multi_e2e   (headless; UIModule_static + SoundManagerModule.cpp)
//      exit 0 = the click crossed all three real-hosted modules and the sound fired.
// ============================================================================

#include "real_multi_scenario.h"

int main() {
    spdlog::set_level(spdlog::level::off);
    return grove::real_multi::runRealMultiScenario(
        grove::ModuleSystemType::THREADED, "THREADED (one worker thread per module)");
}
