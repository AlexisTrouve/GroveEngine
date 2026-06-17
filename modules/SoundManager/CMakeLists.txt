# SoundManager - music + SFX via IIO topics, SDL_mixer backend.
# Needs SDL2 + SDL2_mixer; skips cleanly if either is missing.

find_package(SDL2 QUIET)
find_package(SDL2_mixer QUIET)

if(NOT (SDL2_FOUND AND SDL2_mixer_FOUND))
    message(WARNING "SoundManager: SDL2/SDL2_mixer not found - SoundManager will NOT be built")
    message(STATUS "  SDL2_mixer devel package must be on the toolchain (find_package(SDL2_mixer)).")
    set(GROVE_SOUND_MODULE_SKIPPED TRUE PARENT_SCOPE)
    return()
endif()

# Static variant — for hosts that link the engine into the executable (e.g. Drifterra). The
# host instantiates SoundManagerModule, injects a SDLMixerBackend, and drives process() each
# frame. SoundManagerModule itself never includes SDL_mixer (it's behind ISoundBackend); only
# SDLMixerBackend.cpp does. No SHARED/extern "C" variant: this module is consumed static.
add_library(SoundManager_static STATIC
    SoundManagerModule.cpp
    SDLMixerBackend.cpp
)
target_include_directories(SoundManager_static PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(SoundManager_static PRIVATE cxx_std_17)
target_link_libraries(SoundManager_static PUBLIC
    GroveEngine::impl
    SDL2::SDL2
    SDL2_mixer::SDL2_mixer
)

message(STATUS "SoundManager: SoundManager_static enabled (SDL2_mixer found)")
