#pragma once
// ============================================================================
// Shared scenario: several REAL modules hosted TOGETHER on the engine under a chosen
// strategy, with a real cross-module IIO chain. Used by both the THREADED and the
// THREAD_POOL E2E locks so the exact same behaviour is asserted against both hosting
// systems (DRY — the only difference is the ModuleSystemType passed in).
//
//   click ─▶ UIModule (real) ─▶ "ship:part" ─▶ GameCollector ─▶ "sound:sfx"
//                                                                   │
//                                                 SoundManager (real) ─▶ MockBackend.playSound
//
// UIModule + SoundManager are the real production modules (UIModule headless; SoundManager
// via MockSoundBackend — no SDL_mixer). GameCollector is the "game" glue; two WorkerProbe
// modules run alongside so the in-flight probe records >=2 process() bodies at once.
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

namespace grove {
namespace real_multi {

using nlohmann::json;

// --- Concurrency probe: do hosted modules' process() bodies overlap in TIME? ----------
// inline globals (C++17) so the header can be included by both .cpp without ODR clashes.
inline std::atomic<int> g_inProcess{0};
inline std::atomic<int> g_maxConcurrent{0};
struct ProcGuard {
    ProcGuard() {
        int n = g_inProcess.fetch_add(1) + 1;
        int prev = g_maxConcurrent.load();
        while (n > prev && !g_maxConcurrent.compare_exchange_weak(prev, n)) {}
        // Widen the overlap window: the sleep parks the thread (frees the core) so another
        // worker can enter ITS ProcGuard while this one is held → g_inProcess reaches >=2.
        // With 5 hosted modules incl. the heavier real ones, wake-up jitter exceeds a few
        // hundred µs, so 2ms is needed to observe the overlap reliably rather than flakily.
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

// GameCollector — the "game" glue. Reacts to a selected ship part by asking SoundManager to
// play a sound (so the chain crosses three hosted modules), and counts UIModule's sprites.
// Its handlers mutate state on its OWN worker (archi A: worker drains its inbox after process()).
class GameCollectorModule : public BaseModule {
public:
    void setConfiguration(const IDataNode&, IIO* io, ITaskScheduler*) override {
        io_ = io;
        io_->subscribe("ship:part", [this](const Message& m) {
            ++partEvents_;
            if (m.data) lastPart_ = m.data->getString("id", "");
            if (io_) {
                auto s = std::make_unique<JsonDataNode>("sfx");
                s->setString("path", "select.wav");
                io_->publish("sound:sfx", std::move(s));   // crosses into SoundManager
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

// WorkerProbe — parallelism filler: real work on its worker so g_maxConcurrent reaches >=2.
class WorkerProbeModule : public BaseModule {
public:
    void setConfiguration(const IDataNode&, IIO* io, ITaskScheduler*) override { io_ = io; }
    void process(const IDataNode&) override { ProcGuard _pg; ++count_; }
    std::string getType() const override { return "WorkerProbeModule"; }
private:
    int count_ = 0;
};

// Run the scenario against `strategy`. `label` names the hosting system in the report.
// Returns 0 if every property holds, 1 otherwise.
inline int runRealMultiScenario(ModuleSystemType strategy, const char* label) {
    auto& mgr = IntraIOManager::getInstance();
    auto hostPub = mgr.createInstance("rmm_host");   // injects input + ui:data
    auto game    = mgr.createInstance("rmm_game");   // observes outputs

    DebugEngine engine;
    engine.initialize();

    // Host the REAL UIModule under the chosen strategy.
    {
        auto cfg = std::make_unique<JsonDataNode>("config");
        cfg->setInt("windowWidth", 1280);
        cfg->setInt("windowHeight", 720);
        cfg->setString("layoutFile", "../../assets/ui/test_e2e_blueprint.json");
        cfg->setInt("baseLayer", 1000);
        engine.registerStaticModule("rmm_ui", std::make_unique<UIModule>(), strategy, std::move(cfg));
    }

    // Host the REAL SoundManager with a headless MockBackend.
    test::MockSoundBackend* mock = nullptr;
    {
        auto sound = std::make_unique<SoundManagerModule>();
        auto backend = std::make_unique<test::MockSoundBackend>();
        mock = backend.get();
        sound->setBackend(std::move(backend));     // wire backend BEFORE the engine takes the module
        engine.registerStaticModule("rmm_sound", std::move(sound), strategy,
                                    std::make_unique<JsonDataNode>("config"));
    }

    // Host the game glue + parallelism probes.
    auto collector = std::make_unique<GameCollectorModule>();
    GameCollectorModule* col = collector.get();
    engine.registerStaticModule("rmm_collector", std::move(collector), strategy,
                                std::make_unique<JsonDataNode>("config"));
    engine.registerStaticModule("rmm_probeA", std::make_unique<WorkerProbeModule>(), strategy,
                                std::make_unique<JsonDataNode>("config"));
    engine.registerStaticModule("rmm_probeB", std::make_unique<WorkerProbeModule>(), strategy,
                                std::make_unique<JsonDataNode>("config"));

    std::string obsPart;
    game->subscribe("ship:part", [&](const Message& m) { obsPart = m.data->getString("id", ""); });

    auto step   = [&]{ engine.step(1.0f / 60.0f); while (game->hasMessages() > 0) game->pullAndDispatch(); };
    auto settle = [&](int n){ for (int i = 0; i < n; ++i) step(); };
    auto move   = [&](double x, double y){ auto d = std::make_unique<JsonDataNode>("d"); d->setDouble("x", x); d->setDouble("y", y); hostPub->publish("input:mouse:move", std::move(d)); settle(3); };
    auto btn    = [&](bool p){ auto d = std::make_unique<JsonDataNode>("d"); d->setInt("button", 0); d->setBool("pressed", p); hostPub->publish("input:mouse:button", std::move(d)); settle(3); };

    settle(5);  // boot all workers

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

    move(442, 192); btn(true); btn(false);   // real click → crosses UIModule → ship:part → glue → sound
    settle(12);                              // let the 3-module cascade propagate across workers

    // Snapshot BEFORE shutdown (shutdown destroys the hosted modules).
    const int   sfxPlays   = mock ? static_cast<int>(mock->playSoundCalls.size()) : -1;
    const int   maxc       = g_maxConcurrent.load();
    const std::string colPart = col->lastPart();
    const int   colSprites = col->sprites();
    const int   colEvents  = col->partEvents();

    engine.shutdown();
    mgr.removeInstance("rmm_host");
    mgr.removeInstance("rmm_game");

    const bool uiClick    = (obsPart == "cockpit");
    const bool collected  = (colPart == "cockpit" && colSprites > 0);
    const bool soundFired = (sfxPlays >= 1);
    const bool parallel   = (maxc >= 2);

    std::printf("================================================================\n");
    std::printf("  Multiple REAL modules hosted on the engine — %s\n", label);
    std::printf("  UIModule click -> ship:part        : %s (obs='%s')\n", uiClick ? "OK" : "FAIL", obsPart.c_str());
    std::printf("  cross-module IIO (glue saw part)   : %s (part='%s' sprites=%d events=%d)\n",
                collected ? "OK" : "FAIL", colPart.c_str(), colSprites, colEvents);
    std::printf("  chain reached SoundManager (real)  : %s (playSound calls=%d)\n", soundFired ? "OK" : "FAIL", sfxPlays);
    std::printf("  real parallelism (maxConc>=2)      : %s (maxConc=%d)\n", parallel ? "OK" : "FAIL", maxc);
    const bool ok = uiClick && collected && soundFired && parallel;
    std::printf(ok ? "  ✅ Several REAL modules work hosted together via the engine.\n"
                   : "  ❌ Multi-real-module hosting BROKEN.\n");
    std::printf("================================================================\n");
    return ok ? 0 : 1;
}

} // namespace real_multi
} // namespace grove
