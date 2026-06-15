/**
 * Regression test: DebugPass must not overflow its vertex buffer.
 *
 * WHY: DebugPass sizes m_lineVB for MAX_DEBUG_LINES*2 vertices (assuming every
 * primitive is a line), but execute() writes debugLineCount*2 + debugRectCount*8
 * vertices with NO clamp. A scene with many debug rects (collision boxes, HUD bars,
 * shipyard grid) makes debugRectCount*8 exceed the buffer capacity, and the
 * subsequent device.updateBuffer() uploads more bytes than the buffer holds —
 * out-of-bounds write / GPU corruption exactly when the scene gets busy.
 *
 * This test feeds far more rects than fit and asserts the uploaded byte count never
 * exceeds the buffer capacity. It locks the clamp fix.
 */

#include <catch2/catch_test_macros.hpp>

#include "Passes/DebugPass.h"
#include "Frame/FramePacket.h"
#include "RHI/RHICommandBuffer.h"
#include "../mocks/MockRHIDevice.h"

#include <vector>

using namespace grove;
using namespace grove::test;

TEST_CASE("DebugPass clamps vertex upload to buffer capacity (no overflow)", "[debug_pass][unit]") {
    MockRHIDevice device;

    // Give the pass a valid shader handle (execute() early-returns on an invalid one).
    rhi::ShaderHandle shader = device.createShader(rhi::ShaderDesc{});
    REQUIRE(shader.isValid());

    DebugPass pass(shader);
    pass.setup(device);

    // Buffer capacity (mirrors DebugPass: MAX_DEBUG_LINES=10000 * 2 verts * 28 bytes).
    // MAX_DEBUG_LINES is private, so the value is duplicated here intentionally; if it
    // changes, update both. sizeof(DebugVertex) == 28.
    const uint32_t capacityBytes = 10000u * 2u * 28u;

    // 5000 rects -> 5000*8 = 40000 vertices -> 1,120,000 bytes, well over the
    // 560,000-byte (20000-vertex) capacity.
    std::vector<DebugRect> rects(5000, DebugRect{0.0f, 0.0f, 10.0f, 10.0f, 0xFFFFFFFFu, false});

    FramePacket frame;
    frame.debugRects = rects.data();
    frame.debugRectCount = rects.size();

    rhi::RHICommandBuffer cmd;
    pass.execute(frame, device, cmd);

    INFO("maxUpdateBufferSize=" << device.maxUpdateBufferSize.load()
         << " capacityBytes=" << capacityBytes);

    REQUIRE(device.updateBufferCount.load() > 0);                       // it did upload
    REQUIRE(device.maxUpdateBufferSize.load() <= capacityBytes);        // but never past capacity

    pass.shutdown(device);
}
