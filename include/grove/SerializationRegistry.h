#pragma once

#include <unordered_map>
#include <memory>
#include <string>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace warfactory {

class ASerializable;

class SerializationRegistry {
private:
    std::unordered_map<std::string, ASerializable*> registered_objects;

    SerializationRegistry() = default;

public:
    static SerializationRegistry& getInstance();

    void registerObject(const std::string& instance_id, ASerializable* object);
    void unregisterObject(const std::string& instance_id);

    json serializeAll() const;
    void deserializeAll(const json& data);

    json serializeObject(const std::string& instance_id) const;
    void deserializeObject(const std::string& instance_id, const json& data);

    size_t getRegisteredCount() const { return registered_objects.size(); }
    std::vector<std::string> getRegisteredIds() const;

    void clear();
};

} // namespace warfactory