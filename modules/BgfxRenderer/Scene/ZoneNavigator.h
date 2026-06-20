#pragma once

/**
 * grove::camera::ZoneNavigator — nested-zones camera navigation (slice 2; see
 * docs/design/zone-navigation.md).
 *
 * WHAT : holds a game-synced tree of zones (id, parent, world bounds) + a continuous zoom & focus,
 *        and per frame eases the camera to FRAME the active zone (soft magnet), CLAMPS pan to it, and
 *        scales pan by zoom. Zooming descends into the child zone under the focus / ascends to the
 *        parent. update() returns the CameraView to publish on render:camera.
 *
 * WHY  : the navigation FEEL (enter a thing, stay bounded in it, pan at a scale-appropriate speed,
 *        glide seamlessly) is engine mechanics — built once on the pure Camera.h helpers and tested
 *        headless. The GAME owns the zone hierarchy (what/where) and drives zoom/pan input.
 *
 * HOW  : active zone = the deepest zone containing the focus whose framing zoom <= current zoom. The
 *        magnet TARGET = centerOn(focus, zoom) clamped to the active zone (Camera.h); update() damps
 *        the live view toward it (framerate-independent), so any change — input, setActive, a future
 *        zone deletion (slice 3) — is seamless. Pure logic, header-only, no bgfx.
 */

#include "Camera.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace grove {
namespace camera {

class ZoneNavigator {
public:
    // Viewport + feel. margin = framing padding (fraction); magnetRate = how snappy the glide is.
    void configure(float viewportW, float viewportH, float margin = 0.05f, float magnetRate = 6.0f) {
        m_vpW = viewportW; m_vpH = viewportH; m_margin = margin; m_magnetRate = magnetRate;
        if (!m_rootId.empty()) m_minZoom = framingZoom(m_zones[m_rootId].bounds);
    }

    // Sync a zone from the game. An empty parentId marks the ROOT. Bounds are world units.
    void addZone(const std::string& id, const std::string& parentId, const WorldBounds& bounds) {
        Zone z; z.id = id; z.parentId = parentId; z.bounds = bounds;
        m_zones[id] = z;
        if (parentId.empty()) { m_rootId = id; m_minZoom = framingZoom(bounds); }
        else if (m_zones.count(parentId)) m_zones[parentId].childIds.push_back(id);
    }

    // Snap the view to frame the root. Call once after the tree is built.
    void reset() {
        if (m_rootId.empty()) return;
        m_activeId = m_rootId;
        m_minZoom = framingZoom(m_zones[m_rootId].bounds);
        m_zoom = m_minZoom;
        recenterOnActive();
        clampFocus();
        m_view = target();   // initial frame is a snap, not a glide
    }

    // --- input ---

    // Multiply the zoom (e.g. 1.25 in, 0.8 out). May descend into / ascend out of a zone.
    void zoomBy(float factor) {
        if (m_activeId.empty()) return;
        m_zoom = clampZoom(m_zoom * factor, m_minZoom, m_maxZoom);
        const std::string na = computeActive();
        if (na != m_activeId) { m_activeId = na; recenterOnActive(); }  // soft magnet: re-center on enter
        clampFocus();
    }

    // Pan by an ON-SCREEN delta; scaled by 1/zoom (constant screen feel) then clamped to the zone.
    void panScreen(float dxScreen, float dyScreen) {
        if (m_activeId.empty()) return;
        m_focusX += worldPanForScreen(dxScreen, m_zoom);
        m_focusY += worldPanForScreen(dyScreen, m_zoom);
        clampFocus();   // pan can't leave the active zone
    }

    // Explicitly enter a zone (frame it). Eased by update().
    void setActive(const std::string& id) {
        auto it = m_zones.find(id);
        if (it == m_zones.end()) return;
        m_activeId = id;
        m_zoom = clampZoom(framingZoom(it->second.bounds), m_minZoom, m_maxZoom);
        recenterOnActive();
        clampFocus();
    }

    // --- output ---

    // The un-damped magnet target (deterministic): what the live view eases toward.
    CameraView target() const {
        CameraView c = centerOn(m_focusX, m_focusY, m_zoom, m_vpW, m_vpH);
        auto it = m_zones.find(m_activeId);
        if (it != m_zones.end()) clampPanToBounds(c, it->second.bounds);
        return c;
    }

    // Ease the view toward the target (soft magnet + seamless) and return it for render:camera.
    CameraView update(float dt) {
        const CameraView t = target();
        m_view.x    = damp(m_view.x,    t.x,    m_magnetRate, dt);
        m_view.y    = damp(m_view.y,    t.y,    m_magnetRate, dt);
        m_view.zoom = damp(m_view.zoom, t.zoom, m_magnetRate, dt);
        m_view.viewportW = m_vpW; m_view.viewportH = m_vpH;
        return m_view;
    }

    const std::string& activeZone() const { return m_activeId; }
    CameraView view() const { return m_view; }
    float zoom()   const { return m_zoom; }
    float focusX() const { return m_focusX; }
    float focusY() const { return m_focusY; }
    bool  hasZone(const std::string& id) const { return m_zones.count(id) != 0; }

protected:
    struct Zone { std::string id, parentId; WorldBounds bounds; std::vector<std::string> childIds; };

    float framingZoom(const WorldBounds& b) const { return fitBounds(b, m_vpW, m_vpH, m_margin).zoom; }
    static bool contains(const WorldBounds& b, float x, float y) {
        return x >= b.minX && x <= b.maxX && y >= b.minY && y <= b.maxY;
    }
    static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

    void recenterOnActive() {
        auto it = m_zones.find(m_activeId);
        if (it == m_zones.end()) return;
        m_focusX = (it->second.bounds.minX + it->second.bounds.maxX) * 0.5f;
        m_focusY = (it->second.bounds.minY + it->second.bounds.maxY) * 0.5f;
    }

    // Keep the focus (view center) inside the active zone; center it if the zone is smaller than view.
    void clampFocus() {
        auto it = m_zones.find(m_activeId);
        if (it == m_zones.end()) return;
        const WorldBounds& b = it->second.bounds;
        const float z = (m_zoom != 0.0f) ? m_zoom : 1.0f;
        const float visW = m_vpW / z, visH = m_vpH / z;
        const float zw = b.maxX - b.minX, zh = b.maxY - b.minY;
        m_focusX = (visW >= zw) ? (b.minX + b.maxX) * 0.5f
                                : clampf(m_focusX, b.minX + visW * 0.5f, b.maxX - visW * 0.5f);
        m_focusY = (visH >= zh) ? (b.minY + b.maxY) * 0.5f
                                : clampf(m_focusY, b.minY + visH * 0.5f, b.maxY - visH * 0.5f);
    }

    // Deepest zone containing the focus whose framing zoom <= current zoom (descend from the root).
    std::string computeActive() const {
        if (m_rootId.empty()) return std::string();
        std::string cur = m_rootId;
        for (;;) {
            auto it = m_zones.find(cur);
            std::string next;
            for (const std::string& cid : it->second.childIds) {
                auto cit = m_zones.find(cid);
                if (cit == m_zones.end()) continue;
                if (contains(cit->second.bounds, m_focusX, m_focusY) &&
                    framingZoom(cit->second.bounds) <= m_zoom + 1e-3f) {
                    next = cid; break;   // first matching child (siblings are assumed non-overlapping)
                }
            }
            if (next.empty()) break;
            cur = next;
        }
        return cur;
    }

    std::unordered_map<std::string, Zone> m_zones;
    std::string m_rootId, m_activeId;
    float m_vpW = 1280.0f, m_vpH = 720.0f, m_margin = 0.05f, m_magnetRate = 6.0f;
    float m_zoom = 1.0f, m_minZoom = 0.001f, m_maxZoom = 1.0e4f;
    float m_focusX = 0.0f, m_focusY = 0.0f;
    CameraView m_view;
};

} // namespace camera
} // namespace grove
