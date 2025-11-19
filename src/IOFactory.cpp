#include <grove/IOFactory.h>
#include <algorithm>
#include <random>
#include <functional>
#include <logger/Logger.h>

// Include implemented transports
#include <grove/IntraIO.h>
#include <grove/IntraIOManager.h>
// Forward declarations for future implementations
// #include "LocalIO.h"
// #include "NetworkIO.h"

namespace grove {

std::unique_ptr<IIO> IOFactory::create(const std::string& transportType, const std::string& instanceId) {
    auto logger = getFactoryLogger();
    logger->info("🌐 IOFactory: Creating transport '{}' with instanceId '{}'", transportType, instanceId);

    IOType type = parseTransport(transportType);
    return create(type, instanceId);
}

std::unique_ptr<IIO> IOFactory::create(IOType ioType, const std::string& instanceId) {
    auto logger = getFactoryLogger();
    std::string typeStr = transportToString(ioType);
    logger->info("🌐 IOFactory: Creating enum type '{}' with instanceId '{}'", typeStr, instanceId);

    std::unique_ptr<IIO> io;

    switch (ioType) {
        case IOType::INTRA: {
            logger->debug("🔧 Creating IntraIO instance");

            // Generate instanceId if not provided
            std::string actualInstanceId = instanceId;
            if (actualInstanceId.empty()) {
                actualInstanceId = "intra-" + std::to_string(std::random_device{}() % 10000);
                logger->debug("🔧 Generated instanceId: '{}'", actualInstanceId);
            }

            // TEMPORARY SOLUTION: Create direct IntraIO instance
            // TODO: Properly integrate with IntraIOManager without type issues
            io = std::make_unique<IntraIO>(actualInstanceId);

            // Manually register with manager for routing
            auto& manager = IntraIOManager::getInstance();
            manager.registerInstance(actualInstanceId,
                std::static_pointer_cast<IIntraIODelivery>(
                    std::shared_ptr<IntraIO>(static_cast<IntraIO*>(io.get()), [](IntraIO*) {
                        // Don't delete - unique_ptr will handle it
                    })
                )
            );

            logger->info("✅ IntraIO created successfully with instanceId '{}'", actualInstanceId);
            break;
        }

        case IOType::LOCAL:
            logger->debug("🔧 Creating LocalIO instance");
            // TODO: Implement LocalIO
            // io = std::make_unique<LocalIO>();
            logger->error("❌ LocalIO not yet implemented");
            throw std::invalid_argument("LocalIO not yet implemented");

        case IOType::NETWORK:
            logger->debug("🔧 Creating NetworkIO instance");
            // TODO: Implement NetworkIO
            // io = std::make_unique<NetworkIO>();
            logger->error("❌ NetworkIO not yet implemented");
            throw std::invalid_argument("NetworkIO not yet implemented");

        default:
            logger->error("❌ Unknown IOType enum value: {}", static_cast<int>(ioType));
            throw std::invalid_argument("Unknown IOType enum value: " + std::to_string(static_cast<int>(ioType)));
    }

    logger->debug("🎯 IO type verification: created transport reports type '{}'",
                 transportToString(io->getType()));

    return io;
}

std::unique_ptr<IIO> IOFactory::createFromConfig(const IDataNode& config, const std::string& instanceId) {
    auto logger = getFactoryLogger();
    logger->info("🌐 IOFactory: Creating from config with instanceId '{}'", instanceId);

    try {
        // Get type from config
        std::string transportType = config.getString("type", "");
        if (transportType.empty()) {
            logger->error("❌ Config missing 'type' field");
            throw std::invalid_argument("IO config missing 'type' field");
        }

        logger->info("📋 Config specifies transport: '{}'", transportType);

        // Get instanceId from config or parameter
        std::string actualInstanceId = instanceId;
        if (actualInstanceId.empty()) {
            actualInstanceId = config.getString("instance_id", "");
            if (!actualInstanceId.empty()) {
                logger->debug("🔧 Using instanceId from config: '{}'", actualInstanceId);
            }
        }

        // Create base IO transport
        auto io = create(transportType, actualInstanceId);
        auto ioType = io->getType();

        // Apply transport-specific configuration
        if (ioType == IOType::NETWORK) {
            std::string host = config.getString("host", "");
            if (!host.empty()) {
                logger->info("🔧 Network config: host '{}'", host);
                // TODO: Apply host when NetworkIO is implemented
            }

            int port = config.getInt("port", 0);
            if (port > 0) {
                logger->info("🔧 Network config: port {}", port);
                // TODO: Apply port when NetworkIO is implemented
            }

            std::string protocol = config.getString("protocol", "");
            if (!protocol.empty()) {
                logger->info("🔧 Network config: protocol '{}'", protocol);
                // TODO: Apply protocol when NetworkIO is implemented
            }

            int timeout = config.getInt("timeout", 0);
            if (timeout > 0) {
                logger->info("🔧 Network config: timeout {}ms", timeout);
                // TODO: Apply timeout when NetworkIO is implemented
            }
        }

        if (ioType == IOType::LOCAL) {
            std::string socketPath = config.getString("socket_path", "");
            if (!socketPath.empty()) {
                logger->info("🔧 Local config: socket path '{}'", socketPath);
                // TODO: Apply socket path when LocalIO is implemented
            }
        }

        int bufferSize = config.getInt("buffer_size", 0);
        if (bufferSize > 0) {
            logger->info("🔧 IO config: buffer size {} bytes", bufferSize);
            // TODO: Apply buffer size when implementations support it
        }

        bool compression = config.getBool("compression", false);
        if (compression) {
            logger->info("🔧 IO config: compression enabled");
            // TODO: Apply compression settings when implementations support it
        }

        logger->info("✅ IO transport created from config successfully");
        return io;

    } catch (const std::exception& e) {
        logger->error("❌ Error creating IO from config: {}", e.what());
        throw;
    }
}

std::vector<std::string> IOFactory::getAvailableTransports() {
    return {
        "intra",
        "local",
        "network"
    };
}

bool IOFactory::isTransportSupported(const std::string& transportType) {
    try {
        parseTransport(transportType);
        return true;
    } catch (const std::invalid_argument&) {
        return false;
    }
}

IOType IOFactory::parseTransport(const std::string& transportStr) {
    auto logger = getFactoryLogger();
    std::string lowerTransport = toLowercase(transportStr);

    logger->trace("🔍 Parsing transport: '{}' -> '{}'", transportStr, lowerTransport);

    if (lowerTransport == "intra") {
        return IOType::INTRA;
    } else if (lowerTransport == "local") {
        return IOType::LOCAL;
    } else if (lowerTransport == "network" || lowerTransport == "net" || lowerTransport == "tcp") {
        return IOType::NETWORK;
    } else {
        logger->error("❌ Unknown transport: '{}'", transportStr);
        auto availableTransports = getAvailableTransports();
        std::string availableStr = "[";
        for (size_t i = 0; i < availableTransports.size(); ++i) {
            availableStr += availableTransports[i];
            if (i < availableTransports.size() - 1) availableStr += ", ";
        }
        availableStr += "]";

        throw std::invalid_argument("Unknown transport '" + transportStr + "'. Available transports: " + availableStr);
    }
}

std::string IOFactory::transportToString(IOType ioType) {
    switch (ioType) {
        case IOType::INTRA:
            return "intra";
        case IOType::LOCAL:
            return "local";
        case IOType::NETWORK:
            return "network";
        default:
            return "unknown";
    }
}

IOType IOFactory::getRecommendedTransport(int expectedClients, bool distributed, bool development) {
    auto logger = getFactoryLogger();

    logger->debug("🎯 Recommending transport for: {} clients, distributed={}, dev={}",
                 expectedClients, distributed, development);

    if (development || expectedClients <= 1) {
        logger->debug("💡 Development/single-user -> INTRA");
        return IOType::INTRA;
    } else if (!distributed && expectedClients <= 10) {
        logger->debug("💡 Local deployment, few clients -> LOCAL");
        return IOType::LOCAL;
    } else if (distributed || expectedClients > 10) {
        logger->debug("💡 Distributed/many clients -> NETWORK");
        return IOType::NETWORK;
    } else {
        logger->debug("💡 Default fallback -> INTRA");
        return IOType::INTRA;
    }
}

std::unique_ptr<IIO> IOFactory::createWithEndpoint(const std::string& transportType, const std::string& endpoint, const std::string& instanceId) {
    auto logger = getFactoryLogger();
    logger->info("🌐 IOFactory: Creating '{}' with endpoint '{}' and instanceId '{}'", transportType, endpoint, instanceId);

    IOType ioType = parseTransport(transportType);
    auto io = create(ioType, instanceId);

    std::string actualEndpoint = endpoint;
    if (endpoint.empty()) {
        actualEndpoint = generateEndpoint(ioType);
        logger->info("🔧 Auto-generated endpoint: '{}'", actualEndpoint);
    }

    // TODO: Configure endpoint when implementations support it
    logger->debug("🚧 TODO: Configure endpoint '{}' on {} transport", actualEndpoint, transportType);

    return io;
}

// Private helper methods
std::shared_ptr<spdlog::logger> IOFactory::getFactoryLogger() {
    static std::shared_ptr<spdlog::logger> logger = nullptr;

    if (!logger) {
        logger = stillhammer::createDomainLogger("IOFactory", "io");
    }

    return logger;
}

std::string IOFactory::toLowercase(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](char c) { return std::tolower(c); });
    return result;
}

std::string IOFactory::generateEndpoint(IOType ioType) {
    switch (ioType) {
        case IOType::INTRA:
            return "intra://localhost";

        case IOType::LOCAL:
            return "/tmp/warfactory_" + std::to_string(std::random_device{}());

        case IOType::NETWORK: {
            // Generate random port between 8000-9000
            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_int_distribution<> dis(8000, 9000);
            return "tcp://localhost:" + std::to_string(dis(gen));
        }

        default:
            return "unknown://endpoint";
    }
}

} // namespace grove