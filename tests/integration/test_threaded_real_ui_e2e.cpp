// ============================================================================
// E2E: a REAL module (UIModule) hosted THREADED on the engine — real click flows.
//
// WHAT: the IT_053 scenario (data-driven ship blueprint: sprites-as-UI + clickable
//       parts), but UIModule is hosted via registerStaticModule(THREADED) instead of
//       SEQUENTIAL — i.e. it runs on its OWN worker thread, driven by engine.step().
//       A real click must travel input -> UIModule (on its worker thread) -> ship:part.
//
// WHY:  the synthetic Producer/Relay/Sink lock proves the threaded HOSTING mechanism,
//       but UIModule is a REAL self-draining module: it pulls its own inbox INSIDE
//       process() (beginFrame resets input edges, processInput pulls+sets them,
//       updateUI consumes). That ordering is load-bearing. This test is the proof that
//       a real self-draining module survives threaded hosting + the worker-owned IIO
//       drain (archi A) — exactly the case the synthetic modules cannot exercise.
//
// RUN:  ./test_threaded_real_ui_e2e   (headless, no GPU/SDL; UIModule_static)
//       exit 0 = the click flowed through the worker-threaded UIModule.
// ============================================================================

#include <grove/DebugEngine.h>
#include <grove/IModuleSystem.h>
#include <grove/IntraIOManager.h>
#include <grove/IntraIO.h>
#include <grove/JsonDataNode.h>
#include "UIModule.h"   // UIModule_static

#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

using namespace grove;
using nlohmann::json;

int main() {
    spdlog::set_level(spdlog::level::off);

    auto& mgr = IntraIOManager::getInstance();
    auto hostPub = mgr.createInstance("tru_host");   // game side: injects input + ui:data
    auto game    = mgr.createInstance("tru_game");   // observes UIModule output

    DebugEngine engine;
    engine.initialize();

    auto cfg = std::make_unique<JsonDataNode>("config");
    cfg->setInt("windowWidth", 1280);
    cfg->setInt("windowHeight", 720);
    cfg->setString("layoutFile", "../../assets/ui/test_e2e_blueprint.json");
    cfg->setInt("baseLayer", 1000);

    // HOST the real UIModule on its OWN worker thread.
    engine.registerStaticModule("tru_ui", std::make_unique<UIModule>(),
                                ModuleSystemType::THREADED, std::move(cfg));

    int spriteAdds = 0;
    std::vector<std::string> texts;
    std::string partId, partLabel;
    game->subscribe("render:sprite:add", [&](const Message&)   { ++spriteAdds; });
    game->subscribe("render:text:add",    [&](const Message& m){ texts.push_back(m.data->getString("text", "")); });
    game->subscribe("render:text:update", [&](const Message& m){ texts.push_back(m.data->getString("text", "")); });
    game->subscribe("ship:part",          [&](const Message& m){ partId = m.data->getString("id", ""); partLabel = m.data->getString("label", ""); });

    // One engine frame, then drain the external observer. Threaded hosting adds ~1
    // frame of latency per hop, so the action helpers below step several times.
    auto step  = [&]{ engine.step(1.0f / 60.0f); while (game->hasMessages() > 0) game->pullAndDispatch(); };
    auto settle= [&](int n){ for (int i = 0; i < n; ++i) step(); };
    auto saw   = [&](const std::string& t){ return std::find(texts.begin(), texts.end(), t) != texts.end(); };
    auto move  = [&](double x, double y){ auto d = std::make_unique<JsonDataNode>("d"); d->setDouble("x", x); d->setDouble("y", y); hostPub->publish("input:mouse:move", std::move(d)); settle(3); };
    auto btn   = [&](bool p){ auto d = std::make_unique<JsonDataNode>("d"); d->setInt("button", 0); d->setBool("pressed", p); hostPub->publish("input:mouse:button", std::move(d)); settle(3); };
    auto click = [&](double x, double y){ move(x, y); btn(true); btn(false); };

    settle(4);  // boot

    // Push the ship blueprint (identical to IT_053 / IT_044).
    auto part = [](const char* id, int x, int y, int w, int h, const char* col, int tex, const char* lbl){
        return json{ {"id",id},{"x",x},{"y",y},{"w",w},{"h",h},{"color",col},{"tex",tex},{"label",lbl},{"stat","..."} };
    };
    {
        json parts = json::array({
            part("cockpit",160,10,80,80,  "0xFFFFFFFF",1,"Cockpit"),
            part("hullA",  140,90,120,44, "0x3a4a63FF",0,"Coque avant"),
            part("gunL",   66,100,64,64,  "0xFFFFFFFF",4,"Canon babord"),
            part("reactor",160,190,80,80, "0xFFFFFFFF",2,"Reacteur"),
            part("engL",   108,312,70,92, "0xFFFFFFFF",3,"Moteur babord")
        });
        hostPub->publish("ui:data", std::make_unique<JsonDataNode>("d", json{
            {"ship", {{"name","S.S. Aurora"},{"parts",parts}}}, {"noPart", true},
            {"selectedPart", {{"label",""},{"stat",""}}} }));
    }
    spriteAdds = 0; texts.clear(); settle(5);

    // --- A. sprites-as-UI + text emitted THROUGH the worker-threaded UIModule ---
    const bool aSprites = (spriteAdds >= 4);
    const bool aTitle   = saw("S.S. Aurora");
    const bool aHint    = saw("Clique une piece de la maquette");

    // --- B. a real click travels input -> (worker-threaded) UIModule -> ship:part ---
    partId.clear(); partLabel.clear();
    click(442, 192);   // cockpit (same coords as IT_044/IT_053)
    settle(3);
    const bool bCockpit = (partId == "cockpit") && (partLabel == "Cockpit");

    // --- C. reactive info: merge selectedPart -> label renders ---
    {
        json patch = { {"noPart", false}, {"selectedPart", {{"label","Cockpit"},{"stat","PV 120"}}} };
        hostPub->publish("ui:data:merge", std::make_unique<JsonDataNode>("d", std::move(patch)));
    }
    texts.clear(); settle(5);
    const bool cLabel = saw("Cockpit");

    engine.shutdown();
    mgr.removeInstance("tru_host");
    mgr.removeInstance("tru_game");

    std::printf("================================================================\n");
    std::printf("  Threaded REAL UIModule E2E (hosted on its own worker thread)\n");
    std::printf("  A sprites>=4 : %s (got %d)\n", aSprites ? "OK" : "FAIL", spriteAdds);
    std::printf("  A title      : %s\n", aTitle ? "OK" : "FAIL");
    std::printf("  A hint       : %s\n", aHint  ? "OK" : "FAIL");
    std::printf("  B click->part: %s (id='%s' label='%s')\n", bCockpit ? "OK" : "FAIL", partId.c_str(), partLabel.c_str());
    std::printf("  C reactive   : %s\n", cLabel ? "OK" : "FAIL");
    const bool ok = aSprites && aTitle && aHint && bCockpit && cLabel;
    std::printf(ok ? "  ✅ Real UIModule works hosted THREADED — click flowed.\n"
                   : "  ❌ Real UIModule BROKEN under threaded hosting.\n");
    std::printf("================================================================\n");
    return ok ? 0 : 1;
}
