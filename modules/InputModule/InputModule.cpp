#include "InputModule.h"
#include <grove/JsonDataNode.h>
#include <spdlog/spdlog.h>

namespace grove {

InputModule::InputModule() {
    m_state = std::make_unique<InputState>();
    m_config = std::make_unique<JsonDataNode>("config");
}

InputModule::~InputModule() {
    // Qualified call: shutdown() is virtual (IModule override), but InputModule has no subclass, so we
    // want THIS class's shutdown during destruction — the qualification makes the non-virtual dispatch
    // explicit and documents intent (clang-analyzer optin.cplusplus.VirtualCall).
    InputModule::shutdown();
}

void InputModule::setConfiguration(const IDataNode& config, IIO* io, ITaskScheduler* scheduler) {
    m_io = io;
    m_converter = std::make_unique<InputConverter>(io);

    // Parse configuration
    m_backend = config.getString("backend", "sdl");
    m_enableMouse = config.getBool("enableMouse", true);
    m_enableKeyboard = config.getBool("enableKeyboard", true);
    m_enableGamepad = config.getBool("enableGamepad", false);

    spdlog::info("[InputModule] Configured with backend={}, mouse={}, keyboard={}, gamepad={}",
                 m_backend, m_enableMouse, m_enableKeyboard, m_enableGamepad);
}

void InputModule::process(const IDataNode& input) {
    m_frameCount++;

    // 1. Lock and retrieve events from buffer
    std::vector<SDL_Event> events;
    {
        std::lock_guard<std::mutex> lock(m_bufferMutex);
        events = std::move(m_eventBuffer);
        m_eventBuffer.clear();
    }

    // 2. Convert SDL → Generic → IIO
    for (const auto& sdlEvent : events) {
        SDLBackend::InputEvent genericEvent;

        if (!SDLBackend::convert(sdlEvent, genericEvent)) {
            continue;  // Event not supported, skip
        }

        // 3. Update state and publish to IIO
        switch (genericEvent.type) {
            case SDLBackend::InputEvent::MouseMove:
                if (m_enableMouse) {
                    m_state->setMousePosition(genericEvent.mouseX, genericEvent.mouseY);
                    m_converter->publishMouseMove(genericEvent.mouseX, genericEvent.mouseY);
                }
                break;

            case SDLBackend::InputEvent::MouseButton:
                if (m_enableMouse) {
                    m_state->setMouseButton(genericEvent.button, genericEvent.pressed);
                    m_converter->publishMouseButton(genericEvent.button, genericEvent.pressed,
                                                     genericEvent.mouseX, genericEvent.mouseY);
                }
                break;

            case SDLBackend::InputEvent::MouseWheel:
                if (m_enableMouse) {
                    m_converter->publishMouseWheel(genericEvent.wheelDelta);
                }
                break;

            case SDLBackend::InputEvent::KeyboardKey:
                if (m_enableKeyboard) {
                    m_state->setKey(genericEvent.scancode, genericEvent.pressed);
                    m_state->updateModifiers(genericEvent.shift, genericEvent.ctrl, genericEvent.alt);
                    m_converter->publishKeyboardKey(genericEvent.scancode, genericEvent.pressed,
                                                     genericEvent.repeat, genericEvent.shift,
                                                     genericEvent.ctrl, genericEvent.alt);
                }
                break;

            case SDLBackend::InputEvent::KeyboardText:
                if (m_enableKeyboard) {
                    m_converter->publishKeyboardText(genericEvent.text);
                }
                break;
        }

        m_eventsProcessed++;
    }
}

void InputModule::shutdown() {
    spdlog::info("[InputModule] Shutdown - Processed {} events over {} frames",
                 m_eventsProcessed, m_frameCount);
    m_io = nullptr;
}

std::unique_ptr<IDataNode> InputModule::getState() {
    auto state = std::make_unique<JsonDataNode>("state");

    // Mouse state
    state->setInt("mouseX", m_state->mouseX);
    state->setInt("mouseY", m_state->mouseY);
    state->setBool("mouseButton0", m_state->mouseButtons[0]);
    state->setBool("mouseButton1", m_state->mouseButtons[1]);
    state->setBool("mouseButton2", m_state->mouseButtons[2]);

    // Buffered events count (can't serialize SDL_Event, but track count)
    std::lock_guard<std::mutex> lock(m_bufferMutex);
    state->setInt("bufferedEventCount", static_cast<int>(m_eventBuffer.size()));

    // Stats
    state->setInt("frameCount", static_cast<int>(m_frameCount));
    state->setInt("eventsProcessed", static_cast<int>(m_eventsProcessed));

    return state;
}

void InputModule::setState(const IDataNode& state) {
    // Restore mouse state
    m_state->mouseX = state.getInt("mouseX", 0);
    m_state->mouseY = state.getInt("mouseY", 0);
    m_state->mouseButtons[0] = state.getBool("mouseButton0", false);
    m_state->mouseButtons[1] = state.getBool("mouseButton1", false);
    m_state->mouseButtons[2] = state.getBool("mouseButton2", false);

    // Restore stats
    m_frameCount = static_cast<uint64_t>(state.getInt("frameCount", 0));
    m_eventsProcessed = static_cast<uint64_t>(state.getInt("eventsProcessed", 0));

    // Note: We can't restore the event buffer (SDL_Event is not serializable)
    // This is acceptable - we lose at most 1 frame of events during hot-reload

    spdlog::info("[InputModule] State restored - mouse=({},{}), frames={}, events={}",
                 m_state->mouseX, m_state->mouseY, m_frameCount, m_eventsProcessed);
}

const IDataNode& InputModule::getConfiguration() {
    if (!m_config) {
        m_config = std::make_unique<JsonDataNode>("config");
    }

    // Rebuild config from current state
    m_config->setString("backend", m_backend);
    m_config->setBool("enableMouse", m_enableMouse);
    m_config->setBool("enableKeyboard", m_enableKeyboard);
    m_config->setBool("enableGamepad", m_enableGamepad);

    return *m_config;
}

std::unique_ptr<IDataNode> InputModule::getHealthStatus() {
    auto health = std::make_unique<JsonDataNode>("health");
    health->setString("status", "healthy");
    health->setInt("frameCount", static_cast<int>(m_frameCount));
    health->setInt("eventsProcessed", static_cast<int>(m_eventsProcessed));

    double eventsPerFrame = (m_frameCount > 0) ?
        (static_cast<double>(m_eventsProcessed) / static_cast<double>(m_frameCount)) : 0.0;
    health->setDouble("eventsPerFrame", eventsPerFrame);

    return health;
}

void InputModule::feedEvent(const void* nativeEvent) {
    const SDL_Event* sdlEvent = static_cast<const SDL_Event*>(nativeEvent);

    std::lock_guard<std::mutex> lock(m_bufferMutex);
    m_eventBuffer.push_back(*sdlEvent);
}

} // namespace grove

// ============================================================================
// C Export (required for dlopen/LoadLibrary in the .dll/hot-reload path)
// ============================================================================
// Skipped in the STATIC build (GROVE_MODULE_STATIC): a host that static-links several
// modules would otherwise get multiple definitions of createModule/destroyModule. The
// static host instantiates InputModule directly and calls feedEvent()/process() on it
// (no opaque IModule* trampoline needed). Mirrors BgfxRenderer_static.
#ifndef GROVE_MODULE_STATIC

#ifdef _WIN32
#define GROVE_MODULE_EXPORT __declspec(dllexport)
#else
#define GROVE_MODULE_EXPORT
#endif

extern "C" {

GROVE_MODULE_EXPORT grove::IModule* createModule() {
    return new grove::InputModule();
}

GROVE_MODULE_EXPORT void destroyModule(grove::IModule* module) {
    delete module;
}

GROVE_MODULE_EXPORT void feedEventToInputModule(grove::IModule* module, const void* event) {
    if (module) {
        grove::InputModule* inputModule = static_cast<grove::InputModule*>(module);
        inputModule->feedEvent(event);
    }
}

}

#endif // GROVE_MODULE_STATIC
