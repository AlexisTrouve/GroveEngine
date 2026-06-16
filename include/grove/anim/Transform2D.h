#pragma once

/**
 * grove::anim — 2D transform composition + node hierarchy (animation system, slice 1).
 *
 * WHAT  : A 2D TRS transform (translation / rotation / scale) and a small parent/child
 *         Hierarchy. update() recomputes each node's WORLD transform from its LOCAL transform
 *         and its parent's world transform — the foundation for "linked objects" (child
 *         sprites that move with a parent: a hull with turrets, a body with limbs).
 *
 * WHY   : Engine sprites are flat and independent. Animation-by-movement needs child transforms
 *         expressed relative to a parent and composed to world space. Keeping this pure and
 *         header-only (like grove::camera / grove::input) makes it consumable by a static-link
 *         host and fully testable headless — no GPU. Clips, players and blending (slices 2-4)
 *         drive the LOCAL transforms; this layer turns them into world transforms the game can
 *         publish as (retained) sprites.
 *
 * HOW   : Composition applies, in order, scale → rotation → translation (standard 2D TRS).
 *         compose(parent, local) places the child origin via parent.transformPoint(local.x,
 *         local.y), adds rotations and multiplies scales. Nodes live in an array with a parent
 *         index; a node's parent is always added BEFORE it, so update() is one forward pass.
 *
 * NOTE  : Non-uniform scale combined with rotation does not form an exact affine TRS when
 *         composed (shear is dropped). This is the conventional, good-enough sprite-hierarchy
 *         approximation; if true shear is ever needed, switch to full 2x3 matrices.
 */

#include <cmath>
#include <cstddef>
#include <vector>

namespace grove {
namespace anim {

// A 2D transform: translation (x,y), rotation (radians, CCW), non-uniform scale.
struct Transform2D {
    float x = 0.0f;
    float y = 0.0f;
    float rotation = 0.0f;   // radians
    float scaleX = 1.0f;
    float scaleY = 1.0f;
};

// Map a point from this transform's LOCAL space to the space the transform lives in:
// scale, then rotate, then translate.
inline void transformPoint(const Transform2D& t, float localX, float localY,
                           float& outX, float& outY) {
    const float sx = localX * t.scaleX;
    const float sy = localY * t.scaleY;
    const float c = std::cos(t.rotation);
    const float s = std::sin(t.rotation);
    outX = t.x + (sx * c - sy * s);
    outY = t.y + (sx * s + sy * c);
}

// Compose a child's LOCAL transform under a PARENT (world) transform -> child world transform.
// Origin is placed by the parent; rotations add; scales multiply.
inline Transform2D compose(const Transform2D& parent, const Transform2D& local) {
    Transform2D w;
    transformPoint(parent, local.x, local.y, w.x, w.y);
    w.rotation = parent.rotation + local.rotation;
    w.scaleX = parent.scaleX * local.scaleX;
    w.scaleY = parent.scaleY * local.scaleY;
    return w;
}

// A flat tree of transform nodes. Parent index < node index (parents added first), so world
// transforms are resolved in one forward pass.
class Hierarchy {
public:
    // Add a node with an optional parent (-1 = root). Returns the new node id (its index).
    // The parent must already exist (added earlier) to keep the forward-pass invariant.
    int addNode(int parent = -1, const Transform2D& local = Transform2D{}) {
        const int id = static_cast<int>(m_local.size());
        m_parent.push_back(parent);
        m_local.push_back(local);
        m_world.push_back(local);
        return id;
    }

    // Recompute every node's world transform from locals + parents. Call after editing locals.
    void update() {
        for (size_t i = 0; i < m_local.size(); ++i) {
            const int p = m_parent[i];
            m_world[i] = (p < 0) ? m_local[i] : compose(m_world[p], m_local[i]);
        }
    }

    // Mutable local transform (edit, then update()).
    Transform2D& local(int id) { return m_local[static_cast<size_t>(id)]; }
    const Transform2D& local(int id) const { return m_local[static_cast<size_t>(id)]; }

    // World transform computed by the last update().
    const Transform2D& world(int id) const { return m_world[static_cast<size_t>(id)]; }

    int parent(int id) const { return m_parent[static_cast<size_t>(id)]; }
    size_t size() const { return m_local.size(); }

private:
    std::vector<int> m_parent;          // parent index per node (-1 = root)
    std::vector<Transform2D> m_local;   // local transforms (driven by animation)
    std::vector<Transform2D> m_world;   // world transforms (output of update())
};

} // namespace anim
} // namespace grove
