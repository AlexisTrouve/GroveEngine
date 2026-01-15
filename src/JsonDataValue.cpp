#include "grove/JsonDataValue.h"

namespace grove {

JsonDataValue::JsonDataValue(const json& value) : m_value(value) {}

JsonDataValue::JsonDataValue(json&& value) : m_value(std::move(value)) {}

// Type checking
bool JsonDataValue::isNull() const {
    return m_value.is_null();
}

bool JsonDataValue::isBool() const {
    return m_value.is_boolean();
}

bool JsonDataValue::isNumber() const {
    return m_value.is_number();
}

bool JsonDataValue::isString() const {
    return m_value.is_string();
}

bool JsonDataValue::isArray() const {
    return m_value.is_array();
}

bool JsonDataValue::isObject() const {
    return m_value.is_object();
}

// Value access with defaults
bool JsonDataValue::asBool(bool defaultValue) const {
    if (!m_value.is_boolean()) {
        return defaultValue;
    }
    return m_value.get<bool>();
}

int JsonDataValue::asInt(int defaultValue) const {
    if (!m_value.is_number()) {
        return defaultValue;
    }
    return m_value.get<int>();
}

double JsonDataValue::asDouble(double defaultValue) const {
    if (!m_value.is_number()) {
        return defaultValue;
    }
    return m_value.get<double>();
}

std::string JsonDataValue::asString(const std::string& defaultValue) const {
    if (!m_value.is_string()) {
        return defaultValue;
    }
    return m_value.get<std::string>();
}

// Array/Object access
size_t JsonDataValue::size() const {
    if (m_value.is_array() || m_value.is_object()) {
        return m_value.size();
    }
    return 0;
}

std::unique_ptr<IDataValue> JsonDataValue::get(size_t index) const {
    if (!m_value.is_array() || index >= m_value.size()) {
        return std::make_unique<JsonDataValue>(json(nullptr));
    }
    return std::make_unique<JsonDataValue>(m_value[index]);
}

std::unique_ptr<IDataValue> JsonDataValue::get(const std::string& key) const {
    if (!m_value.is_object() || !m_value.contains(key)) {
        return std::make_unique<JsonDataValue>(json(nullptr));
    }
    return std::make_unique<JsonDataValue>(m_value[key]);
}

bool JsonDataValue::has(const std::string& key) const {
    if (!m_value.is_object()) {
        return false;
    }
    return m_value.contains(key);
}

// Serialization
std::string JsonDataValue::toString() const {
    return m_value.dump();
}

} // namespace grove
