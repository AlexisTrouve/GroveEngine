# VideoModule - video playback runtime (video slice 6c). The decode backend is behind IVideoBackend
# (ffmpeg CLI in 6c-1); the module itself (A/V sync + topic logic) is dependency-free.

# Static variant — for hosts that link the engine into the executable (e.g. Drifterra). The host
# instantiates VideoModule, injects a backend, drives process(), and pushes video:* topics.
add_library(VideoModule_static STATIC
    VideoModule.cpp
)
target_include_directories(VideoModule_static PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(VideoModule_static PRIVATE cxx_std_17)
target_link_libraries(VideoModule_static PUBLIC
    GroveEngine::impl
)

message(STATUS "VideoModule: VideoModule_static enabled")
