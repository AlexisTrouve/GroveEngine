/**
 * FxWorldUnit — pure oracle for the entity/component/behavior core (scene/entity layer, slice E1).
 *
 * No IIO / renderer: asserts the load-bearing logic directly — component set/get, the fixed behavior
 * library tick (move / spin / lifetime), lifecycle (destroy + lifetime expiry), and the retained-render
 * diff (Add on first appearance, Update on change, Remove on death, nothing when unchanged). The E2E
 * (through FxModule) will prove it end-to-end; this locks the primitives so a regression points at
 * the exact broken method.
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "grove/fx/FxWorld.h"

#include <cmath>

using namespace grove::fx;
using Catch::Matchers::WithinAbs;

namespace {
// Count ops of a given kind in a diff.
size_t count(const std::vector<FxWorld::RenderOp>& ops, FxWorld::RenderOp::Kind k) {
    size_t n = 0;
    for (const auto& o : ops) if (o.kind == k) ++n;
    return n;
}
const FxWorld::RenderOp* find(const std::vector<FxWorld::RenderOp>& ops, EntityId id) {
    for (const auto& o : ops) if (o.id == id) return &o;
    return nullptr;
}
} // namespace

TEST_CASE("FxWorldUnit: spawn + sprite -> Add once, then no ops when unchanged", "[fx][unit]") {
    FxWorld w;
    EntityId e = w.spawn();
    w.setTransform(e, Transform{100.0f, 200.0f, 0.0f, 1.0f, 1.0f});
    w.setSprite(e, Sprite{true, "ship/hull", 0, 0xFFFFFFFFu, 10});

    auto ops = w.diffRender();
    REQUIRE(count(ops, FxWorld::RenderOp::Kind::Add) == 1);
    const auto* op = find(ops, e);
    REQUIRE(op != nullptr);
    REQUIRE(op->kind == FxWorld::RenderOp::Kind::Add);
    REQUIRE_THAT(op->transform.cx, WithinAbs(100.0f, 0.001f));   // cx,cy = CENTER
    REQUIRE_THAT(op->transform.cy, WithinAbs(200.0f, 0.001f));
    REQUIRE(op->sprite.asset == "ship/hull");

    // Nothing changed -> a second diff yields NO ops (retained: minimal traffic).
    REQUIRE(w.diffRender().empty());
}

TEST_CASE("FxWorldUnit: a transform-only entity (no sprite) produces no render ops", "[fx][unit]") {
    FxWorld w;
    EntityId e = w.spawn();
    w.setTransform(e, Transform{5.0f, 5.0f});
    REQUIRE(w.diffRender().empty());
    REQUIRE(w.aliveCount() == 1);   // it exists, it just doesn't draw
}

TEST_CASE("FxWorldUnit: move behavior advances the center by v*dt -> Update", "[fx][unit]") {
    FxWorld w;
    EntityId e = w.spawn();
    w.setSprite(e, Sprite{true, "", 1, 0xFFFFFFFFu, 0});
    w.addBehavior(e, move(10.0f, -4.0f));
    w.diffRender();                 // consume the initial Add

    w.tick(1.0f);
    REQUIRE_THAT(w.get(e)->transform.cx, WithinAbs(10.0f, 0.001f));
    REQUIRE_THAT(w.get(e)->transform.cy, WithinAbs(-4.0f, 0.001f));
    auto ops = w.diffRender();
    REQUIRE(count(ops, FxWorld::RenderOp::Kind::Update) == 1);
    REQUIRE_THAT(find(ops, e)->transform.cx, WithinAbs(10.0f, 0.001f));
}

TEST_CASE("FxWorldUnit: spin behavior advances rotation (deg/s -> rad)", "[fx][unit]") {
    FxWorld w;
    EntityId e = w.spawn();
    w.addBehavior(e, spin(90.0f));   // 90 deg/s
    w.tick(1.0f);
    REQUIRE_THAT(w.get(e)->transform.rotation, WithinAbs(1.5707963f, 0.0001f));   // 90 deg in rad
}

TEST_CASE("FxWorldUnit: lifetime expiry kills the entity and emits Remove", "[fx][unit]") {
    FxWorld w;
    EntityId e = w.spawn();
    w.setSprite(e, Sprite{true, "spark", 0, 0xFFFFFFFFu, 0});
    w.addBehavior(e, lifetime(2.0f));
    w.diffRender();                  // initial Add

    w.tick(1.0f);
    REQUIRE(w.aliveCount() == 1);    // still alive at t=1 < 2
    REQUIRE(w.diffRender().empty()); // no render change

    w.tick(1.5f);                    // t=2.5 >= 2 -> expired
    REQUIRE(w.aliveCount() == 0);
    auto ops = w.diffRender();
    REQUIRE(count(ops, FxWorld::RenderOp::Kind::Remove) == 1);
    REQUIRE(find(ops, e)->kind == FxWorld::RenderOp::Kind::Remove);
    // GC'd: it's gone, and a further diff is empty.
    REQUIRE(w.get(e) == nullptr);
    REQUIRE(w.diffRender().empty());
}

TEST_CASE("FxWorldUnit: destroy emits Remove and GCs the entity", "[fx][unit]") {
    FxWorld w;
    EntityId e = w.spawn();
    w.setSprite(e, Sprite{true, "x", 0, 0xFFFFFFFFu, 0});
    w.diffRender();                  // Add
    w.destroy(e);
    auto ops = w.diffRender();
    REQUIRE(count(ops, FxWorld::RenderOp::Kind::Remove) == 1);
    REQUIRE(w.get(e) == nullptr);
}

TEST_CASE("FxWorldUnit: an entity destroyed before its first Add emits no Remove", "[fx][unit]") {
    FxWorld w;
    EntityId e = w.spawn();
    w.setSprite(e, Sprite{true, "x", 0, 0xFFFFFFFFu, 0});
    w.destroy(e);                    // died before any diff emitted an Add
    auto ops = w.diffRender();
    REQUIRE(ops.empty());            // never added -> never removed
    REQUIRE(w.get(e) == nullptr);    // still GC'd
}

TEST_CASE("FxWorldUnit: behaviors compose in list order (move + lifetime on one entity)", "[fx][unit]") {
    FxWorld w;
    EntityId e = w.spawn();
    w.setSprite(e, Sprite{true, "bullet", 0, 0xFFFFFFFFu, 5});
    w.addBehavior(e, move(100.0f, 0.0f));
    w.addBehavior(e, lifetime(0.5f));
    w.diffRender();

    w.tick(0.25f);                   // moved 25px, age 0.25 < 0.5
    REQUIRE_THAT(w.get(e)->transform.cx, WithinAbs(25.0f, 0.001f));
    REQUIRE(w.aliveCount() == 1);

    w.tick(0.25f);                   // moved to 50px, age 0.5 >= 0.5 -> dead
    REQUIRE(w.aliveCount() == 0);
    REQUIRE(count(w.diffRender(), FxWorld::RenderOp::Kind::Remove) == 1);
}

TEST_CASE("FxWorldUnit: spawnFromPrefab copies the template's components + behaviors", "[fx][unit][prefab]") {
    FxWorld w;
    Prefab bullet;
    bullet.transform = Transform{0.0f, 0.0f};
    bullet.sprite = Sprite{true, "bullet", 0, 0xFF0000FFu, 5};
    bullet.behaviors = { move(100.0f, 0.0f), lifetime(1.0f) };
    w.registerPrefab("bullet", bullet);
    REQUIRE(w.hasPrefab("bullet"));

    EntityId e = w.spawnFromPrefab("bullet");
    REQUIRE(e != 0);
    REQUIRE(w.get(e)->sprite.asset == "bullet");     // component copied
    REQUIRE(w.get(e)->sprite.layer == 5);
    // the template's behaviors run on the instance
    w.tick(0.5f);
    REQUIRE_THAT(w.get(e)->transform.cx, WithinAbs(50.0f, 0.001f));
    REQUIRE(w.aliveCount() == 1);                    // age 0.5 < 1.0

    // First render diff -> Add carrying the prefab's sprite.
    auto ops = w.diffRender();
    REQUIRE(count(ops, FxWorld::RenderOp::Kind::Add) == 1);
    REQUIRE(find(ops, e)->sprite.asset == "bullet");
}

TEST_CASE("FxWorldUnit: prefab instances have INDEPENDENT behavior state", "[fx][unit][prefab]") {
    FxWorld w;
    Prefab p; p.sprite = Sprite{true, "x", 0, 0xFFFFFFFFu, 0}; p.behaviors = { lifetime(1.0f) };
    w.registerPrefab("mortal", p);

    EntityId a = w.spawnFromPrefab("mortal");
    w.tick(0.7f); w.diffRender();       // a: age 0.7, alive
    EntityId b = w.spawnFromPrefab("mortal");
    w.tick(0.5f);                       // a: age 1.2 -> dead ; b: age 0.5 -> alive
    REQUIRE(w.aliveCount() == 1);       // only b alive (fresh age copy, not shared)
    w.diffRender();                     // GCs the dead a
    REQUIRE(w.get(a) == nullptr);       // a expired independently
    REQUIRE(w.get(b) != nullptr);
}

TEST_CASE("FxWorldUnit: spawnFromPrefab on an unknown name fails soft (0, no entity)", "[fx][unit][prefab]") {
    FxWorld w;
    REQUIRE_FALSE(w.hasPrefab("ghost"));
    REQUIRE(w.spawnFromPrefab("ghost") == 0);
    REQUIRE(w.aliveCount() == 0);
}
