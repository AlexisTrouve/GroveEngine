// ============================================================================
// AccessGuard.cpp — the debug tripwire implementation (grove::detail).
// See AccessGuard.h for the WHAT/WHY. The report is deliberately an ERROR log + a counter (not an
// abort): a debug run stays alive so the developer sees the message and the surrounding context,
// while the counter lets a test assert the guard bit.
// ============================================================================

#include <grove/detail/AccessGuard.h>

#include <atomic>

#if GROVE_DEBUG
#include <sstream>
#include <thread>
#include <logger/Logger.h>
#endif

namespace grove {
namespace detail {

std::atomic<std::uint64_t>& accessViolationCount() {
    static std::atomic<std::uint64_t> counter{0};
    return counter;
}

#if GROVE_DEBUG

namespace {
// spdlog can't format std::thread::id directly — stringify it via its stream operator.
std::string threadIdStr() {
    std::ostringstream os;
    os << std::this_thread::get_id();
    return os.str();
}
} // namespace

ScopedAccessGuard::ScopedAccessGuard(std::atomic<int>& active, const char* op, const std::string& instanceId)
    : active_(active) {
    const int prev = active_.fetch_add(1, std::memory_order_acq_rel);
    if (prev != 0) {
        // Concurrent OVERLAP: a second thread entered while the first is still inside.
        accessViolationCount().fetch_add(1, std::memory_order_relaxed);
        static auto logger = stillhammer::createDomainLogger("IIOGuard", "engine");
        logger->error(
            "🧵❌ CONCURRENCY INVARIANT VIOLATION: '{}' on instance '{}' entered by thread {} while "
            "{} other thread(s) already inside. This object is single-owning-thread by contract; "
            "concurrent access is a data race (silent heap corruption in a release build). "
            "Fix: give each thread its OWN instance, or serialize access at the call site.",
            op, instanceId, threadIdStr(), prev);
    }
}

ScopedAccessGuard::~ScopedAccessGuard() {
    active_.fetch_sub(1, std::memory_order_acq_rel);
}

#endif // GROVE_DEBUG

} // namespace detail
} // namespace grove
