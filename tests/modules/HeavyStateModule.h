#pragma once
#include "grove/IModule.h"
#include "grove/IDataNode.h"
#include <vector>
#include <deque>
#include <unordered_map>
#include <cstdint>
#include <string>
#include <memory>
#include <spdlog/spdlog.h>

namespace grove {

class HeavyStateModule : public IModule {
public:
    struct Particle {
        float x, y;           // Position
        float vx, vy;         // Vélocité
        float lifetime;       // Temps restant
        uint32_t color;       // RGBA
    };

    struct TerrainCell {
        uint8_t height;       // 0-255
        uint8_t type;         // Grass, water, rock, etc.
        uint8_t metadata;     // Flags
        uint8_t reserved;
    };

    struct FrameSnapshot {
        uint32_t frameId;
        float avgFPS;
        size_t particleCount;
        uint64_t timestamp;
    };

    // IModule interface
    void process(const IDataNode& input) override;
    void setConfiguration(const IDataNode& configNode, IIO* io, ITaskScheduler* scheduler) override;
    const IDataNode& getConfiguration() override;
    std::unique_ptr<IDataNode> getHealthStatus() override;
    void shutdown() override;
    std::unique_ptr<IDataNode> getState() override;
    void setState(const IDataNode& state) override;
    std::string getType() const override;
    bool isIdle() const override { return true; }

private:
    std::vector<Particle> particles;               // 1M particules = ~32MB
    std::vector<TerrainCell> terrain;              // 100M cells = ~100MB (ou moins selon config)
    std::deque<FrameSnapshot> history;             // 10k frames = ~160KB
    std::unordered_map<uint32_t, std::vector<uint8_t>> textureCache; // 50k textures simulées

    float initDuration = 8.0f;  // Temps d'init simulé (secondes)
    float initTimeout = 15.0f;  // Timeout pour init
    int frameCount = 0;
    std::string version = "v1.0";
    bool incrementalMode = false;

    int particleTargetCount = 1000000;
    int terrainWidth = 10000;
    int terrainHeight = 10000;
    int historyMaxSize = 10000;

    std::shared_ptr<spdlog::logger> logger;
    std::unique_ptr<IDataNode> config;

    void updateParticles(float dt);
    void spawnParticles(size_t count);
    void initializeTerrain(int width, int height);
    bool validateState(const IDataNode& state) const;

    // Helpers pour compression/décompression
    std::string compressParticleData() const;
    void decompressParticleData(const std::string& compressed);
    std::string compressTerrainData() const;
    void decompressTerrainData(const std::string& compressed);
};

} // namespace grove

// Export symbols
extern "C" {
    grove::IModule* createModule();
    void destroyModule(grove::IModule* module);
}
