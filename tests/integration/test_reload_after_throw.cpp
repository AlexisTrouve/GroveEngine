// ============================================================================
// IT — Hot-reload safety when reloading FROM WITHIN a catch handler.
//
// WHAT:  A deterministic reproduction (and regression lock) for the flaky
//        SIGSEGV that ChaosMonkey hits during crash-recovery. It runs the
//        exact crash-recovery shape a game uses — "module threw → reload() →
//        re-register" — but forces a throw on EVERY frame so the reload path
//        is exercised hundreds of times back-to-back, deterministically.
//
// WHY:   ChaosMonkey crashes intermittently after several successful
//        recoveries (heisenbug, un-reproducible under gdb). Root cause: a
//        module throws a std::exception FROM INSIDE ITS DLL. On MinGW each
//        module .dll statically links its OWN copy of std::runtime_error's
//        vtable + destructor + the what() string buffer. The host catches the
//        exception BY REFERENCE and then calls loader.reload() — which
//        FreeLibrary's that DLL — WHILE the caught object is still alive. When
//        the catch scope exits, the runtime runs ~runtime_error() through a
//        vtable that now lives in unmapped memory → use-after-free → SIGSEGV.
//        It is flaky because whether the freed page has been reused/unmapped
//        yet is timing/address-dependent (exactly why gdb can't catch it).
//
//        This is the SAME bug class the loader already fixed for the *state*
//        object (ModuleLoader::reload Step 1b) — but the caller's caught
//        exception lives in the caller's catch scope, out of the loader's reach.
//
// PROOF STRUCTURE (two parts, run in ONE process):
//   Part A — CONTROL (safe pattern): copy what() out, LEAVE the catch (the
//            exception object is destroyed while its DLL is still mapped), and
//            only THEN reload(). This must survive — it isolates the variable
//            and shows clean reload-after-throw is fine.
//   Part B — THE BUG (chaos pattern): call reload() INSIDE the catch, while the
//            exception object is still alive. Pre-fix this UAFs; post-fix
//            (deferred DLL unload) the old DLL stays mapped past the catch
//            scope, so ~e runs against valid code and Part B survives too.
//
//   If Part A survives and Part B crashes, the mechanism is proven beyond the
//   "it crashes sometimes" of ChaosMonkey. After the fix BOTH parts complete
//   and this test is a permanent green lock.
//
// RUN:   from build/tests (loads ./libChaosModule.dll, like ChaosMonkey).
// ============================================================================

#include "grove/ModuleLoader.h"
#include "grove/JsonDataNode.h"
#include <spdlog/spdlog.h>

#include <csignal>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

using namespace grove;

// A SIGSEGV/SIGABRT here means the use-after-free fired: the test process dies
// with a non-zero exit so ctest records a hard failure (you cannot "assert"
// your way around a native crash — surviving the loop IS the assertion).
static void crashHandler(int sig) {
    std::cerr << "\n❌ FATAL: signal " << sig
              << " (SIGSEGV/SIGABRT) — reload-after-throw use-after-free fired.\n";
    std::_Exit(1);  // _Exit: no atexit/global dtors (they would re-enter the UAF)
}

namespace {

#ifdef _WIN32
const char* kModulePath = "./libChaosModule.dll";
#else
const char* kModulePath = "./libChaosModule.so";
#endif

// Build a config that makes ChaosModule throw on (essentially) every frame.
//
// WHY: ChaosModule::getState/setState do NOT carry crashProbability, so a
// reloaded instance reverts to the 0.10 default. To exercise the reload path
// densely and deterministically we re-apply this config to every (re)loaded
// instance, forcing crashProbability = 1.0 → triggerChaosEvent() each frame.
std::unique_ptr<JsonDataNode> makeAlwaysCrashConfig() {
    nlohmann::json j;
    j["seed"] = 1234;
    j["crashProbability"] = 1.0;       // roll < 1.0 is always true → throw every frame
    j["corruptionProbability"] = 0.0;
    j["hotReloadProbability"] = 0.0;
    j["invalidConfigProbability"] = 0.0;
    return std::make_unique<JsonDataNode>("config", j);
}

// Force the freshly (re)loaded instance to keep throwing, and silence its logger
// so the hundreds of recovery cycles don't drown the output.
void armModule(IModule* m) {
    auto cfg = makeAlwaysCrashConfig();
    m->setConfiguration(*cfg, nullptr, nullptr);
    if (auto lg = spdlog::get("ChaosModule")) lg->set_level(spdlog::level::off);
}

// Number of recovery cycles per part. The pre-fix UAF fires within the first
// few dozen cycles (observed ~cycle 37), so 60 reliably reproduces it RED while
// keeping the post-fix GREEN run well inside the ctest timeout (the loader's
// adaptive post-FreeLibrary waits dominate wall time: ~0.5s/reload at steady state).
constexpr int kCycles = 60;

const JsonDataNode kEmptyInput{"input", nlohmann::json::object()};

} // namespace

int main() {
    std::signal(SIGSEGV, crashHandler);
    std::signal(SIGABRT, crashHandler);
    spdlog::set_level(spdlog::level::off);  // keep the engine/loader quiet

    std::cout << "================================================================\n";
    std::cout << "  Reload-after-throw safety  (" << kCycles << " recovery cycles/part)\n";
    std::cout << "================================================================\n";

    // ---------------------------------------------------------------------
    // Part A — CONTROL: reload AFTER the exception object has been destroyed.
    // The catch block only records what() and sets a flag; the reload happens
    // after the catch scope exits, so ~e ran while the DLL was still mapped.
    // This path must always survive (pre- and post-fix).
    // ---------------------------------------------------------------------
    {
        ModuleLoader loader;
        auto mod = loader.load(kModulePath, "ChaosModule", false);
        armModule(mod.get());

        int recovered = 0;
        for (int i = 0; i < kCycles; ++i) {
            bool threw = false;
            try {
                mod->process(kEmptyInput);
            } catch (const std::exception& e) {
                (void)e.what();  // touch it INSIDE the catch — legal, DLL still mapped
                threw = true;
            } // <-- ~e runs HERE, BEFORE any unload(): always safe

            if (threw) {
                mod = loader.reload(std::move(mod));  // reload with NO live exception
                armModule(mod.get());
                ++recovered;
            }
        }
        std::cout << "  ✅ Part A (safe pattern) survived " << recovered
                  << " reloads-after-throw.\n";
    }

    // ---------------------------------------------------------------------
    // Part B — THE BUG: reload INSIDE the catch, while `e` is still alive.
    // Byte-for-byte the ChaosMonkey recovery shape. Pre-fix: ~e at the closing
    // brace dereferences a vtable in the just-FreeLibrary'd DLL → UAF → crash.
    // Post-fix (deferred unload): the DLL outlives this scope → survives.
    // ---------------------------------------------------------------------
    {
        ModuleLoader loader;
        auto mod = loader.load(kModulePath, "ChaosModule", false);
        armModule(mod.get());

        int recovered = 0;
        for (int i = 0; i < kCycles; ++i) {
            try {
                mod->process(kEmptyInput);
            } catch (const std::exception& e) {
                std::cout << "  [cycle " << i << "] caught: " << e.what()
                          << " — reloading while it is alive\n";
                mod = loader.reload(std::move(mod));  // FreeLibrary's the DLL HERE
                armModule(mod.get());
                ++recovered;
            } // <-- ~e runs HERE against the (pre-fix) unmapped DLL → UAF
        }
        std::cout << "  ✅ Part B (chaos pattern) survived " << recovered
                  << " reloads-after-throw.\n";
    }

    std::cout << "================================================================\n";
    std::cout << "  ✅ ALL GOOD — reload-after-throw is memory-safe.\n";
    std::cout << "================================================================\n";
    return 0;
}
