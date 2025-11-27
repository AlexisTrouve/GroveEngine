#pragma once

#include "../Frame/FramePacket.h"
#include <vector>
#include <string>

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
    void setup(IIO* io);

    // Collect all IIO messages at frame start (called in process)
    // Pull-based: module controls when to read messages
    void collect(IIO* io, float deltaTime);

    // Generate immutable FramePacket for render passes
    FramePacket finalize(FrameAllocator& allocator);

    // Reset for next frame
    void clear();

private:
    // Staging buffers (filled during collect, copied to FramePacket in finalize)
    std::vector<SpriteInstance> m_sprites;
    std::vector<TilemapChunk> m_tilemaps;
    std::vector<std::vector<uint16_t>> m_tilemapTiles;  // Owns tile data until finalize
    std::vector<TextCommand> m_texts;
    std::vector<std::string> m_textStrings;  // Owns text data until finalize
    std::vector<ParticleInstance> m_particles;
    std::vector<DebugLine> m_debugLines;
    std::vector<DebugRect> m_debugRects;

    // View state
    ViewInfo m_mainView;
    uint32_t m_clearColor = 0x303030FF;
    uint64_t m_frameNumber = 0;
    float m_deltaTime = 0.0f;

    // Message parsing helpers
    void parseSprite(const IDataNode& data);
    void parseSpriteBatch(const IDataNode& data);
    void parseTilemap(const IDataNode& data);
    void parseText(const IDataNode& data);
    void parseParticle(const IDataNode& data);
    void parseCamera(const IDataNode& data);
    void parseClear(const IDataNode& data);
    void parseDebugLine(const IDataNode& data);
    void parseDebugRect(const IDataNode& data);

    // Initialize default view
    void initDefaultView(uint16_t width, uint16_t height);
};

} // namespace grove
