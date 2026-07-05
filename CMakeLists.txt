cmake_minimum_required(VERSION 3.20)
# C is enabled alongside CXX so in-tree C sources (e.g. the vendored miniz/zlib used by the
# mapview chunk compressor) can be compiled directly. bgfx already requires a C toolchain
# transitively; this just makes the dependency explicit at the top level.
project(GroveEngine VERSION 1.0.0 LANGUAGES CXX C)

# C++ Standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ============================================================================
# Sanitizers for Testing
# ============================================================================
option(GROVE_ENABLE_TSAN "Enable ThreadSanitizer" OFF)

if(GROVE_ENABLE_TSAN)
    message(STATUS "ðŸ” ThreadSanitizer enabled (5-15x slowdown expected)")
    add_compile_options(-fsanitize=thread -g -O1 -fno-omit-frame-pointer)
    add_link_options(-fsanitize=thread)

    # Disable optimizations that confuse TSan
    add_compile_options(-fno-optimize-sibling-calls)

    message(WARNING "âš ï¸  TSan cannot be combined with ASan - build separately")
endif()

# ----------------------------------------------------------------------------
# AddressSanitizer + LeakSanitizer / UndefinedBehaviorSanitizer.
# MinGW ships none of these -> build under WSL/Linux (see docs/design/quality-hardening-handoff.md).
# ASan and UBSan COMPOSE (-fsanitize=address,undefined); TSan is exclusive (build separately).
# ----------------------------------------------------------------------------
option(GROVE_ENABLE_ASAN  "Enable AddressSanitizer + LeakSanitizer" OFF)
option(GROVE_ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer"       OFF)

if(GROVE_ENABLE_ASAN OR GROVE_ENABLE_UBSAN)
    if(GROVE_ENABLE_TSAN)
        message(FATAL_ERROR "GROVE_ENABLE_ASAN/UBSAN cannot combine with GROVE_ENABLE_TSAN - build separately")
    endif()

    # Build the comma list for whichever subset is on (both compose into one -fsanitize=).
    set(_grove_san "")
    if(GROVE_ENABLE_ASAN)
        message(STATUS "AddressSanitizer + LeakSanitizer enabled (~2-3x slowdown)")
        set(_grove_san "address")
    endif()
    if(GROVE_ENABLE_UBSAN)
        message(STATUS "UndefinedBehaviorSanitizer enabled (fails hard via -fno-sanitize-recover)")
        if(_grove_san)
            set(_grove_san "${_grove_san},undefined")
        else()
            set(_grove_san "undefined")
        endif()
    endif()

    add_compile_options(-fsanitize=${_grove_san} -g -O1 -fno-omit-frame-pointer)
    add_link_options(-fsanitize=${_grove_san})

    # A UB violation must FAIL the test (non-zero exit), not just print-and-continue.
    if(GROVE_ENABLE_UBSAN)
        add_compile_options(-fno-sanitize-recover=undefined)
    endif()
endif()

# ============================================================================
# Helgrind (Valgrind) Integration
# ============================================================================
option(GROVE_ENABLE_HELGRIND "Add Helgrind test target" OFF)

if(GROVE_ENABLE_HELGRIND)
    find_program(VALGRIND_EXECUTABLE valgrind)

    if(VALGRIND_EXECUTABLE)
        message(STATUS "âœ… Valgrind found: ${VALGRIND_EXECUTABLE}")

        # Add custom target for all tests
        add_custom_target(helgrind
            COMMAND ${CMAKE_COMMAND} -E echo "ðŸ” Running Helgrind (10-50x slowdown, be patient)..."
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
            COMMAND ${CMAKE_COMMAND} -E echo "ðŸ” Running single test with Helgrind..."
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

        message(STATUS "âœ… Helgrind targets added:")
        message(STATUS "   - make helgrind        (all tests)")
        message(STATUS "   - make helgrind-single (test_13 only)")
    else()
        message(WARNING "âš ï¸  Valgrind not found - Helgrind targets disabled")
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

# Catch2 for unit testing - declared here so all subdirectories can use it
# without each needing their own FetchContent_Declare (which would cause git clone on fresh builds).
# tests/CMakeLists.txt uses Catch2::Catch2WithMain without declaring it, so we declare it centrally.
FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.5.0
)
FetchContent_MakeAvailable(Catch2)

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
    # Find OpenSSL for SHA256 hashing (optional on MinGW)
    find_package(OpenSSL QUIET)
    if(OpenSSL_FOUND)
        message(STATUS "OpenSSL found - using hardware-accelerated SHA256")
        set(GROVE_USE_OPENSSL ON)
    else()
        message(STATUS "OpenSSL not found - using fallback SHA256 implementation")
        set(GROVE_USE_OPENSSL OFF)
    endif()

    add_library(grove_impl STATIC
        # --- Working files (IDataNode-based) ---
        src/ResourceRegistry.cpp
        src/JsonDataValue.cpp
        src/JsonDataNode.cpp
        src/JsonDataTree.cpp
        src/DataTreeFactory.cpp
        src/IntraIO.cpp  # âœ… Fixed for IDataNode
        src/IntraIOManager.cpp  # âœ… Fixed for IDataNode
        src/SequentialModuleSystem.cpp  # âœ… Fixed for IDataNode
        src/ThreadedModuleSystem.cpp  # âœ… Phase 2 implementation
        src/ThreadPoolModuleSystem.cpp  # âœ… Phase 3: shared worker pool + work-stealing
        src/IOFactory.cpp  # âœ… Fixed for IDataNode
        src/ModuleFactory.cpp  # âœ… Should work (no json in main API)
        src/ModuleSystemFactory.cpp  # âœ… Needs check
        src/EngineFactory.cpp  # âœ… Needs check
        src/DebugEngine.cpp  # âœ… Needs migration
        src/ModuleLoader.cpp  # âœ… Hot-reload support

        # --- TODO: Fix API mismatch (json vs IDataNode) ---
        # src/ImGuiUI.cpp  # Requires imgui dependency
    )

    target_link_libraries(grove_impl PUBLIC
        GroveEngine::core
        topictree::topictree
        stillhammer_logger
        spdlog::spdlog
        ${CMAKE_DL_LIBS}
    )

    if(GROVE_USE_OPENSSL)
        target_link_libraries(grove_impl PUBLIC OpenSSL::Crypto)
        target_compile_definitions(grove_impl PUBLIC GROVE_USE_OPENSSL=1)
    else()
        target_compile_definitions(grove_impl PUBLIC GROVE_USE_OPENSSL=0)
    endif()

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

    # UIModule (declarative UI system)
    option(GROVE_BUILD_UI_MODULE "Build UIModule" OFF)
    if(GROVE_BUILD_UI_MODULE)
        add_subdirectory(modules/UIModule)
    endif()

    # InputModule (input capture and conversion)
    option(GROVE_BUILD_INPUT_MODULE "Build InputModule" ON)
    if(GROVE_BUILD_INPUT_MODULE)
        add_subdirectory(modules/InputModule)
    endif()

    # SoundManager (music + SFX via SDL_mixer)
    option(GROVE_BUILD_SOUND_MODULE "Build SoundManager module" OFF)
    if(GROVE_BUILD_SOUND_MODULE)
        add_subdirectory(modules/SoundManager)
    endif()

    # DialogueModule (VN/cutscene runtime — pure logic, drives the UI via ui:data)
    option(GROVE_BUILD_DIALOGUE_MODULE "Build DialogueModule" OFF)
    if(GROVE_BUILD_DIALOGUE_MODULE)
        add_subdirectory(modules/DialogueModule)
    endif()

    # VideoModule (video playback — A/V sync; decode behind IVideoBackend, ffmpeg CLI in 6c-1)
    option(GROVE_BUILD_VIDEO_MODULE "Build VideoModule" OFF)
    if(GROVE_BUILD_VIDEO_MODULE)
        add_subdirectory(modules/VideoModule)
    endif()
endif()

# Testing
option(GROVE_BUILD_TESTS "Build GroveEngine tests" ON)

if(GROVE_BUILD_TESTS)
    enable_testing()

    # Windows/MinGW: Copy runtime DLLs to build directories for CTest
    if(WIN32 AND MINGW)
        get_filename_component(MINGW_BIN_DIR ${CMAKE_CXX_COMPILER} DIRECTORY)
        set(MINGW_RUNTIME_DLLS libgcc_s_seh-1.dll libstdc++-6.dll libwinpthread-1.dll)

        foreach(DLL ${MINGW_RUNTIME_DLLS})
            set(DLL_PATH "${MINGW_BIN_DIR}/${DLL}")
            if(EXISTS "${DLL_PATH}")
                file(COPY "${DLL_PATH}" DESTINATION "${CMAKE_BINARY_DIR}")
                file(COPY "${DLL_PATH}" DESTINATION "${CMAKE_BINARY_DIR}/tests")
                file(COPY "${DLL_PATH}" DESTINATION "${CMAKE_BINARY_DIR}/external/StillHammer/topictree/tests")
            endif()
        endforeach()

        message(STATUS "MinGW runtime DLLs copied for CTest")
    endif()

    add_subdirectory(tests)
endif()

# Installation
install(DIRECTORY include/grove
    DESTINATION include
    FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp"
)
