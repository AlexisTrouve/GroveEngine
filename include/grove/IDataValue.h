#pragma once

#include <string>
#include <vector>
#include <memory>

namespace warfactory {

/**
 * @brief Interface for data values - abstracts underlying data format
 *
 * This allows IDataNode to work with values without exposing JSON directly.
 * Concrete implementations can use JSON, MessagePack, or any other format.
 */
class IDataValue {
public:
    virtual ~IDataValue() = default;

    // Type checking
    virtual bool isNull() const = 0;
    virtual bool isBool() const = 0;
    virtual bool isNumber() const = 0;
    virtual bool isString() const = 0;
    virtual bool isArray() const = 0;
    virtual bool isObject() const = 0;

    // Value access with defaults
    virtual bool asBool(bool defaultValue = false) const = 0;
    virtual int asInt(int defaultValue = 0) const = 0;
    virtual double asDouble(double defaultValue = 0.0) const = 0;
    virtual std::string asString(const std::string& defaultValue = "") const = 0;

    // Array/Object access
    virtual size_t size() const = 0;
    virtual std::unique_ptr<IDataValue> get(size_t index) const = 0;
    virtual std::unique_ptr<IDataValue> get(const std::string& key) const = 0;
    virtual bool has(const std::string& key) const = 0;

    // Serialization
    virtual std::string toString() const = 0;
};

} // namespace warfactory
