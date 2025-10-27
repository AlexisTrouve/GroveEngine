#pragma once

#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace warfactory {

class ISerializable {
public:
    virtual ~ISerializable() = default;

    virtual json serialize() const = 0;
    virtual void deserialize(const json& data) = 0;
};

} // namespace warfactory