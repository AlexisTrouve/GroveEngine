// ============================================================================
// IIOUnsubscribe — a subscription can be REMOVED, so a dying subscriber stops receiving.
//
// QUOI  : locks IIO::unsubscribe(SubscriptionId) and the ScopedSubscription RAII handle: after
//         removal the handler is no longer invoked, the OTHER subscriptions survive, and a handler
//         may remove itself mid-dispatch.
//
// POURQUOI: until this existed, `subscribe` was a ONE-WAY door. Any object with a bounded lifetime
//         that subscribed capturing `this` on a longer-lived IIO left an orphaned handler pointing at
//         freed memory the moment it died — the next dispatch on that topic was a silent
//         use-after-free in release. The consumer could not defend itself: there was no API to remove
//         a subscription. (Diagnosed from the field: a game's scene was destroyed while its
//         `game:*` subscriptions stayed live, and one extra pump after teardown dispatched into the
//         dead object. Six rounds of hunting, and the engine had no fix to offer.)
//
// COMMENT: the "dangling handler" itself cannot be asserted directly — reading freed memory is UB,
//         not a testable value. What IS testable, and what closes the hole, is that the handler STOPS
//         BEING CALLED. Each case therefore proves a live handler fires, removes it, and proves the
//         same publish no longer reaches it — the before/after is the assertion, so a no-op
//         unsubscribe cannot pass.
// ============================================================================

#include <catch2/catch_test_macros.hpp>

#include <grove/IIO.h>
#include <grove/IntraIO.h>
#include <grove/IntraIOManager.h>
#include <grove/JsonDataNode.h>

#include <memory>
#include <string>

using namespace grove;

namespace {

std::unique_ptr<IDataNode> payload() {
    return std::make_unique<JsonDataNode>("d", nlohmann::json::object());
}

// Publish one message from `src` and drain `dst` so every matching handler runs.
void deliver(const std::shared_ptr<IntraIO>& src, const std::shared_ptr<IntraIO>& dst,
             const std::string& topic) {
    src->publish(topic, payload());
    while (dst->hasMessages() > 0) dst->pullAndDispatch();
}

} // namespace

TEST_CASE("unsubscribe stops a handler from being invoked", "[iio][unsubscribe]") {
    auto& mgr = IntraIOManager::getInstance();
    auto src = mgr.createInstance("unsub_src");
    auto dst = mgr.createInstance("unsub_dst");

    int hits = 0;
    const SubscriptionId id = dst->subscribe("unsub:evt", [&](const Message&) { ++hits; });
    REQUIRE(id != 0);                       // a real token, not a placeholder

    deliver(src, dst, "unsub:evt");
    REQUIRE(hits == 1);                     // the handler is genuinely live BEFORE removal

    REQUIRE(dst->unsubscribe(id));          // removed, and it says so

    deliver(src, dst, "unsub:evt");
    REQUIRE(hits == 1);                     // ...and the SAME publish no longer reaches it

    // Removing a token twice, or one that never existed, is a no-op that reports honestly rather
    // than pretending — a caller that double-frees a subscription must be able to tell.
    REQUIRE_FALSE(dst->unsubscribe(id));
    REQUIRE_FALSE(dst->unsubscribe(999999));

    mgr.removeInstance("unsub_src");
    mgr.removeInstance("unsub_dst");
}

TEST_CASE("unsubscribing one subscription leaves the others on the same pattern alive",
          "[iio][unsubscribe]") {
    // The routing entry for a pattern is shared: the manager knows "this instance listens to P", not
    // "this instance listens to P twice". Removing ONE of two local handlers must therefore NOT tear
    // down the shared route, or the survivor would go silent — a far worse bug than the leak it fixes.
    auto& mgr = IntraIOManager::getInstance();
    auto src = mgr.createInstance("unsub2_src");
    auto dst = mgr.createInstance("unsub2_dst");

    int a = 0, b = 0;
    const SubscriptionId idA = dst->subscribe("unsub2:evt", [&](const Message&) { ++a; });
    dst->subscribe("unsub2:evt", [&](const Message&) { ++b; });

    deliver(src, dst, "unsub2:evt");
    REQUIRE(a == 1);
    REQUIRE(b == 1);

    REQUIRE(dst->unsubscribe(idA));

    deliver(src, dst, "unsub2:evt");
    REQUIRE(a == 1);                        // removed
    REQUIRE(b == 2);                        // SURVIVED — the shared route is intact

    mgr.removeInstance("unsub2_src");
    mgr.removeInstance("unsub2_dst");
}

TEST_CASE("a handler can unsubscribe ITSELF during dispatch", "[iio][unsubscribe]") {
    // The real-world shape: a one-shot listener, or an object tearing itself down from inside its own
    // callback. pullAndDispatch runs handlers OUTSIDE the lock from a snapshot, so mutating the
    // subscription list mid-dispatch must be safe — this pins that.
    auto& mgr = IntraIOManager::getInstance();
    auto src = mgr.createInstance("unsub3_src");
    auto dst = mgr.createInstance("unsub3_dst");

    int hits = 0;
    SubscriptionId self = 0;
    self = dst->subscribe("unsub3:evt", [&](const Message&) {
        ++hits;
        dst->unsubscribe(self);             // remove myself while I am running
    });

    deliver(src, dst, "unsub3:evt");
    REQUIRE(hits == 1);

    deliver(src, dst, "unsub3:evt");
    REQUIRE(hits == 1);                     // the self-removal took effect

    mgr.removeInstance("unsub3_src");
    mgr.removeInstance("unsub3_dst");
}

TEST_CASE("ScopedSubscription removes the subscription when it goes out of scope",
          "[iio][unsubscribe][raii]") {
    // THE case that motivated the API. `Listener` is the shape that used to leave a dangling handler:
    // it subscribes capturing `this`, then dies while the IIO lives on. Holding the subscription in a
    // ScopedSubscription member ties the two lifetimes together, so its destructor closes the hole
    // without the owner having to remember anything.
    auto& mgr = IntraIOManager::getInstance();
    auto src = mgr.createInstance("unsub4_src");
    auto dst = mgr.createInstance("unsub4_dst");

    int hits = 0;

    struct Listener {
        int& counter;
        ScopedSubscription sub;
        Listener(IIO& io, int& c)
            : counter(c),
              sub(io, io.subscribe("unsub4:evt", [this](const Message&) { ++counter; })) {}
    };

    {
        Listener listener(*dst, hits);
        deliver(src, dst, "unsub4:evt");
        REQUIRE(hits == 1);                 // live while the owner is alive
    }                                       // <- listener dies; its ScopedSubscription unsubscribes

    // Without the RAII handle this publish would dispatch into the destroyed Listener — the exact
    // use-after-free this API exists to prevent. It must now reach nothing.
    deliver(src, dst, "unsub4:evt");
    REQUIRE(hits == 1);

    mgr.removeInstance("unsub4_src");
    mgr.removeInstance("unsub4_dst");
}

TEST_CASE("ScopedSubscription is movable and releases exactly once", "[iio][unsubscribe][raii]") {
    // Moving a handle must transfer ownership, not duplicate the removal: a moved-from handle that
    // still unsubscribed on destruction would silently kill the NEW owner's subscription.
    auto& mgr = IntraIOManager::getInstance();
    auto src = mgr.createInstance("unsub5_src");
    auto dst = mgr.createInstance("unsub5_dst");

    int hits = 0;
    {
        ScopedSubscription first(*dst, dst->subscribe("unsub5:evt", [&](const Message&) { ++hits; }));
        ScopedSubscription second(std::move(first));   // ownership moves here

        deliver(src, dst, "unsub5:evt");
        REQUIRE(hits == 1);                 // the moved-from handle did NOT tear it down
    }                                       // <- `second` releases it; `first` is inert

    deliver(src, dst, "unsub5:evt");
    REQUIRE(hits == 1);

    mgr.removeInstance("unsub5_src");
    mgr.removeInstance("unsub5_dst");
}
