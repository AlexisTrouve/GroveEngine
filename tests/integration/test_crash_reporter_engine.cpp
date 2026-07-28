// ============================================================================
// CrashReporterEngineE2E — the crash reporter wired into DebugEngine (B1c).
//
// QUOI  : two proofs. (1) In-process, no crash: snapshotCrashContext() captures LIVE engine state
//         (frame count, module names) + the last-N IIO messages from the ReplaySink. (2) Windows,
//         real crash: a doomed child engine faults and its installed handler writes the CrashContext
//         JSON (with live frameCount) + a minidump.
//
// POURQUOI: (1) locks the snapshot content — including the ReplaySink->MessageTrace mapping (the IIO
//         trail) — deterministically and cross-platform. (2) proves the initialize()->install->real
//         crash->file path end to end. Together: the wiring bites, on real state and a real fault.
// ============================================================================

#include <catch2/catch_test_macros.hpp>

#include <grove/DebugEngine.h>
#include <grove/IModule.h>
#include <grove/IModuleSystem.h>
#include <grove/IIO.h>
#include <grove/IntraIO.h>
#include <grove/IntraIOManager.h>
#include <grove/JsonDataNode.h>

#include <memory>
#include <string>

using namespace grove;

namespace {

// Minimal module so the engine has a registered name to snapshot.
class TinyModule : public IModule {
    std::unique_ptr<IDataNode> cfg_;
public:
    void process(const IDataNode&) override {}
    void setConfiguration(const IDataNode&, IIO*, ITaskScheduler*) override {
        cfg_ = std::make_unique<JsonDataNode>("config", nlohmann::json::object());
    }
    const IDataNode& getConfiguration() override { return *cfg_; }
    std::unique_ptr<IDataNode> getHealthStatus() override { return std::make_unique<JsonDataNode>("h", nlohmann::json::object()); }
    void shutdown() override {}
    std::unique_ptr<IDataNode> getState() override { return std::make_unique<JsonDataNode>("s", nlohmann::json::object()); }
    void setState(const IDataNode&) override {}
    std::string getType() const override { return "TinyModule"; }
    bool isIdle() const override { return true; }
};

std::unique_ptr<IDataNode> emptyNode() { return std::make_unique<JsonDataNode>("d", nlohmann::json::object()); }

} // namespace

TEST_CASE("DebugEngine::snapshotCrashContext captures live state + the IIO trail", "[crash][reporter]") {
    auto& mgr = IntraIOManager::getInstance();
    mgr.enableReplaySink(64);                 // fresh capture session (clears)
    mgr.setSimTime(/*tick=*/1234, /*simTime=*/20.0);

    // Real IIO traffic so the ReplaySink has a trail to fold into the report.
    auto src = mgr.createInstance("SRC");
    auto snk = mgr.createInstance("SNK");
    snk->subscribe("evt:test", [](const Message&) {});
    src->publish("evt:test", emptyNode());
    src->publish("evt:test", emptyNode());

    DebugEngine engine;
    engine.initialize();
    engine.registerStaticModule("tiny", std::make_unique<TinyModule>(), ModuleSystemType::SEQUENTIAL);
    engine.step(0.016f);
    engine.step(0.016f);                      // frameCount -> 2

    const crash::CrashContext ctx = engine.snapshotCrashContext("SIGSEGV");
    REQUIRE(ctx.reason == "SIGSEGV");
    REQUIRE(ctx.frameCount == 2);
    REQUIRE(ctx.moduleNames.size() == 1);
    REQUIRE(ctx.moduleNames[0] == "tiny");

    // The ReplaySink trail is captured AND correctly mapped to MessageTrace (topic + source).
    REQUIRE_FALSE(ctx.recentMessages.empty());
    bool sawEvt = false;
    for (const auto& m : ctx.recentMessages) {
        if (m.topic == "evt:test" && m.source == "SRC") sawEvt = true;
    }
    REQUIRE(sawEvt);

    // And the whole thing serializes to the report shape.
    const auto j = crash::toJson(ctx);
    REQUIRE(j["grove_crash"]["frameCount"] == 2);
    REQUIRE(j["grove_crash"]["modules"][0] == "tiny");

    mgr.disableReplaySink();
}

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cstdio>
#include <fstream>
#include <iterator>

namespace {
std::string exeDir() {
    char buf[MAX_PATH];
    DWORD n = GetModuleFileNameA(nullptr, buf, MAX_PATH);
    std::string p(buf, n);
    auto slash = p.find_last_of("\\/");
    return (slash == std::string::npos) ? std::string(".") : p.substr(0, slash);
}
} // namespace

TEST_CASE("real crash through the engine writes the CrashContext + minidump", "[crash][reporter][real]") {
    const std::string dir  = exeDir();
    const std::string base = dir + "\\grove_enginecrash";
    const std::string json = base + ".json";
    const std::string dump = base + ".dmp";
    const std::string child = dir + "\\crash_child_engine.exe";

    std::remove(json.c_str());
    std::remove(dump.c_str());
    REQUIRE(std::ifstream(child).good());

    _putenv_s("GROVE_CRASH_BASE", base.c_str());

    STARTUPINFOA si;        ZeroMemory(&si, sizeof(si)); si.cb = sizeof(si);
    PROCESS_INFORMATION pi; ZeroMemory(&pi, sizeof(pi));
    std::string cmd = "\"" + child + "\"";
    BOOL ok = CreateProcessA(child.c_str(), cmd.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr, &si, &pi);
    REQUIRE(ok);
    WaitForSingleObject(pi.hProcess, 30000);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    // The engine's handler wrote the CrashContext JSON with LIVE frameCount (3 steps before the fault).
    std::ifstream jf(json);
    REQUIRE(jf.good());
    const std::string body((std::istreambuf_iterator<char>(jf)), std::istreambuf_iterator<char>());
    REQUIRE(body.find("grove_crash") != std::string::npos);
    REQUIRE(body.find("EXCEPTION_ACCESS_VIOLATION") != std::string::npos);
    REQUIRE(body.find("\"frameCount\": 3") != std::string::npos);

    // And the native minidump.
    std::ifstream df(dump, std::ios::binary | std::ios::ate);
    REQUIRE(df.good());
    REQUIRE(static_cast<long long>(df.tellg()) > 0);
}

namespace {

// Run the doomed child once and return the size of the minidump it wrote (-1 if none).
// `full` selects DumpDetail::Full inside the child via GROVE_CRASH_FULL.
long long crashChildDumpSize(const std::string& base, bool full) {
    const std::string dir   = exeDir();
    const std::string dump  = base + ".dmp";
    const std::string child = dir + "\\crash_child_engine.exe";

    std::remove(dump.c_str());
    _putenv_s("GROVE_CRASH_BASE", base.c_str());
    _putenv_s("GROVE_CRASH_FULL", full ? "1" : "");   // "" REMOVES the variable on Windows

    STARTUPINFOA si;        ZeroMemory(&si, sizeof(si)); si.cb = sizeof(si);
    PROCESS_INFORMATION pi; ZeroMemory(&pi, sizeof(pi));
    std::string cmd = "\"" + child + "\"";
    if (!CreateProcessA(child.c_str(), cmd.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr, &si, &pi)) {
        return -1;
    }
    WaitForSingleObject(pi.hProcess, 60000);   // a full-memory dump takes longer to write than a normal one
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    std::ifstream df(dump, std::ios::binary | std::ios::ate);
    return df.good() ? static_cast<long long>(df.tellg()) : -1;
}

} // namespace

TEST_CASE("DumpDetail::Full puts the HEAP in the minidump (Normal cannot)", "[crash][reporter][real]") {
    // QUOI  : same child, same fault, two runs — the only difference is setCrashDumpDetail(Full).
    //         The Full dump must be MUCH larger, because it carries the process's memory.
    //
    // POURQUOI: this is the whole point of the setting, and it is exactly the kind of flag that can
    //         be plumbed through an interface, stored in a member, and then never actually reach
    //         MiniDumpWriteDump — every layer looks right and the dump is silently still Normal. A
    //         test on the API ("the setter stores it") would pass in that broken state. Only the
    //         artifact's SIZE proves the flag reached the OS call. Verified red first: with the
    //         plumbing in place but the flags still hardcoded to MiniDumpNormal, the two dumps came
    //         out the same size and this failed.
    //
    // COMMENT: the threshold is a RATIO, not an absolute size — absolute bytes depend on the machine,
    //          the CRT and the build. A full-memory dump of even a tiny engine process is orders of
    //          magnitude past a stacks-only one, so 4x is far below the real margin yet immune to
    //          a Normal dump that happens to be a bit fat.
    const std::string dir = exeDir();

    const long long normalSize = crashChildDumpSize(dir + "\\grove_dump_normal", /*full=*/false);
    REQUIRE(normalSize > 0);

    const long long fullSize = crashChildDumpSize(dir + "\\grove_dump_full", /*full=*/true);
    REQUIRE(fullSize > 0);

    INFO("normal=" << normalSize << " bytes, full=" << fullSize << " bytes");
    REQUIRE(fullSize > normalSize * 4);
}

#endif // _WIN32
