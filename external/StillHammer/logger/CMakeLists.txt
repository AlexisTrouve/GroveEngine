cmake_minimum_required(VERSION 3.15)
project(StillHammer_Logger VERSION 1.0.0 LANGUAGES CXX)

# C++17 required
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# spdlog should be available from parent project
# (either via find_package or FetchContent)

# Library target
add_library(stillhammer_logger
    src/Logger.cpp
)

target_include_directories(stillhammer_logger
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(stillhammer_logger
    PUBLIC
        spdlog::spdlog
)

# Tests (will be enabled when integrated into full project)
# For now, tests can be run manually or via separate test project

# Export targets
install(TARGETS stillhammer_logger
    EXPORT StillHammerLoggerTargets
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
)

install(DIRECTORY include/
    DESTINATION include
)
