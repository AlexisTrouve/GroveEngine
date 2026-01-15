cmake_minimum_required(VERSION 3.14)
project(topictree VERSION 1.0.0 LANGUAGES CXX)

# Header-only library
add_library(topictree INTERFACE)
add_library(topictree::topictree ALIAS topictree)

target_include_directories(topictree INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_compile_features(topictree INTERFACE cxx_std_17)

# Installation rules (optional, for future packaging)
include(GNUInstallDirs)

install(TARGETS topictree
    EXPORT topictreeTargets
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(DIRECTORY include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(EXPORT topictreeTargets
    FILE topictreeTargets.cmake
    NAMESPACE topictree::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/topictree
)

# Testing
option(BUILD_TESTS "Build TopicTree tests" ON)

if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
