/**
 * Integration Test IT_045: SMART RESIZE — a window lays out its content responsively.
 *
 * The inspector window is sized in PERCENT of the viewport and its content is a vertical body with a
 * horizontal split (a flex blueprint + a fixed 220px side panel). When the viewport grows (ui:resize), the
 * window grows, the flex blueprint widens, and the right-anchored side panel moves RIGHT — i.e. the content
 * REFLOWS, it isn't pinned to fixed pixels. Verified via the rendered x of the side panel's "Piece" label.
 */

#include <catch2/catch_test_macros.hpp>
#include <grove/ModuleLoader.h>
#include <grove/IntraIOManager.h>
#include <grove/IntraIO.h>
#include <grove/JsonDataNode.h>

using namespace grove;

TEST_CASE("IT_045: window content reflows on viewport resize (smart resize)", "[integration][ui][e2e]") {
    auto& mgr = IntraIOManager::getInstance();
    auto hostPub  = mgr.createInstance("rs_host");
    auto uiIO     = mgr.createInstance("rs_ui");
    auto observer = mgr.createInstance("rs_observer");

    ModuleLoader uiLoader;
    std::string uiPath = "../modules/libUIModule.so";
#ifdef _WIN32
    uiPath = "../modules/libUIModule.dll";
#endif
    std::unique_ptr<IModule> uiModule;
    REQUIRE_NOTHROW(uiModule = uiLoader.load(uiPath, "rs_ui"));
    REQUIRE(uiModule != nullptr);

    JsonDataNode cfg("config");
    cfg.setInt("windowWidth", 1280);
    cfg.setInt("windowHeight", 720);
    cfg.setString("layoutFile", "../../assets/ui/demo_ship_inspector.json");
    cfg.setInt("baseLayer", 1000);
    REQUIRE_NOTHROW(uiModule->setConfiguration(cfg, uiIO.get(), nullptr));

    double pieceX = -1.0;   // latest rendered x of the side panel's "Piece" label
    auto cap = [&](const Message& m) { if (m.data->getString("text","") == "Piece") pieceX = m.data->getDouble("x", -1.0); };
    observer->subscribe("render:text:add", cap);
    observer->subscribe("render:text:update", cap);

    auto pump = [&] {
        JsonDataNode input("input"); input.setDouble("deltaTime", 0.016);
        uiModule->process(input);
        while (observer->hasMessages() > 0) observer->pullAndDispatch();
    };

    pump(); pump();
    const double xSmall = pieceX;
    INFO("Piece x @1280 = " << xSmall);
    REQUIRE(xSmall > 0.0);             // the responsive side panel rendered

    // Grow the viewport: the % window grows, the flex blueprint widens, the side panel moves right.
    { auto d = std::make_unique<JsonDataNode>("d"); d->setInt("width", 1600); d->setInt("height", 900);
      hostPub->publish("ui:resize", std::move(d)); }
    pump(); pump();
    INFO("Piece x @1600 = " << pieceX);
    REQUIRE(pieceX > xSmall + 80.0);  // content reflowed (not pinned to fixed pixels)

    uiModule->shutdown();
}
