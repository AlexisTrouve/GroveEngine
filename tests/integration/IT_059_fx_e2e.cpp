/**
 * Integration Test IT_059: FxModule end-to-end — the data-driven scene/entity layer (slice E2).
 *
 * Drives the module with real entity:* topics and asserts the render:sprite:* traffic it emits: a spawn
 * yields render:sprite:add at the entity's CENTER (cx,cy — the anchor convention), a partial entity:set
 * merges (only the changed field moves) and yields render:sprite:update, an engine-side behavior (move +
 * lifetime) advances the entity and then removes it on expiry, and entity:destroy yields render:sprite:
 * remove. This is the E2E lock — the pure FxWorld logic proven THROUGH the module + IIO.
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "FxModule.h"
#include "grove/IntraIOManager.h"
#include "grove/IntraIO.h"
#include "grove/JsonDataNode.h"

#include <chrono>
#include <memory>
#include <sstream>
#include <string>

using namespace grove;
using nlohmann::json;
using Catch::Matchers::WithinAbs;

static std::string uid(const std::string& p) {
    auto n = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    std::ostringstream o; o << p << "_" << n; return o.str();
}

namespace {
struct Harness {
    std::unique_ptr<FxModule> module;
    std::shared_ptr<IIO> moduleIO, pubIO, obsIO;

    int addCount = 0, updateCount = 0, removeCount = 0;
    int lastAddId = -1, lastUpdateId = -1, lastRemoveId = -1;
    // render:text:* observers (floating numbers) — separate retained pool from sprites.
    int txtAddCount = 0, txtUpdateCount = 0, txtRemoveCount = 0;
    int lastTxtId = -1;
    double lastTxtX = 0, lastTxtY = 0;
    std::string lastTxtString;
    uint32_t lastTxtColor = 0;
    int lastTxtFontSize = -1;
    double lastAddCx = 0, lastAddCy = 0, lastUpdateCx = 0, lastUpdateCy = 0;
    std::string lastAddAsset;
    int lastAddLayer = -1;
    uint32_t lastAddColor = 0, lastUpdateColor = 0;   // sprite color (0xRRGGBBAA) — fade drives the AA byte

    Harness() {
        auto& mgr = IntraIOManager::getInstance();
        moduleIO = mgr.createInstance(uid("ent_mod"));
        pubIO    = mgr.createInstance(uid("ent_pub"));
        obsIO    = mgr.createInstance(uid("ent_obs"));

        module = std::make_unique<FxModule>();
        JsonDataNode cfg("config");
        module->setConfiguration(cfg, moduleIO.get(), nullptr);

        obsIO->subscribe("render:sprite:add", [this](const Message& m) {
            ++addCount; lastAddId = m.data->getInt("renderId", -1);
            lastAddCx = m.data->getDouble("cx", 0); lastAddCy = m.data->getDouble("cy", 0);
            lastAddAsset = m.data->getString("asset", ""); lastAddLayer = m.data->getInt("layer", -1);
            lastAddColor = static_cast<uint32_t>(m.data->getInt("color", 0));
        });
        obsIO->subscribe("render:sprite:update", [this](const Message& m) {
            ++updateCount; lastUpdateId = m.data->getInt("renderId", -1);
            lastUpdateCx = m.data->getDouble("cx", 0); lastUpdateCy = m.data->getDouble("cy", 0);
            lastUpdateColor = static_cast<uint32_t>(m.data->getInt("color", 0));
        });
        obsIO->subscribe("render:sprite:remove", [this](const Message& m) {
            ++removeCount; lastRemoveId = m.data->getInt("renderId", -1);
        });
        obsIO->subscribe("render:text:add", [this](const Message& m) {
            ++txtAddCount; lastTxtId = m.data->getInt("renderId", -1);
            lastTxtX = m.data->getDouble("x", 0); lastTxtY = m.data->getDouble("y", 0);
            lastTxtString = m.data->getString("text", "");
            lastTxtColor = static_cast<uint32_t>(m.data->getInt("color", 0));
            lastTxtFontSize = m.data->getInt("fontSize", -1);
        });
        obsIO->subscribe("render:text:update", [this](const Message& m) {
            ++txtUpdateCount; lastTxtId = m.data->getInt("renderId", -1);
            lastTxtX = m.data->getDouble("x", 0); lastTxtY = m.data->getDouble("y", 0);
            lastTxtColor = static_cast<uint32_t>(m.data->getInt("color", 0));
        });
        obsIO->subscribe("render:text:remove", [this](const Message& m) {
            ++txtRemoveCount; lastTxtId = m.data->getInt("renderId", -1);
        });
    }

    void send(const std::string& topic, json payload, double dt = 0.016) {
        pubIO->publish(topic, std::make_unique<JsonDataNode>("m", std::move(payload)));
        step(dt);
    }
    void step(double dt) {
        JsonDataNode in("input");
        in.setDouble("deltaTime", dt);
        module->process(in);
        while (obsIO->hasMessages() > 0) obsIO->pullAndDispatch();
    }
};
} // namespace

TEST_CASE("IT_059: FxModule spawn/set/destroy -> render:sprite:* (anchor cx,cy)", "[integration][fx][e2e]") {
    Harness h;

    // --- spawn a sprite entity -> render:sprite:add at its CENTER. ---
    h.send("fx:spawn", json{{"id", "player"},
                                {"transform", {{"cx", 100.0}, {"cy", 200.0}}},
                                {"sprite", {{"asset", "ship/hull"}, {"layer", 10}}}});
    REQUIRE(h.addCount == 1);
    REQUIRE(h.lastAddAsset == "ship/hull");
    REQUIRE(h.lastAddLayer == 10);
    REQUIRE_THAT(h.lastAddCx, WithinAbs(100.0, 0.001));
    REQUIRE_THAT(h.lastAddCy, WithinAbs(200.0, 0.001));
    const int playerId = h.lastAddId;   // renderId = world EntityId, stable across add/update/remove

    // --- partial set: move only cx -> update, cy preserved (merge). ---
    h.send("fx:set", json{{"id", "player"}, {"transform", {{"cx", 150.0}}}});
    REQUIRE(h.updateCount == 1);
    REQUIRE(h.lastUpdateId == playerId);
    REQUIRE_THAT(h.lastUpdateCx, WithinAbs(150.0, 0.001));
    REQUIRE_THAT(h.lastUpdateCy, WithinAbs(200.0, 0.001));   // untouched by the partial set

    // --- destroy -> render:sprite:remove for that renderId. ---
    h.send("fx:destroy", json{{"id", "player"}});
    REQUIRE(h.removeCount == 1);
    REQUIRE(h.lastRemoveId == playerId);
}

TEST_CASE("IT_059b: engine-side behaviors (move + lifetime) drive the sprite through the module", "[integration][fx][e2e]") {
    Harness h;

    // A bullet: textureId sprite, moves right at 100 px/s, lives 0.5 s. Behaviors are ENGINE-side data.
    h.send("fx:spawn", json{{"id", "bullet"},
                                {"transform", {{"cx", 0.0}, {"cy", 0.0}}},
                                {"sprite", {{"textureId", 1}}},
                                {"behaviors", json::array({
                                    json{{"type", "move"}, {"vx", 100.0}, {"vy", 0.0}},
                                    json{{"type", "lifetime"}, {"seconds", 0.5}}})}},
           0.0);   // dt=0 on the spawn step so the Add reports the spawn position
    REQUIRE(h.addCount == 1);
    REQUIRE_THAT(h.lastAddCx, WithinAbs(0.0, 0.001));
    const int bulletId = h.lastAddId;

    // Tick 0.25 s -> moved to cx=25 -> update.
    h.step(0.25);
    REQUIRE(h.updateCount == 1);
    REQUIRE(h.lastUpdateId == bulletId);
    REQUIRE_THAT(h.lastUpdateCx, WithinAbs(25.0, 0.001));

    // Tick past the lifetime (total 0.5 s) -> the engine kills it -> render:sprite:remove. No per-project code.
    h.step(0.25);
    REQUIRE(h.removeCount == 1);
    REQUIRE(h.lastRemoveId == bulletId);
}

TEST_CASE("IT_059c: prefab/archetype — register once, spawn with per-instance overrides", "[integration][fx][e2e][prefab]") {
    Harness h;

    // Register a reusable "bullet" archetype (sprite + move + lifetime) — no entity spawned, no render yet.
    h.send("fx:prefab", json{{"name", "bullet"},
                                 {"sprite", {{"asset", "bullet"}, {"layer", 5}}},
                                 {"behaviors", json::array({
                                     json{{"type", "move"}, {"vx", 100.0}, {"vy", 0.0}},
                                     json{{"type", "lifetime"}, {"seconds", 0.5}}})}}, 0.0);
    REQUIRE(h.addCount == 0);   // registering a prefab spawns nothing

    // Spawn an instance of the archetype, overriding only its position.
    h.send("fx:spawn", json{{"id", "b1"}, {"archetype", "bullet"}, {"transform", {{"cx", 50.0}, {"cy", 0.0}}}}, 0.0);
    REQUIRE(h.addCount == 1);
    REQUIRE(h.lastAddAsset == "bullet");    // inherited from the prefab
    REQUIRE(h.lastAddLayer == 5);           // inherited
    REQUIRE_THAT(h.lastAddCx, WithinAbs(50.0, 0.001));   // overridden per instance

    // The prefab's behaviors run on the instance: move -> update, then lifetime -> remove.
    h.step(0.25);
    REQUIRE(h.updateCount == 1);
    REQUIRE_THAT(h.lastUpdateCx, WithinAbs(75.0, 0.001));   // 50 + 100*0.25
    h.step(0.25);
    REQUIRE(h.removeCount == 1);            // lifetime 0.5 expired -> the engine removed it
}

TEST_CASE("IT_059d: fade + velocity behaviors drive the sprite through the module", "[integration][fx][e2e]") {
    Harness h;

    // A muzzle-flash-like effect: a white sprite that fades out over 1 s while drifting east and slowing (drag).
    // fade + velocity are ENGINE-side behaviors — the game just declares them, no per-project tick code.
    // Tick at a realistic frame dt (0.1 s) so the drag deceleration is gradual (a single big dt would kill vx).
    h.send("fx:spawn", json{{"id", "flash"},
                                {"transform", {{"cx", 0.0}, {"cy", 0.0}}},
                                {"sprite", {{"textureId", 1}, {"color", 0xFFFFFFFF}}},
                                {"behaviors", json::array({
                                    json{{"type", "velocity"}, {"vx", 100.0}, {"vy", 0.0}, {"drag", 1.0}},
                                    json{{"type", "fade"}, {"seconds", 1.0}, {"fromAlpha", 1.0}, {"toAlpha", 0.0}}})}},
           0.0);   // dt=0 so the Add reports the spawn state (full alpha, cx=0)
    REQUIRE(h.addCount == 1);
    REQUIRE((h.lastAddColor & 0xFFu) == 0xFFu);          // spawn: full alpha
    REQUIRE_THAT(h.lastAddCx, WithinAbs(0.0, 0.001));
    const int flashId = h.lastAddId;

    // 5 frames of 0.1 s -> t=0.5. Each frame: fades a bit AND drifts east (by a shrinking amount — drag).
    double prevCx = 0.0;
    for (int i = 0; i < 5; ++i) {
        h.step(0.1);
        REQUIRE(h.lastUpdateId == flashId);
        REQUIRE(h.lastUpdateCx > prevCx);                // velocity: monotonically drifting east...
        prevCx = h.lastUpdateCx;
    }
    REQUIRE((h.lastUpdateColor & 0xFFu) == 128u);        // fade: alpha halved at t=0.5 (255*0.5)
    const double cxAt05 = h.lastUpdateCx;

    // 5 more frames -> t=1.0: fully faded (alpha 0) and it kept drifting (slower — drag).
    for (int i = 0; i < 5; ++i) h.step(0.1);
    REQUIRE((h.lastUpdateColor & 0xFFu) == 0u);          // fully transparent
    REQUIRE(h.lastUpdateCx > cxAt05);                    // still moved east over the 2nd half
}

TEST_CASE("IT_059e: floating damage number archetype -> render:text:* (rise, fade, expire)", "[integration][fx][e2e][text]") {
    Harness h;

    // Register the drifterra-priority archetype ONCE: a text label that rises (velocity up), fades out over
    // 0.5 s, and self-expires. The string is a per-instance override (the resolved damage value) — the engine
    // never localizes (i18n-agnostic). No sprite: this drives render:text:*, a pool distinct from sprites.
    h.send("fx:prefab", json{{"name", "damage_number"},
                                 {"text", {{"text", ""}, {"color", 0xFFFFFFFF}, {"layer", 1000}, {"fontSize", 18}}},
                                 {"behaviors", json::array({
                                     json{{"type", "velocity"}, {"vx", 0.0}, {"vy", -40.0}, {"drag", 0.0}},
                                     json{{"type", "fade"}, {"seconds", 0.5}, {"fromAlpha", 1.0}, {"toAlpha", 0.0}},
                                     json{{"type", "lifetime"}, {"seconds", 0.5}}})}}, 0.0);
    REQUIRE(h.txtAddCount == 0);          // registering a prefab renders nothing

    // Spawn a hit: "-25" at (300,150). Only the string + position are per-instance.
    h.send("fx:spawn", json{{"id", "hit1"}, {"archetype", "damage_number"},
                                {"transform", {{"cx", 300.0}, {"cy", 150.0}}},
                                {"text", {{"text", "-25"}}}}, 0.0);
    REQUIRE(h.txtAddCount == 1);
    REQUIRE(h.lastTxtString == "-25");                    // per-instance string override
    REQUIRE(h.lastTxtFontSize == 18);                     // inherited from the archetype
    REQUIRE_THAT(h.lastTxtX, WithinAbs(300.0, 0.001));    // render:text x,y = the transform position (corner anchor)
    REQUIRE_THAT(h.lastTxtY, WithinAbs(150.0, 0.001));
    const int hitId = h.lastTxtId;
    REQUIRE((h.lastTxtColor & 0xFFu) == 0xFFu);           // spawn: opaque
    REQUIRE(h.addCount == 0);                             // NO sprite traffic — text pool only

    // Tick to t=0.25: it rose (y decreased) and is half-faded.
    h.step(0.25);
    REQUIRE(h.txtUpdateCount >= 1);
    REQUIRE(h.lastTxtId == hitId);
    REQUIRE(h.lastTxtY < 150.0);                          // rose (velocity vy=-40)
    REQUIRE((h.lastTxtColor & 0xFFu) == 128u);            // half-faded at t=0.25 of a 0.5 s fade

    // Tick past the lifetime -> the engine removes it from the text pool.
    h.step(0.30);
    REQUIRE(h.txtRemoveCount == 1);
    REQUIRE(h.lastTxtId == hitId);
    REQUIRE(h.removeCount == 0);                          // never touched the sprite pool
}

TEST_CASE("IT_059f: emitter burst -> a batch of render:sprite:add particles (explosion)", "[integration][fx][e2e][emitter]") {
    Harness h;

    // The particle template: a spark sprite that fades + dies over 0.4 s (authored once, reused per particle).
    h.send("fx:prefab", json{{"name", "spark"},
                                 {"sprite", {{"asset", "fx/spark"}, {"layer", 900}}},
                                 {"behaviors", json::array({
                                     json{{"type","fade"},{"seconds",0.4},{"fromAlpha",1.0},{"toAlpha",0.0}},
                                     json{{"type","lifetime"},{"seconds",0.4}}})}}, 0.0);
    REQUIRE(h.addCount == 0);

    // Spawn an emitter that bursts 10 omni sparks (speed [60,120]) at a hit point. The burst fires on the
    // step's tick -> 10 render:sprite:add (the particles). The emitter itself is invisible -> no sprite of its own.
    h.send("fx:spawn", json{{"id","boom"},
                                {"transform", {{"cx", 400.0}, {"cy", 300.0}}},
                                {"emitter", {{"prefab","spark"}, {"count",10},
                                             {"speedMin",60.0}, {"speedMax",120.0},
                                             {"spreadDeg",360.0}, {"dirDeg",0.0}}}}, 0.016);
    REQUIRE(h.addCount == 10);                            // the whole burst appeared
    REQUIRE(h.lastAddAsset == "fx/spark");               // particles inherit the prefab's look
    REQUIRE(h.removeCount == 0);

    // The particles drift (velocity the emitter attached) -> updates next step.
    const int before = h.updateCount;
    h.step(0.05);
    REQUIRE(h.updateCount > before);

    // Past the particle lifetime -> the engine removes all 10.
    h.step(0.4);
    REQUIRE(h.removeCount == 10);
}
