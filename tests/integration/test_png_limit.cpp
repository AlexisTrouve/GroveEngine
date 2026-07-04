/**
 * Unit Tests: PNG writer HARD SIZE LIMIT (svpng) — writeRgbaAsPng must REFUSE, not corrupt.
 *
 * WHAT : Locks the guard added after the poster work: svpng encodes each row as ONE 16-bit-length stored
 *        DEFLATE block (width <= 21844 for RGB) and a 32-bit IDAT length. Past either it emits a CORRUPT file,
 *        and writeRgbaAsPng USED to return true anyway. Now it fails-franc (returns false, writes nothing).
 *
 * WHY  : --poster's output is `cells x pixels-per-cell` with no cap, so a big map easily exceeds svpng. Silent
 *        corruption is exactly the trap the doctrine forbids — this proves the writer refuses instead.
 */

#include <catch2/catch_test_macros.hpp>

#include "PngCapture.h"

#include <filesystem>
#include <string>
#include <vector>

using namespace grove::mvdemo;

TEST_CASE("PNG limit - svpngCanEncode enforces the 16-bit width + 32-bit IDAT bounds", "[png][limit][unit]") {
    REQUIRE(svpngCanEncode(kSvpngMaxWidth, 8));             // 21844 wide = the last valid width
    REQUIRE_FALSE(svpngCanEncode(kSvpngMaxWidth + 1, 8));   // 21845 overflows the 16-bit stored-block length
    REQUIRE_FALSE(svpngCanEncode(40000, 40000));            // a 10000-cell map @ 4 px/cell -> far over
    REQUIRE_FALSE(svpngCanEncode(kSvpngMaxWidth, 70000));   // a tall strip: the 32-bit IDAT length overflows
    REQUIRE_FALSE(svpngCanEncode(0, 8));                    // degenerate
    REQUIRE_FALSE(svpngCanEncode(8, 0));
}

TEST_CASE("PNG limit - writeRgbaAsPng refuses an over-limit image (no corrupt file)", "[png][limit][unit]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "grove_pnglimit";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);

    // Over the width limit: refuse (return false) and write NOTHING — the guard trips before touching the buffer.
    const std::string over = (dir / "over.png").string();
    const std::vector<uint8_t> tiny(4, 0);
    REQUIRE_FALSE(writeRgbaAsPng(over, kSvpngMaxWidth + 1, 1, tiny));
    REQUIRE_FALSE(std::filesystem::exists(over));

    // At the boundary: a real (short) PNG IS written.
    const std::string ok = (dir / "ok.png").string();
    const std::vector<uint8_t> img(static_cast<size_t>(kSvpngMaxWidth) * 2 * 4, 128);  // 21844 x 2 RGBA
    REQUIRE(writeRgbaAsPng(ok, kSvpngMaxWidth, 2, img));
    REQUIRE(std::filesystem::exists(ok));
}
