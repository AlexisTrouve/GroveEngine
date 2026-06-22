# ============================================================================
# UIModule - CMake Configuration
# ============================================================================

cmake_minimum_required(VERSION 3.20)

# ============================================================================
# UIModule Shared Library
# ============================================================================

add_library(UIModule SHARED
    # Main module
    UIModule.cpp

    # Core
    Core/UITree.cpp
    Core/UILayout.cpp
    Core/UIContext.cpp
    Core/UIStyle.cpp
    Core/UITooltip.cpp

    # Widgets
    Widgets/UIPanel.cpp
    Widgets/UILabel.cpp
    Widgets/UIButton.cpp
    Widgets/UIImage.cpp
    Widgets/UISlider.cpp
    Widgets/UICheckbox.cpp
    Widgets/UIProgressBar.cpp
    Widgets/UITextInput.cpp
    Widgets/UIScrollPanel.cpp
    Widgets/UIRadial.cpp
    Widgets/UIWindow.cpp
    Widgets/UITabs.cpp
    Widgets/UIDrawer.cpp
    Widgets/UIModal.cpp
    Widgets/UIList.cpp

    # Rendering
    Rendering/UIRenderer.cpp
)

target_include_directories(UIModule PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)

target_link_libraries(UIModule PRIVATE
    GroveEngine::impl
    spdlog::spdlog
    nlohmann_json::nlohmann_json
)

target_compile_features(UIModule PRIVATE cxx_std_17)

set_target_properties(UIModule PROPERTIES
    PREFIX "lib"
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/modules
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/modules
)

# ============================================================================
# Platform-specific settings
# ============================================================================

if(WIN32)
    target_compile_definitions(UIModule PRIVATE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
endif()

if(UNIX AND NOT APPLE)
    target_link_libraries(UIModule PRIVATE
        pthread
        dl
    )
endif()

# ============================================================================
# UIModule Static Library (for tests that can't use DLL loading)
# ============================================================================

add_library(UIModule_static STATIC
    # Main module
    UIModule.cpp

    # Core
    Core/UITree.cpp
    Core/UILayout.cpp
    Core/UIContext.cpp
    Core/UIStyle.cpp
    Core/UITooltip.cpp

    # Widgets
    Widgets/UIPanel.cpp
    Widgets/UILabel.cpp
    Widgets/UIButton.cpp
    Widgets/UIImage.cpp
    Widgets/UISlider.cpp
    Widgets/UICheckbox.cpp
    Widgets/UIProgressBar.cpp
    Widgets/UITextInput.cpp
    Widgets/UIScrollPanel.cpp
    Widgets/UIRadial.cpp
    Widgets/UIWindow.cpp
    Widgets/UITabs.cpp
    Widgets/UIDrawer.cpp
    Widgets/UIModal.cpp
    Widgets/UIList.cpp

    # Rendering
    Rendering/UIRenderer.cpp
)

target_include_directories(UIModule_static PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)

target_link_libraries(UIModule_static PUBLIC
    GroveEngine::impl
    spdlog::spdlog
    nlohmann_json::nlohmann_json
)

target_compile_features(UIModule_static PRIVATE cxx_std_17)

# Mark as static build to skip C exports (avoids multiple definition errors)
target_compile_definitions(UIModule_static PRIVATE GROVE_MODULE_STATIC)

if(WIN32)
    target_compile_definitions(UIModule_static PRIVATE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
endif()
