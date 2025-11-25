cmake_minimum_required(VERSION 3.20)
project(GroveEngine VERSION 1.0.0 LANGUAGES CXX)

# C++ Standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ============================================================================
# Sanitizers for Testing
# ============================================================================
option(GROVE_ENABLE_TSAN "Enable ThreadSanitizer" OFF)

if(GROVE_ENABLE_TSAN)
    message(STATUS "🔍 ThreadSanitizer enabled (5-15x slowdown expected)")
    add_compile_options(-fsanitize=thread -g -O1 -fno-omit-frame-pointer)
    add_link_options(-fsanitize=thread)

    # Disable optimizations that confuse TSan
    add_compile_options(-fno-optimize-sibling-calls)

    message(WARNING "⚠️  TSan cannot be combined with ASan - build separately")
endif()

# ============================================================================
# Helgrind (Valgrind) Integration
# ============================================================================
option(GROVE_ENABLE_HELGRIND "Add Helgrind test target" OFF)

if(GROVE_ENABLE_HELGRIND)
    find_program(VALGRIND_EXECUTABLE valgrind)

    if(VALGRIND_EXECUTABLE)
        message(STATUS "✅ Valgrind found: ${VALGRIND_EXECUTABLE}")

        # Add custom target for all tests
        add_custom_target(helgrind
            COMMAND ${CMAKE_COMMAND} -E echo "🔍 Running Helgrind (10-50x slowdown, be patient)..."
            COMMAND ${VALGRIND_EXECUTABLE}
                --tool=helgrind
                --log-file=${CMAKE_BINARY_DIR}/helgrind-full.log
                --suppressions=${CMAKE_SOURCE_DIR}/helgrind.supp
                --error-exitcode=1
                --read-var-info=yes
                ${CMAKE_CTEST_COMMAND} --output-on-failure --timeout 600
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
            COMMENT "Running all tests with Helgrind deadlock detector"
        )

        # Add convenience target for single test
        add_custom_target(helgrind-single
            COMMAND ${CMAKE_COMMAND} -E echo "🔍 Running single test with Helgrind..."
            COMMAND ${VALGRIND_EXECUTABLE}
                --tool=helgrind
                -v
                --log-file=${CMAKE_BINARY_DIR}/helgrind-single.log
                --suppressions=${CMAKE_SOURCE_DIR}/helgrind.supp
                --error-exitcode=1
                --read-var-info=yes
                ./tests/test_13_cross_system
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
            COMMENT "Running test_13_cross_system with Helgrind"
        )

        message(STATUS "✅ Helgrind targets added:")
        message(STATUS "   - make helgrind        (all tests)")
        message(STATUS "   - make helgrind-single (test_13 only)")
    else()
        message(WARNING "⚠️  Valgrind not found - Helgrind targets disabled")
        message(STATUS "   Install: sudo apt-get install valgrind")
    endif()
endif()

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
set(CMAKE_POSITION_INDEPENDENT_CODE ON)  # Force PIC for all targets
FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG v1.12.0
)
FetchContent_MakeAvailable(spdlog)

# StillHammer libraries
add_subdirectory(external/StillHammer/topictree)  # Ultra-fast topic routing
add_subdirectory(external/StillHammer/logger)     # Domain-organized logging

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
        topictree::topictree
        stillhammer_logger
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

# ============================================================================
# Modules (hot-reloadable .so)
# ============================================================================
option(GROVE_BUILD_MODULES "Build GroveEngine modules" ON)

if(GROVE_BUILD_MODULES)
    # BgfxRenderer module (2D rendering via bgfx)
    option(GROVE_BUILD_BGFX_RENDERER "Build BgfxRenderer module" OFF)
    if(GROVE_BUILD_BGFX_RENDERER)
        add_subdirectory(modules/BgfxRenderer)
    endif()
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
