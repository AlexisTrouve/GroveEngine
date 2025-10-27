#pragma once

#include "Resource.h"
#include <vector>
#include <unordered_map>
#include <string>
#include <memory>

namespace warfactory {

/**
 * @brief Singleton registry for all game resources with fast uint32_t ID lookup
 *
 * Centralizes resource management with O(1) access by numeric ID.
 * Resources are loaded once at startup and accessed by ID throughout the game.
 */
class ResourceRegistry {
private:
    static std::unique_ptr<ResourceRegistry> instance;
    static bool initialized;

    std::vector<Resource> resources;                    // Indexed by ID (resources[id])
    std::unordered_map<std::string, uint32_t> name_to_id; // String -> ID mapping (init only)
    uint32_t next_id = 1; // Start at 1 (0 = invalid/null resource)

    ResourceRegistry() = default;

public:
    // Singleton access
    static ResourceRegistry& getInstance();
    static void initialize();
    static void shutdown();

    // ========================================
    // REGISTRATION (Initialization Phase)
    // ========================================

    /**
     * @brief Register a resource and get its assigned ID
     * @param resource The resource to register
     * @return The assigned uint32_t ID for this resource
     */
    uint32_t registerResource(const Resource& resource);

    /**
     * @brief Load resources from JSON configuration
     * @param resources_json JSON object containing all resources
     */
    void loadResourcesFromJson(const json& resources_json);

    // ========================================
    // RUNTIME ACCESS (Performance Critical)
    // ========================================

    /**
     * @brief Get resource by ID (O(1) access)
     * @param id The resource ID
     * @return Pointer to resource or nullptr if not found
     */
    const Resource* getResource(uint32_t id) const;

    /**
     * @brief Get resource ID by name (use sparingly - prefer caching IDs)
     * @param name The resource name/identifier
     * @return The resource ID or 0 if not found
     */
    uint32_t getResourceId(const std::string& name) const;

    /**
     * @brief Check if resource ID is valid
     */
    bool isValidResourceId(uint32_t id) const;

    // ========================================
    // BULK OPERATIONS
    // ========================================

    /**
     * @brief Get all registered resource IDs
     */
    std::vector<uint32_t> getAllResourceIds() const;

    /**
     * @brief Get total number of registered resources
     */
    size_t getResourceCount() const;

    /**
     * @brief Clear all registered resources (testing/reset)
     */
    void clear();

    // ========================================
    // CONVENIENCE CONSTANTS
    // ========================================

    static constexpr uint32_t INVALID_RESOURCE_ID = 0;
    static constexpr uint32_t MAX_RESOURCES = 1000000; // 1M resources max

    // Prevent copy/assignment
    ResourceRegistry(const ResourceRegistry&) = delete;
    ResourceRegistry& operator=(const ResourceRegistry&) = delete;
};

// ========================================
// CONVENIENCE MACROS FOR PERFORMANCE
// ========================================

#define RESOURCE_ID(name) warfactory::ResourceRegistry::getInstance().getResourceId(name)
#define GET_RESOURCE(id) warfactory::ResourceRegistry::getInstance().getResource(id)
#define VALID_RESOURCE(id) warfactory::ResourceRegistry::getInstance().isValidResourceId(id)

} // namespace warfactory