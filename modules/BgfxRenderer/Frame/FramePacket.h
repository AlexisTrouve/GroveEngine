#pragma once

#include <cstdint>
#include <cstddef>

namespace grove {

class FrameAllocator;

// ============================================================================
// Sprite Instance Data
// ============================================================================

// GPU Instance data - must match shader layout (5 x vec4 = 80 bytes)
// i_data0: x, y, scaleX, scaleY
// i_data1: rotation, u0, v0, u1
// i_data2: v1, textureId (as float), layer (as float), padding
// i_data3: reserved
// i_data4: r, g, b, a (color as floats 0-1)
struct SpriteInstance {
    // i_data0
    float x, y;           // Position
    float scaleX, scaleY; // Scale
    // i_data1
    float rotation;       // Radians
    float u0, v0, u1;     // UV start + end x
    // i_data2
    float v1;             // UV end y
    float textureId;      // As float for GPU compatibility
    float layer;          // Z-order as float
    // BLEND MODE, read CPU-side by SpritePass: 0 = alpha (the default and the historical
    // behaviour), 1 = ADDITIVE. Uploaded to the GPU but IGNORED by the sprite shader - exactly like
    // reserved[] below, which carries the scissor rect. Reusing this slot rather than growing the
    // instance keeps the stride at 80 bytes, so the bulk path and the shader layout are untouched.
    //
    // Additive exists for glowing STRETCHED quads (engine plumes): render:sprite could stretch and
    // rotate but only in alpha, render:particle was additive but a square billboard - neither could
    // draw the one thing that look needs.
    float padding0;
    // i_data3 — optional CPU-side clip rect {x,y,w,h} in framebuffer pixels (w<=0 = no clip).
    // Uploaded to the GPU but IGNORED by the sprite shader; SpritePass reads it to drive a per-batch
    // bgfx scissor (UI clipping — scroll panels, windows). Not a GPU input, just rides in this slot.
    float reserved[4];
    // i_data4
    float r, g, b, a;     // Color as floats (0-1)
};
static_assert(sizeof(SpriteInstance) == 80, "SpriteInstance must be 80 bytes for GPU instancing");

// ============================================================================
// Tilemap Chunk Data
// ============================================================================

// One tile LAYER within a chunk (Strategy A multi-layer). Each layer is its own tile grid + tileset,
// drawn in order: layer 0 OPAQUE (terrain), layers >0 alpha-blended on top (decals/overlays whose atlas
// tiles use transparency — tile id 0 is fully transparent + discarded). All layers share the chunk's
// geometry (width/height/tileW/H) and fog. The tilemap state is already BlendMode::Alpha, so drawing the
// layers back-to-front composites them with no extra state.
struct TilemapLayer {
    const uint16_t* tiles = nullptr;
    size_t tileCount = 0;
    uint16_t textureId = 0;   // tileset for this layer (0 = procedural color atlas)
};

struct TilemapChunk {
    float x, y;           // Chunk position
    uint16_t width, height;
    uint16_t tileWidth, tileHeight;
    uint16_t textureId;
    const uint16_t* tiles; // Tile indices in tileset
    size_t tileCount;
    // Per-tile fog/visibility (Slice fog): width*height bytes, 0 = hidden (drawn dark) .. 255 =
    // fully visible. nullptr = no fog (everything visible). Sampled mipped, so it dims correctly at
    // every zoom; revealed incrementally via the A4.2 partial-update path.
    const uint8_t* fog = nullptr;
    // Retained-mode identity (Slice A4). id == 0 -> ephemeral chunk (re-sent every frame, always
    // uploaded). id != 0 -> retained chunk (render:tilemap:add/update/remove by this id); the pass
    // caches its index texture by id and uploads only when `dirty` is set (the frame it was
    // added/updated), so a static retained chunk uploads exactly once.
    uint32_t id = 0;
    bool dirty = true;
    // Dirty sub-rectangle for PARTIAL retained updates (Slice A4.2). When dirty && dirtyW>0, only
    // [dirtyX, dirtyY, +dirtyW, +dirtyH] of the index texture is re-uploaded (fog reveal, terrain
    // edits) instead of the whole grid. dirtyW==0 (with dirty) = full-grid upload (add / full update).
    uint16_t dirtyX = 0, dirtyY = 0, dirtyW = 0, dirtyH = 0;
    // FOG-ONLY partial reveal (render:tilemap:fog). Separate from the tile `dirty` so a fog-of-war reveal
    // patches just the R8 visibility mask sub-rect (mip 0, region update) WITHOUT re-uploading tiles or
    // re-baking the LOD colour. The fog texture is non-mipped + mutable so the region update applies (a
    // bgfx texture created WITH data is immutable). fogDirtyW>0 + fogDirty = patch [fogDirtyX..+W, ..+H].
    bool fogDirty = false;
    uint16_t fogDirtyX = 0, fogDirtyY = 0, fogDirtyW = 0, fogDirtyH = 0;
    // Multi-layer (Strategy A): when layerCount>0 the chunk renders these layers (0 opaque, >0 alpha
    // over) instead of just `tiles`. layers[0] mirrors `tiles`/`textureId` (the collector sets both), so
    // the legacy single-layer path + LOD/partial/fog code is unchanged; layers[1..] are the overlays.
    // Retained chunks only (id != 0). layerCount==0 -> legacy single-`tiles` path.
    const TilemapLayer* layers = nullptr;
    size_t layerCount = 0;
};

// ============================================================================
// Text Command Data
// ============================================================================

struct TextCommand {
    float x, y;
    const char* text;     // Null-terminated, allocated in FrameAllocator
    uint16_t fontId;
    uint16_t fontSize;
    uint32_t color;
    uint16_t layer;
    // Horizontal alignment of the text relative to `x` (0 = left → text starts at x; 1 = center → x is the
    // centre; 2 = right → text ENDS at x). Applied per line (each \n-delimited line is measured + offset).
    // Default 0 keeps every existing caller left-aligned (byte-identical). See TextPass.
    uint8_t align = 0;
    // Synthetic bold: when 1, each glyph is drawn twice (a sub-pixel x-offset copy) so the single-weight
    // bitmap font reads bolder. Default 0 = normal weight. Cheap, no extra font atlas.
    uint8_t bold = 0;
    // Optional clip rect {x,y,w,h} in framebuffer pixels (w<=0 = no clip). TextPass breaks the glyph
    // batch on a clip change and applies a bgfx scissor — same UI-clipping mechanism as sprites.
    float clipX = 0.0f, clipY = 0.0f, clipW = 0.0f, clipH = 0.0f;
    // Optional width budget in framebuffer px (<=0 = unlimited, the default). A line wider than this is
    // cut on a whole codepoint and finished with an ellipsis — see TextPass / Text/TextFit.h.
    float maxWidth = 0.0f;
};

// ============================================================================
// Particle Instance Data
// ============================================================================

struct ParticleInstance {
    float x, y;
    float vx, vy;
    float size;
    float life;           // 0-1, remaining time
    uint32_t color;
    uint16_t textureId;
};

// ============================================================================
// Light Data (lighting L2)
// ============================================================================

// One radial light for this frame, in WORLD space.
//
// cx,cy is the CENTRE (the field name carries the anchor — render-anchor-convention.md). The colour
// is already split into 0..1 channels because that is what the shader wants; `intensity` multiplies
// it and is deliberately NOT clamped to 1, since the accumulation target is RGBA16F and the
// overbright is what the bloom pass will feed on.
//
// There is no instance-buffer packing here on purpose: lights are TENS per frame, so one draw each
// costs nothing, and instancing them would add a format to maintain for no measurable gain. Same
// reasoning that ruled out a bulk IIO path.
struct LightCommand {
    float cx, cy;        // world CENTRE
    float radius;        // attenuation reaches exactly 0 here (grove::light::attenuation)
    float r, g, b;       // colour, 0..1
    float intensity;     // scales the colour; >1 allowed
    // CONE (L3), in the grove::fx::Emitter convention: degrees, 0 = +x, 90 = +y (screen-down),
    // 360 = omni. Sharing that convention means a thruster's flame emitter and its light take the
    // same numbers. 360 is the default AND the non-regression — a light written before L3 is a disc.
    float dirDeg;
    float spreadDeg;
};

// ============================================================================
// Occluder Data (lighting W1)
// ============================================================================

// One opaque rectangle that light does not pass through, in WORLD space.
//
// x,y is the top-left CORNER (the field name carries the anchor - render-anchor-convention.md),
// unlike a light's cx,cy centre. Shifting a wall by half its size would read as "the shadows are
// offset" rather than as an anchor mistake, which is exactly why the convention is name-encoded.
//
// There is no transmittance field here on purpose: an occluder IS the opaque case. Coloured partial
// transmission is plan F, and it will add its own primitive rather than overload this one.
struct OccluderCommand {
    float x, y;      // world top-left CORNER
    float w, h;      // extent; a non-positive extent is dropped by the collector
};

// ============================================================================
// Debug Shape Data
// ============================================================================

struct DebugLine {
    float x1, y1, x2, y2;
    uint32_t color;
};

struct DebugRect {
    float x, y, w, h;
    uint32_t color;
    bool filled;
};

// ============================================================================
// Sector (filled ring-sector / pie wedge) — a colored-geometry primitive (render:sector). Drawn by
// SectorPass as triangles (grove::geom::appendSector) with the plain color shader. (cx,cy) = centre;
// r0/r1 = inner/outer radius (r0=0 => a pie slice); a0/a1 = angles (rad, screen y-down). Used by the
// UIRadial action wheel (screen-space, layer 1000+) and reusable for rings/gauges/radars.
// ============================================================================
struct SectorCommand {
    float cx, cy;
    float r0, r1;
    float a0, a1;
    uint32_t color;
    uint16_t layer;
};

// ============================================================================
// Camera/View Info
// ============================================================================

struct ViewInfo {
    float viewMatrix[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};  // Identity matrix
    float projMatrix[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};  // Identity matrix
    float positionX = 0.0f, positionY = 0.0f;
    float zoom = 1.0f;
    float rotation = 0.0f;   // radians; rotates the view around the screen-centre pivot (0 = none)
    uint16_t viewportX = 0, viewportY = 0;
    uint16_t viewportW = 1280, viewportH = 720;
};

// ============================================================================
// Frame Packet - IMMUTABLE after construction
// ============================================================================

struct FramePacket {
    uint64_t frameNumber = 0;
    float deltaTime = 0.016f;
    float elapsedTime = 0.0f;   // seconds since start (accumulated dt) — drives time-based shaders (animated tiles)

    // Collected data (read-only for passes)
    const SpriteInstance* sprites = nullptr;
    size_t spriteCount = 0;

    const TilemapChunk* tilemaps = nullptr;
    size_t tilemapCount = 0;

    const TextCommand* texts = nullptr;
    size_t textCount = 0;

    const ParticleInstance* particles = nullptr;
    size_t particleCount = 0;

    const DebugLine* debugLines = nullptr;
    size_t debugLineCount = 0;

    const DebugRect* debugRects = nullptr;
    size_t debugRectCount = 0;

    // Sectors (filled wedges). World bucket -> view 0 (zooms with the camera); HUD bucket (space:
    // "screen") -> view 1 (fixed). Ephemeral (re-sent each frame, like debug + render:rect).
    const SectorCommand* sectors = nullptr;
    size_t sectorCount = 0;
    const SectorCommand* hudSectors = nullptr;
    size_t hudSectorCount = 0;

    // HUD / screen-space buckets — sprites & texts published with space:"screen". Drawn on
    // hudView (a fixed screen-space transform), AFTER the world, so the HUD does NOT zoom or
    // pan with the world camera. Reuse the same SpriteInstance/TextCommand layout as world.
    const SpriteInstance* hudSprites = nullptr;
    size_t hudSpriteCount = 0;

    const TextCommand* hudTexts = nullptr;
    size_t hudTextCount = 0;

    // Main view (initialized to identity transforms)
    ViewInfo mainView = {};

    // HUD overlay view: screen-space ortho (1px = 1 unit, top-left origin), zoom always 1,
    // no pan. Independent of render:camera — it only tracks the viewport size.
    ViewInfo hudView = {};

    // Clear color (default dark gray)
    uint32_t clearColor = 0x303030FF;

    // Global ambient light term (lighting L1) — RGBA, set by `render:ambient`.
    //
    // QUOI  : the base illumination every lit surface receives before any light is added; the
    //         composite computes `final = scene * (ambient + lightAccum)`.
    //
    // POURQUOI 0 = UNSET, and why that matters more than it looks: 0 is the signal that lighting is
    //         INACTIVE, so the renderer skips the offscreen targets entirely and draws straight to
    //         the backbuffer — byte-identical to a build with no lighting at all. Every current
    //         consumer publishes no ambient, and none of them should pay two full-screen RGBA16F
    //         targets for a feature they never asked for. A non-zero default here would switch them
    //         all onto the lit path silently.
    //
    // COMMENT: this is GLOBAL FRAME STATE like clearColor and the camera, not an ephemeral
    //          primitive: published once, it governs every later frame until it changes, and it
    //          survives SceneCollector::clear().
    uint32_t ambientColor = 0;

    // Radial lights for THIS frame (ephemeral, like sprites and particles). Null + 0 when the game
    // published none — no arena slice is claimed for a feature nobody used.
    const LightCommand* lights = nullptr;
    size_t lightCount = 0;

    // Opaque occluders for THIS frame (ephemeral). Null + 0 when none were published.
    const OccluderCommand* occluders = nullptr;
    size_t occluderCount = 0;

    // Allocator for temporary pass data
    FrameAllocator* allocator = nullptr;
};

} // namespace grove
