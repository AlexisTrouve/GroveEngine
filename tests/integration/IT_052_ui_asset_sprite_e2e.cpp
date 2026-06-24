/**
 * Integration Test IT_052: a UI widget can reference its texture by a streamed ASSET ID (string).
 *
 * This locks the missing link between the asset streaming system and the UI: a UIButton with an
 * `asset` prop (literal OR data-bound `{{}}`) must publish render:sprite:add carrying that `asset`
 * STRING — which the BgfxRenderer's SceneCollector::resolveSpriteTexture already resolves to a streamed
 * texture + atlas UV (that renderer half is locked by AssetSpriteGpu / AssetTopicsGpu). Before this
 * slice the UI only ever published a numeric `textureId`, so a UI sprite could not use the AssetManager.
 *
 *   A. LITERAL asset — a button with "asset":"icons/play" publishes render:sprite:add{asset:"icons/play"}.
 *   B. DATA-BOUND asset — a repeater template with "asset":"{{icon}}" publishes one sprite:add per item,
 *      each carrying that item's resolved asset id (icons/iron, icons/copper).
 */

#include <catch2/catch_test_macros.hpp>
#include <grove/ModuleLoader.h>
#include <grove/IntraIOManager.h>
#include <grove/IntraIO.h>
#include <grove/JsonDataNode.h>
#include <vector>
#include <string>
#include <algorithm>

using namespace grove;

TEST_CASE("IT_052: a UI sprite can reference a streamed asset id (string)", "[integration][ui][e2e][assets]") {
    auto& mgr = IntraIOManager::getInstance();
    auto hostPub = mgr.createInstance("asb_host");
    auto uiIO    = mgr.createInstance("asb_ui");
    auto game    = mgr.createInstance("asb_game");

    ModuleLoader uiLoader;
    std::string uiPath = "../modules/libUIModule.so";
#ifdef _WIN32
    uiPath = "../modules/libUIModule.dll";
#endif
    std::unique_ptr<IModule> uiModule;
    REQUIRE_NOTHROW(uiModule = uiLoader.load(uiPath, "asb_ui"));
    REQUIRE(uiModule != nullptr);

    JsonDataNode cfg("config");
    cfg.setInt("windowWidth", 400);
    cfg.setInt("windowHeight", 300);
    cfg.setString("layoutFile", "../../assets/ui/test_e2e_asset_button.json");
    cfg.setInt("baseLayer", 1000);
    REQUIRE_NOTHROW(uiModule->setConfiguration(cfg, uiIO.get(), nullptr));

    // Collect the `asset` string of every render:sprite:add (empty string = a numeric/color sprite).
    std::vector<std::string> assets;
    game->subscribe("render:sprite:add", [&](const Message& m){
        const std::string a = m.data->getString("asset", "");
        if (!a.empty()) assets.push_back(a);
    });
    game->subscribe("render:sprite:update", [&](const Message& m){
        const std::string a = m.data->getString("asset", "");
        if (!a.empty()) assets.push_back(a);
    });

    auto pump = [&] {
        JsonDataNode input("input"); input.setDouble("deltaTime", 0.016);
        uiModule->process(input);
        while (game->hasMessages() > 0) game->pullAndDispatch();
    };
    auto sawAsset = [&](const std::string& id){ return std::find(assets.begin(), assets.end(), id) != assets.end(); };

    // First pump: the literal-asset button renders once (retained-mode sprite:add carries asset:"icons/play").
    // We accumulate from the start — retained mode only publishes on add/change, so each add is seen once.
    pump();

    // Push the repeater data: two icon cells, each naming its texture by a streamed asset id.
    hostPub->publish("ui:data", std::make_unique<JsonDataNode>("d", json{
        {"items", json::array({
            json{ {"id","a"}, {"icon","icons/iron"},   {"x",0}  },
            json{ {"id","b"}, {"icon","icons/copper"}, {"x",60} }
        })}
    }));
    pump(); pump();

    INFO("assets seen: " << [&]{ std::string s; for (auto& a : assets) s += a + " "; return s; }());

    // --- A. literal asset on a plain button. ---
    REQUIRE(sawAsset("icons/play"));

    // --- B. data-bound asset on each repeater item. ---
    REQUIRE(sawAsset("icons/iron"));
    REQUIRE(sawAsset("icons/copper"));

    uiModule->shutdown();
}
