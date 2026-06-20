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
#include <grove/snap.h>   // generic directional detent snap (zoom snaps to framings)

#include <string>
#include <unordered_map>
#include <vector>

namespace grove {
namespace camera {

class ZoneNavigator {
public:
    // Viewport + feel. margin = framing padding (fraction); magnetRate = glide snappiness;
    // panMargin = how far you may pan PAST a zone's edge, as a fraction of the visible size (so a
    // slice of the screen can sit outside a POI for context). 0 = strict hard clamp.
    // leadSeconds = velocity LEAD: when the active zone moves, look this many seconds AHEAD of it
    // (anticipate motion) so a fast entity isn't dragged to the screen edge by the magnet lag. 0 = off.
    // MERDIER FLAG: this is now 9 positional params — past the readable limit. A `Config` struct
    // (named fields + defaults) is the right refactor; deferred to avoid rippling the showcase + tests
    // mid-feature. Surgical for now.
    void configure(float viewportW, float viewportH, float margin = 0.05f, float magnetRate = 6.0f,
                   float panMargin = 0.25f, float maxDetail = 3.0f, float snapStrength = 8.0f,
                   float snapRange = 0.7f, float leadSeconds = 0.0f) {
        m_vpW = viewportW; m_vpH = viewportH; m_margin = margin; m_magnetRate = magnetRate;
        m_panMarginFrac = panMargin; m_maxDetailFactor = maxDetail;
        m_snapStrength = snapStrength; m_snapRange = snapRange; m_leadSeconds = leadSeconds;
        recomputeZoomLimits();
    }

    // Sync a zone from the game. An empty parentId marks the ROOT. Bounds are world units. Idempotent:
    // re-adding an existing id just updates its bounds (a zone that MOVED/RESIZED) — children + parent
    // link are kept, and the camera stays coherent (seamless, since target() re-clamps each frame).
    void addZone(const std::string& id, const std::string& parentId, const WorldBounds& bounds) {
        auto it = m_zones.find(id);
        if (it != m_zones.end()) {
            if (id == m_activeId) {
                // LOCK onto a moving active zone: ride its centre delta so the camera FOLLOWS the
                // entity (not just edge-clamps when it slides under us). The game updates the bounds
                // each frame; we shift the focus by the same translation before re-clamping.
                const WorldBounds& ob = it->second.bounds;    // old bounds (still current here)
                m_focusX += (bounds.minX + bounds.maxX) * 0.5f - (ob.minX + ob.maxX) * 0.5f;
                m_focusY += (bounds.minY + bounds.maxY) * 0.5f - (ob.minY + ob.maxY) * 0.5f;
            }
            it->second.bounds = bounds;                       // moved/resized
            recomputeZoomLimits();
            clampFocus();
            return;
        }
        Zone z; z.id = id; z.parentId = parentId; z.bounds = bounds;
        m_zones[id] = z;
        if (parentId.empty()) m_rootId = id;
        else if (m_zones.count(parentId)) m_zones[parentId].childIds.push_back(id);
        recomputeZoomLimits();
    }

    // Remove a zone and its whole subtree. If the active zone vanishes, BACK OUT to the nearest
    // still-alive ancestor (parent; if it's gone too, grandparent; …) — eased by update(), so the
    // retreat is seamless (slice 3, the special case Alexi flagged).
    void removeZone(const std::string& id) {
        if (!m_zones.count(id)) return;

        // Capture the ACTIVE zone's ancestor chain BEFORE erasing (parent links are lost after).
        std::vector<std::string> activeAncestors;
        if (m_zones.count(m_activeId)) {
            std::string p = m_zones[m_activeId].parentId;
            while (!p.empty() && m_zones.count(p)) { activeAncestors.push_back(p); p = m_zones[p].parentId; }
        }

        std::vector<std::string> doomed;
        collectSubtree(id, doomed);
        const bool activeDoomed = vecHas(doomed, m_activeId);

        const std::string deletedParent = m_zones[id].parentId;
        if (m_zones.count(deletedParent)) removeChild(deletedParent, id);
        for (const std::string& d : doomed) m_zones.erase(d);
        if (vecHas(doomed, m_rootId)) m_rootId.clear();
        recomputeZoomLimits();

        if (activeDoomed) {
            std::string anc;
            for (const std::string& a : activeAncestors) if (m_zones.count(a)) { anc = a; break; }
            if (!anc.empty()) setActive(anc);                 // eased glide out (seamless back-out)
            else { m_activeId.clear(); m_view = CameraView{}; } // whole tree gone
        }
    }

    // Snap the view to frame the root. Call once after the tree is built.
    void reset() {
        if (m_rootId.empty()) return;
        m_activeId = m_rootId;
        recomputeZoomLimits();
        m_zoom = m_minZoom;
        recenterOnActive();
        clampFocus();
        m_view = target();   // initial frame is a snap, not a glide
    }

    // --- input ---

    // Zoom toward the viewport CENTER (back-compat; keyboard zoom without a cursor).
    void zoomBy(float factor) { zoomAtAnchor(factor, m_vpW * 0.5f, m_vpH * 0.5f); }

    // Zoom toward a SCREEN anchor (the cursor): the world point under it stays PINNED — this is what
    // makes "zoom into the thing under the mouse" work (without it the nested-zones idea falls apart).
    // Entering/leaving a zone follows the focus, which migrates toward the cursor as you zoom in, so
    // there is no hard re-center to yank the view off the cursor.
    void zoomBy(float factor, float anchorScreenX, float anchorScreenY) {
        zoomAtAnchor(factor, anchorScreenX, anchorScreenY);
    }

    // Pan by an ON-SCREEN delta, in the CAMERA frame: rotate the delta by the camera roll (R^-1) so
    // "pan right" follows what you SEE (not world +x), scaled by 1/zoom, then clamped to the zone.
    void panScreen(float dxScreen, float dyScreen) {
        if (m_activeId.empty()) return;
        const float z = (m_zoom != 0.0f) ? m_zoom : 1.0f;
        const float cs = std::cos(m_rotation), sn = std::sin(m_rotation);   // R^-1 = [[c,s],[-s,c]]
        m_focusX += (cs * dxScreen + sn * dyScreen) / z;
        m_focusY += (-sn * dxScreen + cs * dyScreen) / z;
        clampFocus();   // pan can't leave the active zone
    }

    // Camera roll (radians). rotateBy accumulates (e.g. Q/E held); setRotation jumps (e.g. snap to an
    // entity heading). The navigator OWNS rotation so pan + cursor-zoom stay in the camera frame and
    // update() outputs it for render:camera.
    void rotateBy(float dRadians) { m_rotation += dRadians; }
    void setRotation(float radians) { m_rotation = radians; }
    float rotation() const { return m_rotation; }

    // Explicitly enter a zone (frame it). Eased by update().
    void setActive(const std::string& id) {
        auto it = m_zones.find(id);
        if (it == m_zones.end()) return;
        m_activeId = id;
        recomputeZoomLimits();   // this layer's bounds
        m_zoom = clampZoom(framingZoom(it->second.bounds), m_minZoom, m_maxZoom);
        recenterOnActive();
        clampFocus();
    }

    // --- output ---

    // The un-damped magnet target (deterministic): what the live view eases toward. The velocity LEAD
    // (m_lead*, computed in update) offsets the focus so the camera looks AHEAD of a moving zone; it's
    // bounded by both the per-axis cap (updateLead) and the zone clamp below, so it never escapes.
    CameraView target() const {
        CameraView c = centerOn(m_focusX + m_leadX, m_focusY + m_leadY, m_zoom, m_vpW, m_vpH);
        auto it = m_zones.find(m_activeId);
        if (it != m_zones.end()) {
            const float z = (m_zoom != 0.0f) ? m_zoom : 1.0f;
            clampPanToBounds(c, it->second.bounds, m_panMarginFrac * (m_vpW / z), m_panMarginFrac * (m_vpH / z));
        }
        c.rotation = m_rotation;   // NOTE: the clamp above is still axis-aligned (rotated-rect clamp is a later slice)
        return c;
    }

    // Ease the view toward the target (soft magnet + seamless) and return it for render:camera.
    CameraView update(float dt) {
        updateLead(dt);   // velocity lead: estimate the active zone's motion and look ahead of it
        // Zoom SNAP (anti semi-zoom): when NOT actively zooming, ease the zoom toward the nearest
        // framing "detent" — the levels you can settle on from here (the active zone + its ancestors,
        // plus the child under the cursor = the next level you can enter). A fling-zoom therefore
        // auto-completes to FRAME the object instead of stopping half-way. snapStrength 0 disables it.
        // Snap ONLY on zoom-IN, and ONLY upward (toward the zone you're entering = focus). Zoom-OUT is
        // ALWAYS free — the snap can NEVER zoom you out. Two guards: m_lastZoomDir > 0 (your last move
        // was a zoom-in) and det > m_zoom (the detent is above you). Far from a framing it's free; the
        // last stretch toward the zone you're entering gets completed.
        if (m_snapStrength > 0.0f && !m_zoomActive && m_lastZoomDir > 0) {
            std::vector<float> dets;
            collectDetents(dets);
            const float det = snap::directionalDetent(m_zoom, dets.data(), static_cast<int>(dets.size()),
                                                      /*dir up*/+1, m_snapRange, /*logSpace*/true);
            if (det > m_zoom + 1e-4f) {   // only ever zoom IN
                float nz = damp(m_zoom, det, m_snapStrength, dt);
                if (std::fabs(nz - det) < det * 0.005f) nz = det;   // settle exactly on the detent
                applyZoom(nz);
            }
        }
        m_zoomActive = false;

        const CameraView t = target();
        m_view.x    = damp(m_view.x,    t.x,    m_magnetRate, dt);
        m_view.y    = damp(m_view.y,    t.y,    m_magnetRate, dt);
        m_view.zoom = damp(m_view.zoom, t.zoom, m_magnetRate, dt);
        m_view.rotation = t.rotation;   // immediate (input is already smooth; entity-heading snap can damp later)
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

    // Deepest (largest) framing zoom among `id` and all its descendants — i.e. how far you can zoom
    // in from this layer down to the smallest thing reachable below it.
    float maxFramingInSubtree(const std::string& id) const {
        auto it = m_zones.find(id);
        if (it == m_zones.end()) return m_minZoom;
        float m = framingZoom(it->second.bounds);
        for (const std::string& c : it->second.childIds) {
            const float cm = maxFramingInSubtree(c);
            if (cm > m) m = cm;
        }
        return m;
    }

    // PER-LAYER zoom bounds (recomputed when the active zone or the tree changes; the current zoom is
    // re-clamped). min = the root's framing (can't zoom out past the whole world). max = the deepest
    // framing in the ACTIVE zone's SUBTREE * detail factor — so a shallow zone caps low (no zooming it
    // to the void) while a deep zone lets you plunge to its smallest descendant + detail headroom.
    void recomputeZoomLimits() {
        if (m_rootId.empty() || !m_zones.count(m_rootId)) return;
        m_minZoom = framingZoom(m_zones[m_rootId].bounds);
        const std::string base = m_zones.count(m_activeId) ? m_activeId : m_rootId;
        m_maxZoom = maxFramingInSubtree(base) * m_maxDetailFactor;
        if (m_maxZoom < m_minZoom) m_maxZoom = m_minZoom;
        m_zoom = clampZoom(m_zoom, m_minZoom, m_maxZoom);
    }

    // Set the zoom (centre-anchored, focus unchanged) and re-evaluate the active zone + clamp. Used by
    // the idle snap (which eases the zoom toward a detent without moving the focus).
    void applyZoom(float newZoom) {
        m_zoom = clampZoom(newZoom, m_minZoom, m_maxZoom);
        const std::string na = computeActive();
        if (na != m_activeId) { m_activeId = na; recomputeZoomLimits(); }
        clampFocus();
    }

    // Velocity LEAD — anticipate ahead of a moving active zone.
    // WHAT : estimate the active zone's world velocity (smoothed) and set m_lead* = leadSeconds*velocity,
    //        the view offset target() adds to the focus so the camera looks WHERE THE ENTITY IS GOING.
    // WHY  : the soft magnet lags a fast mover — without lead the entity drifts to the leading screen
    //        edge and you can't see ahead. Lead counteracts the lag (and overshoots into anticipation).
    //        Pure position-lock (slice 6) keeps the entity on screen; lead is the polish on top. Off by
    //        default (leadSeconds 0) — it's a feel choice the game opts into.
    // HOW  : 1. sample the active zone's centre each frame; 2. instantaneous velocity = Δcentre/dt;
    //        3. smooth it (damp, leadSmoothRate) so a noisy/instant resync doesn't jerk the camera;
    //        4. lead = leadSeconds*velocity, CAPPED per axis to leadMaxFrac of the visible span so the
    //        led-to point can't leave the screen (the zone clamp in target() bounds it further). On an
    //        active-zone change or lead-off, tracking resets and the lead decays to zero.
    void updateLead(float dt) {
        auto it = m_zones.find(m_activeId);
        if (m_leadSeconds <= 0.0f || it == m_zones.end() || dt <= 1e-6f) {
            m_velX = m_velY = m_leadX = m_leadY = 0.0f;   // off / no active zone -> no lead, no tracking
            m_velValid = false;
            return;
        }
        const float cx = (it->second.bounds.minX + it->second.bounds.maxX) * 0.5f;
        const float cy = (it->second.bounds.minY + it->second.bounds.maxY) * 0.5f;
        if (m_velValid && m_velActiveId == m_activeId) {
            const float instVX = (cx - m_velCentreX) / dt;        // world units / second
            const float instVY = (cy - m_velCentreY) / dt;
            m_velX = damp(m_velX, instVX, kLeadSmoothRate, dt);   // smooth: no jerk on a noisy resync
            m_velY = damp(m_velY, instVY, kLeadSmoothRate, dt);
        } else {
            m_velX = m_velY = 0.0f;                               // active zone changed -> restart clean
        }
        m_velCentreX = cx; m_velCentreY = cy; m_velActiveId = m_activeId; m_velValid = true;

        const float z = (m_zoom != 0.0f) ? m_zoom : 1.0f;
        const float capX = kLeadMaxFrac * (m_vpW / z), capY = kLeadMaxFrac * (m_vpH / z);
        m_leadX = clampf(m_leadSeconds * m_velX, -capX, capX);    // keep the led-to point on screen
        m_leadY = clampf(m_leadSeconds * m_velY, -capY, capY);
    }

    static constexpr float kLeadSmoothRate = 5.0f;   // velocity-estimate smoothing rate (1/s)
    static constexpr float kLeadMaxFrac    = 0.35f;  // max lead as a fraction of the visible span (per axis)

    // The framings reachable from here = the snap detents: the active zone + its ancestors (zoom-OUT
    // targets, BELOW the current zoom) AND the children of the active zone under the focus (zoom-IN
    // targets you're heading into, ABOVE). The directional snap (update) then picks the one in your
    // zoom direction — so zooming in completes IN to frame a child, zooming out settles on a layer.
    void collectDetents(std::vector<float>& out) const {
        if (m_activeId.empty() || !m_zones.count(m_activeId)) return;
        for (std::string id = m_activeId; !id.empty() && m_zones.count(id); id = m_zones.at(id).parentId)
            out.push_back(framingZoom(m_zones.at(id).bounds));               // active + ancestors
        const float z = (m_zoom > 0.0f) ? m_zoom : 1.0f;
        const float mX = m_panMarginFrac * (m_vpW / z), mY = m_panMarginFrac * (m_vpH / z);
        for (const std::string& c : m_zones.at(m_activeId).childIds) {       // child under the cursor
            auto cit = m_zones.find(c);
            if (cit != m_zones.end() && containsM(cit->second.bounds, m_focusX, m_focusY, mX, mY))
                out.push_back(framingZoom(cit->second.bounds));
        }
    }
    static bool contains(const WorldBounds& b, float x, float y) {
        return x >= b.minX && x <= b.maxX && y >= b.minY && y <= b.maxY;
    }
    static bool containsM(const WorldBounds& b, float x, float y, float mx, float my) {
        return x >= b.minX - mx && x <= b.maxX + mx && y >= b.minY - my && y <= b.maxY + my;
    }
    static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
    static bool vecHas(const std::vector<std::string>& v, const std::string& s) {
        for (const std::string& e : v) if (e == s) return true;
        return false;
    }

    // DFS the subtree rooted at `id` (id + all descendants) into `out`.
    void collectSubtree(const std::string& id, std::vector<std::string>& out) const {
        auto it = m_zones.find(id);
        if (it == m_zones.end()) return;
        out.push_back(id);
        for (const std::string& c : it->second.childIds) collectSubtree(c, out);
    }

    // Drop `child` from `parent`'s child list.
    void removeChild(const std::string& parent, const std::string& child) {
        auto it = m_zones.find(parent);
        if (it == m_zones.end()) return;
        std::vector<std::string> kept;
        for (const std::string& c : it->second.childIds) if (c != child) kept.push_back(c);
        it->second.childIds.swap(kept);
    }

    void recenterOnActive() {
        auto it = m_zones.find(m_activeId);
        if (it == m_zones.end()) return;
        m_focusX = (it->second.bounds.minX + it->second.bounds.maxX) * 0.5f;
        m_focusY = (it->second.bounds.minY + it->second.bounds.maxY) * 0.5f;
    }

    // Cursor-anchored zoom: keep the world point under (sx,sy) pinned there as the zoom changes, then
    // re-evaluate the active zone (descend/ascend follows the focus). No re-center -> the cursor stays.
    void zoomAtAnchor(float factor, float sx, float sy) {
        if (m_activeId.empty()) return;
        m_zoomActive = true;   // actively zooming this frame -> the idle snap pauses
        if (factor > 1.0f)      m_lastZoomDir = 1;    // zoom IN  -> snap completes INWARD (focus)
        else if (factor < 1.0f) m_lastZoomDir = -1;   // zoom OUT -> snap settles on a lower layer
        CameraView cur = centerOn(m_focusX, m_focusY, m_zoom, m_vpW, m_vpH);
        cur.rotation = m_rotation;                              // anchor in the CAMERA frame
        float pinX = 0.0f, pinY = 0.0f;
        screenToWorld(cur, sx, sy, pinX, pinY);                 // world point currently under the cursor
        const float nz = clampZoom(m_zoom * factor, m_minZoom, m_maxZoom);
        // Re-frame so pinWorld stays under (sx,sy): focus = pin - R^-1*(anchor - centre)/nz.
        const float cs = std::cos(m_rotation), sn = std::sin(m_rotation);
        const float ax = sx - m_vpW * 0.5f, ay = sy - m_vpH * 0.5f;
        m_focusX = pinX - (cs * ax + sn * ay) / nz;
        m_focusY = pinY - (-sn * ax + cs * ay) / nz;
        m_zoom = nz;
        const std::string na = computeActive();
        if (na != m_activeId) { m_activeId = na; recomputeZoomLimits(); }  // new layer -> new per-layer bounds
        clampFocus();
    }

    // Keep the focus (view center) inside the active zone EXPANDED by the pan margin (so a slice of
    // the screen may sit outside the POI). Center it if the expanded zone is smaller than the view.
    void clampFocus() {
        auto it = m_zones.find(m_activeId);
        if (it == m_zones.end()) return;
        const WorldBounds& b = it->second.bounds;
        const float z = (m_zoom != 0.0f) ? m_zoom : 1.0f;
        const float visW = m_vpW / z, visH = m_vpH / z;
        const float mX = m_panMarginFrac * visW, mY = m_panMarginFrac * visH;
        const float minX = b.minX - mX, maxX = b.maxX + mX, minY = b.minY - mY, maxY = b.maxY + mY;
        const float loX = minX + visW * 0.5f, hiX = maxX - visW * 0.5f;
        const float loY = minY + visH * 0.5f, hiY = maxY - visH * 0.5f;
        m_focusX = (loX > hiX) ? (minX + maxX) * 0.5f : clampf(m_focusX, loX, hiX);
        m_focusY = (loY > hiY) ? (minY + maxY) * 0.5f : clampf(m_focusY, loY, hiY);
    }

    // Deepest zone containing the focus whose framing zoom <= current zoom (descend from the root).
    std::string computeActive() const {
        if (m_rootId.empty()) return std::string();
        const float z = (m_zoom != 0.0f) ? m_zoom : 1.0f;
        const float mX = m_panMarginFrac * (m_vpW / z), mY = m_panMarginFrac * (m_vpH / z);
        std::string cur = m_rootId;
        for (;;) {
            auto it = m_zones.find(cur);
            std::string next;
            for (const std::string& cid : it->second.childIds) {
                auto cit = m_zones.find(cid);
                if (cit == m_zones.end()) continue;
                // Use the margin-expanded bounds so a focus parked in the overshoot margin still
                // counts as "in the zone" (otherwise zooming there would pop back out to the parent).
                if (containsM(cit->second.bounds, m_focusX, m_focusY, mX, mY) &&
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
    float m_panMarginFrac = 0.25f;   // pan overshoot past a zone edge, as a fraction of the visible size
    float m_maxDetailFactor = 3.0f;  // max zoom-in past the deepest zone's framing (anti-void)
    float m_snapStrength = 8.0f;     // idle zoom-snap rate toward a framing detent (0 = off)
    float m_snapRange = 0.7f;        // only snap within this log-zoom distance of a detent (free beyond)
    bool  m_zoomActive = false;      // a zoomBy happened this frame -> pause the snap
    int   m_lastZoomDir = 0;         // +1/-1: direction of the last zoomBy (drives the directional snap)
    // Velocity LEAD (anticipate ahead of a moving active zone). leadSeconds = how far ahead to look;
    // m_vel* = the smoothed world velocity of the active zone's centre; m_lead* = the resulting view
    // offset applied in target(); the m_velCentre*/m_velActiveId/m_velValid triplet samples the centre
    // frame-to-frame to estimate that velocity.
    float m_leadSeconds = 0.0f;      // 0 = off (opt-in: pure position-lock stays the default behaviour)
    float m_velX = 0.0f, m_velY = 0.0f;          // smoothed active-zone velocity (world units / second)
    float m_leadX = 0.0f, m_leadY = 0.0f;        // computed lead offset (world), added to focus in target()
    float m_velCentreX = 0.0f, m_velCentreY = 0.0f;   // last sampled active-zone centre
    std::string m_velActiveId;       // which zone the velocity sample belongs to (reset on active change)
    bool  m_velValid = false;        // do we have a previous centre sample to diff against?
    float m_zoom = 1.0f, m_minZoom = 0.001f, m_maxZoom = 1.0e4f;
    float m_focusX = 0.0f, m_focusY = 0.0f;
    float m_rotation = 0.0f;   // camera roll (radians); pan/zoom honour it, update() outputs it
    CameraView m_view;
};

} // namespace camera
} // namespace grove
