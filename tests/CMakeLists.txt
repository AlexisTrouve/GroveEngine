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

# Hot-reload test executable
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
