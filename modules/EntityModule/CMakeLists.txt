# EntityModule - data-driven scene/entity layer (slice E2). Wraps the pure grove::entity::EntityWorld
# (entities + components + the fixed behavior library) and maps it onto IIO: entity:* topics in,
# render:sprite:* out. No SDL / renderer link — pure logic over the bus, like DialogueModule.

# Static variant — for hosts that link the engine into the executable (e.g. Drifterra). The host
# instantiates EntityModule, drives world()/topics, and calls process(dt) each frame to tick + emit.
add_library(EntityModule_static STATIC
    EntityModule.cpp
)
target_include_directories(EntityModule_static PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(EntityModule_static PRIVATE cxx_std_17)
target_link_libraries(EntityModule_static PUBLIC
    GroveEngine::impl
    nlohmann_json::nlohmann_json
)

message(STATUS "EntityModule: EntityModule_static enabled")
