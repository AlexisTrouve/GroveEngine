#include "grove/JsonDataNode.h"
#include "grove/JsonDataTree.h"
#include "../helpers/TestMetrics.h"
#include "../helpers/TestAssertions.h"
#include "../helpers/TestReporter.h"
#include <iostream>
#include <fstream>
#include <filesystem>
#include <thread>
#include <chrono>

using namespace grove;

int main() {
    TestReporter reporter("DataNode Integration Test");
    TestMetrics metrics;

    std::cout << "================================================================================\n";
    std::cout << "TEST: DataNode Integration Test\n";
    std::cout << "================================================================================\n\n";

    // === SETUP ===
    std::cout << "Setup: Creating test directories...\n";
    std::filesystem::create_directories("test_data/config");
    std::filesystem::create_directories("test_data/data");

    // Créer IDataTree
    auto tree = std::make_unique<JsonDataTree>("test_data");

    // ========================================================================
    // TEST 1: Typed Setters & Getters
    // ========================================================================
    std::cout << "\n=== TEST 1: Typed Setters & Getters ===\n";

    auto dataRoot = tree->getDataRoot();

    // Create player node directly through tree
    auto playerNode = std::make_unique<JsonDataNode>("player", nlohmann::json::object());

    // Test setInt
    playerNode->setInt("score", 100);
    ASSERT_EQ(playerNode->getInt("score"), 100, "setInt should work");
    std::cout << "  ✓ setInt/getInt works\n";

    // Test setString
    playerNode->setString("name", "Player1");
    ASSERT_EQ(playerNode->getString("name"), "Player1", "setString should work");
    std::cout << "  ✓ setString/getString works\n";

    // Test setBool
    playerNode->setBool("active", true);
    ASSERT_EQ(playerNode->getBool("active"), true, "setBool should work");
    std::cout << "  ✓ setBool/getBool works\n";

    // Test setDouble
    playerNode->setDouble("ratio", 3.14);
    double ratio = playerNode->getDouble("ratio");
    ASSERT_TRUE(std::abs(ratio - 3.14) < 0.001, "setDouble should work");
    std::cout << "  ✓ setDouble/getDouble works\n";

    reporter.addAssertion("typed_setters", true);
    std::cout << "✓ TEST 1 PASSED\n";

    // ========================================================================
    // TEST 2: Data Hash
    // ========================================================================
    std::cout << "\n=== TEST 2: Data Hash ===\n";

    auto testNode = std::make_unique<JsonDataNode>("test", nlohmann::json{
        {"value", 42}
    });

    std::string hash1 = testNode->getDataHash();
    std::cout << "  Hash 1: " << hash1.substr(0, 16) << "...\n";

    // Modify data
    testNode->setInt("value", 43);

    std::string hash2 = testNode->getDataHash();
    std::cout << "  Hash 2: " << hash2.substr(0, 16) << "...\n";

    ASSERT_TRUE(hash1 != hash2, "Hashes should differ after data change");

    reporter.addAssertion("data_hash", true);
    std::cout << "✓ TEST 2 PASSED\n";

    // ========================================================================
    // TEST 3: Tree Hash
    // ========================================================================
    std::cout << "\n=== TEST 3: Tree Hash ===\n";

    auto root = std::make_unique<JsonDataNode>("root", nlohmann::json::object());
    auto child1 = std::make_unique<JsonDataNode>("child1", nlohmann::json{{"data", 1}});
    auto child2 = std::make_unique<JsonDataNode>("child2", nlohmann::json{{"data", 2}});

    // Get raw pointers before moving
    auto* child1Ptr = child1.get();

    root->setChild("child1", std::move(child1));
    root->setChild("child2", std::move(child2));

    std::string treeHash1 = root->getTreeHash();
    std::cout << "  Tree Hash 1: " << treeHash1.substr(0, 16) << "...\n";

    // Modify child1 through parent
    child1Ptr->setInt("data", 999);

    std::string treeHash2 = root->getTreeHash();
    std::cout << "  Tree Hash 2: " << treeHash2.substr(0, 16) << "...\n";

    ASSERT_TRUE(treeHash1 != treeHash2, "Tree hash should change when child changes");

    reporter.addAssertion("tree_hash", true);
    std::cout << "✓ TEST 3 PASSED\n";

    // ========================================================================
    // TEST 4: Property Queries
    // ========================================================================
    std::cout << "\n=== TEST 4: Property Queries ===\n";

    auto vehiclesNode = std::make_unique<JsonDataNode>("vehicles", nlohmann::json::object());

    // Create vehicles with different armor values
    auto tank1 = std::make_unique<JsonDataNode>("tank1", nlohmann::json{{"armor", 150}});
    auto tank2 = std::make_unique<JsonDataNode>("tank2", nlohmann::json{{"armor", 180}});
    auto scout = std::make_unique<JsonDataNode>("scout", nlohmann::json{{"armor", 50}});

    vehiclesNode->setChild("tank1", std::move(tank1));
    vehiclesNode->setChild("tank2", std::move(tank2));
    vehiclesNode->setChild("scout", std::move(scout));

    // Query: armor > 100
    auto armoredVehicles = vehiclesNode->queryByProperty("armor",
        [](const IDataValue& val) {
            return val.isNumber() && val.asInt() > 100;
        });

    std::cout << "  Vehicles with armor > 100: " << armoredVehicles.size() << "\n";
    for (const auto& node : armoredVehicles) {
        int armor = node->getInt("armor");
        std::cout << "    - " << node->getName() << " (armor=" << armor << ")\n";
        ASSERT_TRUE(armor > 100, "All queried vehicles should have armor > 100");
    }
    ASSERT_EQ(armoredVehicles.size(), 2, "Should find 2 armored vehicles");

    reporter.addAssertion("property_queries", true);
    std::cout << "✓ TEST 4 PASSED\n";

    // ========================================================================
    // TEST 5: Pattern Matching
    // ========================================================================
    std::cout << "\n=== TEST 5: Pattern Matching ===\n";

    auto unitsNode = std::make_unique<JsonDataNode>("units", nlohmann::json::object());

    auto heavy_mk1 = std::make_unique<JsonDataNode>("heavy_mk1", nlohmann::json{{"type", "tank"}});
    auto heavy_mk2 = std::make_unique<JsonDataNode>("heavy_mk2", nlohmann::json{{"type", "tank"}});
    auto heavy_trooper = std::make_unique<JsonDataNode>("heavy_trooper", nlohmann::json{{"type", "infantry"}});
    auto light_scout = std::make_unique<JsonDataNode>("light_scout", nlohmann::json{{"type", "vehicle"}});

    unitsNode->setChild("heavy_mk1", std::move(heavy_mk1));
    unitsNode->setChild("heavy_mk2", std::move(heavy_mk2));
    unitsNode->setChild("heavy_trooper", std::move(heavy_trooper));
    unitsNode->setChild("light_scout", std::move(light_scout));

    // Pattern: *heavy*
    auto heavyUnits = unitsNode->getChildrenByNameMatch("*heavy*");
    std::cout << "  Pattern '*heavy*' matched: " << heavyUnits.size() << " units\n";
    for (const auto& node : heavyUnits) {
        std::cout << "    - " << node->getName() << "\n";
    }
    ASSERT_EQ(heavyUnits.size(), 3, "Should match 3 'heavy' units");
    reporter.addMetric("pattern_heavy_count", heavyUnits.size());

    // Pattern: *_mk*
    auto mkUnits = unitsNode->getChildrenByNameMatch("*_mk*");
    std::cout << "  Pattern '*_mk*' matched: " << mkUnits.size() << " units\n";
    ASSERT_EQ(mkUnits.size(), 2, "Should match 2 '_mk' units");

    reporter.addAssertion("pattern_matching", true);
    std::cout << "✓ TEST 5 PASSED\n";

    // ========================================================================
    // TEST 6: Defaults
    // ========================================================================
    std::cout << "\n=== TEST 6: Type Defaults ===\n";

    auto configNode = std::make_unique<JsonDataNode>("config", nlohmann::json{
        {"existing", 42}
    });

    // Test defaults for missing properties
    int missing = configNode->getInt("missing", 100);
    ASSERT_EQ(missing, 100, "getInt with default should return default");

    std::string missingStr = configNode->getString("missing", "default");
    ASSERT_EQ(missingStr, "default", "getString with default should return default");

    bool missingBool = configNode->getBool("missing", true);
    ASSERT_EQ(missingBool, true, "getBool with default should return default");

    reporter.addAssertion("type_defaults", true);
    std::cout << "✓ TEST 6 PASSED\n";

    // ========================================================================
    // CLEANUP
    // ========================================================================
    std::filesystem::remove_all("test_data");

    // ========================================================================
    // RAPPORT FINAL
    // ========================================================================

    metrics.printReport();
    reporter.printFinalReport();

    return reporter.getExitCode();
}
