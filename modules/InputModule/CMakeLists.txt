# InputModule - Input capture and conversion module
# Converts native input events (SDL, GLFW, etc.) to IIO messages

add_library(InputModule SHARED
    InputModule.cpp
    Core/InputState.cpp
    Core/InputConverter.cpp
    Backends/SDLBackend.cpp
)

target_include_directories(InputModule
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        /usr/include/SDL2
)

# Try to find SDL2, but don't fail if not found (use system paths)
find_package(SDL2 QUIET)

if(SDL2_FOUND)
    target_link_libraries(InputModule
        PRIVATE
            GroveEngine::impl
            SDL2::SDL2
            nlohmann_json::nlohmann_json
            spdlog::spdlog
    )
else()
    # Fallback to system SDL2
    target_link_libraries(InputModule
        PRIVATE
            GroveEngine::impl
            SDL2
            nlohmann_json::nlohmann_json
            spdlog::spdlog
    )
endif()

# Install to modules directory
install(TARGETS InputModule
    LIBRARY DESTINATION modules
    RUNTIME DESTINATION modules
)

# Set output directory for development builds
set_target_properties(InputModule PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/modules"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/modules"
)
