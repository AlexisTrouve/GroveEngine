# DialogueModule - VN/cutscene runtime (UI framework slice 7). Pure logic: a data-driven dialogue
# state machine (DialogueRuntime) driven over IIO. No SDL / renderer — drives the UI via ui:data.

# Static variant — for hosts that link the engine into the executable (e.g. Drifterra). The host
# instantiates DialogueModule, drives process() each frame, and pushes scene:* topics. Consumed
# static like SoundManager; no SHARED/extern "C" variant needed.
add_library(DialogueModule_static STATIC
    DialogueModule.cpp
)
target_include_directories(DialogueModule_static PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(DialogueModule_static PRIVATE cxx_std_17)
target_link_libraries(DialogueModule_static PUBLIC
    GroveEngine::impl
    nlohmann_json::nlohmann_json
)

message(STATUS "DialogueModule: DialogueModule_static enabled")
