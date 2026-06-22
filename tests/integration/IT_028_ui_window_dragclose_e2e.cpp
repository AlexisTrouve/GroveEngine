/**
 * Integration Test IT_028: in-app window title-bar drag + close button (UI framework slice 3b-2).
 *
 * w1 (100,100,200,150) over a big background button. We:
 *   1. click its content button (control),
 *   2. DRAG it by the title bar (+50,+50) and click the content button at its NEW position,
 *   3. click the CLOSE button -> the window hides, and a click where its content was now falls
 *      through to the background.
 *
 * After the +50,+50 drag, w1 is at (150,150): titleBarHeight 28 -> content origin (150,178);
 * winbtn (content 50,50) -> abs [200,260]x[228,258] (center 230,243); close button -> [326,344]x[155,173].
 */

#include <catch2/catch_test_macros.hpp>
#include <grove/ModuleLoader.h>
#include <grove/IntraIOManager.h>
#include <grove/IntraIO.h>
#include <grove/JsonDataNode.h>

using namespace grove;

TEST_CASE("IT_028: an in-app window drags by its title bar and closes (UI slice 3b-2)", "[integration][ui][e2e]") {
    auto& mgr = IntraIOManager::getInstance();
    auto hostPub  = mgr.createInstance("wd_host");
    auto uiIO     = mgr.createInstance("wd_ui");
    auto observer = mgr.createInstance("wd_observer");

    ModuleLoader uiLoader;
    std::string uiPath = "../modules/libUIModule.so";
#ifdef _WIN32
    uiPath = "../modules/libUIModule.dll";
#endif
    std::unique_ptr<IModule> uiModule;
    REQUIRE_NOTHROW(uiModule = uiLoader.load(uiPath, "wd_ui"));
    REQUIRE(uiModule != nullptr);

    JsonDataNode cfg("config");
    cfg.setInt("windowWidth", 800);
    cfg.setInt("windowHeight", 600);
    cfg.setString("layoutFile", "../../assets/ui/test_e2e_window_dragclose.json");
    cfg.setInt("baseLayer", 1000);
    REQUIRE_NOTHROW(uiModule->setConfiguration(cfg, uiIO.get(), nullptr));

    std::string lastAction;
    int closedCount = 0;
    std::string closedId;
    observer->subscribe("ui:action", [&](const Message& m) { lastAction = m.data->getString("action", ""); });
    observer->subscribe("ui:window:closed", [&](const Message& m) { closedCount++; closedId = m.data->getString("id", ""); });

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

    pump();  // settle

    // 1. Control: the window's content button is clickable at its start position (center 180,193).
    lastAction.clear();
    click(180, 193);
    INFO("control action='" << lastAction << "'");
    REQUIRE(lastAction == "winbtn");

    // 2. Drag the title bar from (120,114) by (+50,+50) -> window moves to (150,150).
    sendMove(120, 114); pump();
    sendButton(true);   pump();   // grab the title bar
    sendMove(170, 164); pump();   // move while held -> window follows
    sendButton(false);  pump();   // release

    // The content button now responds at its NEW position; the OLD spot no longer does.
    lastAction.clear();
    click(230, 243);
    INFO("after-drag action='" << lastAction << "'");
    REQUIRE(lastAction == "winbtn");

    // 3. Click the close button -> the window hides and emits ui:window:closed.
    lastAction.clear();
    click(335, 164);
    INFO("close action='" << lastAction << "' closedCount=" << closedCount << " closedId='" << closedId << "'");
    REQUIRE(closedCount == 1);
    REQUIRE(closedId == "w1");

    // A click where the (now closed) content button was falls through to the background.
    lastAction.clear();
    click(230, 243);
    INFO("post-close action='" << lastAction << "'");
    REQUIRE(lastAction == "background");

    uiModule->shutdown();
}
