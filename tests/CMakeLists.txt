# Hot-reload test suite

# ================================================================================
# SDL2 Detection (used by visual tests, demos, and InputModule-dependent tests)
# ================================================================================
find_package(SDL2 QUIET)

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
