// ============================================================================
// Phase 2 consolidation: MULTIPLE REAL modules hosted THREADED together on the
// engine, with a real cross-module IIO chain — strong assertions, no GPU/SDL.
//
// WHAT: three+ modules hosted via registerStaticModule(THREADED), driven only by
//   engine.step(). A real click drives a REAL chain that crosses three hosted modules:
//
//     input click ─▶ UIModule (real) ─▶ "ship:part" ─▶ GameCollector ─▶ "sound:sfx"
//                                                                          │
//                                                          SoundManager (real) ─▶ MockBackend.playSound
//
//   UIModule and SoundManager are the actual production modules (UIModule headless;
//   SoundManager driven through its MockSoundBackend — no SDL_mixer). GameCollector is
//   the "game" glue that reacts to a selected part by asking for a sound. Two WorkerProbe
//   modules run alongside so the in-flight probe records >=2 process() bodies at once.
//
// WHY: the prior "real module" threaded tests either load GPU .dlls (skipped headless) or
//   are actually synthetic, and host a SINGLE module or via system->registerModule direct
//   (not the engine path). NOTHING proved several REAL modules hosted THREADED *together*
//   through the engine, exchanging IIO across threads, with real behaviour asserted. The
//   renderer (bgfx::init) and InputModule (SDL+display) can't run headless, so they can't
//   be in a CI regression — UIModule + SoundManager are the real, headless pair. This is
//   that lock. (Raw threaded-hosting parallelism + race-freedom is already TSan-proven by
//   ThreadedHostingE2E; this test asserts cross-module BEHAVIOUR.)
//
// RUN: ./test_threaded_real_multi_e2e   (headless; UIModule_static + SoundManagerModule.cpp)
//      exit 0 = the click crossed all three real-hosted modules and the sound fired.
// ============================================================================

#include <grove/DebugEngine.h>
#include <grove/IModuleSystem.h>
#include <grove/IModule.h>
#include <grove/IIO.h>
#include <grove/IntraIOManager.h>
#include <grove/IntraIO.h>
#include <grove/JsonDataNode.h>

#include "UIModule.h"                              // real UIModule (UIModule_static)
#include "SoundManager/SoundManagerModule.h"       // real SoundManager (compiled in)
#include "../mocks/MockSoundBackend.h"             // headless audio backend

#include <nlohmann/json.hpp>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace grove;
using nlohmann::json;

namespace {

// --- Concurrency probe: do hosted modules' process() bodies overlap in TIME? ----------
std::atomic<int> g_inProcess{0};
std::atomic<int> g_maxConcurrent{0};
struct ProcGuard {
    ProcGuard() {
        int n = g_inProcess.fetch_add(1) + 1;
        int prev = g_maxConcurrent.load();
        while (n > prev && !g_maxConcurrent.compare_exchange_weak(prev, n)) {}
        // Widen the overlap window. The sleep parks the thread (frees the core), so another
        // worker can enter ITS ProcGuard while this one is held → g_inProcess reaches >=2. With
        // 5 hosted modules (incl. the heavier real UIModule/SoundManager) the worker wake-up
        // jitter exceeds a few hundred µs, so a 2ms window is needed to observe the overlap
        // reliably rather than flakily.
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    ~ProcGuard() { g_inProcess.fetch_sub(1); }
};

// IModule boilerplate (mirrors the threaded-hosting test's ChainModule).
class BaseModule : public IModule {
public:
    const IDataNode& getConfiguration() override {
        if (!cfg_) cfg_ = std::make_unique<JsonDataNode>("config", json::object());
        return *cfg_;
    }
    std::unique_ptr<IDataNode> getHealthStatus() override {
        return std::make_unique<JsonDataNode>("health", json{{"status", "healthy"}});
    }
    void shutdown() override {}
    std::unique_ptr<IDataNode> getState() override {
        return std::make_unique<JsonDataNode>("state", json::object());
    }
    void setState(const IDataNode&) override {}
    bool isIdle() const override { return true; }
protected:
    IIO* io_ = nullptr;
    std::unique_ptr<IDataNode> cfg_;
};

// GameCollector — the "game" glue, hosted threaded. Reacts to a selected ship part by
// asking SoundManager to play a sound (so the chain crosses three hosted modules), and
// counts the sprites UIModule emits. Its handlers mutate state on its OWN worker thread
// (archi A: worker drains its inbox after process()), then process() reads it.
class GameCollectorModule : public BaseModule {
public:
    void setConfiguration(const IDataNode&, IIO* io, ITaskScheduler*) override {
        io_ = io;
        io_->subscribe("ship:part", [this](const Message& m) {
            ++partEvents_;
            if (m.data) lastPart_ = m.data->getString("id", "");
            // The game plays a UI sound when a part is selected → crosses into SoundManager.
            if (io_) {
                auto s = std::make_unique<JsonDataNode>("sfx");
                s->setString("path", "select.wav");
                io_->publish("sound:sfx", std::move(s));
            }
        });
        io_->subscribe("render:sprite:add", [this](const Message&) { ++sprites_; });
    }
    void process(const IDataNode&) override { ProcGuard _pg; lastSeenSprites_ = sprites_; }
    std::string getType() const override { return "GameCollectorModule"; }
    int partEvents() const { return partEvents_; }
    std::string lastPart() const { return lastPart_; }
    int sprites() const { return sprites_; }
private:
    int partEvents_ = 0, sprites_ = 0, lastSeenSprites_ = 0;
    std::string lastPart_;
};

// WorkerProbe — pure parallelism filler: real work on its worker thread so g_maxConcurrent
// can reach >=2 alongside GameCollector. Mutates its own state (no cross-thread sharing).
class WorkerProbeModule : public BaseModule {
public:
    void setConfiguration(const IDataNode&, IIO* io, ITaskScheduler*) override { io_ = io; }
    void process(const IDataNode&) override { ProcGuard _pg; ++count_; }
    std::string getType() const override { return "WorkerProbeModule"; }
private:
    int count_ = 0;
};

} // namespace

int main() {
    spdlog::set_level(spdlog::level::off);

    auto& mgr = IntraIOManager::getInstance();
    auto hostPub = mgr.createInstance("trm_host");   // injects input + ui:data
    auto game    = mgr.createInstance("trm_game");   // observes outputs

    DebugEngine engine;
    engine.initialize();

    // --- Host the REAL UIModule (threaded) ---
    {
        auto cfg = std::make_unique<JsonDataNode>("config");
        cfg->setInt("windowWidth", 1280);
        cfg->setInt("windowHeight", 720);
        cfg->setString("layoutFile", "../../assets/ui/test_e2e_blueprint.json");
        cfg->setInt("baseLayer", 1000);
        engine.registerStaticModule("trm_ui", std::make_unique<UIModule>(),
                                    ModuleSystemType::THREADED, std::move(cfg));
    }

    // --- Host the REAL SoundManager (threaded) with a headless MockBackend ---
    test::MockSoundBackend* mock = nullptr;
    {
        auto sound = std::make_unique<SoundManagerModule>();
        auto backend = std::make_unique<test::MockSoundBackend>();
        mock = backend.get();
        sound->setBackend(std::move(backend));     // wire backend BEFORE the engine takes the module
        engine.registerStaticModule("trm_sound", std::move(sound),
                                    ModuleSystemType::THREADED, std::make_unique<JsonDataNode>("config"));
    }

    // --- Host the game glue + parallelism probes (threaded) ---
    auto collector = std::make_unique<GameCollectorModule>();
    GameCollectorModule* col = collector.get();
    engine.registerStaticModule("trm_collector", std::move(collector),
                                ModuleSystemType::THREADED, std::make_unique<JsonDataNode>("config"));
    engine.registerStaticModule("trm_probeA", std::make_unique<WorkerProbeModule>(),
                                ModuleSystemType::THREADED, std::make_unique<JsonDataNode>("config"));
    engine.registerStaticModule("trm_probeB", std::make_unique<WorkerProbeModule>(),
                                ModuleSystemType::THREADED, std::make_unique<JsonDataNode>("config"));

    // External observer of the final outputs.
    std::string obsPart;
    game->subscribe("ship:part", [&](const Message& m) { obsPart = m.data->getString("id", ""); });

    auto step   = [&]{ engine.step(1.0f / 60.0f); while (game->hasMessages() > 0) game->pullAndDispatch(); };
    auto settle = [&](int n){ for (int i = 0; i < n; ++i) step(); };
    auto move   = [&](double x, double y){ auto d = std::make_unique<JsonDataNode>("d"); d->setDouble("x", x); d->setDouble("y", y); hostPub->publish("input:mouse:move", std::move(d)); settle(3); };
    auto btn    = [&](bool p){ auto d = std::make_unique<JsonDataNode>("d"); d->setInt("button", 0); d->setBool("pressed", p); hostPub->publish("input:mouse:button", std::move(d)); settle(3); };

    settle(5);  // boot all workers

    // Ship blueprint (same as IT_044 / ThreadedRealUIE2E).
    auto part = [](const char* id, int x, int y, int w, int h, const char* col, int tex, const char* lbl){
        return json{ {"id",id},{"x",x},{"y",y},{"w",w},{"h",h},{"color",col},{"tex",tex},{"label",lbl},{"stat","..."} };
    };
    hostPub->publish("ui:data", std::make_unique<JsonDataNode>("d", json{
        {"ship", {{"name","S.S. Aurora"},{"parts", json::array({
            part("cockpit",160,10,80,80,  "0xFFFFFFFF",1,"Cockpit"),
            part("hullA",  140,90,120,44, "0x3a4a63FF",0,"Coque avant"),
            part("gunL",   66,100,64,64,  "0xFFFFFFFF",4,"Canon babord"),
            part("reactor",160,190,80,80, "0xFFFFFFFF",2,"Reacteur"),
            part("engL",   108,312,70,92, "0xFFFFFFFF",3,"Moteur babord") })}}},
        {"noPart", true}, {"selectedPart", {{"label",""},{"stat",""}}} }));
    settle(6);

    g_maxConcurrent.store(0);  // measure parallelism over the active part of the run

    // The real click → crosses UIModule → ship:part → GameCollector → sound:sfx → SoundManager.
    move(442, 192); btn(true); btn(false);
    settle(12);  // let the 3-module cascade propagate across worker threads

    // Snapshot BEFORE shutdown (shutdown destroys the hosted modules).
    const int   sfxPlays  = mock ? static_cast<int>(mock->playSoundCalls.size()) : -1;
    const int   maxc      = g_maxConcurrent.load();
    const std::string colPart = col->lastPart();
    const int   colSprites = col->sprites();
    const int   colEvents  = col->partEvents();

    engine.shutdown();
    mgr.removeInstance("trm_host");
    mgr.removeInstance("trm_game");

    const bool uiClick   = (obsPart == "cockpit");            // UIModule (real, threaded) handled the click
    const bool collected = (colPart == "cockpit" && colSprites > 0);  // glue saw cross-module IIO
    const bool soundFired= (sfxPlays >= 1);                   // chain reached SoundManager (real, threaded)
    const bool parallel  = (maxc >= 2);                       // real parallel execution

    std::printf("================================================================\n");
    std::printf("  Phase 2 — multiple REAL modules hosted THREADED together\n");
    std::printf("  UIModule click -> ship:part        : %s (obs='%s')\n", uiClick ? "OK" : "FAIL", obsPart.c_str());
    std::printf("  cross-module IIO (glue saw part)   : %s (part='%s' sprites=%d events=%d)\n",
                collected ? "OK" : "FAIL", colPart.c_str(), colSprites, colEvents);
    std::printf("  chain reached SoundManager (real)  : %s (playSound calls=%d)\n", soundFired ? "OK" : "FAIL", sfxPlays);
    std::printf("  real parallelism (maxConc>=2)      : %s (maxConc=%d)\n", parallel ? "OK" : "FAIL", maxc);
    const bool ok = uiClick && collected && soundFired && parallel;
    std::printf(ok ? "  ✅ Several REAL modules work hosted THREADED together via the engine.\n"
                   : "  ❌ Multi-real-module threaded hosting BROKEN.\n");
    std::printf("================================================================\n");
    return ok ? 0 : 1;
}
