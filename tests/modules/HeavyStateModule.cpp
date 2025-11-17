#include "HeavyStateModule.h"
#include "grove/JsonDataNode.h"
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <iostream>
#include <random>
#include <chrono>
#include <thread>
#include <cmath>
#include <sstream>
#include <iomanip>

namespace grove {

void HeavyStateModule::setConfiguration(const IDataNode& configNode, IIO* io, ITaskScheduler* scheduler) {
    // Logger
    logger = spdlog::get("HeavyStateModule");
    if (!logger) {
        logger = spdlog::stdout_color_mt("HeavyStateModule");
    }
    logger->set_level(spdlog::level::info);

    // Clone config
    const auto* jsonConfigNode = dynamic_cast<const JsonDataNode*>(&configNode);
    if (jsonConfigNode) {
        config = std::make_unique<JsonDataNode>("config", jsonConfigNode->getJsonData());
    } else {
        config = std::make_unique<JsonDataNode>("config");
    }

    // Extraire configuration
    version = configNode.getString("version", "v1.0");
    particleTargetCount = configNode.getInt("particleCount", 1000000);
    terrainWidth = configNode.getInt("terrainSize", 10000);
    terrainHeight = terrainWidth;
    initDuration = static_cast<float>(configNode.getDouble("initDuration", 8.0));
    initTimeout = static_cast<float>(configNode.getDouble("initTimeout", 15.0));
    incrementalMode = configNode.getBool("incrementalState", false);

    logger->info("Initializing HeavyStateModule {}", version);
    logger->info("  Particles: {}", particleTargetCount);
    logger->info("  Terrain: {}x{}", terrainWidth, terrainHeight);
    logger->info("  Init duration: {}s", initDuration);

    // Simuler initialisation longue avec timeout check
    auto startTime = std::chrono::high_resolution_clock::now();

    // Spawner particules progressivement
    const int batchSize = 10000;  // Batches plus petits pour vérifier le timeout plus souvent
    for (int spawned = 0; spawned < particleTargetCount; spawned += batchSize) {
        int toSpawn = std::min(batchSize, particleTargetCount - spawned);
        spawnParticles(toSpawn);

        // Check timeout
        auto currentTime = std::chrono::high_resolution_clock::now();
        float elapsed = std::chrono::duration<float>(currentTime - startTime).count();

        if (elapsed > initTimeout) {
            throw std::runtime_error("Module initialization exceeded timeout (" +
                                   std::to_string(elapsed) + "s > " +
                                   std::to_string(initTimeout) + "s)");
        }

        // Simuler travail (proportionnel à initDuration)
        float sleepTime = (initDuration / (particleTargetCount / (float)batchSize)) * 1000.0f;
        std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(sleepTime)));
    }

    // Initialiser terrain
    initializeTerrain(terrainWidth, terrainHeight);

    auto endTime = std::chrono::high_resolution_clock::now();
    float totalTime = std::chrono::duration<float>(endTime - startTime).count();

    logger->info("Initialization completed in {}s", totalTime);
    logger->info("  Particles spawned: {}", particles.size());
    logger->info("  Terrain cells: {}", terrain.size());

    frameCount = 0;
}

const IDataNode& HeavyStateModule::getConfiguration() {
    return *config;
}

void HeavyStateModule::process(const IDataNode& input) {
    float deltaTime = static_cast<float>(input.getDouble("deltaTime", 1.0 / 60.0));

    frameCount++;

    // Update particules
    updateParticles(deltaTime);

    // Record frame snapshot
    if (history.size() >= static_cast<size_t>(historyMaxSize)) {
        history.pop_front();
    }

    FrameSnapshot snapshot;
    snapshot.frameId = frameCount;
    snapshot.avgFPS = 1.0f / deltaTime;
    snapshot.particleCount = particles.size();
    snapshot.timestamp = std::chrono::system_clock::now().time_since_epoch().count();

    history.push_back(snapshot);

    if (frameCount % 300 == 0) {
        logger->trace("Frame {}: {} particles", frameCount, particles.size());
    }
}

std::unique_ptr<IDataNode> HeavyStateModule::getHealthStatus() {
    nlohmann::json healthJson;
    healthJson["status"] = "healthy";
    healthJson["particleCount"] = particles.size();
    healthJson["terrainCells"] = terrain.size();
    healthJson["frameCount"] = frameCount;
    healthJson["historySize"] = history.size();
    return std::make_unique<JsonDataNode>("health", healthJson);
}

void HeavyStateModule::shutdown() {
    logger->info("Shutting down HeavyStateModule");
    particles.clear();
    terrain.clear();
    history.clear();
    textureCache.clear();
}

std::string HeavyStateModule::getType() const {
    return "heavystate";
}

std::unique_ptr<IDataNode> HeavyStateModule::getState() {
    auto startTime = std::chrono::high_resolution_clock::now();

    nlohmann::json state;
    state["version"] = version;
    state["frameCount"] = frameCount;

    // Config
    state["config"]["particleCount"] = particleTargetCount;
    state["config"]["terrainWidth"] = terrainWidth;
    state["config"]["terrainHeight"] = terrainHeight;
    state["config"]["historySize"] = historyMaxSize;

    // Particles (compressé)
    state["particles"]["count"] = particles.size();
    state["particles"]["data"] = compressParticleData();

    // Terrain (compressé)
    state["terrain"]["width"] = terrainWidth;
    state["terrain"]["height"] = terrainHeight;
    state["terrain"]["compressed"] = true;
    state["terrain"]["data"] = compressTerrainData();

    // History
    nlohmann::json historyArray = nlohmann::json::array();
    for (const auto& snap : history) {
        historyArray.push_back({
            {"frame", snap.frameId},
            {"fps", snap.avgFPS},
            {"particles", snap.particleCount},
            {"ts", snap.timestamp}
        });
    }
    state["history"] = historyArray;

    // Texture cache metadata
    state["textureCache"]["count"] = textureCache.size();
    size_t totalCacheSize = 0;
    for (const auto& [id, data] : textureCache) {
        totalCacheSize += data.size();
    }
    state["textureCache"]["totalSize"] = totalCacheSize;

    auto endTime = std::chrono::high_resolution_clock::now();
    float elapsed = std::chrono::duration<float, std::milli>(endTime - startTime).count();

    logger->info("getState() completed in {}ms", elapsed);

    return std::make_unique<JsonDataNode>("state", state);
}

void HeavyStateModule::setState(const IDataNode& stateNode) {
    // Initialiser logger si nécessaire (peut être appelé avant setConfiguration lors du reload)
    if (!logger) {
        logger = spdlog::get("HeavyStateModule");
        if (!logger) {
            logger = spdlog::stdout_color_mt("HeavyStateModule");
        }
        logger->set_level(spdlog::level::info);
    }

    auto startTime = std::chrono::high_resolution_clock::now();

    // Valider avant d'appliquer
    if (!validateState(stateNode)) {
        throw std::runtime_error("State validation failed - corrupted or invalid state");
    }

    const auto* jsonNode = dynamic_cast<const JsonDataNode*>(&stateNode);
    if (!jsonNode) {
        throw std::runtime_error("HeavyStateModule requires JsonDataNode for state");
    }

    const auto& data = jsonNode->getJsonData();

    // Restaurer version
    version = data["version"].get<std::string>();
    frameCount = data["frameCount"].get<int>();

    // Restaurer config
    particleTargetCount = data["config"]["particleCount"];
    terrainWidth = data["config"]["terrainWidth"];
    terrainHeight = data["config"]["terrainHeight"];

    // Restaurer particules
    decompressParticleData(data["particles"]["data"].get<std::string>());

    // Restaurer terrain
    decompressTerrainData(data["terrain"]["data"].get<std::string>());

    // Restaurer historique
    history.clear();
    for (const auto& snap : data["history"]) {
        FrameSnapshot snapshot;
        snapshot.frameId = snap["frame"];
        snapshot.avgFPS = snap["fps"];
        snapshot.particleCount = snap["particles"];
        snapshot.timestamp = snap["ts"];
        history.push_back(snapshot);
    }

    auto endTime = std::chrono::high_resolution_clock::now();
    float elapsed = std::chrono::duration<float, std::milli>(endTime - startTime).count();

    logger->info("setState() completed in {}ms", elapsed);
    logger->info("  Particles restored: {}", particles.size());
    logger->info("  Terrain cells: {}", terrain.size());
    logger->info("  History entries: {}", history.size());
}

void HeavyStateModule::updateParticles(float dt) {
    for (auto& p : particles) {
        // Update position
        p.x += p.vx * dt;
        p.y += p.vy * dt;

        // Update lifetime
        p.lifetime -= dt;

        // Respawn si mort
        if (p.lifetime <= 0) {
            static std::mt19937 rng(42);
            static std::uniform_real_distribution<float> distPos(0.0f, 100.0f);
            static std::uniform_real_distribution<float> distVel(-5.0f, 5.0f);
            static std::uniform_real_distribution<float> distLife(1.0f, 10.0f);

            p.x = distPos(rng);
            p.y = distPos(rng);
            p.vx = distVel(rng);
            p.vy = distVel(rng);
            p.lifetime = distLife(rng);
        }
    }
}

void HeavyStateModule::spawnParticles(size_t count) {
    static std::mt19937 rng(42);  // Seed fixe pour reproductibilité
    static std::uniform_real_distribution<float> distPos(0.0f, 100.0f);
    static std::uniform_real_distribution<float> distVel(-5.0f, 5.0f);
    static std::uniform_real_distribution<float> distLife(1.0f, 10.0f);
    static std::uniform_int_distribution<uint32_t> distColor(0x00000000, 0xFFFFFFFF);

    for (size_t i = 0; i < count; i++) {
        Particle p;
        p.x = distPos(rng);
        p.y = distPos(rng);
        p.vx = distVel(rng);
        p.vy = distVel(rng);
        p.lifetime = distLife(rng);
        p.color = distColor(rng);

        particles.push_back(p);
    }
}

void HeavyStateModule::initializeTerrain(int width, int height) {
    static std::mt19937 rng(1337);  // Seed différent du spawn particules
    static std::uniform_int_distribution<int> distHeight(0, 255);
    static std::uniform_int_distribution<int> distType(0, 5);

    size_t totalCells = static_cast<size_t>(width) * static_cast<size_t>(height);
    terrain.reserve(totalCells);

    for (size_t i = 0; i < totalCells; i++) {
        TerrainCell cell;
        cell.height = static_cast<uint8_t>(distHeight(rng));
        cell.type = static_cast<uint8_t>(distType(rng));
        cell.metadata = 0;
        cell.reserved = 0;

        terrain.push_back(cell);
    }
}

bool HeavyStateModule::validateState(const IDataNode& stateNode) const {
    const auto* jsonNode = dynamic_cast<const JsonDataNode*>(&stateNode);
    if (!jsonNode) {
        logger->error("State is not JsonDataNode");
        return false;
    }

    const auto& data = jsonNode->getJsonData();

    // Vérifier champs requis
    if (!data.contains("version") || !data.contains("config") ||
        !data.contains("particles") || !data.contains("terrain")) {
        logger->error("Missing required fields");
        return false;
    }

    // Vérifier types
    if (!data["frameCount"].is_number_integer()) {
        logger->error("frameCount must be integer");
        return false;
    }

    // Vérifier limites
    int particleCount = data["config"]["particleCount"];
    if (particleCount < 0 || particleCount > 10000000) {
        logger->error("Invalid particle count: {}", particleCount);
        return false;
    }

    // Vérifier NaN/Infinity
    int terrainW = data["config"]["terrainWidth"];
    int terrainH = data["config"]["terrainHeight"];

    if (std::isnan(static_cast<float>(terrainW)) || std::isinf(static_cast<float>(terrainW)) ||
        std::isnan(static_cast<float>(terrainH)) || std::isinf(static_cast<float>(terrainH))) {
        logger->error("Terrain dimensions are NaN/Inf");
        return false;
    }

    if (terrainW < 0 || terrainH < 0 || terrainW > 20000 || terrainH > 20000) {
        logger->error("Invalid terrain dimensions");
        return false;
    }

    return true;
}

std::string HeavyStateModule::compressParticleData() const {
    // Pour simplifier, on encode en hexadécimal
    // Dans une vraie implémentation, on utiliserait zlib ou autre
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');

    // Encoder seulement un échantillon pour ne pas créer un JSON énorme
    size_t sampleSize = std::min(particles.size(), size_t(1000));

    for (size_t i = 0; i < sampleSize; i++) {
        const auto& p = particles[i];
        oss << std::setw(8) << *reinterpret_cast<const uint32_t*>(&p.x);
        oss << std::setw(8) << *reinterpret_cast<const uint32_t*>(&p.y);
        oss << std::setw(8) << *reinterpret_cast<const uint32_t*>(&p.vx);
        oss << std::setw(8) << *reinterpret_cast<const uint32_t*>(&p.vy);
        oss << std::setw(8) << *reinterpret_cast<const uint32_t*>(&p.lifetime);
        oss << std::setw(8) << p.color;
    }

    return oss.str();
}

void HeavyStateModule::decompressParticleData(const std::string& compressed) {
    // Recréer les particules à partir de l'échantillon
    // (simplifié - dans la vraie vie on décompresserait tout)

    particles.clear();
    spawnParticles(particleTargetCount);  // Recréer avec seed fixe
}

std::string HeavyStateModule::compressTerrainData() const {
    // Similaire - échantillon seulement
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');

    size_t sampleSize = std::min(terrain.size(), size_t(1000));

    for (size_t i = 0; i < sampleSize; i++) {
        const auto& cell = terrain[i];
        oss << std::setw(2) << static_cast<int>(cell.height);
        oss << std::setw(2) << static_cast<int>(cell.type);
    }

    return oss.str();
}

void HeavyStateModule::decompressTerrainData(const std::string& compressed) {
    // Recréer terrain avec seed fixe
    terrain.clear();
    initializeTerrain(terrainWidth, terrainHeight);
}

} // namespace grove

// Export symbols
extern "C" {
    grove::IModule* createModule() {
        return new grove::HeavyStateModule();
    }

    void destroyModule(grove::IModule* module) {
        delete module;
    }
}
