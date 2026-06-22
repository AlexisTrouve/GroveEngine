/**
 * Integration Test IT_031: modal dialog — focus-trap + open/close (UI framework slice 5a).
 *
 * A closed modal over a big background button. We assert:
 *   1. closed -> the background button is clickable,
 *   2. opened -> the dialog's button is clickable,
 *   3. a click on the DIM (outside the dialog) is trapped (background does NOT fire) AND closes the
 *      modal (ui:modal:closed),
 *   4. closed again -> the background button is clickable once more.
 *
 * dialog 300x200 centered in 800x600 -> [250,550]x[200,400]; ok_btn at content (20,20) -> abs
 * [270,370]x[220,250] (center 320,235). Point (100,100) is on the background and OUTSIDE the dialog.
 */

#include <catch2/catch_test_macros.hpp>
#include <grove/ModuleLoader.h>
#include <grove/IntraIOManager.h>
#include <grove/IntraIO.h>
#include <grove/JsonDataNode.h>

using namespace grove;

TEST_CASE("IT_031: a modal traps input and closes on an outside click (UI slice 5a)", "[integration][ui][e2e]") {
    auto& mgr = IntraIOManager::getInstance();
    auto hostPub  = mgr.createInstance("md_host");
    auto uiIO     = mgr.createInstance("md_ui");
    auto observer = mgr.createInstance("md_observer");

    ModuleLoader uiLoader;
    std::string uiPath = "../modules/libUIModule.so";
#ifdef _WIN32
    uiPath = "../modules/libUIModule.dll";
#endif
    std::unique_ptr<IModule> uiModule;
    REQUIRE_NOTHROW(uiModule = uiLoader.load(uiPath, "md_ui"));
    REQUIRE(uiModule != nullptr);

    JsonDataNode cfg("config");
    cfg.setInt("windowWidth", 800);
    cfg.setInt("windowHeight", 600);
    cfg.setString("layoutFile", "../../assets/ui/test_e2e_modal.json");
    cfg.setInt("baseLayer", 1000);
    REQUIRE_NOTHROW(uiModule->setConfiguration(cfg, uiIO.get(), nullptr));

    std::string lastAction;
    int modalClosed = 0;
    observer->subscribe("ui:action", [&](const Message& m) { lastAction = m.data->getString("action", ""); });
    observer->subscribe("ui:modal:closed", [&](const Message&) { modalClosed++; });

    auto pump = [&] {
        JsonDataNode input("input");
        input.setDouble("deltaTime", 0.016);
        uiModule->process(input);
        while (observer->hasMessages() > 0) observer->pullAndDispatch();
    };
    auto sendMove = [&](double x, double y) {
        auto d = std::make_unique<JsonDataNode>("d"); d->setDouble("x", x); d->setDouble("y", y);
        hostPub->publish("input:mouse:move", std::move(d));
    };
    auto sendButton = [&](bool pressed) {
        auto d = std::make_unique<JsonDataNode>("d"); d->setInt("button", 0); d->setBool("pressed", pressed);
        hostPub->publish("input:mouse:button", std::move(d));
    };
    auto click = [&](double x, double y) { sendMove(x, y); pump(); sendButton(true); pump(); sendButton(false); pump(); };
    auto openModal = [&] {
        auto d = std::make_unique<JsonDataNode>("d"); d->setString("id", "modal");
        hostPub->publish("ui:modal:open", std::move(d));
        pump();
    };

    pump();  // settle

    // 1. Closed: the background is clickable.
    lastAction.clear();
    click(100, 100);
    INFO("closed action='" << lastAction << "'");
    REQUIRE(lastAction == "background");

    // 2. Open: the dialog's button is clickable.
    openModal();
    lastAction.clear();
    click(320, 235);
    INFO("dialog action='" << lastAction << "'");
    REQUIRE(lastAction == "dialogok");

    // 3. Click the dim (outside the dialog): trapped (no background) AND the modal closes.
    lastAction.clear();
    click(100, 100);
    INFO("trap action='" << lastAction << "' modalClosed=" << modalClosed);
    REQUIRE(lastAction.empty());
    REQUIRE(modalClosed == 1);

    // 4. Closed again: the background is clickable.
    lastAction.clear();
    click(100, 100);
    INFO("reopened-bg action='" << lastAction << "'");
    REQUIRE(lastAction == "background");

    uiModule->shutdown();
}
