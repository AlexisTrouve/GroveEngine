#pragma once

#include "../Frame/FramePacket.h"
#include <vector>
#include <string>
#include <unordered_map>

namespace grove {

class IIO;
class IDataNode;
class FrameAllocator;

// ============================================================================
// Scene Collector - Gathers render data from IIO messages
// ============================================================================

class SceneCollector {
public:
    SceneCollector() = default;

    // Configure IIO subscriptions (called in setConfiguration)
    // width/height: Window dimensions for default view initialization
    void setup(IIO* io, uint16_t width = 1280, uint16_t height = 720);

    // Collect all IIO messages at frame start (called in process)
    // Pull-based: module controls when to read messages
    void collect(IIO* io, float deltaTime);

    // Generate immutable FramePacket for render passes
    FramePacket finalize(FrameAllocator& allocator);

    // Reset for next frame
    void clear();

private:
    // Retained mode: persistent sprites/texts (not cleared each frame)
    std::unordered_map<uint32_t, SpriteInstance> m_retainedSprites;
    std::unordered_map<uint32_t, TextCommand> m_retainedTexts;
    std::unordered_map<uint32_t, std::string> m_retainedTextStrings;  // Text content for retained texts

    // Ephemeral mode: staging buffers (filled during collect, cleared each frame)
    std::vector<SpriteInstance> m_sprites;
    std::vector<TilemapChunk> m_tilemaps;
    std::vector<std::vector<uint16_t>> m_tilemapTiles;  // Owns tile data until finalize
    std::vector<TextCommand> m_texts;
    std::vector<std::string> m_textStrings;  // Owns text data until finalize
    std::vector<ParticleInstance> m_particles;
    std::vector<DebugLine> m_debugLines;
    std::vector<DebugRect> m_debugRects;

    // HUD / screen-space staging (filled when a command carries space:"screen"). Ephemeral
    // only — drawn on m_hudView so the HUD ignores the world camera's zoom/pan.
    std::vector<SpriteInstance> m_hudSprites;
    std::vector<TextCommand> m_hudTexts;
    std::vector<std::string> m_hudTextStrings;  // Owns HUD text data until finalize

    // View state
    ViewInfo m_mainView;
    ViewInfo m_hudView;  // Fixed screen-space view for the HUD bucket (see FramePacket::hudView)
    uint32_t m_clearColor = 0x303030FF;
    uint64_t m_frameNumber = 0;
    float m_deltaTime = 0.0f;

    // Message parsing helpers (ephemeral mode - legacy)
    void parseSprite(const IDataNode& data);
    void parseRect(const IDataNode& data);  // filled colored quad via the layered sprite path (A2)
    void parseSpriteBatch(const IDataNode& data);
    void parseTilemap(const IDataNode& data);
    void parseText(const IDataNode& data);
    void parseParticle(const IDataNode& data);
    void parseCamera(const IDataNode& data);
    void parseClear(const IDataNode& data);
    void parseDebugLine(const IDataNode& data);
    void parseDebugRect(const IDataNode& data);

    // Message parsing helpers (retained mode - new)
    void parseSpriteAdd(const IDataNode& data);
    void parseSpriteUpdate(const IDataNode& data);
    void parseSpriteRemove(const IDataNode& data);
    void parseTextAdd(const IDataNode& data);
    void parseTextUpdate(const IDataNode& data);
    void parseTextRemove(const IDataNode& data);

    // Initialize default view
    void initDefaultView(uint16_t width, uint16_t height);
};

} // namespace grove
