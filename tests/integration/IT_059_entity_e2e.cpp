/**
 * Integration Test IT_059: EntityModule end-to-end — the data-driven scene/entity layer (slice E2).
 *
 * Drives the module with real entity:* topics and asserts the render:sprite:* traffic it emits: a spawn
 * yields render:sprite:add at the entity's CENTER (cx,cy — the anchor convention), a partial entity:set
 * merges (only the changed field moves) and yields render:sprite:update, an engine-side behavior (move +
 * lifetime) advances the entity and then removes it on expiry, and entity:destroy yields render:sprite:
 * remove. This is the E2E lock — the pure EntityWorld logic proven THROUGH the module + IIO.
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "EntityModule.h"
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
    std::unique_ptr<EntityModule> module;
    std::shared_ptr<IIO> moduleIO, pubIO, obsIO;

    int addCount = 0, updateCount = 0, removeCount = 0;
    int lastAddId = -1, lastUpdateId = -1, lastRemoveId = -1;
    double lastAddCx = 0, lastAddCy = 0, lastUpdateCx = 0, lastUpdateCy = 0;
    std::string lastAddAsset;
    int lastAddLayer = -1;

    Harness() {
        auto& mgr = IntraIOManager::getInstance();
        moduleIO = mgr.createInstance(uid("ent_mod"));
        pubIO    = mgr.createInstance(uid("ent_pub"));
        obsIO    = mgr.createInstance(uid("ent_obs"));

        module = std::make_unique<EntityModule>();
        JsonDataNode cfg("config");
        module->setConfiguration(cfg, moduleIO.get(), nullptr);

        obsIO->subscribe("render:sprite:add", [this](const Message& m) {
            ++addCount; lastAddId = m.data->getInt("renderId", -1);
            lastAddCx = m.data->getDouble("cx", 0); lastAddCy = m.data->getDouble("cy", 0);
            lastAddAsset = m.data->getString("asset", ""); lastAddLayer = m.data->getInt("layer", -1);
        });
        obsIO->subscribe("render:sprite:update", [this](const Message& m) {
            ++updateCount; lastUpdateId = m.data->getInt("renderId", -1);
            lastUpdateCx = m.data->getDouble("cx", 0); lastUpdateCy = m.data->getDouble("cy", 0);
        });
        obsIO->subscribe("render:sprite:remove", [this](const Message& m) {
            ++removeCount; lastRemoveId = m.data->getInt("renderId", -1);
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

TEST_CASE("IT_059: EntityModule spawn/set/destroy -> render:sprite:* (anchor cx,cy)", "[integration][entity][e2e]") {
    Harness h;

    // --- spawn a sprite entity -> render:sprite:add at its CENTER. ---
    h.send("entity:spawn", json{{"id", "player"},
                                {"transform", {{"cx", 100.0}, {"cy", 200.0}}},
                                {"sprite", {{"asset", "ship/hull"}, {"layer", 10}}}});
    REQUIRE(h.addCount == 1);
    REQUIRE(h.lastAddAsset == "ship/hull");
    REQUIRE(h.lastAddLayer == 10);
    REQUIRE_THAT(h.lastAddCx, WithinAbs(100.0, 0.001));
    REQUIRE_THAT(h.lastAddCy, WithinAbs(200.0, 0.001));
    const int playerId = h.lastAddId;   // renderId = world EntityId, stable across add/update/remove

    // --- partial set: move only cx -> update, cy preserved (merge). ---
    h.send("entity:set", json{{"id", "player"}, {"transform", {{"cx", 150.0}}}});
    REQUIRE(h.updateCount == 1);
    REQUIRE(h.lastUpdateId == playerId);
    REQUIRE_THAT(h.lastUpdateCx, WithinAbs(150.0, 0.001));
    REQUIRE_THAT(h.lastUpdateCy, WithinAbs(200.0, 0.001));   // untouched by the partial set

    // --- destroy -> render:sprite:remove for that renderId. ---
    h.send("entity:destroy", json{{"id", "player"}});
    REQUIRE(h.removeCount == 1);
    REQUIRE(h.lastRemoveId == playerId);
}

TEST_CASE("IT_059b: engine-side behaviors (move + lifetime) drive the sprite through the module", "[integration][entity][e2e]") {
    Harness h;

    // A bullet: textureId sprite, moves right at 100 px/s, lives 0.5 s. Behaviors are ENGINE-side data.
    h.send("entity:spawn", json{{"id", "bullet"},
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

TEST_CASE("IT_059c: prefab/archetype — register once, spawn with per-instance overrides", "[integration][entity][e2e][prefab]") {
    Harness h;

    // Register a reusable "bullet" archetype (sprite + move + lifetime) — no entity spawned, no render yet.
    h.send("entity:prefab", json{{"name", "bullet"},
                                 {"sprite", {{"asset", "bullet"}, {"layer", 5}}},
                                 {"behaviors", json::array({
                                     json{{"type", "move"}, {"vx", 100.0}, {"vy", 0.0}},
                                     json{{"type", "lifetime"}, {"seconds", 0.5}}})}}, 0.0);
    REQUIRE(h.addCount == 0);   // registering a prefab spawns nothing

    // Spawn an instance of the archetype, overriding only its position.
    h.send("entity:spawn", json{{"id", "b1"}, {"archetype", "bullet"}, {"transform", {{"cx", 50.0}, {"cy", 0.0}}}}, 0.0);
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
