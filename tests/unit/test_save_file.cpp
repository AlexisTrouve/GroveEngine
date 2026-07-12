/**
 * SaveFileUnit — the pure oracle for grove::save::SaveFile (whole-game save/load).
 *
 * Asserts the load-bearing logic directly, headless: capture -> save(disk) -> load -> state/restoreInto round
 * trips module state; capture deep-copies immediately (survives the source's destruction — the cross-DLL
 * safety); and every path is fail-soft (missing / malformed / future-version files, missing modules).
 */

#include <catch2/catch_test_macros.hpp>

#include "grove/save/SaveFile.h"
#include "grove/IModule.h"
#include "grove/JsonDataNode.h"

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

using namespace grove;
using namespace grove::save;

namespace {

// A minimal stateful module: {level, name} round-tripped through getState/setState (like a real game module).
class StatefulModule : public IModule {
public:
    int level = 0;
    std::string name;

    void setConfiguration(const IDataNode&, IIO*, ITaskScheduler*) override {}
    void process(const IDataNode&) override {}
    void shutdown() override {}
    std::unique_ptr<IDataNode> getState() override {
        return std::make_unique<JsonDataNode>("state", nlohmann::json{{"level", level}, {"name", name}});
    }
    void setState(const IDataNode& s) override {
        if (const auto* jn = dynamic_cast<const JsonDataNode*>(&s)) {
            const auto& j = jn->getJsonData();
            level = j.value("level", 0);
            name  = j.value("name", std::string{});
        }
    }
    const IDataNode& getConfiguration() override { static JsonDataNode c("c"); return c; }
    std::unique_ptr<IDataNode> getHealthStatus() override { return std::make_unique<JsonDataNode>("h"); }
    std::string getType() const override { return "Stateful"; }
    bool isIdle() const override { return true; }
};

std::string tempPath(const std::string& tag) {
    return (std::filesystem::temp_directory_path() / ("grove_save_" + tag + ".json")).string();
}

} // namespace

TEST_CASE("SaveFileUnit: capture -> save -> load round-trips module states", "[save][unit]") {
    const auto path = tempPath("rt");
    std::filesystem::remove(path);
    {
        SaveFile sf;
        sf.capture("fleet",   JsonDataNode("s", nlohmann::json{{"ships", 12}}));
        sf.capture("economy", JsonDataNode("s", nlohmann::json{{"credits", 3400}}));
        REQUIRE(sf.save(path));
    }
    REQUIRE(std::filesystem::exists(path));

    SaveFile loaded;
    REQUIRE(loaded.load(path));
    REQUIRE(loaded.loadedFormatVersion() == SaveFile::kFormatVersion);
    REQUIRE(loaded.savedAtUnixMs() > 0);                 // wall-clock stamp
    REQUIRE(loaded.has("fleet"));
    REQUIRE(loaded.has("economy"));
    REQUIRE((*loaded.state("fleet"))["ships"].get<int>() == 12);
    REQUIRE((*loaded.state("economy"))["credits"].get<int>() == 3400);
    REQUIRE(loaded.moduleNames().size() == 2);

    std::filesystem::remove(path);
}

TEST_CASE("SaveFileUnit: captureModule -> save -> load -> restoreInto restores a module's state", "[save][unit]") {
    const auto path = tempPath("mod");
    std::filesystem::remove(path);

    StatefulModule saved;
    saved.level = 7;
    saved.name  = "Andromeda";
    {
        SaveFile sf;
        sf.captureModule("game", saved);                 // captures saved.getState()
        REQUIRE(sf.save(path));
    }

    SaveFile loaded;
    REQUIRE(loaded.load(path));

    StatefulModule fresh;                                // defaults: level=0, name=""
    REQUIRE(loaded.restoreInto("game", fresh));          // calls fresh.setState() with the saved state
    REQUIRE(fresh.level == 7);
    REQUIRE(fresh.name == "Andromeda");

    // A module absent from the save is left UNTOUCHED (fail-soft — the game evolved).
    StatefulModule other;
    other.level = 99;
    REQUIRE_FALSE(loaded.restoreInto("not_in_save", other));
    REQUIRE(other.level == 99);

    std::filesystem::remove(path);
}

TEST_CASE("SaveFileUnit: capture deep-copies immediately (survives the source's destruction)", "[save][unit]") {
    // The cross-DLL safety property: capture() reads the node NOW and never retains it. Prove it by destroying
    // the source node BEFORE reading the captured value back.
    SaveFile sf;
    {
        JsonDataNode ephemeral("s", nlohmann::json{{"v", 42}});
        sf.capture("m", ephemeral);
    }                                                    // ephemeral destroyed here
    REQUIRE(sf.has("m"));
    REQUIRE((*sf.state("m"))["v"].get<int>() == 42);     // data survived — it was deep-copied, not referenced
}

TEST_CASE("SaveFileUnit: load fails soft on missing / malformed / future-version / wrong-shape files", "[save][unit]") {
    SaveFile sf;
    REQUIRE_FALSE(sf.load(tempPath("does_not_exist_xyz")));   // missing file

    const auto bad = tempPath("bad");
    std::ofstream(bad) << "{ not valid json ";
    REQUIRE_FALSE(sf.load(bad));                              // malformed json

    const auto future = tempPath("future");
    std::ofstream(future) << R"({"grove_save":{"formatVersion":9999,"modules":{}}})";
    REQUIRE_FALSE(sf.load(future));                           // newer format than this build understands

    const auto v0 = tempPath("v0");
    std::ofstream(v0) << R"({"grove_save":{"formatVersion":0,"modules":{}}})";
    REQUIRE_FALSE(sf.load(v0));                               // version 0 (== the missing-key default) is rejected

    const auto wrong = tempPath("wrong");
    std::ofstream(wrong) << R"({"hello":1})";
    REQUIRE_FALSE(sf.load(wrong));                            // valid json, but not a grove save

    std::filesystem::remove(bad);
    std::filesystem::remove(future);
    std::filesystem::remove(v0);
    std::filesystem::remove(wrong);
}
