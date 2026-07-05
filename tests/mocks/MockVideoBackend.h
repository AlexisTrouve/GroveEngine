#pragma once

/**
 * Mock IVideoBackend for headless video tests. Reports configurable geometry/fps/length and returns a
 * synthetic RGBA frame (filled with the frame index) so a test can drive the module's A/V-sync + topic
 * logic with no real decoder. Records which frame indices were fetched.
 */

#include "VideoModule/IVideoBackend.h"

#include <cstdint>
#include <string>
#include <vector>

namespace grove {
namespace test {

class MockVideoBackend : public video::IVideoBackend {
public:
    // Configurable metadata (set before open()).
    int         w = 4, h = 4;
    double      fpsVal = 30.0;
    int         frames = 90;      // 3 s @ 30 fps
    std::string audio;            // "" = silent

    // Observations.
    bool               opened = false;
    std::string        openedPath;
    std::vector<int>   fetchedFrames;   // frame indices frameRGBA() was asked for

    bool open(const std::string& path) override { openedPath = path; opened = true; return true; }
    void close() override { opened = false; }

    int    width() const override { return w; }
    int    height() const override { return h; }
    double fps() const override { return fpsVal; }
    int    frameCount() const override { return frames; }

    const uint8_t* frameRGBA(int index) override {
        fetchedFrames.push_back(index);
        m_buf.assign(static_cast<size_t>(w) * static_cast<size_t>(h) * 4u,
                     static_cast<uint8_t>(index & 0xFF));   // fill with the index (inspectable)
        return m_buf.data();
    }

    std::string audioTrack() const override { return audio; }

private:
    std::vector<uint8_t> m_buf;
};

} // namespace test
} // namespace grove
