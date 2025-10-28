#pragma once

#include "IDataValue.h"
#include <nlohmann/json.hpp>
#include <memory>
#include <string>
#include <vector>

namespace grove {

using json = nlohmann::json;

/**
 * @brief Concrete implementation of IDataValue backed by nlohmann::json
 */
class JsonDataValue : public IDataValue {
public:
    explicit JsonDataValue(const json& value);
    explicit JsonDataValue(json&& value);
    virtual ~JsonDataValue() = default;

    // Type checking
    bool isNull() const override;
    bool isBool() const override;
    bool isNumber() const override;
    bool isString() const override;
    bool isArray() const override;
    bool isObject() const override;

    // Value access with defaults
    bool asBool(bool defaultValue = false) const override;
    int asInt(int defaultValue = 0) const override;
    double asDouble(double defaultValue = 0.0) const override;
    std::string asString(const std::string& defaultValue = "") const override;

    // Array/Object access
    size_t size() const override;
    std::unique_ptr<IDataValue> get(size_t index) const override;
    std::unique_ptr<IDataValue> get(const std::string& key) const override;
    bool has(const std::string& key) const override;

    // Serialization
    std::string toString() const override;

    // Direct JSON access (for internal use)
    const json& getJson() const { return m_value; }
    json& getJson() { return m_value; }

private:
    json m_value;
};

} // namespace grove
