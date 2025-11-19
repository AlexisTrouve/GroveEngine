cmake_minimum_required(VERSION 3.14)

# Fetch Catch2 for testing
include(FetchContent)
FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.5.0
)
FetchContent_MakeAvailable(Catch2)

# Helper macro to create test executables
macro(add_topictree_test test_name)
    add_executable(${test_name} ${test_name}.cpp)
    target_link_libraries(${test_name} PRIVATE
        topictree::topictree
        Catch2::Catch2WithMain
    )
    target_compile_features(${test_name} PRIVATE cxx_std_17)
    add_test(NAME ${test_name} COMMAND ${test_name})
endmacro()

# Add all scenario tests
add_topictree_test(scenario_01_basic_exact)
add_topictree_test(scenario_02_single_wildcard)
add_topictree_test(scenario_03_multilevel_wildcard)
add_topictree_test(scenario_04_overlapping)
add_topictree_test(scenario_05_unregister_specific)
add_topictree_test(scenario_06_unregister_all)
add_topictree_test(scenario_07_deep_hierarchies)
add_topictree_test(scenario_08_performance)
add_topictree_test(scenario_09_threadsafety)
add_topictree_test(scenario_10_edge_cases)

# Enable pthread for thread-safety tests
find_package(Threads REQUIRED)
target_link_libraries(scenario_09_threadsafety PRIVATE Threads::Threads)
