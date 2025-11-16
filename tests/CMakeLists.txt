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
add_test(NAME ProductionHotReload COMMAND test_01_production_hotreload)

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
add_test(NAME ChaosMonkey COMMAND test_02_chaos_monkey)

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
add_test(NAME StressTest COMMAND test_03_stress_test)

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
add_test(NAME RaceConditionHunter COMMAND test_04_race_condition)

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
add_test(NAME MemoryLeakHunter COMMAND test_05_memory_leak)

# Memory leak profiler (detailed analysis)
add_executable(profile_memory_leak
    profile_memory_leak.cpp
)

target_link_libraries(profile_memory_leak PRIVATE
    test_helpers
    GroveEngine::core
    GroveEngine::impl
)

add_dependencies(profile_memory_leak LeakTestModule)

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
add_test(NAME ErrorRecovery COMMAND test_06_error_recovery)
