# Hot-reload test suite

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

# Helpers library (partagée par tous les tests)
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

# TankModule pour tests d'intégration
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
add_test(NAME ProductionHotReload COMMAND test_01_production_hotreload WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

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
add_test(NAME ChaosMonkey COMMAND test_02_chaos_monkey WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

# StressModule pour tests de stabilité long-terme
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
add_test(NAME StressTest COMMAND test_03_stress_test WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

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
add_test(NAME RaceConditionHunter COMMAND test_04_race_condition WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

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
add_test(NAME MemoryLeakHunter COMMAND test_05_memory_leak WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

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
add_test(NAME ErrorRecovery COMMAND test_06_error_recovery WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

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

# CTest integration
add_test(NAME LimitsTest COMMAND test_07_limits WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
add_test(NAME DataNodeTest COMMAND test_12_datanode WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

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
add_test(NAME CrossSystemIntegration COMMAND test_13_cross_system WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

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
add_test(NAME ConfigHotReload COMMAND test_08_config_hotreload WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

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
add_test(NAME ModuleDependencies COMMAND test_09_module_dependencies WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

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
add_test(NAME MultiVersionCoexistence COMMAND test_10_multiversion_coexistence WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

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
add_test(NAME IOSystemStress COMMAND test_11_io_system WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

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
    add_test(NAME BgfxRHI COMMAND test_20_bgfx_rhi WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

    # Test 21: Visual Triangle Test (requires SDL2 and display)
    find_package(SDL2 QUIET)
    if(SDL2_FOUND OR EXISTS "/usr/include/SDL2/SDL.h")
        add_executable(test_21_bgfx_triangle
            visual/test_bgfx_triangle.cpp
        )

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

        # Not added to CTest (requires display)
        message(STATUS "Visual test 'test_21_bgfx_triangle' enabled (run manually)")

        # Test 22: Sprite Integration Test (requires SDL2, display, and BgfxRenderer module)
        add_executable(test_22_bgfx_sprites
            visual/test_bgfx_sprites.cpp
        )

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

        # Not added to CTest (requires display)
        message(STATUS "Visual test 'test_22_bgfx_sprites' enabled (run manually)")

        # Test 23: Visual Sprites Test via Module + IIO
        add_executable(test_23_bgfx_sprites_visual
            visual/test_23_bgfx_sprites_visual.cpp
        )

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

        # Not added to CTest (requires display)
        message(STATUS "Visual test 'test_23_bgfx_sprites_visual' enabled (run manually)")

        # Test 24: UIModule Visual Test (requires SDL2, display, BgfxRenderer and UIModule)
        if(GROVE_BUILD_UI_MODULE)
            add_executable(test_24_ui_basic
                visual/test_24_ui_basic.cpp
            )

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

            # Not added to CTest (requires display)
            message(STATUS "Visual test 'test_24_ui_basic' enabled (run manually)")

            # Test 25: UIModule Layout System Test (Phase 2)
            add_executable(test_25_ui_layout
                visual/test_25_ui_layout.cpp
            )

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

            # Not added to CTest (requires display)
            message(STATUS "Visual test 'test_25_ui_layout' enabled (run manually)")

            # Test 26: UIModule Interactive Buttons Test (Phase 3)
            add_executable(test_26_ui_buttons
                visual/test_26_ui_buttons.cpp
            )

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

            # Not added to CTest (requires display)
            message(STATUS "Visual test 'test_26_ui_buttons' enabled (run manually)")

            # Test 28: UIModule ScrollPanel Test (Phase 7.1)
            add_executable(test_28_ui_scroll
                visual/test_28_ui_scroll.cpp
            )

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

            # Not added to CTest (requires display)
            message(STATUS "Visual test 'test_28_ui_scroll' enabled (run manually)")

            # Test 29: UIModule Advanced Features Test (Phase 7.2 - Tooltips)
            add_executable(test_29_ui_advanced
                visual/test_29_ui_advanced.cpp
            )

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

            # Not added to CTest (requires display)
            message(STATUS "Visual test 'test_29_ui_advanced' enabled (run manually)")
        endif()
    else()
        message(STATUS "SDL2 not found - visual tests disabled")
    endif()

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
    add_test(NAME PipelineHeadless COMMAND test_pipeline_headless WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

    add_test(NAME BgfxSpritesHeadless COMMAND test_22_bgfx_sprites_headless WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
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
    add_test(NAME UIModuleIntegration COMMAND IT_014_ui_module_integration WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

    message(STATUS "Integration test 'IT_014_ui_module_integration' enabled")
endif()

# ============================================
# UIModule Interactive Showcase Demo
# ============================================

if(GROVE_BUILD_UI_MODULE AND GROVE_BUILD_BGFX_RENDERER)
    add_executable(demo_ui_showcase
        demo/demo_ui_showcase.cpp
    )

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

    message(STATUS "UIModule showcase demo 'demo_ui_showcase' enabled")
endif()
