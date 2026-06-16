# InputModule - Input capture and conversion module
# Converts native input events (SDL, GLFW, etc.) to IIO messages

# Find SDL2 - REQUIRED for InputModule to function
find_package(SDL2 QUIET)

# Check if SDL2 is available
set(SDL2_USABLE FALSE)
if(SDL2_FOUND)
    set(SDL2_USABLE TRUE)
    message(STATUS "InputModule: SDL2 found via find_package")
elseif(UNIX AND EXISTS "/usr/include/SDL2/SDL.h")
    set(SDL2_USABLE TRUE)
    message(STATUS "InputModule: SDL2 found at system path (Linux)")
endif()

if(NOT SDL2_USABLE)
    message(WARNING "InputModule: SDL2 not found - InputModule will NOT be built")
    message(STATUS "  On Windows MinGW, install via: pacman -S mingw-w64-ucrt-x86_64-SDL2")
    message(STATUS "  On Ubuntu/Debian, install via: apt install libsdl2-dev")
    # Set a variable so parent can know InputModule was skipped
    set(GROVE_INPUT_MODULE_SKIPPED TRUE PARENT_SCOPE)
    return()
endif()

add_library(InputModule SHARED
    InputModule.cpp
    Core/InputState.cpp
    Core/InputConverter.cpp
    Backends/SDLBackend.cpp
)

if(SDL2_FOUND)
    target_include_directories(InputModule
        PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
        PRIVATE
            ${CMAKE_SOURCE_DIR}/include
    )

    target_link_libraries(InputModule
        PRIVATE
            GroveEngine::impl
            SDL2::SDL2
            nlohmann_json::nlohmann_json
            spdlog::spdlog
    )
else()
    # Linux system SDL2
    target_include_directories(InputModule
        PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
        PRIVATE
            ${CMAKE_SOURCE_DIR}/include
            /usr/include/SDL2
    )

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

# ============================================================================
# Static variant — for hosts that link modules into the executable (no .dll).
# Mirrors BgfxRenderer_static: GROVE_MODULE_STATIC skips the extern "C" exports
# (no multiple-definition when several static modules are linked). The host
# instantiates InputModule directly and drives feedEvent()/process() per frame.
# Used by games like Drifterra that static-link the whole engine.
# ============================================================================
add_library(InputModule_static STATIC
    InputModule.cpp
    Core/InputState.cpp
    Core/InputConverter.cpp
    Backends/SDLBackend.cpp
)
target_include_directories(InputModule_static PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(InputModule_static PRIVATE cxx_std_17)
target_compile_definitions(InputModule_static PRIVATE GROVE_MODULE_STATIC)
if(SDL2_FOUND)
    target_link_libraries(InputModule_static PUBLIC
        GroveEngine::impl
        SDL2::SDL2
        nlohmann_json::nlohmann_json
        spdlog::spdlog
    )
else()
    target_include_directories(InputModule_static PUBLIC /usr/include/SDL2)
    target_link_libraries(InputModule_static PUBLIC
        GroveEngine::impl
        SDL2
        nlohmann_json::nlohmann_json
        spdlog::spdlog
    )
endif()
