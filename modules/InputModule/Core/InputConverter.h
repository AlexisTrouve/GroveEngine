#pragma once

#include <grove/IIO.h>
#include <string>

namespace grove {

class InputConverter {
public:
    InputConverter(IIO* io);
    ~InputConverter() = default;

    void publishMouseMove(int x, int y);
    void publishMouseButton(int button, bool pressed, int x, int y);
    void publishMouseWheel(float delta);
    void publishKeyboardKey(int scancode, bool pressed, bool repeat,
                           bool shift, bool ctrl, bool alt);
    void publishKeyboardText(const std::string& text);

private:
    IIO* m_io;
};

} // namespace grove
