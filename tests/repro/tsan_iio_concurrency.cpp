/**
 * TSan repro driver: the concurrency shape of IOSystemStress TEST 6, and nothing else.
 *
 * WHAT : N publisher threads, EACH owning its own IntraIO instance, all publishing to one topic that a
 *        SINGLE consumer thread drains from its own instance. That is exactly TEST 6's shape — and the
 *        SUPPORTED concurrency per the IntraIO contract (one owning thread per instance; distinct
 *        instances route concurrently).
 *
 * WHY  : IOSystemStress fails intermittently (~4% of runs) with STATUS_HEAP_CORRUPTION on Windows, and
 *        the failure was localised to TEST 6. Windows can't be instrumented usefully here — 0xC0000374
 *        goes through RtlFailFast, which bypasses SetUnhandledExceptionFilter, so the in-process SEH
 *        backtrace never fires. The debug tripwire (ScopedAccessGuard) stays SILENT across 30 runs, so
 *        the known one-thread-per-instance violation is NOT what is happening. That leaves a race the
 *        contract is supposed to permit — which is exactly what a sanitizer is for.
 *
 * HOW  : built directly with g++ -fsanitize=thread under WSL (all deps are vendored, no network), the
 *        engine's documented recipe for concurrency work that MinGW cannot do. The driver carries no
 *        module loading, so it reaches the concurrent phase immediately and can be looped cheaply.
 *        Knobs come from argv so the pressure can be raised well past the real test's 5x100.
 */

#include "grove/IntraIOManager.h"
#include "grove/IntraIO.h"
#include "grove/JsonDataNode.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace grove;

int main(int argc, char** argv) {
    // Defaults mirror TEST 6 (5 publishers x 100 messages); argv raises the pressure to make a ~4%
    // failure reproduce in far fewer runs.
    const int publishers = (argc > 1) ? std::atoi(argv[1]) : 5;
    const int perThread  = (argc > 2) ? std::atoi(argv[2]) : 100;
    const int delayUs    = (argc > 3) ? std::atoi(argv[3]) : 100;

    auto& mgr = IntraIOManager::getInstance();
    auto consumerIO = mgr.createInstance("consumer");

    std::atomic<int> received{0};
    std::atomic<int> published{0};
    std::atomic<bool> running{true};

    consumerIO->subscribe("thread:.*", [&](const Message&) { received++; });

    // Each publisher gets its OWN stable instance, created up front — the supported pattern (N modules
    // each owning one instance for its lifetime).
    std::vector<std::shared_ptr<IntraIO>> pubIOs;
    pubIOs.reserve(static_cast<size_t>(publishers));
    for (int t = 0; t < publishers; ++t) {
        pubIOs.push_back(mgr.createInstance("pub_" + std::to_string(t)));
    }

    std::vector<std::thread> pubThreads;
    for (int t = 0; t < publishers; ++t) {
        pubThreads.emplace_back([&, t] {
            for (int i = 0; i < perThread; ++i) {
                auto data = std::make_unique<JsonDataNode>("data", nlohmann::json{{"thread", t}, {"id", i}});
                pubIOs[t]->publish("thread:test", std::move(data));
                published++;
                if (delayUs > 0) std::this_thread::sleep_for(std::chrono::microseconds(delayUs));
            }
        });
    }

    // ONE consumer thread owns consumerIO for its whole life (draining one instance from several
    // threads would be the contract violation the tripwire already catches — not what we're hunting).
    std::thread consumer([&] {
        while (running || consumerIO->hasMessages() > 0) {
            if (consumerIO->hasMessages() > 0) {
                try { consumerIO->pullAndDispatch(); } catch (...) {}
            }
            std::this_thread::sleep_for(std::chrono::microseconds(delayUs > 0 ? delayUs * 5 : 50));
        }
    });

    for (auto& t : pubThreads) t.join();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    running = false;
    consumer.join();

    std::printf("published=%d received=%d\n", published.load(), received.load());
    return 0;
}
