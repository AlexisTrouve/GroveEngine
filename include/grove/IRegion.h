#pragma once

#include <string>

namespace grove {

/**
 * @brief Interface for geological regions during world generation
 *
 * Represents discrete regions with specific properties like resource deposits,
 * volcanic activity, or tectonic formations.
 */
class IRegion {
public:
    virtual ~IRegion() = default;

    // ========================================
    // IDENTIFICATION
    // ========================================

    virtual int getId() const = 0;
    virtual const std::string& getType() const = 0;

    // ========================================
    // POSITION & SIZE
    // ========================================

    virtual float getX() const = 0;
    virtual float getY() const = 0;
    virtual float getRadius() const = 0;
    virtual float getMass() const = 0;

    // ========================================
    // LIFECYCLE
    // ========================================

    virtual void update(float delta_time) = 0;
    virtual bool isActive() const = 0;

    // ========================================
    // PROPERTIES
    // ========================================

    virtual float getIntensity() const = 0;
    virtual void setIntensity(float intensity) = 0;

    virtual bool canFuseWith(const IRegion* other) const = 0;
};

} // namespace grove