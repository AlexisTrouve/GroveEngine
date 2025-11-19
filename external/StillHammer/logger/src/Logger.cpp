#include <logger/Logger.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <filesystem>
#include <algorithm>

namespace stillhammer {

namespace {

// Convert our LogLevel to spdlog level
spdlog::level::level_enum toSpdlogLevel(LogLevel level) {
    switch (level) {
        case LogLevel::Trace: return spdlog::level::trace;
        case LogLevel::Debug: return spdlog::level::debug;
        case LogLevel::Info: return spdlog::level::info;
        case LogLevel::Warn: return spdlog::level::warn;
        case LogLevel::Error: return spdlog::level::err;
        case LogLevel::Critical: return spdlog::level::critical;
        case LogLevel::Off: return spdlog::level::off;
    }
    return spdlog::level::info;
}

// Convert component name to filename: "IntraIO" → "intra_io"
std::string toSnakeCase(const std::string& name) {
    std::string result;
    result.reserve(name.size() + 5);

    for (size_t i = 0; i < name.size(); ++i) {
        char c = name[i];

        // Insert underscore before uppercase letters (except first char)
        if (i > 0 && std::isupper(c) && std::islower(name[i-1])) {
            result += '_';
        }

        result += std::tolower(c);
    }

    return result;
}

// Ensure directory exists
void ensureDirectoryExists(const std::string& path) {
    std::filesystem::path dirPath = std::filesystem::path(path).parent_path();
    if (!dirPath.empty() && !std::filesystem::exists(dirPath)) {
        std::filesystem::create_directories(dirPath);
    }
}

} // anonymous namespace

std::shared_ptr<spdlog::logger> createLogger(
    const std::string& name,
    const LoggerConfig& config
) {
    // Check if logger already exists
    auto existing = spdlog::get(name);
    if (existing) {
        return existing;
    }

    std::vector<spdlog::sink_ptr> sinks;

    // Console sink
    if (config.enableConsole) {
        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        console_sink->set_level(toSpdlogLevel(config.consoleLevel));
        sinks.push_back(console_sink);
    }

    // File sink
    if (config.enableFile) {
        // Build file path: logs/[domain/]component.log
        std::string filename = toSnakeCase(name) + ".log";
        std::string filepath;

        if (config.domain.empty()) {
            filepath = "logs/" + filename;
        } else {
            filepath = "logs/" + config.domain + "/" + filename;
        }

        ensureDirectoryExists(filepath);

        auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(filepath, true);
        file_sink->set_level(toSpdlogLevel(config.fileLevel));
        sinks.push_back(file_sink);
    }

    // Create logger
    auto logger = std::make_shared<spdlog::logger>(name, sinks.begin(), sinks.end());

    // Set pattern
    logger->set_pattern(config.pattern);

    // Set level to the minimum of console and file levels
    auto minLevel = std::min(config.consoleLevel, config.fileLevel);
    logger->set_level(toSpdlogLevel(minLevel));

    // Flush on warning or higher
    logger->flush_on(spdlog::level::warn);

    // Register globally
    spdlog::register_logger(logger);

    return logger;
}

std::shared_ptr<spdlog::logger> createDomainLogger(
    const std::string& name,
    const std::string& domain,
    const LoggerConfig& config
) {
    LoggerConfig domainConfig = config;
    domainConfig.domain = domain;
    return createLogger(name, domainConfig);
}

std::shared_ptr<spdlog::logger> getLogger(const std::string& name) {
    return spdlog::get(name);
}

void setGlobalLogLevel(LogLevel level) {
    spdlog::set_level(toSpdlogLevel(level));
}

void flushAll() {
    spdlog::apply_all([](std::shared_ptr<spdlog::logger> logger) {
        logger->flush();
    });
}

} // namespace stillhammer
