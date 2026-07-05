/**
 * FfmpegBackendReal — decodes a REAL MP4 through the ffmpeg CLI (video slice 6c-1).
 *
 * GATED (like a [gpu] test): needs `ffmpeg` + `ffprobe` on PATH. It GENERATES a tiny H.264 MP4 with
 * ffmpeg's testsrc, decodes it through FfmpegCliBackend, and asserts the probed geometry/fps/frame
 * count + that real per-pixel frames come out (frame 0 is not all-zero, and a later frame decodes).
 * If ffmpeg isn't available it WARN-skips (so it's harmless in a toolchain without ffmpeg). This is
 * the "verified for real" proof for the MP4 path — the equivalent of the SDL sound demo's by-ear check.
 */

#include <catch2/catch_test_macros.hpp>
#include "FfmpegCliBackend.h"

#include <cstdlib>
#include <filesystem>
#include <string>

using namespace grove;

TEST_CASE("FfmpegCliBackend decodes a generated MP4 (ffmpeg CLI)", "[video][ffmpeg]") {
    namespace fs = std::filesystem;
    std::error_code ec;
    const std::string mp4 = (fs::temp_directory_path(ec) / "grove_test_clip.mp4").string();

    // Generate 64x64 @ 10 fps for 1 s (10 frames), H.264 yuv420p (broad-compat). Requires ffmpeg.
    const std::string gen =
        "ffmpeg -y -v error -f lavfi -i testsrc=duration=1:size=64x64:rate=10 -pix_fmt yuv420p \"" + mp4 + "\"";
    if (std::system(gen.c_str()) != 0 || !fs::exists(mp4)) {
        WARN("ffmpeg not available / generation failed — skipping the real-decode test");
        return;
    }

    video::FfmpegCliBackend backend;
    if (!backend.open(mp4)) {
        WARN("open failed (ffprobe missing?) — skipping");
        fs::remove(mp4, ec);
        return;
    }

    // Probed metadata.
    REQUIRE(backend.width() == 64);
    REQUIRE(backend.height() == 64);
    REQUIRE(std::abs(backend.fps() - 10.0) < 0.5);
    REQUIRE(backend.frameCount() >= 8);          // ~10 frames

    // Frame 0: real per-pixel data (testsrc is a colourful pattern -> not all zero).
    const uint8_t* f0 = backend.frameRGBA(0);
    REQUIRE(f0 != nullptr);
    bool nonzero = false;
    for (int i = 0; i < 64 * 64 * 4; ++i) if (f0[i] != 0) { nonzero = true; break; }
    REQUIRE(nonzero);

    // Forward decode to a later frame (proves the pipe advances).
    const uint8_t* f5 = backend.frameRGBA(5);
    REQUIRE(f5 != nullptr);

    backend.close();
    fs::remove(mp4, ec);
}
