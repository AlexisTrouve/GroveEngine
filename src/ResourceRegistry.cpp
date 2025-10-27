#include "warfactory/ResourceRegistry.h"
#include <algorithm>

namespace warfactory {

// Static member initialization
std::unique_ptr<ResourceRegistry> ResourceRegistry::instance = nullptr;
bool ResourceRegistry::initialized = false;

ResourceRegistry& ResourceRegistry::getInstance() {
    if (!initialized) {
        initialize();
    }
    return *instance;
}

void ResourceRegistry::initialize() {
    if (!initialized) {
        instance = std::unique_ptr<ResourceRegistry>(new ResourceRegistry());
        initialized = true;
    }
}

void ResourceRegistry::shutdown() {
    if (initialized) {
        instance.reset();
        initialized = false;
    }
}

// ========================================
// REGISTRATION (Initialization Phase)
// ========================================

uint32_t ResourceRegistry::registerResource(const Resource& resource) {
    if (next_id >= MAX_RESOURCES) {
        // Handle overflow - could throw or return INVALID_RESOURCE_ID
        return INVALID_RESOURCE_ID;
    }

    const uint32_t assigned_id = next_id++;

    // Ensure vector is large enough
    if (resources.size() <= assigned_id) {
        resources.resize(assigned_id + 1);
    }

    // Store resource at index = ID
    resources[assigned_id] = resource;

    // Map name to ID for lookup
    name_to_id[resource.getResourceId()] = assigned_id;

    return assigned_id;
}

void ResourceRegistry::loadResourcesFromJson(const json& resources_json) {
    for (json::const_iterator it = resources_json.begin(); it != resources_json.end(); ++it) {
        const std::string& resource_name = it.key();
        const json& resource_data = it.value();

        // Create resource from JSON
        Resource resource = Resource::loadFromJson(resource_name, resource_data);

        // Register it
        const uint32_t resource_id = registerResource(resource);

        // Log or handle registration result if needed
        (void)resource_id; // Suppress unused variable warning
    }
}

// ========================================
// RUNTIME ACCESS (Performance Critical)
// ========================================

const Resource* ResourceRegistry::getResource(uint32_t id) const {
    if (id == INVALID_RESOURCE_ID || id >= resources.size()) {
        return nullptr;
    }
    return &resources[id];
}

uint32_t ResourceRegistry::getResourceId(const std::string& name) const {
    const std::unordered_map<std::string, uint32_t>::const_iterator it = name_to_id.find(name);
    return (it != name_to_id.end()) ? it->second : INVALID_RESOURCE_ID;
}

bool ResourceRegistry::isValidResourceId(uint32_t id) const {
    return id != INVALID_RESOURCE_ID && id < resources.size();
}

// ========================================
// BULK OPERATIONS
// ========================================

std::vector<uint32_t> ResourceRegistry::getAllResourceIds() const {
    std::vector<uint32_t> ids;
    ids.reserve(next_id - 1); // -1 because we start at 1

    for (uint32_t id = 1; id < next_id; ++id) {
        if (id < resources.size()) {
            ids.push_back(id);
        }
    }

    return ids;
}

size_t ResourceRegistry::getResourceCount() const {
    return next_id - 1; // -1 because we start at 1
}

void ResourceRegistry::clear() {
    resources.clear();
    name_to_id.clear();
    next_id = 1;
}

} // namespace warfactory