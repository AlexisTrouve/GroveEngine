#pragma once
#include "grove/IModule.h"
#include "grove/IDataNode.h"
#include <vector>
#include <random>
#include <memory>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

namespace grove {

class ConfigurableModule : public IModule {
public:
    struct Entity {
        float x, y;
        float vx, vy;
        std::string color;
        float speed;      // Config snapshot à la création
        int id;
    };

    struct Config {
        int spawnRate = 10;         // Entités par seconde
        int maxEntities = 50;       // Limite totale
        float entitySpeed = 5.0f;   // Vitesse de déplacement
        std::vector<std::string> colors = {"red", "blue"};  // Couleurs disponibles

        struct Physics {
            float gravity = 9.8f;
            float friction = 0.5f;
        } physics;

        // Validation
        bool validate(std::string& errorMsg) const {
            if (spawnRate < 0 || spawnRate > 1000) {
                errorMsg = "spawnRate must be in [0, 1000]";
                return false;
            }
            if (maxEntities < 1 || maxEntities > 10000) {
                errorMsg = "maxEntities must be in [1, 10000]";
                return false;
            }
            if (entitySpeed < 0.0f) {
                errorMsg = "entitySpeed must be >= 0";
                return false;
            }
            if (colors.empty()) {
                errorMsg = "colors list cannot be empty";
                return false;
            }
            return true;
        }
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

    // Config hot-reload API
    bool updateConfig(const IDataNode& newConfigNode);
    bool updateConfigPartial(const IDataNode& partialConfigNode);

private:
    Config currentConfig;
    Config previousConfig;  // Pour rollback
    std::unique_ptr<IDataNode> configNode;  // Store as DataNode for getConfiguration()

    std::vector<Entity> entities;
    float spawnAccumulator = 0.0f;
    int nextEntityId = 0;
    int frameCount = 0;

    std::shared_ptr<spdlog::logger> logger;
    std::mt19937 rng{42};  // Seed fixe pour reproductibilité

    void spawnEntity();
    void updateEntity(Entity& entity, float dt);
    Config parseConfig(const IDataNode& configNode);
    void applyConfig(const Config& cfg);
    nlohmann::json configToJson(const Config& cfg) const;
};

} // namespace grove

// Export symbols
extern "C" {
    grove::IModule* createModule();
    void destroyModule(grove::IModule* module);
}
