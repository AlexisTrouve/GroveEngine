#include "InputConverter.h"
#include <grove/JsonDataNode.h>
#include <memory>

namespace grove {

InputConverter::InputConverter(IIO* io) : m_io(io) {
}

void InputConverter::publishMouseMove(int x, int y) {
    auto msg = std::make_unique<JsonDataNode>("mouse_move");
    msg->setInt("x", x);
    msg->setInt("y", y);
    m_io->publish("input:mouse:move", std::move(msg));
}

void InputConverter::publishMouseButton(int button, bool pressed, int x, int y) {
    auto msg = std::make_unique<JsonDataNode>("mouse_button");
    msg->setInt("button", button);
    msg->setBool("pressed", pressed);
    msg->setInt("x", x);
    msg->setInt("y", y);
    m_io->publish("input:mouse:button", std::move(msg));
}

void InputConverter::publishMouseWheel(float delta) {
    auto msg = std::make_unique<JsonDataNode>("mouse_wheel");
    msg->setDouble("delta", static_cast<double>(delta));
    m_io->publish("input:mouse:wheel", std::move(msg));
}

void InputConverter::publishKeyboardKey(int scancode, bool pressed, bool repeat,
                                        bool shift, bool ctrl, bool alt) {
    auto msg = std::make_unique<JsonDataNode>("keyboard_key");
    msg->setInt("scancode", scancode);
    msg->setBool("pressed", pressed);
    msg->setBool("repeat", repeat);
    msg->setBool("shift", shift);
    msg->setBool("ctrl", ctrl);
    msg->setBool("alt", alt);
    m_io->publish("input:keyboard:key", std::move(msg));
}

void InputConverter::publishKeyboardText(const std::string& text) {
    auto msg = std::make_unique<JsonDataNode>("keyboard_text");
    msg->setString("text", text);
    m_io->publish("input:keyboard:text", std::move(msg));
}

} // namespace grove
