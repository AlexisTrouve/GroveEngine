cmake_minimum_required(VERSION 3.20)
project(GroveEngine VERSION 1.0.0 LANGUAGES CXX)

# C++ Standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Dependencies
include(FetchContent)

# nlohmann_json for JSON handling
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
)
FetchContent_MakeAvailable(nlohmann_json)

# spdlog for logging
FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG v1.12.0
)
FetchContent_MakeAvailable(spdlog)

# Core library (INTERFACE - header-only pour les interfaces)
add_library(grove_core INTERFACE)

target_include_directories(grove_core INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(grove_core INTERFACE
    nlohmann_json::nlohmann_json
)

# Alias for consistent naming
add_library(GroveEngine::core ALIAS grove_core)

# Optional: Build implementations
option(GROVE_BUILD_IMPLEMENTATIONS "Build GroveEngine implementations" ON)

if(GROVE_BUILD_IMPLEMENTATIONS)
    # Find OpenSSL for SHA256 hashing
    find_package(OpenSSL REQUIRED)

    add_library(grove_impl STATIC
        # --- Working files (IDataNode-based) ---
        src/ResourceRegistry.cpp
        src/JsonDataValue.cpp
        src/JsonDataNode.cpp
        src/JsonDataTree.cpp
        src/DataTreeFactory.cpp
        src/IntraIO.cpp  # ✅ Fixed for IDataNode
        src/IntraIOManager.cpp  # ✅ Fixed for IDataNode
        src/SequentialModuleSystem.cpp  # ✅ Fixed for IDataNode
        src/IOFactory.cpp  # ✅ Fixed for IDataNode
        src/ModuleFactory.cpp  # ✅ Should work (no json in main API)
        src/ModuleSystemFactory.cpp  # ✅ Needs check
        src/EngineFactory.cpp  # ✅ Needs check
        src/DebugEngine.cpp  # ✅ Needs migration
        src/ModuleLoader.cpp  # ✅ Hot-reload support

        # --- TODO: Fix API mismatch (json vs IDataNode) ---
        # src/ImGuiUI.cpp  # Requires imgui dependency
    )

    target_link_libraries(grove_impl PUBLIC
        GroveEngine::core
        OpenSSL::Crypto
        spdlog::spdlog
        ${CMAKE_DL_LIBS}
    )

    # Enable position-independent code for static library (needed for .so modules)
    set_target_properties(grove_impl PROPERTIES POSITION_INDEPENDENT_CODE ON)

    # If imgui is available from parent project, link it
    if(TARGET imgui_backends)
        target_link_libraries(grove_impl PUBLIC imgui_backends)
    endif()

    add_library(GroveEngine::impl ALIAS grove_impl)
endif()

# Testing
option(GROVE_BUILD_TESTS "Build GroveEngine tests" ON)

if(GROVE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# Installation
install(DIRECTORY include/grove
    DESTINATION include
    FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp"
)
