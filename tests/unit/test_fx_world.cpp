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

// A text component diffs into its OWN render pool (render:text:*), independent of sprites.
TEST_CASE("FxWorldUnit: a text-bearing entity emits a Text render op (own pool)", "[fx][unit][text]") {
    FxWorld w;
    EntityId e = w.spawn();
    w.setTransform(e, Transform{40.0f, 60.0f});
    w.setText(e, Text{true, "42", 0xFF0000FFu, 900, 20});     // red "42", layer 900, 20px

    auto ops = w.diffRender();
    REQUIRE(count(ops, FxWorld::RenderOp::Kind::Add) == 1);
    const auto* op = find(ops, e);
    REQUIRE(op != nullptr);
    REQUIRE(op->prim == FxWorld::RenderOp::Prim::Text);        // routed to render:text:*
    REQUIRE(op->text.text == "42");
    REQUIRE(op->text.color == 0xFF0000FFu);
    REQUIRE(op->text.fontSize == 20);
    REQUIRE_THAT(op->transform.cx, WithinAbs(40.0f, 0.001f));

    REQUIRE(w.diffRender().empty());                           // unchanged -> no ops

    // Destroying it emits a Text Remove (from the text pool).
    w.destroy(e);
    auto rem = w.diffRender();
    REQUIRE(count(rem, FxWorld::RenderOp::Kind::Remove) == 1);
    REQUIRE(find(rem, e)->prim == FxWorld::RenderOp::Prim::Text);
}

// The floating-damage-number archetype: a Text that rises (velocity up), fades out, and self-expires. This
// is the drifterra priority composition — proven purely, then E2E through the module.
TEST_CASE("FxWorldUnit: floating damage number rises, fades, and expires", "[fx][unit][text]") {
    FxWorld w;
    Prefab dmg;
    dmg.text = Text{true, "", 0xFFFFFFFFu, 1000, 18};          // string overridden per-instance
    dmg.behaviors = { velocity(0.0f, -40.0f, 0.0f), fade(1.0f, 1.0f, 0.0f), lifetime(1.0f) };
    w.registerPrefab("damage_number", dmg);

    EntityId e = w.spawnFromPrefab("damage_number");
    w.setTransform(e, Transform{100.0f, 200.0f});
    w.setText(e, Text{true, "-25", 0xFFFFFFFFu, 1000, 18});    // the resolved value string
    w.diffRender();                                            // consume the initial Add

    w.tick(0.5f);
    REQUIRE_THAT(w.get(e)->transform.cy, WithinAbs(180.0f, 0.001f));   // rose 40*0.5 = 20 (cy decreased)
    REQUIRE((w.get(e)->text.color & 0xFFu) == 128u);          // half-faded
    REQUIRE(w.aliveCount() == 1);

    w.tick(0.5f);                                              // t=1.0 -> lifetime expires
    REQUIRE(w.aliveCount() == 0);
    REQUIRE(count(w.diffRender(), FxWorld::RenderOp::Kind::Remove) == 1);
}

// An Emitter fires a one-shot particle BURST: `count` prefab instances launched in a cone, at the emitter's
// position, with deterministic-random velocity. The emitter (invisible one-shot) retires after firing.
TEST_CASE("FxWorldUnit: emitter fires a one-shot particle burst in a cone", "[fx][unit][emitter]") {
    FxWorld w;
    // The particle template: a small sprite that fades + dies over 0.5 s (authored once, reused per particle).
    Prefab spark;
    spark.sprite = Sprite{true, "fx/spark", 0, 0xFFFFFFFFu, 900};
    spark.behaviors = { fade(0.5f, 1.0f, 0.0f), lifetime(0.5f) };
    w.registerPrefab("spark", spark);

    // Burst 8 sparks eastward (dir 0°, spread 90° -> every particle has vx>0), speed in [50,100].
    EntityId em = w.spawn();
    w.setTransform(em, Transform{200.0f, 300.0f});
    w.setEmitter(em, Emitter{true, "spark", 8, 50.0f, 100.0f, 90.0f, 0.0f, true, false});
    REQUIRE(w.aliveCount() == 1);                    // only the emitter exists pre-tick

    w.tick(0.016f);                                  // fires the burst; the one-shot emitter retires
    REQUIRE(w.aliveCount() == 8);                    // 8 particles alive (emitter now dead, not counted)

    int checked = 0;
    for (EntityId pid = 1; pid <= 100 && checked < 8; ++pid) {
        Entity* p = w.get(pid);
        if (!p || !p->alive || !p->sprite.present) continue;   // skip the retired emitter (no sprite)
        REQUIRE_THAT(p->transform.cx, WithinAbs(200.0f, 0.001f));   // spawned AT the emitter
        REQUIRE_THAT(p->transform.cy, WithinAbs(300.0f, 0.001f));
        REQUIRE(p->sprite.asset == "fx/spark");                     // look inherited from the prefab
        bool foundVel = false;
        for (const Behavior& b : p->behaviors) {
            if (b.type == Behavior::Type::Velocity) {
                foundVel = true;
                const float sp = std::sqrt(b.vx * b.vx + b.vy * b.vy);
                REQUIRE(b.vx > 0.0f);                              // cone dir 0 ± 45° -> eastward launch
                REQUIRE(sp >= 50.0f - 0.5f);                       // speed within [50,100]
                REQUIRE(sp <= 100.0f + 0.5f);
            }
        }
        REQUIRE(foundVel);                                         // the emitter attached a launch velocity
        ++checked;
    }
    REQUIRE(checked == 8);

    // One-shot: a second tick spawns NO more particles (they're still alive at t=0.032 < 0.5).
    w.tick(0.016f);
    REQUIRE(w.aliveCount() == 8);
}

// Fail-soft: an emitter whose particle prefab doesn't exist spawns nothing, and the one-shot still retires.
TEST_CASE("FxWorldUnit: emitter with an unknown particle prefab fires soft (no particles)", "[fx][unit][emitter]") {
    FxWorld w;
    EntityId em = w.spawn();
    w.setEmitter(em, Emitter{true, "ghost", 10, 50.0f, 100.0f, 360.0f, 0.0f, true, false});
    w.tick(0.016f);
    REQUIRE(w.aliveCount() == 0);                    // nothing spawned; the one-shot emitter retired itself
}

// An "explosion" archetype carries the emitter IN the prefab -> spawn it and it bursts on the next tick.
TEST_CASE("FxWorldUnit: a prefab can carry an emitter (explosion archetype)", "[fx][unit][emitter][prefab]") {
    FxWorld w;
    Prefab debris; debris.sprite = Sprite{true, "fx/debris", 0, 0xFFFFFFFFu, 900};
    debris.behaviors = { velocity(0.0f, 0.0f, 1.0f), lifetime(0.4f) };   // (emitter overwrites launch velocity)
    w.registerPrefab("debris", debris);

    Prefab explosion;                                 // no sprite -> the explosion entity is just the emitter
    explosion.emitter = Emitter{true, "debris", 12, 80.0f, 160.0f, 360.0f, 0.0f, true, false};
    w.registerPrefab("explosion", explosion);

    EntityId boom = w.spawnFromPrefab("explosion");
    w.setTransform(boom, Transform{500.0f, 500.0f});
    REQUIRE(w.aliveCount() == 1);

    w.tick(0.016f);
    REQUIRE(w.aliveCount() == 12);                    // 12 debris particles; the emitter retired
}

// A CONTINUOUS (stream) emitter emits ratePerSec particles/second every tick and does NOT self-destruct
// (engine trails / smoke). Use a persist-forever particle (sprite only, no lifetime) to count emission exactly.
TEST_CASE("FxWorldUnit: continuous emitter streams at ratePerSec and persists", "[fx][unit][emitter][stream]") {
    FxWorld w;
    Prefab dust; dust.sprite = Sprite{true, "fx/dust", 0, 0xFFFFFFFFu, 800};   // no lifetime -> persists
    w.registerPrefab("dust", dust);

    EntityId src = w.spawn();
    w.setEmitter(src, streamEmitter("dust", 10.0f, 20.0f, 40.0f, 60.0f, 0.0f));   // 10 particles/sec
    REQUIRE(w.aliveCount() == 1);

    for (int i = 0; i < 10; ++i) w.tick(0.1f);        // 1.0 s at 10/s -> 10 particles (1 per 0.1 s tick)
    REQUIRE(w.aliveCount() == 11);                    // 10 dust + the emitter
    REQUIRE(w.get(src) != nullptr);                   // emitter is NOT retired (continuous)
    REQUIRE(w.get(src)->alive);
}

// Sub-frame rates accumulate: at 10/s a 0.05 s tick is half a particle (0), the next 0.05 s completes one.
TEST_CASE("FxWorldUnit: continuous emitter accumulates fractional particles", "[fx][unit][emitter][stream]") {
    FxWorld w;
    Prefab dust; dust.sprite = Sprite{true, "d", 0, 0xFFFFFFFFu, 0};
    w.registerPrefab("dust", dust);
    EntityId src = w.spawn();
    w.setEmitter(src, streamEmitter("dust", 10.0f, 0.0f, 0.0f));   // 10/s

    w.tick(0.05f);                                    // +0.5 particle -> none yet
    REQUIRE(w.aliveCount() == 1);                     // just the emitter
    w.tick(0.05f);                                    // +0.5 -> 1.0 -> spawns one
    REQUIRE(w.aliveCount() == 2);                     // emitter + 1 dust
}

// The steady-state particle count is self-bounded by the particle's OWN lifetime (rate × lifetime), so a
// trail doesn't grow without limit — new particles replace expiring ones.
TEST_CASE("FxWorldUnit: continuous stream steady-state is bounded by particle lifetime", "[fx][unit][emitter][stream]") {
    FxWorld w;
    Prefab spark; spark.sprite = Sprite{true, "s", 0, 0xFFFFFFFFu, 900};
    spark.behaviors = { lifetime(0.4f) };             // each particle lives 0.4 s
    w.registerPrefab("spark", spark);

    EntityId src = w.spawn();
    w.setEmitter(src, streamEmitter("spark", 25.0f, 10.0f, 20.0f));   // 25/s ; dt 0.04 -> 1 particle/tick

    for (int i = 0; i < 30; ++i) w.tick(0.04f);       // 1.2 s — well past warmup
    // ~lifetime/dt = 0.4/0.04 = 10 particles in flight + the emitter -> ~11. Bounded, NOT ~30.
    REQUIRE(w.aliveCount() >= 9);
    REQUIRE(w.aliveCount() <= 13);
}

// fade ramps the sprite's alpha (AA byte of 0xRRGGBBAA) fromA -> toA over `seconds`, then holds.
TEST_CASE("FxWorldUnit: fade ramps the sprite alpha over its duration", "[fx][unit][fade]") {
    FxWorld w;
    EntityId e = w.spawn();
    w.setSprite(e, Sprite{true, "blast", 0, 0xFFFFFFFFu, 900});   // alpha starts full (0xFF)
    w.addBehavior(e, fade(1.0f, 1.0f, 0.0f));                     // fade out over 1s

    w.tick(0.5f);
    REQUIRE((w.get(e)->sprite.color & 0xFFu) == 128u);           // ~half alpha at t=0.5 (0.5*255=127.5->128)
    w.tick(0.5f);
    REQUIRE((w.get(e)->sprite.color & 0xFFu) == 0u);             // fully faded at t>=1
    w.tick(0.5f);
    REQUIRE((w.get(e)->sprite.color & 0xFFu) == 0u);             // holds at toA
    // The RGB is untouched — only the alpha byte changes.
    REQUIRE((w.get(e)->sprite.color & 0xFFFFFF00u) == 0xFFFFFF00u);
}

// velocity moves by an initial velocity that decelerates by `drag` per second.
TEST_CASE("FxWorldUnit: velocity+drag advances then decelerates", "[fx][unit][velocity]") {
    FxWorld w;
    EntityId e = w.spawn();
    w.addBehavior(e, velocity(100.0f, 0.0f, 2.0f));              // 100 px/s east, drag 2/s

    w.tick(0.1f);
    const float x1 = w.get(e)->transform.cx;                    // += 100*0.1 = 10 ; vx -> 100*(1-0.2)=80
    REQUIRE_THAT(x1, WithinAbs(10.0f, 0.001f));
    w.tick(0.1f);
    const float x2 = w.get(e)->transform.cx;                    // += 80*0.1 = 8 -> 18
    REQUIRE_THAT(x2, WithinAbs(18.0f, 0.001f));
    REQUIRE((x2 - x1) < 10.0f);                                 // decelerating: the 2nd step advanced LESS

    // drag 0 = constant velocity (no deceleration).
    FxWorld w2;
    EntityId e2 = w2.spawn();
    w2.addBehavior(e2, velocity(50.0f, 0.0f, 0.0f));
    w2.tick(0.2f); w2.tick(0.2f);
    REQUIRE_THAT(w2.get(e2)->transform.cx, WithinAbs(20.0f, 0.001f));   // 50*0.4, undamped
}
