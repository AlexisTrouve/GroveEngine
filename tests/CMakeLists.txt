# Hot-reload test suite

# ================================================================================
# Vendored single-file deps (git-tracked in third_party/) — miniz (zlib) + nlohmann/json single header.
# POURQUOI : several tests compile miniz.c / include nlohmann directly. They USED to point into deps/bgfx/…
#   and deps/nlohmann_json/… — but deps/ is GITIGNORED (it holds FetchContent's copies), so those paths exist
#   only on a machine that has already fetched them. A CLEAN CLONE (a fresh Linux box, or a new Windows checkout)
#   then fails to configure. Vendoring the two single-file libs under third_party/ makes every clone build.
# ================================================================================
set(GROVE_MINIZ_DIR    ${CMAKE_SOURCE_DIR}/third_party/miniz)
set(GROVE_NLOHMANN_DIR ${CMAKE_SOURCE_DIR}/third_party/json)

# ================================================================================
# SDL2 Detection (used by visual tests, demos, and InputModule-dependent tests)
# ================================================================================
find_package(SDL2 QUIET)
find_package(SDL2_mixer QUIET)   # audio backend for the sound manager (slice 2)

# Determine if SDL2 is available (either via find_package or system install on Linux)
set(SDL2_AVAILABLE FALSE)
if(SDL2_FOUND)
    set(SDL2_AVAILABLE TRUE)
    message(STATUS "SDL2 found via find_package")
elseif(UNIX AND EXISTS "/usr/include/SDL2/SDL.h")
    set(SDL2_AVAILABLE TRUE)
    message(STATUS "SDL2 found at system path /usr/include/SDL2")
else()
    message(STATUS "SDL2 not found - visual tests and SDL2-dependent demos will be disabled")
    message(STATUS "  On Windows MinGW, install via: pacman -S mingw-w64-ucrt-x86_64-SDL2")
    message(STATUS "  On Ubuntu/Debian, install via: apt install libsdl2-dev")
endif()

# ================================================================================
# Test Modules
# ================================================================================

# Test module as shared library (.so) for hot-reload
add_library(TestModule SHARED
    modules/TestModule.cpp
)

target_link_libraries(TestModule PRIVATE
    GroveEngine::core
    GroveEngine::impl  # For JsonDataNode implementation
)

# Don't add "lib" prefix on Linux (we want TestModule.so, not libTestModule.so)
set_target_properties(TestModule PROPERTIES PREFIX "lib")
set_target_properties(TestModule PROPERTIES OUTPUT_NAME "TestModule")

# Basic hot-reload test executable (manual dlopen/dlclose)
add_executable(test_hotreload
    hotreload/test_hotreload.cpp
)

target_link_libraries(test_hotreload PRIVATE
    GroveEngine::core
    GroveEngine::impl  # For JsonDataNode implementation
    ${CMAKE_DL_LIBS}  # For dlopen/dlclose
)

# Make sure test module is built before test executable
add_dependencies(test_hotreload TestModule)

# Copy test module to test executable directory after build
add_custom_command(TARGET test_hotreload POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
        $<TARGET_FILE:TestModule>
        $<TARGET_FILE_DIR:test_hotreload>/
    COMMENT "Copying TestModule.so to test directory"
)

# Engine hot-reload test (uses DebugEngine + SequentialModuleSystem + FileWatcher)
add_executable(test_engine_hotreload
    hotreload/test_engine_hotreload.cpp
)

target_link_libraries(test_engine_hotreload PRIVATE
    GroveEngine::core
    GroveEngine::impl
    ${CMAKE_DL_LIBS}
)

add_dependencies(test_engine_hotreload TestModule)

add_custom_command(TARGET test_engine_hotreload POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
        $<TARGET_FILE:TestModule>
        $<TARGET_FILE_DIR:test_engine_hotreload>/
    COMMENT "Copying TestModule.so to engine test directory"
)

# ================================================================================
# Integration Tests
# ================================================================================


# ================================================================================
# Windows/MinGW: CTest helper for DLL loading
# ================================================================================
# Copy MinGW runtime DLLs at configure time
if(WIN32 AND MINGW)
    get_filename_component(MINGW_BIN_DIR ${CMAKE_CXX_COMPILER} DIRECTORY)
    set(MINGW_DLLS libgcc_s_seh-1.dll libstdc++-6.dll libwinpthread-1.dll)
    foreach(DLL ${MINGW_DLLS})
        if(EXISTS "${MINGW_BIN_DIR}/${DLL}")
            file(COPY "${MINGW_BIN_DIR}/${DLL}" DESTINATION "${CMAKE_CURRENT_BINARY_DIR}")
        endif()
    endforeach()
endif()

# Resolve the directories containing cmake and the build tool (ninja/make) at configure
# time. These paths are injected into the test environment so that tests which invoke
# cmake --build or ninja/make at runtime (e.g. hot-reload tests) can find the tools
# without relying on the user's PATH being set when CTest is invoked.
get_filename_component(GROVE_CMAKE_DIR "${CMAKE_COMMAND}" DIRECTORY)
get_filename_component(GROVE_MAKE_DIR  "${CMAKE_MAKE_PROGRAM}" DIRECTORY)

# Test macro - uses WORKING_DIRECTORY with PATH environment for Windows DLL loading
# Optional 4th arg: timeout in seconds (default 90)
macro(grove_add_test test_name test_target working_dir)
    # Parse optional timeout argument via ARGN (args beyond the 3 named params).
    # Using list(LENGTH ARGN) is more reliable than ARGC/ARGV3 in CMake macros
    # because ARGC-based checks can silently fail with named param macros.
    set(_timeout 90)
    # Copy ARGN to a regular variable first - list() can't directly operate on implicit ARGN
    set(_grove_argn ${ARGN})
    list(LENGTH _grove_argn _grove_argn_len)
    if(_grove_argn_len GREATER 0)
        list(GET _grove_argn 0 _timeout)
    endif()
    add_test(NAME ${test_name} COMMAND ${test_target} WORKING_DIRECTORY ${working_dir})
    if(WIN32)
        # Set PATH environment variable to include the working directory
        # This ensures dynamically loaded DLLs (via LoadLibrary) can be found
        set_tests_properties(${test_name} PROPERTIES
            ENVIRONMENT "PATH=${working_dir};${working_dir}/modules;${GROVE_CMAKE_DIR};${GROVE_MAKE_DIR};$ENV{PATH};GROVE_CMAKE_COMMAND=${CMAKE_COMMAND};GROVE_MAKE_COMMAND=${CMAKE_MAKE_PROGRAM}"
            TIMEOUT ${_timeout})
    else()
        # On non-Windows, inject cmake/make tool paths so hot-reload tests can find them
        set_tests_properties(${test_name} PROPERTIES
            ENVIRONMENT "GROVE_CMAKE_COMMAND=${CMAKE_COMMAND};GROVE_MAKE_COMMAND=${CMAKE_MAKE_PROGRAM}"
            TIMEOUT ${_timeout})
    endif()
endmacro()



# Helpers library (partag�e par tous les tests)
add_library(test_helpers STATIC
    helpers/TestMetrics.cpp
    helpers/TestReporter.cpp
    helpers/SystemUtils.cpp
    helpers/AutoCompiler.cpp
)

target_include_directories(test_helpers PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(test_helpers PUBLIC
    GroveEngine::core
    spdlog::spdlog
)

# Set PIC for static library
set_target_properties(test_helpers PROPERTIES POSITION_INDEPENDENT_CODE ON)

# TankModule pour tests d'int�gration
add_library(TankModule SHARED
    modules/TankModule.cpp
)

target_link_libraries(TankModule PRIVATE
    GroveEngine::core
    GroveEngine::impl
    spdlog::spdlog
)

# Ensure spdlog is compiled with PIC
set_target_properties(spdlog PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Test 01: Production Hot-Reload
add_executable(test_01_production_hotreload
    integration/test_01_production_hotreload.cpp
)

target_link_libraries(test_01_production_hotreload PRIVATE
    test_helpers
    GroveEngine::core
    GroveEngine::impl
)

add_dependencies(test_01_production_hotreload TankModule)

# CTest integration
grove_add_test(ProductionHotReload test_01_production_hotreload ${CMAKE_CURRENT_BINARY_DIR})

# ThreadedModuleSystem Tests
add_executable(test_threaded_module_system
    integration/test_threaded_module_system.cpp
)

target_link_libraries(test_threaded_module_system PRIVATE
    test_helpers
    GroveEngine::core
    GroveEngine::impl
)

# CTest integration
grove_add_test(ThreadedModuleSystem test_threaded_module_system ${CMAKE_CURRENT_BINARY_DIR} 180)

# ThreadedModuleSystem Stress Tests
add_executable(test_threaded_stress
    integration/test_threaded_stress.cpp
)

target_link_libraries(test_threaded_stress PRIVATE
    test_helpers
    GroveEngine::core
    GroveEngine::impl
)

# CTest integration
grove_add_test(ThreadedStress test_threaded_stress ${CMAKE_CURRENT_BINARY_DIR} 180)

# IT_020: queryModule vs worker process() data race (audit #10) — in-process, no DLLs.
add_executable(IT_020_threaded_query_race
    integration/IT_020_threaded_query_race.cpp
)
target_link_libraries(IT_020_threaded_query_race PRIVATE
    test_helpers
    GroveEngine::core
    GroveEngine::impl
    Catch2::Catch2WithMain
)
grove_add_test(ThreadedQueryRace IT_020_threaded_query_race ${CMAKE_CURRENT_BINARY_DIR} 60)

# ThreadedModuleSystem Real Module Integration Test
add_executable(test_threaded_real_modules
    integration/test_threaded_real_modules.cpp
)

target_link_libraries(test_threaded_real_modules PRIVATE
    test_helpers
    GroveEngine::core
    GroveEngine::impl
)

# Note: This test requires BgfxRenderer and UIModule to be built
# It will run manually, not automatically via CTest
message(STATUS "Real module test 'test_threaded_real_modules' enabled (requires BgfxRenderer+UIModule)")

# ThreadedModuleSystem Simple Real-World Test (no DLL loading)
add_executable(test_threaded_simple_real
    integration/test_threaded_simple_real.cpp
)

target_link_libraries(test_threaded_simple_real PRIVATE
    test_helpers
    GroveEngine::core
    GroveEngine::impl
    stillhammer_logger
)

# This test can be added to CTest as it doesn't require external modules
grove_add_test(ThreadedSimpleReal test_threaded_simple_real ${CMAKE_CURRENT_BINARY_DIR} 120)

# Logger Thread-Safety Test
add_executable(test_logger_threadsafe
    integration/test_logger_threadsafe.cpp
)

target_link_libraries(test_logger_threadsafe PRIVATE
    stillhammer_logger
)

grove_add_test(LoggerThreadSafe test_logger_threadsafe ${CMAKE_CURRENT_BINARY_DIR})

# ChaosModule pour tests de robustesse
add_library(ChaosModule SHARED
    modules/ChaosModule.cpp
)

target_link_libraries(ChaosModule PRIVATE
    GroveEngine::core
    GroveEngine::impl
    spdlog::spdlog
)

# Test 02: Chaos Monkey
add_executable(test_02_chaos_monkey
    integration/test_02_chaos_monkey.cpp
)

target_link_libraries(test_02_chaos_monkey PRIVATE
    test_helpers
    GroveEngine::core
    GroveEngine::impl
)

add_dependencies(test_02_chaos_monkey ChaosModule)

# CTest integration
grove_add_test(ChaosMonkey test_02_chaos_monkey ${CMAKE_CURRENT_BINARY_DIR})

# Reload-after-throw safety: deterministic repro + regression lock for the
# cross-DLL exception-object UAF that makes ChaosMonkey crash intermittently.
# Reuses ChaosModule (it throws from inside its DLL).
add_executable(test_reload_after_throw
    integration/test_reload_after_throw.cpp
)

target_link_libraries(test_reload_after_throw PRIVATE
    GroveEngine::core
    GroveEngine::impl
    spdlog::spdlog
)

add_dependencies(test_reload_after_throw ChaosModule)

grove_add_test(ReloadAfterThrow test_reload_after_throw ${CMAKE_CURRENT_BINARY_DIR} 180)

# StressModule pour tests de stabilit� long-terme
add_library(StressModule SHARED
    modules/StressModule.cpp
)

target_link_libraries(StressModule PRIVATE
    GroveEngine::core
    GroveEngine::impl
    spdlog::spdlog
)

# Test 03: Stress Test - 10 minutes stability
add_executable(test_03_stress_test
    integration/test_03_stress_test.cpp
)

target_link_libraries(test_03_stress_test PRIVATE
    test_helpers
    GroveEngine::core
    GroveEngine::impl
)

add_dependencies(test_03_stress_test StressModule)

# CTest integration
grove_add_test(StressTest test_03_stress_test ${CMAKE_CURRENT_BINARY_DIR})

# Test 04: Race Condition Hunter - Concurrent compilation & reload
add_executable(test_04_race_condition
    integration/test_04_race_condition.cpp
)

target_link_libraries(test_04_race_condition PRIVATE
    test_helpers
    GroveEngine::core
    GroveEngine::impl
)

# This test uses TestModule (not TankModule)
add_dependencies(test_04_race_condition TestModule)

# CTest integration
# WHY 180s timeout: The test runs 10 compilations at 2s intervals (~20s base),
# but each ninja invocation can take 5-15s on Windows (MinGW + incremental build).
# 10 compilations x 15s worst-case + 60s AutoCompiler per-build timeout guard
# = ~210s absolute worst case. 180s is generous for normal CI machines while
# still preventing indefinite hangs. The AutoCompiler itself enforces a 60s
# per-build timeout via CreateProcess + WaitForSingleObject on Windows.
grove_add_test(RaceConditionHunter test_04_race_condition ${CMAKE_CURRENT_BINARY_DIR} 180)

# LeakTestModule pour memory leak detection
add_library(LeakTestModule SHARED
    modules/LeakTestModule.cpp
)

target_link_libraries(LeakTestModule PRIVATE
    GroveEngine::core
    GroveEngine::impl
)

# Test 05: Memory Leak Hunter - 200 reload cycles
add_executable(test_05_memory_leak
    integration/test_05_memory_leak.cpp
)

target_link_libraries(test_05_memory_leak PRIVATE
    test_helpers
    GroveEngine::core
    GroveEngine::impl
)

add_dependencies(test_05_memory_leak LeakTestModule)

# CTest integration
# 240s timeout: the test runs TOTAL_RELOADS=200 cycles at RELOAD_INTERVAL_MS=500
# (≈100s minimum by design, self-capped at MAX_TEST_TIME_SECONDS=180). The previous
# default of 90s was always too short — it only "passed" because the test crashed
# before reaching it; with the dangling-state SIGSEGV fixed the full run surfaces.
grove_add_test(MemoryLeakHunter test_05_memory_leak ${CMAKE_CURRENT_BINARY_DIR} 240)

# Memory leak profiler (detailed analysis)
# TODO: Implement profile_memory_leak.cpp
# add_executable(profile_memory_leak
#     profile_memory_leak.cpp
# )
#
# target_link_libraries(profile_memory_leak PRIVATE
#     test_helpers
#     GroveEngine::core
#     GroveEngine::impl
# )
#
# add_dependencies(profile_memory_leak LeakTestModule)

# ErrorRecoveryModule pour test de recovery automatique
add_library(ErrorRecoveryModule SHARED
    modules/ErrorRecoveryModule.cpp
)

target_link_libraries(ErrorRecoveryModule PRIVATE
    GroveEngine::core
    GroveEngine::impl
    spdlog::spdlog
)

# Test 06: Error Recovery - Crash detection & auto-recovery
add_executable(test_06_error_recovery
    integration/test_06_error_recovery.cpp
)

target_link_libraries(test_06_error_recovery PRIVATE
    test_helpers
    GroveEngine::core
    GroveEngine::impl
)

add_dependencies(test_06_error_recovery ErrorRecoveryModule)

# CTest integration
grove_add_test(ErrorRecovery test_06_error_recovery ${CMAKE_CURRENT_BINARY_DIR})

# HeavyStateModule pour tests de limites
add_library(HeavyStateModule SHARED
    modules/HeavyStateModule.cpp
)

target_link_libraries(HeavyStateModule PRIVATE
    GroveEngine::core
    GroveEngine::impl
    spdlog::spdlog
)

# Test 07: Limite Tests - Large state, timeouts, corruption detection
add_executable(test_07_limits
    integration/test_07_limits.cpp
)

target_link_libraries(test_07_limits PRIVATE
    test_helpers
    GroveEngine::core
    GroveEngine::impl
)

add_dependencies(test_07_limits HeavyStateModule)

# Test 12: DataNode Integration Test
add_executable(test_12_datanode
    integration/test_12_datanode.cpp
)

target_link_libraries(test_12_datanode PRIVATE
    test_helpers
    GroveEngine::core
    GroveEngine::impl
)

# Backpressure regression test (IIO queue bounding)
add_executable(test_iio_backpressure
    integration/test_iio_backpressure.cpp
)
target_link_libraries(test_iio_backpressure PRIVATE
    GroveEngine::core
    GroveEngine::impl
    Catch2::Catch2WithMain
)

# CTest integration
grove_add_test(LimitsTest test_07_limits ${CMAKE_CURRENT_BINARY_DIR})
grove_add_test(DataNodeTest test_12_datanode ${CMAKE_CURRENT_BINARY_DIR})
grove_add_test(IIOBackpressure test_iio_backpressure ${CMAKE_CURRENT_BINARY_DIR})

# ActionMap helper (grove::input): semantic input bindings, scancode-based (layout-proof).
# Header-only, std-only — no SDL, no bgfx, no IIO. Just the header + Catch2.
add_executable(test_action_map unit/test_action_map.cpp)
target_include_directories(test_action_map PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../modules/InputModule
    ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_action_map PRIVATE Catch2::Catch2WithMain)
grove_add_test(ActionMapUnit test_action_map ${CMAKE_CURRENT_BINARY_DIR})

# Asset system phase 1: AssetManager (registry + on-demand load + priority/LRU eviction under a VRAM budget).
# Header-only, std-only — driven by a mock texture provider, so it builds + tests WITHOUT bgfx.
add_executable(test_asset_manager unit/test_asset_manager.cpp)
target_include_directories(test_asset_manager PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../modules/BgfxRenderer
    ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_asset_manager PRIVATE Catch2::Catch2WithMain)
grove_add_test(AssetManagerUnit test_asset_manager ${CMAKE_CURRENT_BINARY_DIR})

# Atlas bin-packing (phase 2b): shelfPack. Header-only, std-only — no images/GPU.
add_executable(test_shelf_packer unit/test_shelf_packer.cpp)
target_include_directories(test_shelf_packer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../modules/BgfxRenderer
    ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_shelf_packer PRIVATE Catch2::Catch2WithMain)
grove_add_test(ShelfPackerUnit test_shelf_packer ${CMAKE_CURRENT_BINARY_DIR})

# Async load state machine (phase 3): placeholder-while-decoding, no duplicate requests, upload-on-pump,
# failure latch. Header-only, std-only — mock provider + mock decoder, no threads/bgfx.
add_executable(test_asset_async unit/test_asset_async.cpp)
target_include_directories(test_asset_async PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../modules/BgfxRenderer
    ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_asset_async PRIVATE Catch2::Catch2WithMain)
grove_add_test(AssetAsyncUnit test_asset_async ${CMAKE_CURRENT_BINARY_DIR})

# Animation system slice 1: Transform2D + Hierarchy (grove::anim). Header-only, std-only —
# in include/grove/anim, already on the public include path.
add_executable(test_transform2d unit/test_transform2d.cpp)
target_include_directories(test_transform2d PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_transform2d PRIVATE Catch2::Catch2WithMain)
grove_add_test(Transform2DUnit test_transform2d ${CMAKE_CURRENT_BINARY_DIR})

# Animation system slice 2a: Easing curves (grove::anim). Header-only, std-only, reusable.
add_executable(test_easing unit/test_easing.cpp)
target_include_directories(test_easing PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_easing PRIVATE Catch2::Catch2WithMain)
grove_add_test(EasingUnit test_easing ${CMAKE_CURRENT_BINARY_DIR})

# Animation system slice 2b: Keyframe/Track/Clip sampling (grove::anim). Header-only, std-only.
add_executable(test_clip unit/test_clip.cpp)
target_include_directories(test_clip PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_clip PRIVATE Catch2::Catch2WithMain)
grove_add_test(ClipUnit test_clip ${CMAKE_CURRENT_BINARY_DIR})

# Animation system slice 3: AnimationPlayer (grove::anim). Header-only, std-only.
add_executable(test_animation_player unit/test_animation_player.cpp)
target_include_directories(test_animation_player PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_animation_player PRIVATE Catch2::Catch2WithMain)
grove_add_test(AnimationPlayerUnit test_animation_player ${CMAKE_CURRENT_BINARY_DIR})

# Animation system flipbook slice F-a: SpriteSheet UV grid (grove::anim). Header-only, std-only.
add_executable(test_sprite_sheet unit/test_sprite_sheet.cpp)
target_include_directories(test_sprite_sheet PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_sprite_sheet PRIVATE Catch2::Catch2WithMain)
grove_add_test(SpriteSheetUnit test_sprite_sheet ${CMAKE_CURRENT_BINARY_DIR})

# Animation system flipbook slice F-b: Flipbook frame timing (grove::anim). Header-only, std-only.
add_executable(test_flipbook unit/test_flipbook.cpp)
target_include_directories(test_flipbook PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_flipbook PRIVATE Catch2::Catch2WithMain)
grove_add_test(FlipbookUnit test_flipbook ${CMAKE_CURRENT_BINARY_DIR})

# Map-viewer engine slice S0a: world-document format — pure bit codec + sparse chunk (de)serialize
# (grove::mapview, include/grove/mapview/). Header-only, std-only — no GPU/SDL/IIO, so it builds + tests
# on WSL too (folds into the sanitizer sweep later). The frozen on-disk contract that parallelizes the
# engine core (S1) and the Theomen adapter (S3). Design: docs/design/mapview.md.
add_executable(test_mapview_format unit/test_mapview_format.cpp)
target_include_directories(test_mapview_format PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_mapview_format PRIVATE Catch2::Catch2WithMain)
grove_add_test(MapViewFormatUnit test_mapview_format ${CMAKE_CURRENT_BINARY_DIR})

# PNG writer hard size limit (svpng): writeRgbaAsPng must REFUSE an over-limit image (width > 21844 / 32-bit
# IDAT overflow) instead of writing a corrupt file — the guard behind --poster. Header-only (PngCapture.h under
# tests/visual), no GPU, so it folds into the sanitizer lenses.
add_executable(test_png_limit integration/test_png_limit.cpp)
target_include_directories(test_png_limit PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/visual
    ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_png_limit PRIVATE Catch2::Catch2WithMain)
grove_add_test(PngLimitUnit test_png_limit ${CMAKE_CURRENT_BINARY_DIR})

# Map-viewer engine slice S0b: per-chunk compression (zlib via the vendored miniz). The Compressor is
# INJECTED so the format core stays dependency-free — only THIS target compiles miniz.c. Locks: compressed
# round-trips identically to raw, and every failure (no decompressor, corrupt/truncated payload) fails franc.
add_executable(test_mapview_compress
    unit/test_mapview_compress.cpp
    ${GROVE_MINIZ_DIR}/miniz.c)
target_include_directories(test_mapview_compress PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${GROVE_MINIZ_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_mapview_compress PRIVATE Catch2::Catch2WithMain)
grove_add_test(MapViewCompressUnit test_mapview_compress ${CMAKE_CURRENT_BINARY_DIR})

# Map-viewer engine slice S0c: disk + JSON manifest. Closes S0 ("format + reader") — manifest round-trips
# through nlohmann (header-only, vendored) and a full document (manifest + sparse chunk blobs, raw and
# zlib) round-trips through a real temp dir. Links miniz.c for the compressed-on-disk case. This is the
# contract the Theomen adapter (S3) writes and the viewer app (S2) reads.
add_executable(test_mapview_disk
    unit/test_mapview_disk.cpp
    ${GROVE_MINIZ_DIR}/miniz.c)
target_include_directories(test_mapview_disk PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${GROVE_NLOHMANN_DIR}
    ${GROVE_MINIZ_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_mapview_disk PRIVATE Catch2::Catch2WithMain)
grove_add_test(MapViewDiskUnit test_mapview_disk ${CMAKE_CURRENT_BINARY_DIR})

# Map-viewer world-document VALIDATOR (grove::mapview::WorldCheck). A headless semantic checker: it catches
# the producer mistakes the reader accepts SILENTLY — inverted bounds, cellCount != chunkDims, a Uint/Int
# field with bits=0, dead chunks outside the world, NaN floats — none of which the codec's fail-franc guards
# reject. Pure checks are unit-locked in RAM; a disk E2E drives checkWorldDir through a temp dir (raw + zlib
# + a corrupt case). This is the DETERMINISTIC proof a `.world` is correct without launching the GPU viewer —
# the S3 support tool that de-risks the Theomen adapter. Links miniz.c + nlohmann like the disk test.
add_executable(test_mapview_worldcheck
    unit/test_mapview_worldcheck.cpp
    ${GROVE_MINIZ_DIR}/miniz.c)
target_include_directories(test_mapview_worldcheck PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${GROVE_NLOHMANN_DIR}
    ${GROVE_MINIZ_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_mapview_worldcheck PRIVATE Catch2::Catch2WithMain)
grove_add_test(MapViewWorldCheckUnit test_mapview_worldcheck ${CMAKE_CURRENT_BINARY_DIR})

# The productized CLI wrapping the same validator: `worldcheck <dir> [--strict] [--json]`. Headless
# (std + miniz + nlohmann, no GPU/SDL) so a producer or CI can run it anywhere to prove a `.world` is
# well-formed; exit 0 = ok, 1 = errors (or warnings under --strict), 2 = usage. Not a ctest (it needs a
# world dir as input) — a hand-run/CI tool, built to build/tests/worldcheck.
add_executable(worldcheck
    tools/worldcheck.cpp
    ${GROVE_MINIZ_DIR}/miniz.c)
target_include_directories(worldcheck PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${GROVE_NLOHMANN_DIR}
    ${GROVE_MINIZ_DIR})

# Map-viewer engine S0 hardening: save/load ROUND-TRIP ROBUSTNESS. Runs save->load x3 in a row across three
# distinct config "formats" (chunk size, field count, byte-unaligned bit widths, Z>1, sparsity) on every
# transport (RAM raw, RAM zlib, disk raw, disk zlib), asserting binary idempotence + value fidelity vs the
# original each iteration. Catches "works once, drifts later" and "works at 4x4, breaks at 17 bits".
# Regression lock — re-run any time to re-verify the format after a change.
add_executable(test_mapview_roundtrip
    unit/test_mapview_roundtrip.cpp
    ${GROVE_MINIZ_DIR}/miniz.c)
target_include_directories(test_mapview_roundtrip PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${GROVE_NLOHMANN_DIR}
    ${GROVE_MINIZ_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_mapview_roundtrip PRIVATE Catch2::Catch2WithMain)
grove_add_test(MapViewRoundtripUnit test_mapview_roundtrip ${CMAKE_CURRENT_BINARY_DIR})

# Map-viewer engine slice S1a: pure geometry — SquareLayout (① topology) + TopDownProjection (② projection),
# usable through the IGridLayout/IProjection interfaces (hex/iso plug in later, no rework). Header-only,
# std-only — the renderer-independent foundation of the MapView core (docs/design/mapview.md §2/§4).
add_executable(test_mapview_layout unit/test_mapview_layout.cpp)
target_include_directories(test_mapview_layout PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_mapview_layout PRIVATE Catch2::Catch2WithMain)
grove_add_test(MapViewLayoutUnit test_mapview_layout ${CMAKE_CURRENT_BINARY_DIR})

# Map-viewer engine slice S1b: streaming — chunksInViewport (cull) + ChunkCache (LRU eviction under a budget,
# fed by the injected IChunkProvider). Header-only, std-only — proven headless with a mock provider that
# counts load() calls (no-reload + never-evict-visible invariants). docs/design/mapview.md §4.
add_executable(test_mapview_streaming unit/test_mapview_streaming.cpp)
target_include_directories(test_mapview_streaming PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_mapview_streaming PRIVATE Catch2::Catch2WithMain)
grove_add_test(MapViewStreamingUnit test_mapview_streaming ${CMAKE_CURRENT_BINARY_DIR})

# Map-viewer engine slice S1c: the recipe bricks — Palette (value->Rgba: ramp/banded/categorical) + Filter
# (minimal composable predicate value->bool). The modular colour/filter of §5. Header-only, std-only.
add_executable(test_mapview_recipe unit/test_mapview_recipe.cpp)
target_include_directories(test_mapview_recipe PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_mapview_recipe PRIVATE Catch2::Catch2WithMain)
grove_add_test(MapViewRecipeUnit test_mapview_recipe ${CMAKE_CURRENT_BINARY_DIR})

# Map-viewer engine slice S1d: the MapView orchestrator (the S1 capstone) — cull -> stream -> compile each
# visible cell through the active Lens -> drainCells(CellDraw). Ties S1a/b/c together; decoupled from
# Manifest/JSON (takes a plain schema + GridSpec). Header-only, std-only — full pipeline E2E, headless.
add_executable(test_mapview_core unit/test_mapview_core.cpp)
target_include_directories(test_mapview_core PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_mapview_core PRIVATE Catch2::Catch2WithMain)
grove_add_test(MapViewCoreUnit test_mapview_core ${CMAKE_CURRENT_BINARY_DIR})

# Map-viewer engine slice S1e: Hillshade — fake-3D relief from a field gradient (Lambertian illumination
# factor), the §5 colour modifier. Header-only, std-only. Pure shading math, locked before integration.
add_executable(test_mapview_hillshade unit/test_mapview_hillshade.cpp)
target_include_directories(test_mapview_hillshade PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_mapview_hillshade PRIVATE Catch2::Catch2WithMain)
grove_add_test(MapViewHillshadeUnit test_mapview_hillshade ${CMAKE_CURRENT_BINARY_DIR})

# Map-viewer engine S1 hardening: one regression lock per real bug from the adversarial review
# (cull zoom-out overflow/UB, cull degenerate cell size, cache exception-safety, hillshade NaN/Inf,
# palette NaN->fallback, worldToCell fractional-boundary round-trip, MapView schema ownership + cellW guard).
add_executable(test_mapview_hardening unit/test_mapview_hardening.cpp)
target_include_directories(test_mapview_hardening PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_mapview_hardening PRIVATE Catch2::Catch2WithMain)
grove_add_test(MapViewHardeningUnit test_mapview_hardening ${CMAKE_CURRENT_BINARY_DIR})

# Map-viewer engine slice S1i: region & marker overlay layers (the §5 "regionSet × style" / "markerSet ×
# icons"). Global vector sets, culled + styled by the viewport, emitting neutral RegionDraw (render:sector)
# and MarkerDraw (render:sprite). Header-only, std-only — what makes a lens show a worldgen's structure.
add_executable(test_mapview_overlays unit/test_mapview_overlays.cpp)
target_include_directories(test_mapview_overlays PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_mapview_overlays PRIVATE Catch2::Catch2WithMain)
grove_add_test(MapViewOverlaysUnit test_mapview_overlays ${CMAKE_CURRENT_BINARY_DIR})

# Map-viewer P1: the CellDraw -> SpriteInstance adapter (modules/BgfxRenderer/MapView/SpriteAdapter.h) — the
# one renderer-coupled bridge. Headless: FramePacket.h (SpriteInstance) is a plain struct, no GPU needed.
add_executable(test_mapview_adapter unit/test_mapview_adapter.cpp)
target_include_directories(test_mapview_adapter PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../modules/BgfxRenderer
    ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_mapview_adapter PRIVATE Catch2::Catch2WithMain)
grove_add_test(MapViewAdapterUnit test_mapview_adapter ${CMAKE_CURRENT_BINARY_DIR})

# Map-viewer disk↔core bridge: WorldDocumentProvider (include/grove/mapview/WorldDocumentProvider.h) — the
# file-backed IChunkProvider. Drives a MapView entirely from a world-document on disk (the "file is the
# interface" seam, proven E2E in a unit test). Links miniz.c + nlohmann like the disk test (compressed case).
add_executable(test_mapview_provider
    unit/test_mapview_provider.cpp
    ${GROVE_MINIZ_DIR}/miniz.c)
target_include_directories(test_mapview_provider PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${GROVE_NLOHMANN_DIR}
    ${GROVE_MINIZ_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_mapview_provider PRIVATE Catch2::Catch2WithMain)
grove_add_test(MapViewProviderUnit test_mapview_provider ${CMAKE_CURRENT_BINARY_DIR})

# Map-viewer tiling path (T3): TileMapper (value->tile-id band mapper, the recipe counterpart of Palette) +
# MapView's tile-chunk emit (a TileLayer -> one TileChunkDraw per visible chunk). Productizes what was a
# demo-local elevToTile into the core. Header-only, std-only — drives a MapView headless, asserts exact ids.
add_executable(test_mapview_tilemapper unit/test_mapview_tilemapper.cpp)
target_include_directories(test_mapview_tilemapper PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_mapview_tilemapper PRIVATE Catch2::Catch2WithMain)
grove_add_test(MapViewTileMapperUnit test_mapview_tilemapper ${CMAKE_CURRENT_BINARY_DIR})

# Map-viewer live-tiling brick (T4): TileChunkStreamer — diffs successive visible tile-chunk sets into
# retained render:tilemap:add/remove deltas (the enter/leave bookkeeping that makes tiling work as the camera
# pans). Pure logic, no GPU — the hard part of live tiling, proven headless. Header-only, std-only.
add_executable(test_mapview_tilestreamer unit/test_mapview_tilestreamer.cpp)
target_include_directories(test_mapview_tilestreamer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_mapview_tilestreamer PRIVATE Catch2::Catch2WithMain)
grove_add_test(MapViewTileStreamerUnit test_mapview_tilestreamer ${CMAKE_CURRENT_BINARY_DIR})

# Sound system slice 1: SoundManagerModule topic/bus logic via MockSoundBackend. No SDL_mixer —
# the real SDLMixerBackend + audio is slice 2. Compiles the module + the test only.
add_executable(test_sound_manager
    unit/test_sound_manager.cpp
    ../modules/SoundManager/SoundManagerModule.cpp)
target_include_directories(test_sound_manager PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../modules
    ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_sound_manager PRIVATE GroveEngine::impl Catch2::Catch2WithMain)
grove_add_test(SoundManagerUnit test_sound_manager ${CMAKE_CURRENT_BINARY_DIR})

# Sound system slice 2: real SDLMixerBackend + manual audio demo (verified by ear). Gated on
# SDL2 + SDL2_mixer being available. Not a ctest (no headless audio assertion).
if(SDL2_FOUND AND SDL2_mixer_FOUND)
    add_executable(test_sound_demo
        visual/test_sound_demo.cpp
        ../modules/SoundManager/SoundManagerModule.cpp
        ../modules/SoundManager/SDLMixerBackend.cpp)
    target_include_directories(test_sound_demo PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../modules
        ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(test_sound_demo PRIVATE
        GroveEngine::impl
        SDL2::SDL2
        SDL2_mixer::SDL2_mixer)
    message(STATUS "Sound demo 'test_sound_demo' enabled (SDL2_mixer found)")

    # Adaptive-audio demo (slices 1-3): synth stems + audio:* layering/quantized-cue/leitmotif.
    # Verified by ear (no headless audio assertion) — the logic is locked by SoundManagerUnit.
    add_executable(test_adaptive_demo
        visual/test_adaptive_demo.cpp
        ../modules/SoundManager/SoundManagerModule.cpp
        ../modules/SoundManager/SDLMixerBackend.cpp)
    target_include_directories(test_adaptive_demo PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../modules
        ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(test_adaptive_demo PRIVATE
        GroveEngine::impl
        SDL2::SDL2
        SDL2_mixer::SDL2_mixer)
    message(STATUS "Adaptive-audio demo 'test_adaptive_demo' enabled (SDL2_mixer found)")
else()
    message(STATUS "Sound demos skipped (SDL2_mixer not found)")
endif()

# ConfigWatcherModule for cross-system integration tests
add_library(ConfigWatcherModule SHARED
    modules/ConfigWatcherModule.cpp
)

target_link_libraries(ConfigWatcherModule PRIVATE
    GroveEngine::core
    GroveEngine::impl
    spdlog::spdlog
)

# PlayerModule for cross-system integration tests
add_library(PlayerModule SHARED
    modules/PlayerModule.cpp
)

target_link_libraries(PlayerModule PRIVATE
    GroveEngine::core
    GroveEngine::impl
    spdlog::spdlog
)

# EconomyModule for cross-system integration tests
add_library(EconomyModule SHARED
    modules/EconomyModule.cpp
)

target_link_libraries(EconomyModule PRIVATE
    GroveEngine::core
    GroveEngine::impl
    spdlog::spdlog
)

# MetricsModule for cross-system integration tests
add_library(MetricsModule SHARED
    modules/MetricsModule.cpp
)

target_link_libraries(MetricsModule PRIVATE
    GroveEngine::core
    GroveEngine::impl
    spdlog::spdlog
)

# Test 13: Cross-System Integration (IO + DataNode)
add_executable(test_13_cross_system
    integration/test_13_cross_system.cpp
)

target_link_libraries(test_13_cross_system PRIVATE
    test_helpers
    GroveEngine::core
    GroveEngine::impl
)

add_dependencies(test_13_cross_system
    ConfigWatcherModule
    PlayerModule
    EconomyModule
    MetricsModule
)

# CTest integration
grove_add_test(CrossSystemIntegration test_13_cross_system ${CMAKE_CURRENT_BINARY_DIR})

# ConfigurableModule pour tests de config hot-reload
add_library(ConfigurableModule SHARED
    modules/ConfigurableModule.cpp
)

target_link_libraries(ConfigurableModule PRIVATE
    GroveEngine::core
    GroveEngine::impl
    spdlog::spdlog
)

# Test 08: Config Hot-Reload - Runtime config changes without code reload
add_executable(test_08_config_hotreload
    integration/test_08_config_hotreload.cpp
)

target_link_libraries(test_08_config_hotreload PRIVATE
    test_helpers
    GroveEngine::core
    GroveEngine::impl
)

add_dependencies(test_08_config_hotreload ConfigurableModule)

# CTest integration
grove_add_test(ConfigHotReload test_08_config_hotreload ${CMAKE_CURRENT_BINARY_DIR})

# BaseModule for dependency testing (no dependencies)
add_library(BaseModule SHARED
    modules/BaseModule.cpp
)

target_link_libraries(BaseModule PRIVATE
    GroveEngine::core
    GroveEngine::impl
    spdlog::spdlog
)

# DependentModule for dependency testing (depends on BaseModule)
add_library(DependentModule SHARED
    modules/DependentModule.cpp
)

target_link_libraries(DependentModule PRIVATE
    GroveEngine::core
    GroveEngine::impl
    spdlog::spdlog
)

# IndependentModule for dependency testing (isolated witness)
add_library(IndependentModule SHARED
    modules/IndependentModule.cpp
)

target_link_libraries(IndependentModule PRIVATE
    GroveEngine::core
    GroveEngine::impl
    spdlog::spdlog
)

# Test 09: Module Dependencies - Cascade reload, unload protection, cycle detection
add_executable(test_09_module_dependencies
    integration/test_09_module_dependencies.cpp
)

target_link_libraries(test_09_module_dependencies PRIVATE
    test_helpers
    GroveEngine::core
    GroveEngine::impl
)

add_dependencies(test_09_module_dependencies BaseModule DependentModule IndependentModule)

# CTest integration
grove_add_test(ModuleDependencies test_09_module_dependencies ${CMAKE_CURRENT_BINARY_DIR})

# GameLogicModuleV1 for multi-version testing (baseline version)
add_library(GameLogicModuleV1 SHARED
    modules/GameLogicModuleV1.cpp
)

target_link_libraries(GameLogicModuleV1 PRIVATE
    GroveEngine::core
    GroveEngine::impl
    spdlog::spdlog
)

# GameLogicModuleV2 for multi-version testing (with collision detection)
add_library(GameLogicModuleV2 SHARED
    modules/GameLogicModuleV2.cpp
)

target_link_libraries(GameLogicModuleV2 PRIVATE
    GroveEngine::core
    GroveEngine::impl
    spdlog::spdlog
)

# GameLogicModuleV3 for multi-version testing (with advanced physics)
add_library(GameLogicModuleV3 SHARED
    modules/GameLogicModuleV3.cpp
)

target_link_libraries(GameLogicModuleV3 PRIVATE
    GroveEngine::core
    GroveEngine::impl
    spdlog::spdlog
)

# Test 10: Multi-Version Coexistence - Canary deployment, progressive migration, rollback
add_executable(test_10_multiversion_coexistence
    integration/test_10_multiversion_coexistence.cpp
)

target_link_libraries(test_10_multiversion_coexistence PRIVATE
    test_helpers
    GroveEngine::core
    GroveEngine::impl
)

add_dependencies(test_10_multiversion_coexistence GameLogicModuleV1 GameLogicModuleV2 GameLogicModuleV3)

# CTest integration
grove_add_test(MultiVersionCoexistence test_10_multiversion_coexistence ${CMAKE_CURRENT_BINARY_DIR})

# ================================================================================
# IO System Test Modules (Scenario 11)
# ================================================================================

# ProducerModule for IO testing
add_library(ProducerModule SHARED
    modules/ProducerModule.cpp
)

target_link_libraries(ProducerModule PRIVATE
    GroveEngine::core
    GroveEngine::impl
    spdlog::spdlog
)

# ConsumerModule for IO testing
add_library(ConsumerModule SHARED
    modules/ConsumerModule.cpp
)

target_link_libraries(ConsumerModule PRIVATE
    GroveEngine::core
    GroveEngine::impl
    spdlog::spdlog
)

# BroadcastModule for IO testing
add_library(BroadcastModule SHARED
    modules/BroadcastModule.cpp
)

target_link_libraries(BroadcastModule PRIVATE
    GroveEngine::core
    GroveEngine::impl
    spdlog::spdlog
)

# BatchModule for IO testing
add_library(BatchModule SHARED
    modules/BatchModule.cpp
)

target_link_libraries(BatchModule PRIVATE
    GroveEngine::core
    GroveEngine::impl
    spdlog::spdlog
)

# IOStressModule for IO testing
add_library(IOStressModule SHARED
    modules/IOStressModule.cpp
)

target_link_libraries(IOStressModule PRIVATE
    GroveEngine::core
    GroveEngine::impl
    spdlog::spdlog
)

# Test 11: IO System Stress Test - IntraIO pub/sub validation
add_executable(test_11_io_system
    integration/test_11_io_system.cpp
)

target_link_libraries(test_11_io_system PRIVATE
    test_helpers
    GroveEngine::core
    GroveEngine::impl
)

add_dependencies(test_11_io_system ProducerModule ConsumerModule BroadcastModule BatchModule IOStressModule)

# CTest integration
grove_add_test(IOSystemStress test_11_io_system ${CMAKE_CURRENT_BINARY_DIR})

# Static engine hosting — IEngine::registerStaticModule + setConfiguration wiring +
# per-module IIO pump. Acceptance lock for the static-linked-game use case: 3
# already-instantiated modules talk to each other THROUGH the engine, driven by
# engine.step(dt) only (no .so, no manual process(), no manual IIO routing).
# In-process, Catch2, no DLLs.
add_executable(test_static_engine_hosting
    integration/test_static_engine_hosting.cpp
)
target_link_libraries(test_static_engine_hosting PRIVATE
    GroveEngine::core
    GroveEngine::impl
    Catch2::Catch2WithMain
)
grove_add_test(StaticEngineHosting test_static_engine_hosting ${CMAKE_CURRENT_BINARY_DIR})

# grove::EngineClock — the engine's authoritative fixed-timestep clock (grove/EngineClock.h).
# Pure, header-only, no engine link: turns real frame deltas into deterministic sim ticks
# (tick / simTime = tick*dt / realTime) plus pause / slow-mo / fast-forward. Part 1 of the
# IO contract build order (docs/design/iio-contract.md). Objective oracle test, no GPU/IIO.
add_executable(test_engine_clock unit/test_engine_clock.cpp)
target_include_directories(test_engine_clock PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../include)
target_link_libraries(test_engine_clock PRIVATE Catch2::Catch2WithMain)
grove_add_test(EngineClockUnit test_engine_clock ${CMAKE_CURRENT_BINARY_DIR})

# EngineClock hosting E2E (IO contract 1b + 1c): a static module reads the engine's clock
# (injected via setClock) while the host drives step() + pause/slow-mo via engine.clock().
# Proves the clock is WIRED, not just unit-correct. In-process, Catch2, no GPU/SDL/.so.
add_executable(test_engine_clock_hosting integration/test_engine_clock_hosting.cpp)
target_link_libraries(test_engine_clock_hosting PRIVATE
    GroveEngine::core
    GroveEngine::impl
    Catch2::Catch2WithMain
)
grove_add_test(EngineClockHosting test_engine_clock_hosting ${CMAKE_CURRENT_BINARY_DIR})

# grove::LamportClock — logical clock for causal ordering (grove/LamportClock.h). Pure,
# header-only, no engine link. The causality primitive of the message envelope (IO contract
# §5, part 2a). Objective oracle test (tick / receive-rule max+1 / monotonicity / causality).
add_executable(test_lamport_clock unit/test_lamport_clock.cpp)
target_include_directories(test_lamport_clock PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../include)
target_link_libraries(test_lamport_clock PRIVATE Catch2::Catch2WithMain)
grove_add_test(LamportClockUnit test_lamport_clock ${CMAKE_CURRENT_BINARY_DIR})

# grove::ReplaySink — the structured replay log (grove/ReplaySink.h). Pure, header-only: an opt-in,
# bounded (drop-oldest), thread-safe sink over the stamped control-plane stream, with the two §8 views
# (per-source filter + merge-sorted canonical timeline). Part 3 of the IO contract build order
# (docs/design/iio-contract.md §8). Objective oracle test incl. a concurrent-record thread-safety case.
add_executable(test_replay_sink unit/test_replay_sink.cpp)
target_include_directories(test_replay_sink PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../include)
target_link_libraries(test_replay_sink PRIVATE Catch2::Catch2WithMain)
if(NOT WIN32)
    target_link_libraries(test_replay_sink PRIVATE pthread)  # std::thread on Linux/WSL (MinGW gets it via winpthread)
endif()
grove_add_test(ReplaySinkUnit test_replay_sink ${CMAKE_CURRENT_BINARY_DIR})

# Message envelope E2E (IO contract §5, part 2b/2c): the transport stamps source/seq/lamport/
# tick/simTime on delivered messages, with Lamport causality across a publish chain, and the
# engine's step() wire pushes the tick the envelope is stamped from. In-process, Catch2, no GPU.
add_executable(test_message_envelope integration/test_message_envelope.cpp)
target_link_libraries(test_message_envelope PRIVATE
    GroveEngine::core
    GroveEngine::impl
    Catch2::Catch2WithMain
)
grove_add_test(MessageEnvelope test_message_envelope ${CMAKE_CURRENT_BINARY_DIR})

# Structured replay sink E2E (IO contract §8, part 3): the IntraIOManager taps its ReplaySink at
# routeMessage, so an enabled sink captures the real stamped stream (publish -> route). Proves the tap is
# WIRED (envelope completed, two query views, opt-in, bounded) — not just the pure ring (ReplaySinkUnit).
add_executable(test_replay_sink_capture integration/test_replay_sink_capture.cpp)
target_link_libraries(test_replay_sink_capture PRIVATE
    GroveEngine::core
    GroveEngine::impl
    Catch2::Catch2WithMain
)
grove_add_test(ReplaySinkCapture test_replay_sink_capture ${CMAKE_CURRENT_BINARY_DIR})

# Per-topic backpressure policy (IO contract §9, part 4): the high-freq inbox honors DropOldest (default) /
# Coalesce (latest-wins) / Reject (protect a critical) through the real publish->route->deliver path.
add_executable(test_backpressure_policy integration/test_backpressure_policy.cpp)
target_link_libraries(test_backpressure_policy PRIVATE
    GroveEngine::core
    GroveEngine::impl
    Catch2::Catch2WithMain
)
grove_add_test(BackpressurePolicy test_backpressure_policy ${CMAKE_CURRENT_BINARY_DIR})

# JsonDataNode BINARY PAYLOAD (blobs) + throw-proof serialize() — the first-class binary that lets the bulk
# fast path stop abusing a JSON string and closes the replay/network crash on non-UTF8 bytes. Header-only-ish
# (pure JsonDataNode + nlohmann), folds into the sanitizer lenses.
add_executable(test_datanode_blob integration/test_datanode_blob.cpp)
target_link_libraries(test_datanode_blob PRIVATE
    GroveEngine::core
    GroveEngine::impl
    Catch2::Catch2WithMain
)
grove_add_test(DataNodeBlobUnit test_datanode_blob ${CMAKE_CURRENT_BINARY_DIR})

# Threaded hosting E2E: a cross-module IIO chain (Producer->Relay->Sink) hosted via
# registerStaticModule(THREADED) and driven by engine.step(). Locks the THREADED
# strategy + cross-thread IIO delivery. Plain main(), no GPU/SDL/.so/Catch2 — so it
# can also be compiled standalone under ThreadSanitizer (WSL) for the race proof.
add_executable(test_threaded_hosting_e2e
    integration/test_threaded_hosting_e2e.cpp
)
target_link_libraries(test_threaded_hosting_e2e PRIVATE
    GroveEngine::core
    GroveEngine::impl
    spdlog::spdlog
)
grove_add_test(ThreadedHostingE2E test_threaded_hosting_e2e ${CMAKE_CURRENT_BINARY_DIR} 60)

# Phase 3 slice-1 lock: ThreadPoolModuleSystem (shared pool + work-stealing) driven
# standalone. Proves correctness + work-stealing balance + real parallelism + archi-A
# inbox drain on the pool. Plain main(), no GPU/SDL/.so/Catch2 — TSan-able (WSL).
add_executable(test_threadpool_lifecycle
    integration/test_threadpool_lifecycle.cpp
)
target_link_libraries(test_threadpool_lifecycle PRIVATE
    GroveEngine::core
    GroveEngine::impl
    spdlog::spdlog
)
grove_add_test(ThreadPoolLifecycle test_threadpool_lifecycle ${CMAKE_CURRENT_BINARY_DIR} 60)

# Engine->pool TSan twin of ThreadedHostingE2E: the synthetic Producer->Relay->Sink
# chain hosted via registerStaticModule(THREAD_POOL) and driven by engine.step().
# Locks the THREAD_POOL strategy + the DebugEngine pool branch + cross-thread IIO
# delivery + archi-A drain, all recombined. Plain main(), no GPU/SDL/.so/Catch2 — so
# it compiles standalone under ThreadSanitizer (WSL) for the engine->pool race proof.
add_executable(test_pool_hosting_e2e
    integration/test_pool_hosting_e2e.cpp
)
target_link_libraries(test_pool_hosting_e2e PRIVATE
    GroveEngine::core
    GroveEngine::impl
    spdlog::spdlog
)
grove_add_test(PoolHostingE2E test_pool_hosting_e2e ${CMAKE_CURRENT_BINARY_DIR} 60)

# Engine hosting demo — human-readable companion to the test above. Hosts 3 static
# modules in a DebugEngine and narrates the inter-module flow frame by frame. Run
# manually (not a ctest — it's a demo, the test is the regression lock). No GPU/SDL/.so.
add_executable(test_engine_hosting_demo
    visual/test_engine_hosting_demo.cpp
)
target_link_libraries(test_engine_hosting_demo PRIVATE
    GroveEngine::core
    GroveEngine::impl
)
message(STATUS "Engine hosting demo 'test_engine_hosting_demo' enabled (run manually)")

# ================================================================================
# Benchmarks
# ================================================================================

# Benchmark helpers demo
add_executable(benchmark_helpers_demo
    benchmarks/benchmark_helpers_demo.cpp
)

target_include_directories(benchmark_helpers_demo PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/benchmarks
)

target_link_libraries(benchmark_helpers_demo PRIVATE
    GroveEngine::core
)

# TopicTree routing benchmark
add_executable(benchmark_topictree
    benchmarks/benchmark_topictree.cpp
)

target_include_directories(benchmark_topictree PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/benchmarks
)

target_link_libraries(benchmark_topictree PRIVATE
    GroveEngine::core
    topictree::topictree
)

# IntraIO batching benchmark
add_executable(benchmark_batching
    benchmarks/benchmark_batching.cpp
)

target_include_directories(benchmark_batching PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/benchmarks
)

target_link_libraries(benchmark_batching PRIVATE
    GroveEngine::core
    GroveEngine::impl
    topictree::topictree
)

# DataNode read-only API benchmark
add_executable(benchmark_readonly
    benchmarks/benchmark_readonly.cpp
)

target_include_directories(benchmark_readonly PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/benchmarks
)

target_link_libraries(benchmark_readonly PRIVATE
    GroveEngine::core
    GroveEngine::impl
)

# End-to-end real world benchmark
add_executable(benchmark_e2e
    benchmarks/benchmark_e2e.cpp
)

target_include_directories(benchmark_e2e PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/benchmarks
)

target_link_libraries(benchmark_e2e PRIVATE
    GroveEngine::core
    GroveEngine::impl
    topictree::topictree
)

# ThreadedModuleSystem vs SequentialModuleSystem performance comparison
add_executable(benchmark_threaded_vs_sequential
    benchmarks/benchmark_threaded_vs_sequential.cpp
)

target_include_directories(benchmark_threaded_vs_sequential PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/benchmarks
)

target_link_libraries(benchmark_threaded_vs_sequential PRIVATE
    test_helpers
    GroveEngine::core
    GroveEngine::impl
)

# ThreadedModuleSystem vs SequentialModuleSystem - REAL CPU-bound benchmark
add_executable(benchmark_threaded_vs_sequential_cpu
    benchmarks/benchmark_threaded_vs_sequential_cpu.cpp
)

target_include_directories(benchmark_threaded_vs_sequential_cpu PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/benchmarks
)

target_link_libraries(benchmark_threaded_vs_sequential_cpu PRIVATE
    test_helpers
    GroveEngine::core
    GroveEngine::impl
)

# Phase 3 slice 3: ThreadPoolModuleSystem vs ThreadedModuleSystem (vs Sequential) CPU-bound,
# swept over module count N — shows WHERE the work-stealing pool beats per-module threading.
add_executable(benchmark_pool_vs_threaded
    benchmarks/benchmark_pool_vs_threaded.cpp
)
target_link_libraries(benchmark_pool_vs_threaded PRIVATE
    GroveEngine::core
    GroveEngine::impl
)

# IIO Message Latency Benchmark (through module systems)
add_executable(benchmark_iio_latency
    benchmarks/benchmark_iio_latency.cpp
)

target_include_directories(benchmark_iio_latency PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/benchmarks
)

target_link_libraries(benchmark_iio_latency PRIVATE
    test_helpers
    GroveEngine::core
    GroveEngine::impl
)

# IIO Direct Performance Benchmark (no module system overhead)
add_executable(benchmark_iio_direct
    benchmarks/benchmark_iio_direct.cpp
)

target_include_directories(benchmark_iio_direct PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/benchmarks
)

target_link_libraries(benchmark_iio_direct PRIVATE
    test_helpers
    GroveEngine::core
    GroveEngine::impl
)

# IIO Zero-Copy Throughput Benchmark (fan-out scaling + coreResident vs re-home)
add_executable(benchmark_iio_zerocopy
    benchmarks/benchmark_iio_zerocopy.cpp
)

target_include_directories(benchmark_iio_zerocopy PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/benchmarks
)

target_link_libraries(benchmark_iio_zerocopy PRIVATE
    GroveEngine::core
    GroveEngine::impl
    topictree::topictree
)

# IIO Big-Blob Routing Benchmark (localize Drifterra's ~39ms: engine machinery vs consumer handler)
add_executable(benchmark_iio_bigblob
    benchmarks/benchmark_iio_bigblob.cpp
)

target_include_directories(benchmark_iio_bigblob PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/benchmarks
)

target_link_libraries(benchmark_iio_bigblob PRIVATE
    GroveEngine::core
    GroveEngine::impl
    topictree::topictree
)

# ================================================================================
# BgfxRenderer Tests (only if GROVE_BUILD_BGFX_RENDERER is ON)
# ================================================================================

if(GROVE_BUILD_BGFX_RENDERER)
    # Test 20: BgfxRenderer RHI Unit Tests (no window required)
    add_executable(test_20_bgfx_rhi
        integration/test_20_bgfx_rhi.cpp
        ../modules/BgfxRenderer/RHI/RHICommandBuffer.cpp
        ../modules/BgfxRenderer/Frame/FrameAllocator.cpp
    )

    target_include_directories(test_20_bgfx_rhi PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../modules/BgfxRenderer
    )

    target_link_libraries(test_20_bgfx_rhi PRIVATE
        GroveEngine::core
    )

    # CTest integration
    grove_add_test(BgfxRHI test_20_bgfx_rhi ${CMAKE_CURRENT_BINARY_DIR})

    # Test 21+: Visual Tests (requires SDL2 and display)
    if(SDL2_AVAILABLE)
        # Test 21: Visual Triangle Test
        add_executable(test_21_bgfx_triangle
            visual/test_bgfx_triangle.cpp
        )

        if(SDL2_FOUND)
            target_link_libraries(test_21_bgfx_triangle PRIVATE
                bgfx
                bx
                SDL2::SDL2main
                SDL2::SDL2
            )
        else()
            # Linux system SDL2
            target_include_directories(test_21_bgfx_triangle PRIVATE
                /usr/include/SDL2
            )
            target_link_libraries(test_21_bgfx_triangle PRIVATE
                bgfx
                bx
                SDL2
                pthread
                dl
                X11
                GL
            )
        endif()

        # Not added to CTest (requires display)
        message(STATUS "Visual test 'test_21_bgfx_triangle' enabled (run manually)")

        # Minimal bgfx test for Windows (no DLL)
        if(SDL2_FOUND)
            # WIN32 makes it a Windows app; SDL2main provides WinMain->main conversion
            add_executable(test_bgfx_minimal_win WIN32
                visual/test_bgfx_minimal_win.cpp
            )
            target_link_libraries(test_bgfx_minimal_win PRIVATE
                bgfx
                bx
                SDL2::SDL2main
                SDL2::SDL2
            )
            message(STATUS "Minimal bgfx test 'test_bgfx_minimal_win' enabled")
        endif()

        # Test 22: Sprite Integration Test (requires SDL2, display, and BgfxRenderer module)
        add_executable(test_22_bgfx_sprites
            visual/test_bgfx_sprites.cpp
        )

        if(SDL2_FOUND)
            target_link_libraries(test_22_bgfx_sprites PRIVATE
                GroveEngine::impl
                bgfx
                bx
                SDL2::SDL2main
                SDL2::SDL2
            )
        else()
            target_include_directories(test_22_bgfx_sprites PRIVATE
                /usr/include/SDL2
            )
            target_link_libraries(test_22_bgfx_sprites PRIVATE
                GroveEngine::impl
                bgfx
                bx
                SDL2
                pthread
                dl
                X11
            )
        endif()

        # Not added to CTest (requires display)
        message(STATUS "Visual test 'test_22_bgfx_sprites' enabled (run manually)")

        # Test 23: Visual Sprites Test via Module + IIO
        add_executable(test_23_bgfx_sprites_visual
            visual/test_23_bgfx_sprites_visual.cpp
        )

        if(SDL2_FOUND)
            target_link_libraries(test_23_bgfx_sprites_visual PRIVATE
                GroveEngine::impl
                SDL2::SDL2main
                SDL2::SDL2
            )
        else()
            target_include_directories(test_23_bgfx_sprites_visual PRIVATE
                /usr/include/SDL2
            )
            target_link_libraries(test_23_bgfx_sprites_visual PRIVATE
                GroveEngine::impl
                SDL2
                pthread
                dl
                X11
            )
        endif()

        # Not added to CTest (requires display)
        message(STATUS "Visual test 'test_23_bgfx_sprites_visual' enabled (run manually)")

        # Test 24: UIModule Visual Test (requires SDL2, display, BgfxRenderer and UIModule)
        if(GROVE_BUILD_UI_MODULE)
            add_executable(test_24_ui_basic
                visual/test_24_ui_basic.cpp
            )

            if(SDL2_FOUND)
                target_link_libraries(test_24_ui_basic PRIVATE
                    GroveEngine::impl
                    SDL2::SDL2main
                    SDL2::SDL2
                )
            else()
                target_include_directories(test_24_ui_basic PRIVATE
                    /usr/include/SDL2
                )
                target_link_libraries(test_24_ui_basic PRIVATE
                    GroveEngine::impl
                    SDL2
                    pthread
                    dl
                    X11
                )
            endif()

            # Not added to CTest (requires display)
            message(STATUS "Visual test 'test_24_ui_basic' enabled (run manually)")

            # Test 25: UIModule Layout System Test (Phase 2)
            add_executable(test_25_ui_layout
                visual/test_25_ui_layout.cpp
            )

            if(SDL2_FOUND)
                target_link_libraries(test_25_ui_layout PRIVATE
                    GroveEngine::impl
                    SDL2::SDL2main
                    SDL2::SDL2
                )
            else()
                target_include_directories(test_25_ui_layout PRIVATE
                    /usr/include/SDL2
                )
                target_link_libraries(test_25_ui_layout PRIVATE
                    GroveEngine::impl
                    SDL2
                    pthread
                    dl
                    X11
                )
            endif()

            # Not added to CTest (requires display)
            message(STATUS "Visual test 'test_25_ui_layout' enabled (run manually)")

            # Test 26: UIModule Interactive Buttons Test (Phase 3)
            add_executable(test_26_ui_buttons
                visual/test_26_ui_buttons.cpp
            )

            if(SDL2_FOUND)
                target_link_libraries(test_26_ui_buttons PRIVATE
                    GroveEngine::impl
                    SDL2::SDL2main
                    SDL2::SDL2
                )
            else()
                target_include_directories(test_26_ui_buttons PRIVATE
                    /usr/include/SDL2
                )
                target_link_libraries(test_26_ui_buttons PRIVATE
                    GroveEngine::impl
                    SDL2
                    pthread
                    dl
                    X11
                )
            endif()

            # Not added to CTest (requires display)
            message(STATUS "Visual test 'test_26_ui_buttons' enabled (run manually)")

            # Test 28: UIModule ScrollPanel Test (Phase 7.1)
            add_executable(test_28_ui_scroll
                visual/test_28_ui_scroll.cpp
            )

            if(SDL2_FOUND)
                target_link_libraries(test_28_ui_scroll PRIVATE
                    GroveEngine::impl
                    SDL2::SDL2main
                    SDL2::SDL2
                )
            else()
                target_include_directories(test_28_ui_scroll PRIVATE
                    /usr/include/SDL2
                )
                target_link_libraries(test_28_ui_scroll PRIVATE
                    GroveEngine::impl
                    SDL2
                    pthread
                    dl
                    X11
                )
            endif()

            # Not added to CTest (requires display)
            message(STATUS "Visual test 'test_28_ui_scroll' enabled (run manually)")

            # Test 29: UIModule Advanced Features Test (Phase 7.2 - Tooltips)
            add_executable(test_29_ui_advanced
                visual/test_29_ui_advanced.cpp
            )

            if(SDL2_FOUND)
                target_link_libraries(test_29_ui_advanced PRIVATE
                    GroveEngine::impl
                    SDL2::SDL2main
                    SDL2::SDL2
                )
            else()
                target_include_directories(test_29_ui_advanced PRIVATE
                    /usr/include/SDL2
                )
                target_link_libraries(test_29_ui_advanced PRIVATE
                    GroveEngine::impl
                    SDL2
                    pthread
                    dl
                    X11
                )
            endif()

            # Not added to CTest (requires display)
            message(STATUS "Visual test 'test_29_ui_advanced' enabled (run manually)")
        endif()

        # Test 30: InputModule Visual Test (requires SDL2, display, and InputModule)
        if(GROVE_BUILD_INPUT_MODULE)
            add_executable(test_30_input_module
                visual/test_30_input_module.cpp
            )

            target_include_directories(test_30_input_module PRIVATE
                ${CMAKE_SOURCE_DIR}/modules
            )

            if(SDL2_FOUND)
                target_link_libraries(test_30_input_module PRIVATE
                    GroveEngine::impl
                    SDL2::SDL2main
                    SDL2::SDL2
                )
            else()
                target_include_directories(test_30_input_module PRIVATE
                    /usr/include/SDL2
                )
                target_link_libraries(test_30_input_module PRIVATE
                    GroveEngine::impl
                    SDL2
                    pthread
                    dl
                    X11
                )
            endif()

            # Not added to CTest (requires display and user interaction)
            message(STATUS "Visual test 'test_30_input_module' enabled (run manually)")
        endif()

        # Full Stack Interactive Test (BgfxRenderer + UIModule + InputModule)
        if(GROVE_BUILD_INPUT_MODULE AND GROVE_BUILD_UI_MODULE)
            add_executable(test_full_stack_interactive
                visual/test_full_stack_interactive.cpp
            )

            target_include_directories(test_full_stack_interactive PRIVATE
                ${CMAKE_SOURCE_DIR}/modules
                ${CMAKE_SOURCE_DIR}/modules/BgfxRenderer
            )

            # Platform-specific: On Windows, link BgfxRenderer statically (bgfx DLL issues)
            if(WIN32)
                # Static linking for BgfxRenderer - required on Windows
                # bgfx crashes when loaded from DLL due to threading/TLS issues
                target_compile_definitions(test_full_stack_interactive PRIVATE USE_STATIC_BGFX)
                target_link_libraries(test_full_stack_interactive PRIVATE
                    GroveEngine::impl
                    BgfxRenderer_static
                    SDL2::SDL2
                    spdlog::spdlog
                )
                # MSVC: Use console subsystem for main() entry point
                if(MSVC)
                    set_target_properties(test_full_stack_interactive PROPERTIES
                        LINK_FLAGS "/SUBSYSTEM:CONSOLE"
                    )
                endif()
            else()
                # Dynamic linking on Linux/Mac - works fine
                target_include_directories(test_full_stack_interactive PRIVATE
                    /usr/include/SDL2
                )
                target_link_libraries(test_full_stack_interactive PRIVATE
                    GroveEngine::impl
                    SDL2
                    pthread
                    dl
                    X11
                    spdlog::spdlog
                )
            endif()

            # Not added to CTest (requires display and user interaction)
            message(STATUS "Visual test 'test_full_stack_interactive' enabled (BgfxRenderer + UIModule + InputModule)")
        endif()

        # Minimal SDL test (for debugging SDL issues on Windows)
        add_executable(test_minimal_sdl
            visual/test_minimal_sdl.cpp
        )

        # Platform-specific SDL2 linking
        if(WIN32)
            target_link_libraries(test_minimal_sdl PRIVATE
                SDL2::SDL2
            )
        else()
            target_include_directories(test_minimal_sdl PRIVATE
                /usr/include/SDL2
            )
            target_link_libraries(test_minimal_sdl PRIVATE
                SDL2
            )
        endif()

        message(STATUS "Minimal SDL test 'test_minimal_sdl' enabled (debugging tool)")

        # Progressive test (debugging full stack issues)
        if(GROVE_BUILD_INPUT_MODULE)
            add_executable(test_progressive
                visual/test_progressive.cpp
            )

            target_include_directories(test_progressive PRIVATE
                ${CMAKE_SOURCE_DIR}/modules
            )

            if(WIN32)
                target_link_libraries(test_progressive PRIVATE
                    GroveEngine::impl
                    SDL2::SDL2
                    spdlog::spdlog
                )
            else()
                target_include_directories(test_progressive PRIVATE
                    /usr/include/SDL2
                )
                target_link_libraries(test_progressive PRIVATE
                    GroveEngine::impl
                    SDL2
                    pthread
                    dl
                    spdlog::spdlog
                )
            endif()

            message(STATUS "Progressive test 'test_progressive' enabled (debugging tool)")
        endif()
    else()
        message(STATUS "SDL2 not found - visual tests disabled")
    endif()

    # Test: GroveEngine Link Test (minimal - just link, don't use)
    add_executable(test_groveengine_link
        visual/test_groveengine_link.cpp
    )
    target_link_libraries(test_groveengine_link PRIVATE
        GroveEngine::impl
    )

    # Test: spdlog only (isolate spdlog issues)
    add_executable(test_spdlog_only
        visual/test_spdlog_only.cpp
    )
    target_link_libraries(test_spdlog_only PRIVATE
        spdlog::spdlog
    )

    # SDL2-dependent visual tests (debugging tools)
    if(SDL2_FOUND)
        # Test: Headers progressive (find which header crashes)
        add_executable(test_headers_progressive
            visual/test_headers_progressive.cpp
        )
        target_link_libraries(test_headers_progressive PRIVATE
            GroveEngine::impl
            SDL2::SDL2
            spdlog::spdlog
        )

        # Test: SDL + GroveEngine linked (same as test_progressive but don't use functions)
        add_executable(test_sdl_groveengine
            visual/test_sdl_groveengine.cpp
        )
        target_link_libraries(test_sdl_groveengine PRIVATE
            GroveEngine::impl
            SDL2::SDL2
            spdlog::spdlog
        )

        # Test: With modules/ in include directories (like test_progressive)
        add_executable(test_with_modules_include
            visual/test_with_modules_include.cpp
        )
        target_include_directories(test_with_modules_include PRIVATE
            ${CMAKE_SOURCE_DIR}/modules
        )
        target_link_libraries(test_with_modules_include PRIVATE
            GroveEngine::impl
            SDL2::SDL2
            spdlog::spdlog
        )

        # Test: Actually USE SDL_Init
        add_executable(test_use_sdl
            visual/test_use_sdl.cpp
        )
        target_link_libraries(test_use_sdl PRIVATE
            SDL2::SDL2
        )

        # Test: USE SDL + IntraIOManager together (like test_progressive)
        add_executable(test_use_sdl_and_iio
            visual/test_use_sdl_and_iio.cpp
        )
        target_link_libraries(test_use_sdl_and_iio PRIVATE
            GroveEngine::impl
            SDL2::SDL2
        )
    endif()

    # Test: IntraIOManager::getInstance() only (no SDL)
    add_executable(test_iio_only
        visual/test_iio_only.cpp
    )
    target_link_libraries(test_iio_only PRIVATE
        GroveEngine::impl
    )

    # Test: Just stillhammer logger (no GroveEngine)
    add_executable(test_logger_only
        visual/test_logger_only.cpp
    )
    target_link_libraries(test_logger_only PRIVATE
        stillhammer_logger
    )

    # Test: Just <filesystem> include
    add_executable(test_filesystem
        visual/test_filesystem.cpp
    )

    # Test: spdlog with register_logger
    add_executable(test_spdlog_register
        visual/test_spdlog_register.cpp
    )
    target_link_libraries(test_spdlog_register PRIVATE
        spdlog::spdlog
    )

    # Test: Logger.cpp compiled directly (not as library)
    add_executable(test_logger_direct
        visual/test_logger_direct.cpp
        ${CMAKE_SOURCE_DIR}/external/StillHammer/logger/src/Logger.cpp
    )
    target_include_directories(test_logger_direct PRIVATE
        ${CMAKE_SOURCE_DIR}/external/StillHammer/logger/include
    )
    target_link_libraries(test_logger_direct PRIVATE
        spdlog::spdlog
    )

    # Test: spdlog + filesystem combined
    add_executable(test_spdlog_filesystem
        visual/test_spdlog_filesystem.cpp
    )
    target_link_libraries(test_spdlog_filesystem PRIVATE
        spdlog::spdlog
    )

    # Test 22b: Headless sprite integration test (no display required)
    add_executable(test_22_bgfx_sprites_headless
        integration/test_22_bgfx_sprites_headless.cpp
    )

    target_link_libraries(test_22_bgfx_sprites_headless PRIVATE
        GroveEngine::impl
        Catch2::Catch2WithMain
    )

    # ========================================
    # Phase 6.5 Sprint 3: Pipeline Headless Tests
    # ========================================

    # Test: Pipeline Headless - End-to-end rendering flow
    add_executable(test_pipeline_headless
        integration/test_pipeline_headless.cpp
        ../modules/BgfxRenderer/Scene/SceneCollector.cpp
        ../modules/BgfxRenderer/Frame/FrameAllocator.cpp
        ../modules/BgfxRenderer/RenderGraph/RenderGraph.cpp
        ../modules/BgfxRenderer/RHI/RHICommandBuffer.cpp
    )
    target_include_directories(test_pipeline_headless PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../modules/BgfxRenderer
        ${CMAKE_CURRENT_SOURCE_DIR}
    )
    target_link_libraries(test_pipeline_headless PRIVATE
        GroveEngine::impl
        
        Catch2::Catch2WithMain
    )
    grove_add_test(PipelineHeadless test_pipeline_headless ${CMAKE_BINARY_DIR})

    grove_add_test(BgfxSpritesHeadless test_22_bgfx_sprites_headless ${CMAKE_CURRENT_BINARY_DIR})

    # ========================================
    # Renderer unit/integration tests (audit 2026-06): these existed under
    # tests/unit and tests/integration but were never registered in ctest — so they
    # contributed ZERO regression coverage. They use MockRHIDevice (no GPU/bgfx) and
    # compile only the bgfx-free RHI/scene sources.
    # ========================================
    set(_GROVE_RHI_HEADLESS_SRCS
        ../modules/BgfxRenderer/Scene/SceneCollector.cpp
        ../modules/BgfxRenderer/Frame/FrameAllocator.cpp
        ../modules/BgfxRenderer/RenderGraph/RenderGraph.cpp
        ../modules/BgfxRenderer/RHI/RHICommandBuffer.cpp
        ../modules/BgfxRenderer/Shaders/ShaderManager.cpp
    )
    foreach(_grove_rtest test_shader_manager test_render_graph test_rhi_command_buffer test_frame_allocator)
        add_executable(${_grove_rtest} unit/${_grove_rtest}.cpp ${_GROVE_RHI_HEADLESS_SRCS})
        target_include_directories(${_grove_rtest} PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/../modules/BgfxRenderer
            ${CMAKE_CURRENT_SOURCE_DIR})
        target_link_libraries(${_grove_rtest} PRIVATE GroveEngine::impl Catch2::Catch2WithMain)
    endforeach()
    grove_add_test(ShaderManagerUnit  test_shader_manager     ${CMAKE_CURRENT_BINARY_DIR})
    grove_add_test(RenderGraphUnit     test_render_graph       ${CMAKE_CURRENT_BINARY_DIR})
    grove_add_test(RHICommandBufferUnit test_rhi_command_buffer ${CMAKE_CURRENT_BINARY_DIR})
    grove_add_test(FrameAllocatorUnit  test_frame_allocator    ${CMAKE_CURRENT_BINARY_DIR})

    add_executable(test_debug_pass
        unit/test_debug_pass.cpp
        ../modules/BgfxRenderer/Passes/DebugPass.cpp
        ../modules/BgfxRenderer/RHI/RHICommandBuffer.cpp
    )
    target_include_directories(test_debug_pass PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../modules/BgfxRenderer
        ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(test_debug_pass PRIVATE GroveEngine::impl Catch2::Catch2WithMain)
    grove_add_test(DebugPassUnit test_debug_pass ${CMAKE_CURRENT_BINARY_DIR})

    # BitmapFont: UTF-8 decode + French accent coverage (engine help A1). No bgfx — MockRHIDevice.
    add_executable(test_bitmap_font
        unit/test_bitmap_font.cpp
        ../modules/BgfxRenderer/Text/BitmapFont.cpp
    )
    target_include_directories(test_bitmap_font PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../modules/BgfxRenderer
        ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(test_bitmap_font PRIVATE GroveEngine::impl Catch2::Catch2WithMain)
    grove_add_test(BitmapFontUnit test_bitmap_font ${CMAKE_CURRENT_BINARY_DIR})

    add_executable(test_scene_collector integration/test_scene_collector.cpp ${_GROVE_RHI_HEADLESS_SRCS})
    target_include_directories(test_scene_collector PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../modules/BgfxRenderer
        ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(test_scene_collector PRIVATE GroveEngine::impl Catch2::Catch2WithMain)
    grove_add_test(SceneCollectorTest test_scene_collector ${CMAKE_CURRENT_BINARY_DIR})

    # Camera helper (grove::camera): pure 2D camera math exposed to consuming games
    # (seamless zoom/pan). Header-only — no bgfx, no GPU. Just the header + Catch2.
    add_executable(test_camera unit/test_camera.cpp)
    target_include_directories(test_camera PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../modules/BgfxRenderer
        ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(test_camera PRIVATE Catch2::Catch2WithMain)
    grove_add_test(CameraUnit test_camera ${CMAKE_CURRENT_BINARY_DIR})

    # ZoomLadder (grove::camera): pure zoom-strata math (plateaus + log-space crossfade) for the
    # zoom continuum. Header-only — no bgfx/GPU. Objective unit test with analytical oracles.
    add_executable(test_zoom_ladder unit/test_zoom_ladder.cpp)
    target_include_directories(test_zoom_ladder PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../modules/BgfxRenderer
        ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(test_zoom_ladder PRIVATE Catch2::Catch2WithMain)
    grove_add_test(ZoomLadderUnit test_zoom_ladder ${CMAKE_CURRENT_BINARY_DIR})

    # Zone-navigation primitives (grove::camera): fitBounds / clampPanToBounds / worldPanForScreen.
    # Pure camera math (Camera.h) for the nested-zones navigation model. Objective oracle test.
    add_executable(test_zone_nav unit/test_zone_nav.cpp)
    target_include_directories(test_zone_nav PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../modules/BgfxRenderer
        ${CMAKE_CURRENT_SOURCE_DIR}/../include
        ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(test_zone_nav PRIVATE Catch2::Catch2WithMain)
    grove_add_test(ZoneNavUnit test_zone_nav ${CMAKE_CURRENT_BINARY_DIR})

    # grove::snap — generic directional detent snapping (grove/snap.h), reused by ZoneNavigator's
    # zoom snap. Pure, no engine coupling. Objective oracle test.
    add_executable(test_snap unit/test_snap.cpp)
    target_include_directories(test_snap PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../include)
    target_link_libraries(test_snap PRIVATE Catch2::Catch2WithMain)
    grove_add_test(SnapUnit test_snap ${CMAKE_CURRENT_BINARY_DIR})

    # HUD overlay view routing: SpritePass/TextPass must submit HUD buckets to bgfx view 1,
    # world buckets to view 0. No GPU (MockRHIDevice), but SpritePass references
    # ResourceCache::getTextureById -> link ResourceCache + TextureLoader (stb_image header).
    add_executable(test_hud_view
        unit/test_hud_view.cpp
        ../modules/BgfxRenderer/Passes/SpritePass.cpp
        ../modules/BgfxRenderer/Passes/TextPass.cpp
        ../modules/BgfxRenderer/Text/BitmapFont.cpp
        ../modules/BgfxRenderer/RHI/RHICommandBuffer.cpp
        ../modules/BgfxRenderer/Resources/ResourceCache.cpp
        ../modules/BgfxRenderer/Resources/TextureLoader.cpp
    )
    target_include_directories(test_hud_view PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../modules/BgfxRenderer
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${bgfx_SOURCE_DIR}/bimg/3rdparty)   # stb_image.h for TextureLoader
    target_link_libraries(test_hud_view PRIVATE GroveEngine::impl Catch2::Catch2WithMain)
    grove_add_test(HudViewUnit test_hud_view ${CMAKE_CURRENT_BINARY_DIR})

    # Render-side view culling in SpritePass + TilemapPass (MockRHIDevice, no GPU).
    add_executable(test_pass_culling
        unit/test_pass_culling.cpp
        ../modules/BgfxRenderer/Passes/SpritePass.cpp
        ../modules/BgfxRenderer/Passes/TilemapPass.cpp
        ../modules/BgfxRenderer/RHI/RHICommandBuffer.cpp
        ../modules/BgfxRenderer/Resources/ResourceCache.cpp
        ../modules/BgfxRenderer/Resources/TextureLoader.cpp
    )
    target_include_directories(test_pass_culling PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../modules/BgfxRenderer
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${bgfx_SOURCE_DIR}/bimg/3rdparty)
    target_link_libraries(test_pass_culling PRIVATE GroveEngine::impl Catch2::Catch2WithMain)
    grove_add_test(PassCullingUnit test_pass_culling ${CMAKE_CURRENT_BINARY_DIR})

    # RHI TextureDesc surface for the GPU tilemap (Slice A0): R16UI integer index format +
    # POINT/CLAMP sampler control. Header-only (RHITypes + MockRHIDevice), no bgfx/GPU link.
    add_executable(test_rhi_texture_desc unit/test_rhi_texture_desc.cpp)
    target_include_directories(test_rhi_texture_desc PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../modules/BgfxRenderer
        ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(test_rhi_texture_desc PRIVATE Catch2::Catch2WithMain)
    grove_add_test(RhiTextureDescUnit test_rhi_texture_desc ${CMAKE_CURRENT_BINARY_DIR})

    # Tilemap LOD color mip generation (Slice B): pure box-filter math (LodColor.h), no bgfx/GPU.
    add_executable(test_lod_color unit/test_lod_color.cpp)
    target_include_directories(test_lod_color PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../modules/BgfxRenderer
        ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(test_lod_color PRIVATE Catch2::Catch2WithMain)
    grove_add_test(LodColorUnit test_lod_color ${CMAKE_CURRENT_BINARY_DIR})

    # Atlas grid -> texture2DArray slicing (Slice A3.3): pure pixel re-layout (AtlasSlice.h), no GPU.
    add_executable(test_atlas_slice unit/test_atlas_slice.cpp)
    target_include_directories(test_atlas_slice PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../modules/BgfxRenderer
        ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(test_atlas_slice PRIVATE Catch2::Catch2WithMain)
    grove_add_test(AtlasSliceUnit test_atlas_slice ${CMAKE_CURRENT_BINARY_DIR})

    # TextureLoader array load (Slice A3.3c): PNG grid -> texture2DArray. Compiles TextureLoader.cpp
    # directly (stb_image) + MockRHIDevice; no GPU. Skips if the asset isn't found.
    add_executable(test_texture_array
        unit/test_texture_array.cpp
        ../modules/BgfxRenderer/Resources/TextureLoader.cpp)
    target_include_directories(test_texture_array PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../modules/BgfxRenderer
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${bgfx_SOURCE_DIR}/bimg/3rdparty)   # stb_image.h
    target_link_libraries(test_texture_array PRIVATE spdlog::spdlog Catch2::Catch2WithMain)
    grove_add_test(TextureArrayUnit test_texture_array ${CMAKE_CURRENT_BINARY_DIR})
endif()

# ================================================================================
# Phase 5 Integration Tests - UIModule
# ================================================================================

# TestControllerModule - Simulates game logic for UI integration tests
add_library(TestControllerModule SHARED
    modules/TestControllerModule.cpp
)

target_link_libraries(TestControllerModule PRIVATE
    GroveEngine::core
    GroveEngine::impl
    spdlog::spdlog
)

# IT_014: UIModule Full Integration Test
if(GROVE_BUILD_UI_MODULE AND GROVE_BUILD_BGFX_RENDERER)
    add_executable(IT_014_ui_module_integration
        integration/IT_014_ui_module_integration.cpp
    )

    target_link_libraries(IT_014_ui_module_integration PRIVATE
        test_helpers
        GroveEngine::core
        GroveEngine::impl
        Catch2::Catch2WithMain
    )

    add_dependencies(IT_014_ui_module_integration TestControllerModule)

    # CTest integration
    grove_add_test(UIModuleIntegration IT_014_ui_module_integration ${CMAKE_CURRENT_BINARY_DIR})

    message(STATUS "Integration test 'IT_014_ui_module_integration' enabled")
endif()

# IT_015: InputModule + UIModule Integration Test
if(GROVE_BUILD_UI_MODULE)
    # IT_015: Simplified UIModule input integration test (no InputModule dependency)
    # This test publishes IIO messages directly to test UIModule input processing
    add_executable(IT_015_input_ui_integration
        integration/IT_015_input_ui_integration.cpp
    )

    target_link_libraries(IT_015_input_ui_integration PRIVATE
        test_helpers
        GroveEngine::core
        GroveEngine::impl
        Catch2::Catch2WithMain
    )

    # CTest integration
    grove_add_test(InputUIIntegration IT_015_input_ui_integration ${CMAKE_CURRENT_BINARY_DIR})

    message(STATUS "Integration test 'IT_015_input_ui_integration' enabled (simplified, no SDL2)")

    # IT_016: first UI interaction E2E — inject a real click, assert ui:click + ui:action.
    add_executable(IT_016_ui_click_assert
        integration/IT_016_ui_click_assert.cpp
    )
    target_link_libraries(IT_016_ui_click_assert PRIVATE
        test_helpers
        GroveEngine::core
        GroveEngine::impl
        Catch2::Catch2WithMain
    )
    grove_add_test(UIClickAssert IT_016_ui_click_assert ${CMAKE_CURRENT_BINARY_DIR})

    # IT_017: checkbox toggle + slider click/drag E2E (extends the IT_016 harness).
    add_executable(IT_017_ui_widgets_e2e
        integration/IT_017_ui_widgets_e2e.cpp
    )
    target_link_libraries(IT_017_ui_widgets_e2e PRIVATE
        test_helpers
        GroveEngine::core
        GroveEngine::impl
        Catch2::Catch2WithMain
    )
    grove_add_test(UIWidgetsE2E IT_017_ui_widgets_e2e ${CMAKE_CURRENT_BINARY_DIR})

    # IT_018: layout hit-test E2E — clicks land on layout-positioned widgets (audit #6).
    add_executable(IT_018_ui_layout_hittest_e2e
        integration/IT_018_ui_layout_hittest_e2e.cpp
    )
    target_link_libraries(IT_018_ui_layout_hittest_e2e PRIVATE
        test_helpers
        GroveEngine::core
        GroveEngine::impl
        Catch2::Catch2WithMain
    )
    grove_add_test(UILayoutHitTestE2E IT_018_ui_layout_hittest_e2e ${CMAKE_CURRENT_BINARY_DIR})

    # IT_019: ScrollPanel E2E — scroll, then click a scrolled-into-view child (audit ScrollPanel).
    add_executable(IT_019_ui_scrollpanel_e2e
        integration/IT_019_ui_scrollpanel_e2e.cpp
    )
    target_link_libraries(IT_019_ui_scrollpanel_e2e PRIVATE
        test_helpers
        GroveEngine::core
        GroveEngine::impl
        Catch2::Catch2WithMain
    )
    grove_add_test(UIScrollPanelE2E IT_019_ui_scrollpanel_e2e ${CMAKE_CURRENT_BINARY_DIR})

    # IT_053: the WHOLE UI runs HOSTED ON THE ENGINE (registerStaticModule + engine.step()),
    # not hand-wired. Same scenario as IT_044 but UIModule is constructed statically
    # (UIModule_static) and driven entirely through DebugEngine. Headless, no .so, no GPU.
    add_executable(IT_053_ui_on_engine_e2e
        integration/IT_053_ui_on_engine_e2e.cpp
    )
    target_link_libraries(IT_053_ui_on_engine_e2e PRIVATE
        UIModule_static          # full UIModule (PUBLIC: include dir + GroveEngine::impl)
        GroveEngine::core
        Catch2::Catch2WithMain
    )
    grove_add_test(UIOnEngineE2E IT_053_ui_on_engine_e2e ${CMAKE_CURRENT_BINARY_DIR})

    # test_threaded_real_ui_e2e: the IT_053 scenario but UIModule is hosted THREADED
    # (registerStaticModule(THREADED)) — runs on its own worker thread. Proves a REAL
    # self-draining module survives threaded hosting + worker-owned IIO drain (archi A).
    # Plain main() (no Catch2): exit 0 = the click flowed through the worker-threaded UIModule.
    add_executable(test_threaded_real_ui_e2e
        integration/test_threaded_real_ui_e2e.cpp
    )
    target_link_libraries(test_threaded_real_ui_e2e PRIVATE
        UIModule_static          # full UIModule (PUBLIC: include dir + GroveEngine::impl)
        GroveEngine::core
    )
    grove_add_test(ThreadedRealUIE2E test_threaded_real_ui_e2e ${CMAKE_CURRENT_BINARY_DIR})

    # Phase 2 consolidation: MULTIPLE real modules hosted THREADED together via the engine.
    # Real UIModule + real SoundManager (MockSoundBackend) + game glue + parallelism probes;
    # a real click crosses three hosted modules (UI -> ship:part -> glue -> sound:sfx -> Sound).
    # Needs both UI and Sound modules built; SoundManagerModule.cpp is compiled in (like the
    # SoundManagerUnit test). Plain main(), headless.
    if(GROVE_BUILD_SOUND_MODULE)
        add_executable(test_threaded_real_multi_e2e
            integration/test_threaded_real_multi_e2e.cpp
            ../modules/SoundManager/SoundManagerModule.cpp
        )
        target_include_directories(test_threaded_real_multi_e2e PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/../modules
            ${CMAKE_CURRENT_SOURCE_DIR}
        )
        target_link_libraries(test_threaded_real_multi_e2e PRIVATE
            UIModule_static          # real UIModule (PUBLIC: include dir + GroveEngine::impl)
            GroveEngine::core
        )
        grove_add_test(ThreadedRealMultiE2E test_threaded_real_multi_e2e ${CMAKE_CURRENT_BINARY_DIR} 60)

        # Phase 3 slice 2: the SAME multi-real-module scenario, but hosted in the THREAD_POOL
        # (registerStaticModule(THREAD_POOL)). Proves the pool's engine integration in real
        # conditions. Shares real_multi_scenario.h with the THREADED lock above.
        add_executable(test_pool_real_multi_e2e
            integration/test_pool_real_multi_e2e.cpp
            ../modules/SoundManager/SoundManagerModule.cpp
        )
        target_include_directories(test_pool_real_multi_e2e PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/../modules
            ${CMAKE_CURRENT_SOURCE_DIR}
        )
        target_link_libraries(test_pool_real_multi_e2e PRIVATE
            UIModule_static
            GroveEngine::core
        )
        grove_add_test(PoolRealMultiE2E test_pool_real_multi_e2e ${CMAKE_CURRENT_BINARY_DIR} 60)
    endif()

    # test_radial_math: pure action-wheel geometry (RadialMath.h), no GPU/module/IIO.
    add_executable(test_radial_math unit/test_radial_math.cpp)
    target_include_directories(test_radial_math PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../modules/UIModule
        ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(test_radial_math PRIVATE Catch2::Catch2WithMain)
    grove_add_test(RadialMathUnit test_radial_math ${CMAKE_CURRENT_BINARY_DIR})

    # test_ui_layout: pure UILayout oracle (reflow + relative/percent sizing) — UI slice 1.1.
    # Links UIModule_static (UILayout lives in a .cpp); no IIO/window/renderer touched.
    add_executable(test_ui_layout unit/test_ui_layout.cpp)
    target_link_libraries(test_ui_layout PRIVATE UIModule_static Catch2::Catch2WithMain)
    grove_add_test(UILayoutUnit test_ui_layout ${CMAKE_CURRENT_BINARY_DIR})

    # test_ui_zorder: pure bringToFront() z-order primitive (UI slice 3a). Links UIModule_static.
    add_executable(test_ui_zorder unit/test_ui_zorder.cpp)
    target_link_libraries(test_ui_zorder PRIVATE UIModule_static Catch2::Catch2WithMain)
    grove_add_test(UIZOrderUnit test_ui_zorder ${CMAKE_CURRENT_BINARY_DIR})

    # test_ui_list: pure UIList geometry/data oracle (rowAt / scroll clamp / parseItems / selection).
    add_executable(test_ui_list unit/test_ui_list.cpp)
    target_link_libraries(test_ui_list PRIVATE UIModule_static Catch2::Catch2WithMain)
    grove_add_test(UIListUnit test_ui_list ${CMAKE_CURRENT_BINARY_DIR})

    # test_ui_binding: pure {{path}} resolver + scope-chain oracle (the JSON-UI binding socle).
    add_executable(test_ui_binding unit/test_ui_binding.cpp)
    target_link_libraries(test_ui_binding PRIVATE UIModule_static Catch2::Catch2WithMain)
    grove_add_test(UIBindingUnit test_ui_binding ${CMAKE_CURRENT_BINARY_DIR})

    # IT_020: action-wheel (radial) click E2E — confirm a wedge by direction, cancel in
    # the dead-zone, miss off-wheel. Locks the new UIRadial widget end-to-end.
    add_executable(IT_020_ui_radial_e2e
        integration/IT_020_ui_radial_e2e.cpp
    )
    target_link_libraries(IT_020_ui_radial_e2e PRIVATE
        test_helpers
        GroveEngine::core
        GroveEngine::impl
        Catch2::Catch2WithMain
    )
    grove_add_test(UIRadialE2E IT_020_ui_radial_e2e ${CMAKE_CURRENT_BINARY_DIR})

    # IT_021: reflow-on-resize E2E — ui:resize re-lays-out the tree; a click that hit the
    # right button before the resize hits the left one after it (UI framework slice 1.1).
    add_executable(IT_021_ui_resize_reflow_e2e
        integration/IT_021_ui_resize_reflow_e2e.cpp
    )
    target_link_libraries(IT_021_ui_resize_reflow_e2e PRIVATE
        test_helpers
        GroveEngine::core
        GroveEngine::impl
        Catch2::Catch2WithMain
    )
    grove_add_test(UIResizeReflowE2E IT_021_ui_resize_reflow_e2e ${CMAKE_CURRENT_BINARY_DIR})

    # IT_022: anchoring reflow E2E — a corner-anchored widget follows the corner across a resize
    # (UI framework slice 1.2).
    add_executable(IT_022_ui_anchor_reflow_e2e
        integration/IT_022_ui_anchor_reflow_e2e.cpp
    )
    target_link_libraries(IT_022_ui_anchor_reflow_e2e PRIVATE
        test_helpers
        GroveEngine::core
        GroveEngine::impl
        Catch2::Catch2WithMain
    )
    grove_add_test(UIAnchorReflowE2E IT_022_ui_anchor_reflow_e2e ${CMAKE_CURRENT_BINARY_DIR})

    # IT_024: ScrollPanel clipping E2E — a child's render entry carries the panel's clip rect
    # (UI framework slice 2a-2b: UIRenderer clip-stack + UIScrollPanel push/pop).
    add_executable(IT_024_ui_scrollpanel_clip_e2e
        integration/IT_024_ui_scrollpanel_clip_e2e.cpp
    )
    target_link_libraries(IT_024_ui_scrollpanel_clip_e2e PRIVATE
        test_helpers
        GroveEngine::core
        GroveEngine::impl
        Catch2::Catch2WithMain
    )
    grove_add_test(UIScrollPanelClipE2E IT_024_ui_scrollpanel_clip_e2e ${CMAKE_CURRENT_BINARY_DIR})

    # IT_025: ScrollPanel hit-test clipping E2E — a child below the panel's visible rect is not
    # clickable (UI framework slice 2b).
    add_executable(IT_025_ui_scrollpanel_hitclip_e2e
        integration/IT_025_ui_scrollpanel_hitclip_e2e.cpp
    )
    target_link_libraries(IT_025_ui_scrollpanel_hitclip_e2e PRIVATE
        test_helpers
        GroveEngine::core
        GroveEngine::impl
        Catch2::Catch2WithMain
    )
    grove_add_test(UIScrollPanelHitClipE2E IT_025_ui_scrollpanel_hitclip_e2e ${CMAKE_CURRENT_BINARY_DIR})

    # IT_026: in-app Window E2E — opaque (absorbs clicks) + clips its content (UI slice 3b-1).
    add_executable(IT_026_ui_window_e2e
        integration/IT_026_ui_window_e2e.cpp
    )
    target_link_libraries(IT_026_ui_window_e2e PRIVATE
        test_helpers
        GroveEngine::core
        GroveEngine::impl
        Catch2::Catch2WithMain
    )
    grove_add_test(UIWindowE2E IT_026_ui_window_e2e ${CMAKE_CURRENT_BINARY_DIR})

    # IT_027 / IT_028: in-app window interaction E2E — raise-on-click, title-bar drag, close (3b-2).
    add_executable(IT_027_ui_window_raise_e2e integration/IT_027_ui_window_raise_e2e.cpp)
    target_link_libraries(IT_027_ui_window_raise_e2e PRIVATE
        test_helpers GroveEngine::core GroveEngine::impl Catch2::Catch2WithMain)
    grove_add_test(UIWindowRaiseE2E IT_027_ui_window_raise_e2e ${CMAKE_CURRENT_BINARY_DIR})

    add_executable(IT_028_ui_window_dragclose_e2e integration/IT_028_ui_window_dragclose_e2e.cpp)
    target_link_libraries(IT_028_ui_window_dragclose_e2e PRIVATE
        test_helpers GroveEngine::core GroveEngine::impl Catch2::Catch2WithMain)
    grove_add_test(UIWindowDragCloseE2E IT_028_ui_window_dragclose_e2e ${CMAKE_CURRENT_BINARY_DIR})

    # IT_029: tabbed container E2E — clicking a tab switches the active page (UI slice 5c).
    add_executable(IT_029_ui_tabs_e2e integration/IT_029_ui_tabs_e2e.cpp)
    target_link_libraries(IT_029_ui_tabs_e2e PRIVATE
        test_helpers GroveEngine::core GroveEngine::impl Catch2::Catch2WithMain)
    grove_add_test(UITabsE2E IT_029_ui_tabs_e2e ${CMAKE_CURRENT_BINARY_DIR})

    # IT_030: edge drawer E2E — slides open/closed; content reachable only when open (UI slice 5b).
    add_executable(IT_030_ui_drawer_e2e integration/IT_030_ui_drawer_e2e.cpp)
    target_link_libraries(IT_030_ui_drawer_e2e PRIVATE
        test_helpers GroveEngine::core GroveEngine::impl Catch2::Catch2WithMain)
    grove_add_test(UIDrawerE2E IT_030_ui_drawer_e2e ${CMAKE_CURRENT_BINARY_DIR})

    # IT_031: modal dialog E2E — focus-trap + open/close on outside click (UI slice 5a).
    add_executable(IT_031_ui_modal_e2e integration/IT_031_ui_modal_e2e.cpp)
    target_link_libraries(IT_031_ui_modal_e2e PRIVATE
        test_helpers GroveEngine::core GroveEngine::impl Catch2::Catch2WithMain)
    grove_add_test(UIModalE2E IT_031_ui_modal_e2e ${CMAKE_CURRENT_BINARY_DIR})

    # IT_032: in-app window resize via the bottom-right grip (UI slice 3b-3).
    add_executable(IT_032_ui_window_resize_e2e integration/IT_032_ui_window_resize_e2e.cpp)
    target_link_libraries(IT_032_ui_window_resize_e2e PRIVATE
        test_helpers GroveEngine::core GroveEngine::impl Catch2::Catch2WithMain)
    grove_add_test(UIWindowResizeE2E IT_032_ui_window_resize_e2e ${CMAKE_CURRENT_BINARY_DIR})

    # IT_033: data-driven ship list E2E — select / scroll+clip click-flip / runtime repopulate (sidebar).
    add_executable(IT_033_ui_list_e2e integration/IT_033_ui_list_e2e.cpp)
    target_link_libraries(IT_033_ui_list_e2e PRIVATE
        test_helpers GroveEngine::core GroveEngine::impl Catch2::Catch2WithMain)
    grove_add_test(UIListE2E IT_033_ui_list_e2e ${CMAKE_CURRENT_BINARY_DIR})

    # IT_034: collapsible warship GROUPS E2E — select-with-group / expand click-flip / collapse-hides.
    add_executable(IT_034_ui_list_groups_e2e integration/IT_034_ui_list_groups_e2e.cpp)
    target_link_libraries(IT_034_ui_list_groups_e2e PRIVATE
        test_helpers GroveEngine::core GroveEngine::impl Catch2::Catch2WithMain)
    grove_add_test(UIListGroupsE2E IT_034_ui_list_groups_e2e ${CMAKE_CURRENT_BINARY_DIR})

    # IT_035: UIList scrollbar + content drag-to-scroll E2E — click-flips + plain-click-still-selects.
    add_executable(IT_035_ui_list_dragscroll_e2e integration/IT_035_ui_list_dragscroll_e2e.cpp)
    target_link_libraries(IT_035_ui_list_dragscroll_e2e PRIVATE
        test_helpers GroveEngine::core GroveEngine::impl Catch2::Catch2WithMain)
    grove_add_test(UIListDragScrollE2E IT_035_ui_list_dragscroll_e2e ${CMAKE_CURRENT_BINARY_DIR})

    # IT_036: input-capture E2E — ui:capture guards click-through (over UI / over world / during a grab).
    add_executable(IT_036_ui_capture_e2e integration/IT_036_ui_capture_e2e.cpp)
    target_link_libraries(IT_036_ui_capture_e2e PRIVATE
        test_helpers GroveEngine::core GroveEngine::impl Catch2::Catch2WithMain)
    grove_add_test(UICaptureE2E IT_036_ui_capture_e2e ${CMAKE_CURRENT_BINARY_DIR})

    # IT_037: JSON-UI data-binding (in) + declarative events (out) — the templating engine, step 2.
    add_executable(IT_037_ui_binding_e2e integration/IT_037_ui_binding_e2e.cpp)
    target_link_libraries(IT_037_ui_binding_e2e PRIVATE
        test_helpers GroveEngine::core GroveEngine::impl Catch2::Catch2WithMain)
    grove_add_test(UIBindingE2E IT_037_ui_binding_e2e ${CMAKE_CURRENT_BINARY_DIR})

    # IT_038: JSON-UI reactivity — partial data updates (ui:data:set / ui:data:merge), step 3.
    add_executable(IT_038_ui_reactivity_e2e integration/IT_038_ui_reactivity_e2e.cpp)
    target_link_libraries(IT_038_ui_reactivity_e2e PRIVATE
        test_helpers GroveEngine::core GroveEngine::impl Catch2::Catch2WithMain)
    grove_add_test(UIReactivityE2E IT_038_ui_reactivity_e2e ${CMAKE_CURRENT_BINARY_DIR})

    # IT_039: JSON-UI repeater — template x data array with per-item scope (binding + events), step 4.
    add_executable(IT_039_ui_repeater_e2e integration/IT_039_ui_repeater_e2e.cpp)
    target_link_libraries(IT_039_ui_repeater_e2e PRIVATE
        test_helpers GroveEngine::core GroveEngine::impl Catch2::Catch2WithMain)
    grove_add_test(UIRepeaterE2E IT_039_ui_repeater_e2e ${CMAKE_CURRENT_BINARY_DIR})

    # IT_040: JSON-UI conditional `if` — data-driven show/hide that purges retained entries, step 5.
    add_executable(IT_040_ui_if_e2e integration/IT_040_ui_if_e2e.cpp)
    target_link_libraries(IT_040_ui_if_e2e PRIVATE
        test_helpers GroveEngine::core GroveEngine::impl Catch2::Catch2WithMain)
    grove_add_test(UIIfE2E IT_040_ui_if_e2e ${CMAKE_CURRENT_BINARY_DIR})

    # IT_041: virtualized template list — list = virtualized repeater (bounded rows, per-item, re-bind), step 6.
    add_executable(IT_041_ui_vlist_e2e integration/IT_041_ui_vlist_e2e.cpp)
    target_link_libraries(IT_041_ui_vlist_e2e PRIVATE
        test_helpers GroveEngine::core GroveEngine::impl Catch2::Catch2WithMain)
    grove_add_test(UIVListE2E IT_041_ui_vlist_e2e ${CMAKE_CURRENT_BINARY_DIR})

    # IT_042: virtualized-list idle gate (perf) — no re-window on idle frames; scroll/data re-window.
    add_executable(IT_042_ui_vlist_idlegate_e2e integration/IT_042_ui_vlist_idlegate_e2e.cpp)
    target_link_libraries(IT_042_ui_vlist_idlegate_e2e PRIVATE
        test_helpers GroveEngine::core GroveEngine::impl Catch2::Catch2WithMain)
    grove_add_test(UIVListIdleGateE2E IT_042_ui_vlist_idlegate_e2e ${CMAKE_CURRENT_BINARY_DIR})

    # IT_043: the FULL demo composition (fleet-command) — everything together, headless verification.
    add_executable(IT_043_ui_full_demo_e2e integration/IT_043_ui_full_demo_e2e.cpp)
    target_link_libraries(IT_043_ui_full_demo_e2e PRIVATE
        test_helpers GroveEngine::core GroveEngine::impl Catch2::Catch2WithMain)
    grove_add_test(UIFullDemoE2E IT_043_ui_full_demo_e2e ${CMAKE_CURRENT_BINARY_DIR})

    # IT_044: ship inspector — data-driven blueprint, sprites-as-UI (SVG->PNG textures), clickable parts.
    add_executable(IT_044_ui_ship_inspector_e2e integration/IT_044_ui_ship_inspector_e2e.cpp)
    target_link_libraries(IT_044_ui_ship_inspector_e2e PRIVATE
        test_helpers GroveEngine::core GroveEngine::impl Catch2::Catch2WithMain)
    grove_add_test(UIShipInspectorE2E IT_044_ui_ship_inspector_e2e ${CMAKE_CURRENT_BINARY_DIR})

    # IT_045: smart resize — a window lays out its content responsively (% + flex reflow on viewport resize).
    add_executable(IT_045_ui_window_resize_smart_e2e integration/IT_045_ui_window_resize_smart_e2e.cpp)
    target_link_libraries(IT_045_ui_window_resize_smart_e2e PRIVATE
        test_helpers GroveEngine::core GroveEngine::impl Catch2::Catch2WithMain)
    grove_add_test(UIWindowResizeSmartE2E IT_045_ui_window_resize_smart_e2e ${CMAKE_CURRENT_BINARY_DIR})

    # IT_046: foldable 50-resource panel — fold/unfold (collapsible group) + scroll-on-overflow inside a window.
    add_executable(IT_046_ui_resources_fold_scroll_e2e integration/IT_046_ui_resources_fold_scroll_e2e.cpp)
    target_link_libraries(IT_046_ui_resources_fold_scroll_e2e PRIVATE
        test_helpers GroveEngine::core GroveEngine::impl Catch2::Catch2WithMain)
    grove_add_test(UIResourcesFoldScrollE2E IT_046_ui_resources_fold_scroll_e2e ${CMAKE_CURRENT_BINARY_DIR})

    # IT_047: off-screen fleet drawer — hidden -> slides in -> data-driven clickable ship vignettes.
    add_executable(IT_047_ui_fleet_drawer_e2e integration/IT_047_ui_fleet_drawer_e2e.cpp)
    target_link_libraries(IT_047_ui_fleet_drawer_e2e PRIVATE
        test_helpers GroveEngine::core GroveEngine::impl Catch2::Catch2WithMain)
    grove_add_test(UIFleetDrawerE2E IT_047_ui_fleet_drawer_e2e ${CMAKE_CURRENT_BINARY_DIR})

    # IT_048: vessel-screen jointure — clicking a fleet icon opens the inspector window for that ship.
    add_executable(IT_048_ui_vessel_open_inspector_e2e integration/IT_048_ui_vessel_open_inspector_e2e.cpp)
    target_link_libraries(IT_048_ui_vessel_open_inspector_e2e PRIVATE
        test_helpers GroveEngine::core GroveEngine::impl Catch2::Catch2WithMain)
    grove_add_test(UIVesselOpenInspectorE2E IT_048_ui_vessel_open_inspector_e2e ${CMAKE_CURRENT_BINARY_DIR})

    # IT_049: data-bound tooltip — a repeater item's tooltip resolves {{name}} and shows it on hover.
    add_executable(IT_049_ui_bound_tooltip_e2e integration/IT_049_ui_bound_tooltip_e2e.cpp)
    target_link_libraries(IT_049_ui_bound_tooltip_e2e PRIVATE
        test_helpers GroveEngine::core GroveEngine::impl Catch2::Catch2WithMain)
    grove_add_test(UIBoundTooltipE2E IT_049_ui_bound_tooltip_e2e ${CMAKE_CURRENT_BINARY_DIR})

    # IT_050: modular inventory grid — scrollpanel > grid > {{inventory}} repeater of icon+count cells.
    add_executable(IT_050_ui_inventory_grid_e2e integration/IT_050_ui_inventory_grid_e2e.cpp)
    target_link_libraries(IT_050_ui_inventory_grid_e2e PRIVATE
        test_helpers GroveEngine::core GroveEngine::impl Catch2::Catch2WithMain)
    grove_add_test(UIInventoryGridE2E IT_050_ui_inventory_grid_e2e ${CMAKE_CURRENT_BINARY_DIR})

    # IT_051: fleet selection — left-click selects, right-click opens, a group label selects the group.
    add_executable(IT_051_ui_select_rightclick_e2e integration/IT_051_ui_select_rightclick_e2e.cpp)
    target_link_libraries(IT_051_ui_select_rightclick_e2e PRIVATE
        test_helpers GroveEngine::core GroveEngine::impl Catch2::Catch2WithMain)
    grove_add_test(UISelectRightClickE2E IT_051_ui_select_rightclick_e2e ${CMAKE_CURRENT_BINARY_DIR})

    # IT_052: a UI sprite can reference a streamed ASSET ID (string) — UIButton "asset" prop (literal +
    # data-bound) publishes render:sprite:add{asset} (marries the asset system with sprite-as-UI).
    add_executable(IT_052_ui_asset_sprite_e2e integration/IT_052_ui_asset_sprite_e2e.cpp)
    target_link_libraries(IT_052_ui_asset_sprite_e2e PRIVATE
        test_helpers GroveEngine::core GroveEngine::impl Catch2::Catch2WithMain)
    grove_add_test(UIAssetSpriteE2E IT_052_ui_asset_sprite_e2e ${CMAKE_CURRENT_BINARY_DIR})

    # IT_054: animated flipbook widget E2E (slice 6a) — a sprite-sheet cell advances over time,
    # republished via render:sprite:update (UV plumbed through the retained renderer + change-detection).
    add_executable(IT_054_ui_flipbook_e2e integration/IT_054_ui_flipbook_e2e.cpp)
    target_link_libraries(IT_054_ui_flipbook_e2e PRIVATE
        test_helpers GroveEngine::core GroveEngine::impl Catch2::Catch2WithMain)
    grove_add_test(UIFlipbookE2E IT_054_ui_flipbook_e2e ${CMAKE_CURRENT_BINARY_DIR})

    # IT_055: radio-player UI E2E (slice 6b) — Play/Stop buttons publish real sound:music /
    # sound:music:stop via declarative events; the now-playing label reflects pushed {{radio.title}}.
    add_executable(IT_055_ui_radio_e2e integration/IT_055_ui_radio_e2e.cpp)
    target_link_libraries(IT_055_ui_radio_e2e PRIVATE
        test_helpers GroveEngine::core GroveEngine::impl Catch2::Catch2WithMain)
    grove_add_test(UIRadioE2E IT_055_ui_radio_e2e ${CMAKE_CURRENT_BINARY_DIR})

    # IT_056: N-level TREE list E2E (slice 5d) — deep leaf select / nested expand click-flip /
    # recursive collapse (extends the one-level groups of IT_034 to arbitrary depth via UIList setTree).
    add_executable(IT_056_ui_list_tree_e2e integration/IT_056_ui_list_tree_e2e.cpp)
    target_link_libraries(IT_056_ui_list_tree_e2e PRIVATE
        test_helpers GroveEngine::core GroveEngine::impl Catch2::Catch2WithMain)
    grove_add_test(UIListTreeE2E IT_056_ui_list_tree_e2e ${CMAKE_CURRENT_BINARY_DIR})

    # IT_023: grid reflow E2E — a 2-column grid's cells fill the width and reflow on resize
    # (UI framework slice 1.3).
    add_executable(IT_023_ui_grid_reflow_e2e
        integration/IT_023_ui_grid_reflow_e2e.cpp
    )
    target_link_libraries(IT_023_ui_grid_reflow_e2e PRIVATE
        test_helpers
        GroveEngine::core
        GroveEngine::impl
        Catch2::Catch2WithMain
    )
    grove_add_test(UIGridReflowE2E IT_023_ui_grid_reflow_e2e ${CMAKE_CURRENT_BINARY_DIR})
endif()

# IT_015_Minimal: IIO-only integration test (no module loading, no DLL issues)
add_executable(IT_015_input_ui_integration_minimal
    integration/IT_015_input_ui_integration_minimal.cpp
)

target_link_libraries(IT_015_input_ui_integration_minimal PRIVATE
    test_helpers
    GroveEngine::core
    GroveEngine::impl
    Catch2::Catch2WithMain
)

# CTest integration
grove_add_test(InputUIIntegration_Minimal IT_015_input_ui_integration_minimal ${CMAKE_CURRENT_BINARY_DIR})

message(STATUS "Integration test 'IT_015_input_ui_integration_minimal' enabled (IIO-only)")

# ============================================
# UIModule Interactive Showcase Demo
# ============================================

if(GROVE_BUILD_UI_MODULE AND GROVE_BUILD_BGFX_RENDERER AND SDL2_AVAILABLE)
    add_executable(demo_ui_showcase
        demo/demo_ui_showcase.cpp
    )

    if(SDL2_FOUND)
        target_link_libraries(demo_ui_showcase PRIVATE
            GroveEngine::core
            GroveEngine::impl
            SDL2::SDL2main
            SDL2::SDL2
        )
    else()
        target_link_libraries(demo_ui_showcase PRIVATE
            GroveEngine::core
            GroveEngine::impl
            SDL2
            pthread
            dl
        )
        # Add X11 on Linux for SDL window integration
        if(UNIX AND NOT APPLE)
            target_link_libraries(demo_ui_showcase PRIVATE X11)
        endif()
    endif()

    message(STATUS "UIModule showcase demo 'demo_ui_showcase' enabled")
endif()

# Minimal test using BgfxRenderer_static only (no DLL loading)
if(WIN32 AND GROVE_BUILD_BGFX_RENDERER)
    add_executable(test_bgfx_static_only WIN32
        visual/test_bgfx_static_only.cpp
    )
    target_include_directories(test_bgfx_static_only PRIVATE
        ${CMAKE_SOURCE_DIR}/modules
        ${CMAKE_SOURCE_DIR}/modules/BgfxRenderer
    )
    target_link_libraries(test_bgfx_static_only PRIVATE
        SDL2::SDL2main
        SDL2::SDL2
        GroveEngine::impl
        BgfxRenderer_static
        spdlog::spdlog
    )
    message(STATUS "Minimal BgfxRenderer_static test 'test_bgfx_static_only' enabled")
endif()

# Test using BgfxDevice directly (no BgfxRendererModule)
if(WIN32 AND GROVE_BUILD_BGFX_RENDERER)
    add_executable(test_bgfx_device_only WIN32
        visual/test_bgfx_device_only.cpp
    )
    target_include_directories(test_bgfx_device_only PRIVATE
        ${CMAKE_SOURCE_DIR}/modules
        ${CMAKE_SOURCE_DIR}/modules/BgfxRenderer
    )
    target_link_libraries(test_bgfx_device_only PRIVATE
        SDL2::SDL2main
        SDL2::SDL2
        BgfxRenderer_static
        spdlog::spdlog
    )
    message(STATUS "BgfxDevice direct test 'test_bgfx_device_only' enabled")
endif()

# ================================================================================
# Complete Showcases (Renderer and UI demonstrations)
# ================================================================================

# BgfxRenderer Complete Showcase - all rendering features
if(WIN32 AND GROVE_BUILD_BGFX_RENDERER AND SDL2_AVAILABLE)
    add_executable(test_renderer_showcase WIN32
        visual/test_renderer_showcase.cpp
    )
    target_include_directories(test_renderer_showcase PRIVATE
        ${CMAKE_SOURCE_DIR}/modules
        ${CMAKE_SOURCE_DIR}/modules/BgfxRenderer
    )
    target_link_libraries(test_renderer_showcase PRIVATE
        SDL2::SDL2main
        SDL2::SDL2
        GroveEngine::impl
        BgfxRenderer_static
        spdlog::spdlog
    )
    message(STATUS "BgfxRenderer showcase 'test_renderer_showcase' enabled")

    # GPU readback test (Slice ②): offscreen render -> readPixels -> assert. Needs a real GPU
    # context (hidden SDL window + bgfx), so it is a [gpu] test — it skips cleanly without one.
    # Catch2 owns main() (SDL_MAIN_HANDLED), so link SDL2 WITHOUT SDL2main.
    add_executable(test_rhi_readback unit/test_rhi_readback.cpp)
    target_include_directories(test_rhi_readback PRIVATE
        ${CMAKE_SOURCE_DIR}/modules
        ${CMAKE_SOURCE_DIR}/modules/BgfxRenderer
        ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(test_rhi_readback PRIVATE
        SDL2::SDL2
        GroveEngine::impl
        BgfxRenderer_static
        spdlog::spdlog
        Catch2::Catch2WithMain)
    grove_add_test(RhiReadbackGpu test_rhi_readback ${CMAKE_CURRENT_BINARY_DIR})

    # GPU end-to-end tilemap LOD test (Slice 2.2): render the TilemapPass offscreen and assert the
    # center pixel analytically (detail -> tile color, zoom-out -> average color). [gpu], skips
    # cleanly without a context.
    add_executable(test_tilemap_lod_gpu unit/test_tilemap_lod_gpu.cpp)
    target_include_directories(test_tilemap_lod_gpu PRIVATE
        ${CMAKE_SOURCE_DIR}/modules
        ${CMAKE_SOURCE_DIR}/modules/BgfxRenderer
        ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(test_tilemap_lod_gpu PRIVATE
        SDL2::SDL2
        GroveEngine::impl
        BgfxRenderer_static
        spdlog::spdlog
        Catch2::Catch2WithMain)
    grove_add_test(TilemapLodGpu test_tilemap_lod_gpu ${CMAKE_CURRENT_BINARY_DIR})

    # GPU test: SpritePass clips a sprite to its reserved[] scissor rect (UI clipping, slice 2a-1).
    add_executable(test_sprite_clip_gpu unit/test_sprite_clip_gpu.cpp)
    target_include_directories(test_sprite_clip_gpu PRIVATE
        ${CMAKE_SOURCE_DIR}/modules
        ${CMAKE_SOURCE_DIR}/modules/BgfxRenderer
        ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(test_sprite_clip_gpu PRIVATE
        SDL2::SDL2
        GroveEngine::impl
        BgfxRenderer_static
        spdlog::spdlog
        Catch2::Catch2WithMain)
    grove_add_test(SpriteClipGpu test_sprite_clip_gpu ${CMAKE_CURRENT_BINARY_DIR})

    # GPU test: AssetManager + BgfxTextureProvider load/evict real textures (asset system phase 1, slice 2).
    add_executable(test_asset_provider_gpu unit/test_asset_provider_gpu.cpp)
    target_include_directories(test_asset_provider_gpu PRIVATE
        ${CMAKE_SOURCE_DIR}/modules
        ${CMAKE_SOURCE_DIR}/modules/BgfxRenderer
        ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(test_asset_provider_gpu PRIVATE
        SDL2::SDL2
        GroveEngine::impl
        BgfxRenderer_static
        spdlog::spdlog
        Catch2::Catch2WithMain)
    grove_add_test(AssetProviderGpu test_asset_provider_gpu ${CMAKE_CURRENT_BINARY_DIR})

    # GPU test: render:sprite by asset id streams the texture in via the AssetManager (slice 3 integration).
    add_executable(test_asset_sprite_gpu unit/test_asset_sprite_gpu.cpp)
    target_include_directories(test_asset_sprite_gpu PRIVATE
        ${CMAKE_SOURCE_DIR}/modules
        ${CMAKE_SOURCE_DIR}/modules/BgfxRenderer
        ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(test_asset_sprite_gpu PRIVATE
        SDL2::SDL2
        GroveEngine::impl
        BgfxRenderer_static
        spdlog::spdlog
        Catch2::Catch2WithMain)
    grove_add_test(AssetSpriteGpu test_asset_sprite_gpu ${CMAKE_CURRENT_BINARY_DIR})

    # GPU test: declarative manifest + asset:* runtime topics feed the AssetManager (slice 4).
    add_executable(test_asset_topics_gpu unit/test_asset_topics_gpu.cpp)
    target_include_directories(test_asset_topics_gpu PRIVATE
        ${CMAKE_SOURCE_DIR}/modules
        ${CMAKE_SOURCE_DIR}/modules/BgfxRenderer
        ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(test_asset_topics_gpu PRIVATE
        SDL2::SDL2
        GroveEngine::impl
        BgfxRenderer_static
        spdlog::spdlog
        Catch2::Catch2WithMain)
    grove_add_test(AssetTopicsGpu test_asset_topics_gpu ${CMAKE_CURRENT_BINARY_DIR})

    # GPU test: runtime atlas packing (phase 2b) — packAtlas + asset:pack topic.
    add_executable(test_atlas_packer_gpu unit/test_atlas_packer_gpu.cpp)
    target_include_directories(test_atlas_packer_gpu PRIVATE
        ${CMAKE_SOURCE_DIR}/modules
        ${CMAKE_SOURCE_DIR}/modules/BgfxRenderer
        ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(test_atlas_packer_gpu PRIVATE
        SDL2::SDL2
        GroveEngine::impl
        BgfxRenderer_static
        spdlog::spdlog
        Catch2::Catch2WithMain)
    grove_add_test(AtlasPackerGpu test_atlas_packer_gpu ${CMAKE_CURRENT_BINARY_DIR})

    # GPU test: async load (phase 3) — ThreadedDecoder decodes a real PNG off-thread, pump uploads it.
    add_executable(test_asset_async_gpu unit/test_asset_async_gpu.cpp)
    target_include_directories(test_asset_async_gpu PRIVATE
        ${CMAKE_SOURCE_DIR}/modules
        ${CMAKE_SOURCE_DIR}/modules/BgfxRenderer
        ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(test_asset_async_gpu PRIVATE
        SDL2::SDL2
        GroveEngine::impl
        BgfxRenderer_static
        spdlog::spdlog
        Catch2::Catch2WithMain)
    grove_add_test(AssetAsyncGpu test_asset_async_gpu ${CMAKE_CURRENT_BINARY_DIR})

    # GPU E2E: async load wiring inside BgfxRendererModule — assetAsyncLoad flag + pumpAsync() in process() +
    # render:sprite{asset} streaming in across frames. Own process (bgfx is a process-wide singleton).
    add_executable(test_asset_async_module_gpu unit/test_asset_async_module_gpu.cpp)
    target_include_directories(test_asset_async_module_gpu PRIVATE
        ${CMAKE_SOURCE_DIR}/modules
        ${CMAKE_SOURCE_DIR}/modules/BgfxRenderer
        ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(test_asset_async_module_gpu PRIVATE
        SDL2::SDL2
        GroveEngine::impl
        BgfxRenderer_static
        spdlog::spdlog
        Catch2::Catch2WithMain)
    grove_add_test(AssetAsyncModuleGpu test_asset_async_module_gpu ${CMAKE_CURRENT_BINARY_DIR})

    # GPU test: runtime textures / painting — render:texture:create + :paint (region) -> render by asset id.
    add_executable(test_runtime_texture_gpu unit/test_runtime_texture_gpu.cpp)
    target_include_directories(test_runtime_texture_gpu PRIVATE
        ${CMAKE_SOURCE_DIR}/modules
        ${CMAKE_SOURCE_DIR}/modules/BgfxRenderer
        ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(test_runtime_texture_gpu PRIVATE
        SDL2::SDL2
        GroveEngine::impl
        BgfxRenderer_static
        spdlog::spdlog
        Catch2::Catch2WithMain)
    grove_add_test(RuntimeTextureGpu test_runtime_texture_gpu ${CMAKE_CURRENT_BINARY_DIR})

    # GPU test: TextPass clips text to a TextCommand's scissor rect (UI clipping, slice 2a-2a).
    add_executable(test_text_clip_gpu unit/test_text_clip_gpu.cpp)
    target_include_directories(test_text_clip_gpu PRIVATE
        ${CMAKE_SOURCE_DIR}/modules
        ${CMAKE_SOURCE_DIR}/modules/BgfxRenderer
        ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(test_text_clip_gpu PRIVATE
        SDL2::SDL2
        GroveEngine::impl
        BgfxRenderer_static
        spdlog::spdlog
        Catch2::Catch2WithMain)
    grove_add_test(TextClipGpu test_text_clip_gpu ${CMAKE_CURRENT_BINARY_DIR})

    # GPU end-to-end: the full "Fleet Command" demo renders on real bgfx (UIModule + BgfxRendererModule,
    # hidden window). Needs the UI module too.
    if(GROVE_BUILD_UI_MODULE)
        add_executable(test_ui_demo_gpu unit/test_ui_demo_gpu.cpp)
        target_include_directories(test_ui_demo_gpu PRIVATE
            ${CMAKE_SOURCE_DIR}/modules
            ${CMAKE_SOURCE_DIR}/modules/BgfxRenderer
            ${CMAKE_SOURCE_DIR}/modules/UIModule
            ${CMAKE_CURRENT_SOURCE_DIR})
        target_link_libraries(test_ui_demo_gpu PRIVATE
            SDL2::SDL2
            GroveEngine::impl
            BgfxRenderer_static
            UIModule_static
            spdlog::spdlog
            Catch2::Catch2WithMain)
        grove_add_test(UIDemoGpu test_ui_demo_gpu ${CMAKE_CURRENT_BINARY_DIR})
    endif()

    # ctest's per-test ENVIRONMENT mangles PATH (CMake splits it on ';', the same char Windows PATH
    # uses as a separator), so only the working_dir survives and SDL2.dll (in the toolchain) is lost
    # -> the GPU tests fail to load (0xc0000135). Copy SDL2.dll next to the exes so it's found in the
    # working_dir regardless of PATH.
    find_file(GROVE_SDL2_DLL NAMES SDL2.dll PATHS ENV PATH)
    if(GROVE_SDL2_DLL)
        foreach(gpu_test test_rhi_readback test_tilemap_lod_gpu test_sprite_clip_gpu test_text_clip_gpu)
            add_custom_command(TARGET ${gpu_test} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${GROVE_SDL2_DLL}" "$<TARGET_FILE_DIR:${gpu_test}>")
        endforeach()
        if(TARGET test_ui_demo_gpu)
            add_custom_command(TARGET test_ui_demo_gpu POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${GROVE_SDL2_DLL}" "$<TARGET_FILE_DIR:test_ui_demo_gpu>")
        endif()
    else()
        message(WARNING "SDL2.dll not found on PATH; GPU tests may fail under ctest (0xc0000135)")
    endif()

    # Retained-mode demo (render:sprite:add/:update/:remove + render:text:add)
    add_executable(test_retained_showcase WIN32
        visual/test_retained_showcase.cpp
    )
    target_include_directories(test_retained_showcase PRIVATE
        ${CMAKE_SOURCE_DIR}/modules
        ${CMAKE_SOURCE_DIR}/modules/BgfxRenderer
    )
    target_link_libraries(test_retained_showcase PRIVATE
        SDL2::SDL2main
        SDL2::SDL2
        GroveEngine::impl
        BgfxRenderer_static
        spdlog::spdlog
    )
    message(STATUS "Retained-mode demo 'test_retained_showcase' enabled")
endif()

# UIModule Complete Showcase - all UI widgets
if(WIN32 AND GROVE_BUILD_BGFX_RENDERER AND GROVE_BUILD_UI_MODULE AND SDL2_AVAILABLE)
    add_executable(test_ui_showcase WIN32
        visual/test_ui_showcase.cpp
    )
    target_include_directories(test_ui_showcase PRIVATE
        ${CMAKE_SOURCE_DIR}/modules
        ${CMAKE_SOURCE_DIR}/modules/BgfxRenderer
        ${CMAKE_SOURCE_DIR}/modules/UIModule
    )
    target_link_libraries(test_ui_showcase PRIVATE
        SDL2::SDL2main
        SDL2::SDL2
        GroveEngine::impl
        BgfxRenderer_static
        UIModule_static
        spdlog::spdlog
    )
    message(STATUS "UIModule showcase 'test_ui_showcase' enabled")
endif()

# FULL UI demo - "Fleet Command": the whole framework + JSON data-binding engine in one screen.
if(WIN32 AND GROVE_BUILD_BGFX_RENDERER AND GROVE_BUILD_UI_MODULE AND SDL2_AVAILABLE)
    add_executable(test_ui_full_demo WIN32
        visual/test_ui_full_demo.cpp
    )
    target_include_directories(test_ui_full_demo PRIVATE
        ${CMAKE_SOURCE_DIR}/modules
        ${CMAKE_SOURCE_DIR}/modules/BgfxRenderer
        ${CMAKE_SOURCE_DIR}/modules/UIModule
    )
    target_link_libraries(test_ui_full_demo PRIVATE
        SDL2::SDL2main
        SDL2::SDL2
        GroveEngine::impl
        BgfxRenderer_static
        UIModule_static
        spdlog::spdlog
    )
    message(STATUS "Full UI demo 'test_ui_full_demo' enabled")

    # Headless CAPTURE of the demo to a PNG (offscreen framebuffer readback). Console exe (own main()),
    # so SDL2 WITHOUT SDL2main. Needs SDL2.dll next to it to run (like the [gpu] tests).
    add_executable(capture_ui_demo visual/capture_ui_demo.cpp)
    target_include_directories(capture_ui_demo PRIVATE
        ${CMAKE_SOURCE_DIR}/modules
        ${CMAKE_SOURCE_DIR}/modules/BgfxRenderer
        ${CMAKE_SOURCE_DIR}/modules/UIModule
    )
    target_link_libraries(capture_ui_demo PRIVATE
        SDL2::SDL2
        GroveEngine::impl
        BgfxRenderer_static
        UIModule_static
        spdlog::spdlog
    )
    if(GROVE_SDL2_DLL)
        add_custom_command(TARGET capture_ui_demo POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${GROVE_SDL2_DLL}" "$<TARGET_FILE_DIR:capture_ui_demo>")
    endif()
    message(STATUS "Demo capture tool 'capture_ui_demo' enabled")

    # Engine-hosted capture: the SAME Fleet Command screen, but UIModule + BgfxRenderer
    # are HOSTED ON THE ENGINE (registerStaticModule + engine.step()), not hand-wired.
    # The visual half of the "UI runs on the engine" proof (headless logic lock = IT_053).
    add_executable(capture_ui_on_engine visual/capture_ui_on_engine.cpp)
    target_include_directories(capture_ui_on_engine PRIVATE
        ${CMAKE_SOURCE_DIR}/modules
        ${CMAKE_SOURCE_DIR}/modules/BgfxRenderer
        ${CMAKE_SOURCE_DIR}/modules/UIModule
    )
    target_link_libraries(capture_ui_on_engine PRIVATE
        SDL2::SDL2
        GroveEngine::impl
        BgfxRenderer_static
        UIModule_static
        spdlog::spdlog
    )
    if(GROVE_SDL2_DLL)
        add_custom_command(TARGET capture_ui_on_engine POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${GROVE_SDL2_DLL}" "$<TARGET_FILE_DIR:capture_ui_on_engine>")
    endif()
    message(STATUS "Engine-hosted capture tool 'capture_ui_on_engine' enabled")

    # Map-viewer capture (P2): a synthetic procedural world -> grove::mapview (terrain palette + hillshade)
    # -> SpriteAdapter (CellDraw->SpriteInstance) -> submitSpriteBatch -> offscreen PNG. The end-to-end
    # render proof of the map viewer. Console exe (own main()) so SDL2 WITHOUT SDL2main; no UIModule.
    add_executable(capture_mapview visual/capture_mapview.cpp)
    target_include_directories(capture_mapview PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/modules
        ${CMAKE_SOURCE_DIR}/modules/BgfxRenderer
    )
    target_link_libraries(capture_mapview PRIVATE
        SDL2::SDL2
        GroveEngine::impl
        BgfxRenderer_static
        spdlog::spdlog
    )
    if(GROVE_SDL2_DLL)
        add_custom_command(TARGET capture_mapview POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${GROVE_SDL2_DLL}" "$<TARGET_FILE_DIR:capture_mapview>")
    endif()
    message(STATUS "Map-viewer capture tool 'capture_mapview' enabled")

    # NOTE: test_mapview_viewer moved to a CROSS-PLATFORM block at the END of this file — it is now Linux-clean
    # (SdlNativeHandle.h for the window/display handle + the renderer's OpenGL backend), so it must NOT be gated
    # to WIN32 like the rest of this block. The E2E below still reads the win32 handle directly -> stays here.

    # Viewer INPUT E2E: injects real SDL events (drag/wheel/keys via SDL_PushEvent) through the app's own event
    # pump and asserts the camera + culled cell set respond — the proof the viewer's interactivity exists (not
    # just reads-as-correct). Real GPU (hidden window + offscreen FB + PNG). Registered as a ctest.
    add_executable(test_mapview_viewer_e2e
        visual/test_mapview_viewer_e2e.cpp
        ${GROVE_MINIZ_DIR}/miniz.c)
    target_include_directories(test_mapview_viewer_e2e PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/modules
        ${CMAKE_SOURCE_DIR}/modules/BgfxRenderer
        ${GROVE_NLOHMANN_DIR}
        ${GROVE_MINIZ_DIR}
    )
    target_link_libraries(test_mapview_viewer_e2e PRIVATE
        SDL2::SDL2
        GroveEngine::impl
        BgfxRenderer_static
        spdlog::spdlog
    )
    if(GROVE_SDL2_DLL)
        add_custom_command(TARGET test_mapview_viewer_e2e POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${GROVE_SDL2_DLL}" "$<TARGET_FILE_DIR:test_mapview_viewer_e2e>")
    endif()
    grove_add_test(MapViewViewerE2E test_mapview_viewer_e2e ${CMAKE_CURRENT_BINARY_DIR} 60)
    message(STATUS "Map viewer input E2E 'test_mapview_viewer_e2e' enabled")

    # Tiled-map capture: the terrain drawn as a TILEMAP from a generated PNG tileset (elevation band -> tile),
    # via render:tilemap:add + the new render:tilemap:tileset topic. The "tiling" path proof (vs sprite cells).
    add_executable(capture_mapview_tiles visual/capture_mapview_tiles.cpp)
    target_include_directories(capture_mapview_tiles PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/modules
        ${CMAKE_SOURCE_DIR}/modules/BgfxRenderer
    )
    target_link_libraries(capture_mapview_tiles PRIVATE
        SDL2::SDL2
        GroveEngine::impl
        BgfxRenderer_static
        spdlog::spdlog
    )
    if(GROVE_SDL2_DLL)
        add_custom_command(TARGET capture_mapview_tiles POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${GROVE_SDL2_DLL}" "$<TARGET_FILE_DIR:capture_mapview_tiles>")
    endif()
    message(STATUS "Tiled-map capture tool 'capture_mapview_tiles' enabled")

    # From-disk capture: the "file is the interface" thesis, pixels-out. WRITES a world-document to disk
    # (manifest + zlib chunks), then LOADS it via WorldDocumentProvider and renders it — proving the full
    # disk -> provider -> MapView chain. Needs nlohmann (Manifest) + miniz.c (compression), unlike the sprite
    # captures. This is the E2E lock the pure-core unit tests couldn't see (they never touch a live renderer).
    add_executable(capture_mapview_from_disk
        visual/capture_mapview_from_disk.cpp
        ${GROVE_MINIZ_DIR}/miniz.c)
    target_include_directories(capture_mapview_from_disk PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/modules
        ${CMAKE_SOURCE_DIR}/modules/BgfxRenderer
        ${GROVE_NLOHMANN_DIR}
        ${GROVE_MINIZ_DIR}
    )
    target_link_libraries(capture_mapview_from_disk PRIVATE
        SDL2::SDL2
        GroveEngine::impl
        BgfxRenderer_static
        spdlog::spdlog
    )
    if(GROVE_SDL2_DLL)
        add_custom_command(TARGET capture_mapview_from_disk POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${GROVE_SDL2_DLL}" "$<TARGET_FILE_DIR:capture_mapview_from_disk>")
    endif()
    message(STATUS "From-disk map-viewer capture tool 'capture_mapview_from_disk' enabled")

    # Ship-inspector capture: proves sprites-as-UI (SVG->PNG part textures) + data-driven part blocks.
    add_executable(capture_ship_inspector visual/capture_ship_inspector.cpp)
    target_include_directories(capture_ship_inspector PRIVATE
        ${CMAKE_SOURCE_DIR}/modules
        ${CMAKE_SOURCE_DIR}/modules/BgfxRenderer
        ${CMAKE_SOURCE_DIR}/modules/UIModule
    )
    target_link_libraries(capture_ship_inspector PRIVATE
        SDL2::SDL2
        GroveEngine::impl
        BgfxRenderer_static
        UIModule_static
        spdlog::spdlog
    )
    if(GROVE_SDL2_DLL)
        add_custom_command(TARGET capture_ship_inspector POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${GROVE_SDL2_DLL}" "$<TARGET_FILE_DIR:capture_ship_inspector>")
    endif()
    message(STATUS "Ship inspector capture 'capture_ship_inspector' enabled")

    # Vessel-screen capture: proves the off-screen fleet drawer opened (horizontal bar of ship icons).
    add_executable(capture_vessel_screen visual/capture_vessel_screen.cpp)
    target_include_directories(capture_vessel_screen PRIVATE
        ${CMAKE_SOURCE_DIR}/modules
        ${CMAKE_SOURCE_DIR}/modules/BgfxRenderer
        ${CMAKE_SOURCE_DIR}/modules/UIModule
    )
    target_link_libraries(capture_vessel_screen PRIVATE
        SDL2::SDL2
        GroveEngine::impl
        BgfxRenderer_static
        UIModule_static
        spdlog::spdlog
    )
    if(GROVE_SDL2_DLL)
        add_custom_command(TARGET capture_vessel_screen POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${GROVE_SDL2_DLL}" "$<TARGET_FILE_DIR:capture_vessel_screen>")
    endif()
    message(STATUS "Vessel screen capture 'capture_vessel_screen' enabled")

    # Visual demo of ASYNC asset streaming (phase 3): a grid of sprites-by-assetId pop in from magenta
    # placeholders to real ships as the worker decodes them. Writes a numbered PNG sequence.
    add_executable(capture_async_stream visual/capture_async_stream.cpp)
    target_include_directories(capture_async_stream PRIVATE
        ${CMAKE_SOURCE_DIR}/modules
        ${CMAKE_SOURCE_DIR}/modules/BgfxRenderer
    )
    target_link_libraries(capture_async_stream PRIVATE
        SDL2::SDL2
        GroveEngine::impl
        BgfxRenderer_static
        spdlog::spdlog
    )
    if(GROVE_SDL2_DLL)
        add_custom_command(TARGET capture_async_stream POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${GROVE_SDL2_DLL}" "$<TARGET_FILE_DIR:capture_async_stream>")
    endif()
    message(STATUS "Async streaming capture 'capture_async_stream' enabled")

    # INTERACTIVE live demo of the async asset streaming system: a real window where a grid of sprites-by-
    # assetId streams in from magenta placeholders (SPACE/auto re-triggers the wave). For VOIR le moteur tourner.
    add_executable(test_async_stream_demo visual/test_async_stream_demo.cpp)
    target_include_directories(test_async_stream_demo PRIVATE
        ${CMAKE_SOURCE_DIR}/modules
        ${CMAKE_SOURCE_DIR}/modules/BgfxRenderer
    )
    target_link_libraries(test_async_stream_demo PRIVATE
        SDL2::SDL2
        GroveEngine::impl
        BgfxRenderer_static
        spdlog::spdlog
    )
    if(GROVE_SDL2_DLL)
        add_custom_command(TARGET test_async_stream_demo POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${GROVE_SDL2_DLL}" "$<TARGET_FILE_DIR:test_async_stream_demo>")
    endif()
    message(STATUS "Async streaming live demo 'test_async_stream_demo' enabled")

    # GROS SAUVAGE — rendering throughput benchmark (windowed, vsync OFF). Ramps sprites/
    # particles/text/tilemap until the frame time melts, reads bgfx GPU/CPU timers, finds the
    # 60/30fps ceilings + names the bottleneck. Plain main() (SDL_MAIN_HANDLED), not a ctest
    # (wall-clock measurement, machine/driver dependent — like benchmark_pool_vs_threaded).
    add_executable(benchmark_render_savage visual/benchmark_render_savage.cpp)
    target_include_directories(benchmark_render_savage PRIVATE
        ${CMAKE_SOURCE_DIR}/modules
        ${CMAKE_SOURCE_DIR}/modules/BgfxRenderer
    )
    target_link_libraries(benchmark_render_savage PRIVATE
        SDL2::SDL2
        GroveEngine::impl
        BgfxRenderer_static
        spdlog::spdlog
    )
    if(GROVE_SDL2_DLL)
        add_custom_command(TARGET benchmark_render_savage POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${GROVE_SDL2_DLL}" "$<TARGET_FILE_DIR:benchmark_render_savage>")
    endif()
    message(STATUS "Render savage benchmark 'benchmark_render_savage' enabled")

    # INTERACTIVE live demo: stream textures from an ONLINE image bank (picsum.photos) via a demo-side
    # curl-backed IAsyncDecoder — proves the asset pipeline streams from remote sources (network-paced fill).
    add_executable(test_online_stream_demo visual/test_online_stream_demo.cpp)
    target_include_directories(test_online_stream_demo PRIVATE
        ${CMAKE_SOURCE_DIR}/modules
        ${CMAKE_SOURCE_DIR}/modules/BgfxRenderer
    )
    target_link_libraries(test_online_stream_demo PRIVATE
        SDL2::SDL2
        GroveEngine::impl
        BgfxRenderer_static
        spdlog::spdlog
    )
    if(GROVE_SDL2_DLL)
        add_custom_command(TARGET test_online_stream_demo POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${GROVE_SDL2_DLL}" "$<TARGET_FILE_DIR:test_online_stream_demo>")
    endif()
    message(STATUS "Online streaming live demo 'test_online_stream_demo' enabled")

    # Interactive SMART-RESIZE demo: the SAME responsive demo_ship_inspector.json in a real RESIZABLE
    # window. Etire un coin -> tout reflow ; clic une piece -> info live. WIN32 GUI app + SDL2main.
    add_executable(test_ship_inspector_demo WIN32 visual/test_ship_inspector_demo.cpp)
    target_include_directories(test_ship_inspector_demo PRIVATE
        ${CMAKE_SOURCE_DIR}/modules
        ${CMAKE_SOURCE_DIR}/modules/BgfxRenderer
        ${CMAKE_SOURCE_DIR}/modules/UIModule
    )
    target_link_libraries(test_ship_inspector_demo PRIVATE
        SDL2::SDL2main
        SDL2::SDL2
        GroveEngine::impl
        BgfxRenderer_static
        UIModule_static
        spdlog::spdlog
    )
    message(STATUS "Ship inspector demo 'test_ship_inspector_demo' enabled")

    # Interactive VESSEL SCREEN demo: the off-screen fleet drawer + data-driven ship vignettes (slices 2+).
    add_executable(test_vessel_screen_demo WIN32 visual/test_vessel_screen_demo.cpp)
    target_include_directories(test_vessel_screen_demo PRIVATE
        ${CMAKE_SOURCE_DIR}/modules
        ${CMAKE_SOURCE_DIR}/modules/BgfxRenderer
        ${CMAKE_SOURCE_DIR}/modules/UIModule
    )
    target_link_libraries(test_vessel_screen_demo PRIVATE
        SDL2::SDL2main
        SDL2::SDL2
        GroveEngine::impl
        BgfxRenderer_static
        UIModule_static
        spdlog::spdlog
    )
    message(STATUS "Vessel screen demo 'test_vessel_screen_demo' enabled")
endif()

# Sprite Rendering Diagnostic Test - isolates IIO sprite pipeline
if(WIN32 AND GROVE_BUILD_BGFX_RENDERER AND SDL2_AVAILABLE)
    add_executable(test_sprite_debug WIN32
        visual/test_sprite_debug.cpp
    )
    target_include_directories(test_sprite_debug PRIVATE
        ${CMAKE_SOURCE_DIR}/modules
        ${CMAKE_SOURCE_DIR}/modules/BgfxRenderer
    )
    target_link_libraries(test_sprite_debug PRIVATE
        SDL2::SDL2main
        SDL2::SDL2
        GroveEngine::impl
        BgfxRenderer_static
        spdlog::spdlog
    )
    message(STATUS "Sprite diagnostic 'test_sprite_debug' enabled")
endif()

# Single Button Test - one UIButton only
if(WIN32 AND GROVE_BUILD_BGFX_RENDERER AND GROVE_BUILD_UI_MODULE AND SDL2_AVAILABLE)
    add_executable(test_single_button WIN32
        visual/test_single_button.cpp
    )
    target_include_directories(test_single_button PRIVATE
        ${CMAKE_SOURCE_DIR}/modules
        ${CMAKE_SOURCE_DIR}/modules/BgfxRenderer
        ${CMAKE_SOURCE_DIR}/modules/UIModule
    )
    target_link_libraries(test_single_button PRIVATE
        SDL2::SDL2main
        SDL2::SDL2
        GroveEngine::impl
        BgfxRenderer_static
        UIModule_static
        spdlog::spdlog
    )
    message(STATUS "Single button test 'test_single_button' enabled")
endif()

# UI Texture Support Test - headless test demonstrating texture properties
if(GROVE_BUILD_UI_MODULE)
    add_executable(test_ui_textures
        visual/test_ui_textures.cpp
    )
    target_include_directories(test_ui_textures PRIVATE
        ${CMAKE_SOURCE_DIR}/modules
        ${CMAKE_SOURCE_DIR}/modules/UIModule
    )
    target_link_libraries(test_ui_textures PRIVATE
        GroveEngine::impl
        UIModule_static
        spdlog::spdlog
    )
    message(STATUS "UI texture support test 'test_ui_textures' enabled")
endif()

# Textured UI Visual Demo - shows widgets with custom textures
if(WIN32 AND GROVE_BUILD_BGFX_RENDERER AND GROVE_BUILD_UI_MODULE AND SDL2_AVAILABLE)
    add_executable(test_ui_textured_demo WIN32
        visual/test_ui_textured_demo.cpp
    )
    target_include_directories(test_ui_textured_demo PRIVATE
        ${CMAKE_SOURCE_DIR}/modules
        ${CMAKE_SOURCE_DIR}/modules/BgfxRenderer
        ${CMAKE_SOURCE_DIR}/modules/UIModule
    )
    target_link_libraries(test_ui_textured_demo PRIVATE
        SDL2::SDL2main
        SDL2::SDL2
        GroveEngine::impl
        BgfxRenderer_static
        UIModule_static
        spdlog::spdlog
    )
    message(STATUS "Textured UI demo 'test_ui_textured_demo' enabled")
endif()

# Simple textured UI demo - shows widget properties (no rendering)
if(GROVE_BUILD_UI_MODULE)
    add_executable(test_ui_textured_simple
        visual/test_ui_textured_simple.cpp
    )
    target_include_directories(test_ui_textured_simple PRIVATE
        ${CMAKE_SOURCE_DIR}/modules
        ${CMAKE_SOURCE_DIR}/modules/UIModule
    )
    target_link_libraries(test_ui_textured_simple PRIVATE
        GroveEngine::impl
        UIModule_static
        spdlog::spdlog
    )
    message(STATUS "Simple textured UI demo 'test_ui_textured_simple' enabled")
endif()

# Textured Button Visual Test - shows REAL textures on buttons
if(WIN32 AND GROVE_BUILD_BGFX_RENDERER AND GROVE_BUILD_UI_MODULE AND SDL2_AVAILABLE)
    add_executable(test_textured_button WIN32
        visual/test_textured_button.cpp
    )
    target_include_directories(test_textured_button PRIVATE
        ${CMAKE_SOURCE_DIR}/modules
        ${CMAKE_SOURCE_DIR}/modules/BgfxRenderer
        ${CMAKE_SOURCE_DIR}/modules/UIModule
    )
    target_link_libraries(test_textured_button PRIVATE
        SDL2::SDL2main
        SDL2::SDL2
        GroveEngine::impl
        BgfxRenderer_static
        UIModule_static
        spdlog::spdlog
    )
    message(STATUS "Textured button test 'test_textured_button' enabled")
endif()

# Minimal Textured Demo - Direct sprite rendering with textures
if(WIN32 AND GROVE_BUILD_BGFX_RENDERER AND SDL2_AVAILABLE)
    add_executable(test_textured_demo_minimal WIN32
        visual/test_textured_demo_minimal.cpp
    )
    target_include_directories(test_textured_demo_minimal PRIVATE
        ${CMAKE_SOURCE_DIR}/modules
        ${CMAKE_SOURCE_DIR}/modules/BgfxRenderer
    )
    target_link_libraries(test_textured_demo_minimal PRIVATE
        SDL2::SDL2main
        SDL2::SDL2
        GroveEngine::impl
        BgfxRenderer_static
        spdlog::spdlog
    )
    message(STATUS "Minimal textured demo 'test_textured_demo_minimal' enabled")
endif()

# Button with PNG texture - Load real PNG file and apply to button
if(WIN32 AND GROVE_BUILD_BGFX_RENDERER AND GROVE_BUILD_UI_MODULE AND SDL2_AVAILABLE)
    add_executable(test_button_with_png WIN32
        visual/test_button_with_png.cpp
    )
    target_include_directories(test_button_with_png PRIVATE
        ${CMAKE_SOURCE_DIR}/modules
        ${CMAKE_SOURCE_DIR}/modules/BgfxRenderer
        ${CMAKE_SOURCE_DIR}/modules/UIModule
    )
    target_link_libraries(test_button_with_png PRIVATE
        SDL2::SDL2main
        SDL2::SDL2
        GroveEngine::impl
        BgfxRenderer_static
        UIModule_static
        spdlog::spdlog
    )
    message(STATUS "PNG button test 'test_button_with_png' enabled")
endif()

# 3 Buttons Minimal Test - 3 textured buttons in minimal layout
if(WIN32 AND GROVE_BUILD_BGFX_RENDERER AND GROVE_BUILD_UI_MODULE AND SDL2_AVAILABLE)
    add_executable(test_3buttons_minimal WIN32
        visual/test_3buttons_minimal.cpp
    )
    target_include_directories(test_3buttons_minimal PRIVATE
        ${CMAKE_SOURCE_DIR}/modules
        ${CMAKE_SOURCE_DIR}/modules/BgfxRenderer
        ${CMAKE_SOURCE_DIR}/modules/UIModule
    )
    target_link_libraries(test_3buttons_minimal PRIVATE
        SDL2::SDL2main
        SDL2::SDL2
        GroveEngine::impl
        BgfxRenderer_static
        UIModule_static
        spdlog::spdlog
    )
    message(STATUS "3 buttons minimal test 'test_3buttons_minimal' enabled")
endif()

# 1 Button Texture 2 Test - diagnostic to see if only texture 1 works
if(WIN32 AND GROVE_BUILD_BGFX_RENDERER AND GROVE_BUILD_UI_MODULE AND SDL2_AVAILABLE)
    add_executable(test_1button_texture2 WIN32
        visual/test_1button_texture2.cpp
    )
    target_include_directories(test_1button_texture2 PRIVATE
        ${CMAKE_SOURCE_DIR}/modules
        ${CMAKE_SOURCE_DIR}/modules/BgfxRenderer
        ${CMAKE_SOURCE_DIR}/modules/UIModule
    )
    target_link_libraries(test_1button_texture2 PRIVATE
        SDL2::SDL2main
        SDL2::SDL2
        GroveEngine::impl
        BgfxRenderer_static
        UIModule_static
        spdlog::spdlog
    )
    message(STATUS "1 button texture 2 test 'test_1button_texture2' enabled")
endif()

# Direct sprite texture test - bypasses UIModule, uses renderer directly
if(WIN32 AND GROVE_BUILD_BGFX_RENDERER AND SDL2_AVAILABLE)
    add_executable(test_direct_sprite_texture WIN32
        visual/test_direct_sprite_texture.cpp
    )
    target_include_directories(test_direct_sprite_texture PRIVATE
        ${CMAKE_SOURCE_DIR}/modules
        ${CMAKE_SOURCE_DIR}/modules/BgfxRenderer
    )
    target_link_libraries(test_direct_sprite_texture PRIVATE
        SDL2::SDL2main
        SDL2::SDL2
        GroveEngine::impl
        BgfxRenderer_static
        spdlog::spdlog
    )
    message(STATUS "Direct sprite texture test 'test_direct_sprite_texture' enabled")
endif()

# InputModule static-host path — headless E2E (engine help B1). feedEvent -> input:*.
# Gated on GROVE_BUILD_INPUT_MODULE (which implies SDL2 was found; InputModule_static
# only exists then). No window/SDL_Init needed — SDL events are constructed as structs.
if(GROVE_BUILD_INPUT_MODULE)
    add_executable(test_input_module unit/test_input_module.cpp)
    target_include_directories(test_input_module PRIVATE ${CMAKE_SOURCE_DIR}/modules)
    target_link_libraries(test_input_module PRIVATE
        GroveEngine::core
        GroveEngine::impl
        InputModule_static
        Catch2::Catch2WithMain
    )
    grove_add_test(InputModuleStatic test_input_module ${CMAKE_CURRENT_BINARY_DIR})
    message(STATUS "Headless test 'test_input_module' enabled (InputModule_static, B1)")
endif()

# ================================================================================
# Cross-platform GPU tools (Windows + Linux). Unlike the WIN32-guarded visual block above, these mains are
# platform-clean — SdlNativeHandle.h resolves the window/display handle per OS, and the renderer picks
# Direct3D11 on Windows / OpenGL on Linux — so they build AND run on Linux too (headless via Xvfb + Mesa
# software GL). Port more visual tools into this block as each is made Linux-clean.
# ================================================================================
if(GROVE_BUILD_BGFX_RENDERER AND SDL2_AVAILABLE)
    # Interactive map viewer + headless PNG export (--shot / --poster). Runs on Linux under Xvfb + software GL.
    add_executable(test_mapview_viewer
        visual/test_mapview_viewer.cpp
        visual/SdlNativeHandle.cpp   # isolates SDL_syswm.h/X11 so it can't collide with RHITypes.h
        ${GROVE_MINIZ_DIR}/miniz.c)
    target_include_directories(test_mapview_viewer PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/modules
        ${CMAKE_SOURCE_DIR}/modules/BgfxRenderer
        ${GROVE_NLOHMANN_DIR}
        ${GROVE_MINIZ_DIR}
    )
    target_link_libraries(test_mapview_viewer PRIVATE
        SDL2::SDL2
        GroveEngine::impl
        BgfxRenderer_static
        spdlog::spdlog
    )
    if(GROVE_SDL2_DLL)  # Windows: copy SDL2.dll next to the exe (no-op on Linux — SDL2 is a system lib)
        add_custom_command(TARGET test_mapview_viewer POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${GROVE_SDL2_DLL}" "$<TARGET_FILE_DIR:test_mapview_viewer>")
    endif()
    message(STATUS "Cross-platform map viewer 'test_mapview_viewer' enabled")
endif()
