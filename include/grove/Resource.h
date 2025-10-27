#pragma once

#include <string>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace warfactory {

class Resource {
private:
    std::string resource_id;
    std::string name;
    std::string category;
    std::string logistic_category;
    float density;
    int stack_size;
    std::string container_type;
    json ui_data;

public:
    Resource() = default;
    Resource(const json& resource_data);

    const std::string& getResourceId() const { return resource_id; }
    const std::string& getName() const { return name; }
    const std::string& getCategory() const { return category; }
    const std::string& getLogisticCategory() const { return logistic_category; }
    float getDensity() const { return density; }
    int getStackSize() const { return stack_size; }
    const std::string& getContainerType() const { return container_type; }
    const json& getUIData() const { return ui_data; }

    static Resource loadFromJson(const std::string& resource_id, const json& resource_data);
};

} // namespace warfactory