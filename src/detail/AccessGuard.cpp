// ============================================================================
// AccessGuard.cpp — the debug tripwire implementation (grove::detail).
// See AccessGuard.h for the WHAT/WHY. The report is deliberately an ERROR log + a counter (not an
// abort): a debug run stays alive so the developer sees the message and the surrounding context,
// while the counter lets a test assert the guard bit.
// ============================================================================

#include <grove/detail/AccessGuard.h>

#include <atomic>

#if GROVE_DEBUG
#include <functional>
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

// Identifiant de fil ramene a un entier comparable atomiquement. 0 est reserve a "personne", donc un
// hachage qui vaudrait 0 est remonte a 1 -- sinon un fil malchanceux passerait pour absent.
static std::size_t currentThreadKey() {
    const std::size_t h = std::hash<std::thread::id>{}(std::this_thread::get_id());
    return h == 0 ? 1u : h;
}

ScopedAccessGuard::ScopedAccessGuard(AccessState& state, const char* op, const std::string& instanceId)
    : state_(state) {
    const std::size_t me = currentThreadKey();
    const int prev = state_.active.fetch_add(1, std::memory_order_acq_rel);

    if (prev == 0) {
        // Section vide : ce fil la revendique.
        state_.owner.store(me, std::memory_order_release);
        return;
    }

    // Deja occupee. Par NOUS ? Alors c'est une re-entrance legitime (un handler qui republie), pas
    // une course -- on ne signale rien. Si deux fils entrent vraiment de front, celui qui lit un
    // `owner` pas encore publie (0) le voit different de `me` et signale : le bon verdict.
    if (state_.owner.load(std::memory_order_acquire) == me) {
        return;
    }

    {
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
    // Le dernier a sortir libere la propriete, pour que le fil suivant puisse revendiquer la section
    // (passation sequentielle) sans etre pris pour un intrus.
    if (state_.active.fetch_sub(1, std::memory_order_acq_rel) == 1) {
        state_.owner.store(0, std::memory_order_release);
    }
}

#endif // GROVE_DEBUG

} // namespace detail
} // namespace grove
