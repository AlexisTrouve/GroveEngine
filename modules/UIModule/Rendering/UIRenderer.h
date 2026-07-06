#pragma once

#include <grove/IIO.h>
#include <string>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace grove {

// Render entry types
enum class RenderEntryType {
    Rect,
    Sprite,
    Text
};

// Cached state for a render entry (to detect changes)
struct RenderEntry {
    // Every field default-initialized: a RenderEntry can be copied (change-detection cache) before all
    // fields are set on a given path, and reading an indeterminate member is UB. Defaults make a
    // partially-built entry well-defined (caught by clang-analyzer core.uninitialized.Assign).
    RenderEntryType type{};
    float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
    uint32_t color = 0;
    int textureId = 0;
    // Streamed asset id (phase: asset-in-UI). Non-empty -> the sprite is published with an `asset` string
    // (the renderer resolves it to a texture + atlas UV via the AssetManager) instead of a numeric textureId.
    std::string assetId;
    int layer = 0;
    std::string text;
    float fontSize = 0.0f;
    // Clip rect {x,y,w,h} in screen px applied to this entry (w<=0 = none). Captured from the clip
    // stack at publish time so a container (scroll panel, window) can clip its descendants.
    float clipX = 0.0f, clipY = 0.0f, clipW = 0.0f, clipH = 0.0f;
    // Sprite-sheet UV rect (animated flipbook). Défaut = texture entière (0,0)-(1,1) : tout appelant
    // non-flipbook garde ces défauts, donc sa sortie est byte-identique. Change-detected pour qu'un
    // simple changement d'UV (avance d'une cellule, position/texture inchangées) republie quand même.
    float u0 = 0.0f, v0 = 0.0f, u1 = 1.0f, v1 = 1.0f;
};

/**
 * @brief Renders UI elements by publishing to IIO topics
 *
 * UIRenderer supports two modes:
 * - Immediate mode (legacy): drawRect/drawText/drawSprite publish every call
 * - Retained mode (new): updateRect/updateText/updateSprite only publish on change
 *
 * Retained mode dramatically reduces IIO message traffic for static UIs.
 */
class UIRenderer {
public:
    explicit UIRenderer(IIO* io);
    ~UIRenderer() = default;

    /**
     * @brief Draw a filled rectangle
     * @param x X position
     * @param y Y position
     * @param w Width
     * @param h Height
     * @param color RGBA color (0xRRGGBBAA)
     */
    void drawRect(float x, float y, float w, float h, uint32_t color);

    /**
     * @brief Draw text
     * @param x X position
     * @param y Y position
     * @param text Text string
     * @param fontSize Font size
     * @param color RGBA color
     */
    void drawText(float x, float y, const std::string& text, float fontSize, uint32_t color);

    /**
     * @brief Draw a textured sprite
     * @param x X position
     * @param y Y position
     * @param w Width
     * @param h Height
     * @param textureId Texture ID
     * @param color Tint color
     */
    void drawSprite(float x, float y, float w, float h, int textureId, uint32_t color = 0xFFFFFFFF);

    /**
     * @brief Draw a filled ring-sector / pie wedge (render:sector). IMMEDIATE (re-published each frame
     *        — used for the radial wheel's wedges, which change with the hover). (cx,cy) = centre,
     *        r0/r1 = inner/outer radius, a0/a1 = angles (radians). Same view/layer space as the rest of
     *        the UI (drawn on the world view, layer 1000+).
     */
    void drawSector(float cx, float cy, float r0, float r1, float a0, float a1, uint32_t color, int layer);

    /**
     * @brief Set the base layer for UI rendering
     * UI elements should render above game sprites (layer 1000+)
     */
    void setBaseLayer(int layer) { m_baseLayer = layer; }

    /**
     * @brief Get current layer and increment
     */
    int nextLayer() { return m_baseLayer + m_layerOffset++; }

    /**
     * @brief Reset layer offset for new frame
     */
    void beginFrame() { m_layerOffset = 0; m_clipStack.clear(); }

    // ========================================================================
    // Clip stack — a container pushes its content rect before rendering its children, so every
    // render entry published while it's on the stack carries that clip (renderer scissors to it).
    // QUOI : pile de rects de clip ; les entrées publiées prennent le sommet (ou aucun si vide).
    // POURQUOI : le clipping est porté par chaque entrée (retained), pas par un état global — un
    //   parent qui clippe (ScrollPanel, Window) entoure son rendu d'enfants par push/pop, et les
    //   widgets n'ont rien à savoir du clipping. COMMENT : currentClip() = sommet, défaut {0,0,0,0}.
    // ========================================================================
    void pushClip(float x, float y, float w, float h) { m_clipStack.push_back({x, y, w, h}); }
    void popClip() { if (!m_clipStack.empty()) m_clipStack.pop_back(); }

    // ========================================================================
    // Retained Mode API
    // ========================================================================

    /**
     * @brief Register a new render entry and get its ID
     * @return Unique render ID for this entry
     */
    uint32_t registerEntry();

    /**
     * @brief Unregister a render entry (widget destroyed)
     * @param renderId ID to remove
     */
    void unregisterEntry(uint32_t renderId);

    /**
     * @brief Update a rectangle (only publishes if changed)
     * @return true if published (changed), false if skipped (unchanged)
     */
    bool updateRect(uint32_t renderId, float x, float y, float w, float h, uint32_t color, int layer);

    /**
     * @brief Update text (only publishes if changed)
     * @return true if published (changed), false if skipped (unchanged)
     */
    bool updateText(uint32_t renderId, float x, float y, const std::string& text, float fontSize, uint32_t color, int layer);

    /**
     * @brief Update a textured sprite (only publishes if changed)
     * @return true if published (changed), false if skipped (unchanged)
     */
    bool updateSprite(uint32_t renderId, float x, float y, float w, float h, int textureId, uint32_t color, int layer);

    /**
     * @brief Update a sprite by STREAMED ASSET ID (string) instead of a numeric texture id (only publishes
     *        if changed). The published render:sprite carries an `asset` field; the BgfxRenderer resolves it
     *        to a resident texture + atlas UV through the AssetManager (on-demand stream + budget eviction).
     *        This is how a part/icon UI sprite uses the asset system rather than a hardcoded texture id.
     * @return true if published (changed), false if skipped (unchanged)
     */
    bool updateSprite(uint32_t renderId, float x, float y, float w, float h, const std::string& assetId, uint32_t color, int layer);

    /**
     * @brief Update a textured sprite with an explicit UV rect (sprite-sheet cell) — only publishes if
     *        changed. Used by the animated flipbook widget (slice 6a): the UV rect selects which sheet
     *        cell is shown, so a UV change alone (same position/texture/color) counts as a change and
     *        republishes → the animation advances. Numeric texture only (a dedicated sheet texture).
     * @return true if published (changed), false if skipped (unchanged)
     */
    bool updateSpriteUV(uint32_t renderId, float x, float y, float w, float h, int textureId,
                        float u0, float v0, float u1, float v1, uint32_t color, int layer);

    /**
     * @brief Number of currently-registered retained entries (introspection).
     *
     * WHY: lets a test assert an invariant on registration count — e.g. a VIRTUALIZED list registers a
     *      pool bounded by its viewport, NOT by its item count (a 10k-item list must not allocate 10k
     *      entries). Pure read of the retained map; no side effects.
     */
    size_t entryCount() const { return m_entries.size(); }

private:
    IIO* m_io;
    int m_baseLayer = 1000;  // UI renders above game content
    int m_layerOffset = 0;   // Increments per draw call for proper ordering

    // Retained mode state
    uint32_t m_nextRenderId = 1;
    std::unordered_map<uint32_t, RenderEntry> m_entries;

    // Active clip stack (see pushClip/popClip). currentClip() returns the top, or {0,0,0,0} = none.
    struct ClipRect { float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f; };
    std::vector<ClipRect> m_clipStack;
    ClipRect currentClip() const { return m_clipStack.empty() ? ClipRect{} : m_clipStack.back(); }

    // Shared core for both updateSprite overloads (numeric textureId vs streamed assetId). assetId non-empty
    // wins (published as `asset`); otherwise the numeric textureId is published. Change-detects both.
    // UV params appended with (0,0)-(1,1) defaults: les appelants existants (rect, image, asset) ne les
    // passent pas -> sortie inchangée ; seul le flipbook passe un vrai rect de cellule.
    bool updateSpriteImpl(uint32_t renderId, float x, float y, float w, float h, int textureId,
                          const std::string& assetId, uint32_t color, int layer,
                          float u0 = 0.0f, float v0 = 0.0f, float u1 = 1.0f, float v1 = 1.0f);

    // Publish helpers. assetId non-empty -> emit `asset` (renderer streams it); else emit numeric textureId.
    void publishSpriteAdd(uint32_t renderId, float x, float y, float w, float h, int textureId, const std::string& assetId, uint32_t color, int layer,
                          float u0 = 0.0f, float v0 = 0.0f, float u1 = 1.0f, float v1 = 1.0f);
    void publishSpriteUpdate(uint32_t renderId, float x, float y, float w, float h, int textureId, const std::string& assetId, uint32_t color, int layer,
                             float u0 = 0.0f, float v0 = 0.0f, float u1 = 1.0f, float v1 = 1.0f);
    void publishSpriteRemove(uint32_t renderId);
    void publishTextAdd(uint32_t renderId, float x, float y, const std::string& text, float fontSize, uint32_t color, int layer);
    void publishTextUpdate(uint32_t renderId, float x, float y, const std::string& text, float fontSize, uint32_t color, int layer);
    void publishTextRemove(uint32_t renderId);
};

} // namespace grove
