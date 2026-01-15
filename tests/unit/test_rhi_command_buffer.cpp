/**
 * Unit Tests: RHICommandBuffer
 *
 * Comprehensive tests for command buffer recording including:
 * - All command types (SetState, SetTexture, SetUniform, etc.)
 * - Command data integrity
 * - Move semantics
 * - Clear/reset behavior
 *
 * Note: Basic tests already in test_20_bgfx_rhi.cpp
 * This file adds complete coverage for Phase 6.5
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "../../modules/BgfxRenderer/RHI/RHICommandBuffer.h"
#include "../../modules/BgfxRenderer/RHI/RHITypes.h"

#include <utility> // std::move

using namespace grove;
using namespace grove::rhi;
using Catch::Matchers::WithinAbs;

// ============================================================================
// SetState Command
// ============================================================================

TEST_CASE("RHICommandBuffer - setState records correct command", "[rhi][command_buffer][unit]") {
    RHICommandBuffer cmd;

    RenderState state;
    state.blend = BlendMode::Additive;
    state.cull = CullMode::CCW;
    state.depthTest = true;
    state.depthWrite = false;

    cmd.setState(state);

    REQUIRE(cmd.size() == 1);
    const auto& commands = cmd.getCommands();
    REQUIRE(commands[0].type == CommandType::SetState);
    REQUIRE(commands[0].setState.state.blend == BlendMode::Additive);
    REQUIRE(commands[0].setState.state.cull == CullMode::CCW);
    REQUIRE(commands[0].setState.state.depthTest == true);
    REQUIRE(commands[0].setState.state.depthWrite == false);
}

TEST_CASE("RHICommandBuffer - setState with all blend modes", "[rhi][command_buffer][unit]") {
    RHICommandBuffer cmd;

    SECTION("None") {
        RenderState state;
        state.blend = BlendMode::None;
        cmd.setState(state);
        REQUIRE(cmd.getCommands()[0].setState.state.blend == BlendMode::None);
    }

    SECTION("Alpha") {
        RenderState state;
        state.blend = BlendMode::Alpha;
        cmd.setState(state);
        REQUIRE(cmd.getCommands()[0].setState.state.blend == BlendMode::Alpha);
    }

    SECTION("Additive") {
        RenderState state;
        state.blend = BlendMode::Additive;
        cmd.setState(state);
        REQUIRE(cmd.getCommands()[0].setState.state.blend == BlendMode::Additive);
    }

    SECTION("Multiply") {
        RenderState state;
        state.blend = BlendMode::Multiply;
        cmd.setState(state);
        REQUIRE(cmd.getCommands()[0].setState.state.blend == BlendMode::Multiply);
    }
}

// ============================================================================
// SetTexture Command
// ============================================================================

TEST_CASE("RHICommandBuffer - setTexture records slot, handle, sampler", "[rhi][command_buffer][unit]") {
    RHICommandBuffer cmd;

    TextureHandle tex;
    tex.id = 999;

    UniformHandle sampler;
    sampler.id = 777;

    cmd.setTexture(3, tex, sampler);

    REQUIRE(cmd.size() == 1);
    const auto& commands = cmd.getCommands();
    REQUIRE(commands[0].type == CommandType::SetTexture);
    REQUIRE(commands[0].setTexture.slot == 3);
    REQUIRE(commands[0].setTexture.texture.id == 999);
    REQUIRE(commands[0].setTexture.sampler.id == 777);
}

TEST_CASE("RHICommandBuffer - setTexture multiple slots", "[rhi][command_buffer][unit]") {
    RHICommandBuffer cmd;

    TextureHandle tex0; tex0.id = 10;
    TextureHandle tex1; tex1.id = 20;
    UniformHandle sampler; sampler.id = 1;

    cmd.setTexture(0, tex0, sampler);
    cmd.setTexture(1, tex1, sampler);

    REQUIRE(cmd.size() == 2);
    REQUIRE(cmd.getCommands()[0].setTexture.slot == 0);
    REQUIRE(cmd.getCommands()[0].setTexture.texture.id == 10);
    REQUIRE(cmd.getCommands()[1].setTexture.slot == 1);
    REQUIRE(cmd.getCommands()[1].setTexture.texture.id == 20);
}

// ============================================================================
// SetUniform Command
// ============================================================================

TEST_CASE("RHICommandBuffer - setUniform single vec4", "[rhi][command_buffer][unit]") {
    RHICommandBuffer cmd;

    UniformHandle uniform;
    uniform.id = 42;

    float data[4] = {1.0f, 2.5f, 3.14f, 4.0f};
    cmd.setUniform(uniform, data, 1);

    REQUIRE(cmd.size() == 1);
    const auto& commands = cmd.getCommands();
    REQUIRE(commands[0].type == CommandType::SetUniform);
    REQUIRE(commands[0].setUniform.uniform.id == 42);
    REQUIRE(commands[0].setUniform.numVec4s == 1);
    REQUIRE_THAT(commands[0].setUniform.data[0], WithinAbs(1.0f, 0.001f));
    REQUIRE_THAT(commands[0].setUniform.data[1], WithinAbs(2.5f, 0.001f));
    REQUIRE_THAT(commands[0].setUniform.data[2], WithinAbs(3.14f, 0.001f));
    REQUIRE_THAT(commands[0].setUniform.data[3], WithinAbs(4.0f, 0.001f));
}

TEST_CASE("RHICommandBuffer - setUniform multiple vec4s", "[rhi][command_buffer][unit]") {
    RHICommandBuffer cmd;

    UniformHandle uniform;
    uniform.id = 1;

    // 4x4 matrix = 4 vec4s
    float matrix[16] = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    };

    cmd.setUniform(uniform, matrix, 4);

    REQUIRE(cmd.size() == 1);
    const auto& commands = cmd.getCommands();
    REQUIRE(commands[0].setUniform.numVec4s == 4);

    // Check diagonal (identity matrix)
    REQUIRE(commands[0].setUniform.data[0] == 1.0f);  // [0][0]
    REQUIRE(commands[0].setUniform.data[5] == 1.0f);  // [1][1]
    REQUIRE(commands[0].setUniform.data[10] == 1.0f); // [2][2]
    REQUIRE(commands[0].setUniform.data[15] == 1.0f); // [3][3]

    // Check off-diagonal (should be 0)
    REQUIRE(commands[0].setUniform.data[1] == 0.0f);
    REQUIRE(commands[0].setUniform.data[4] == 0.0f);
}

// ============================================================================
// SetVertexBuffer Command
// ============================================================================

TEST_CASE("RHICommandBuffer - setVertexBuffer", "[rhi][command_buffer][unit]") {
    RHICommandBuffer cmd;

    BufferHandle buffer;
    buffer.id = 123;

    cmd.setVertexBuffer(buffer, 256);

    REQUIRE(cmd.size() == 1);
    const auto& commands = cmd.getCommands();
    REQUIRE(commands[0].type == CommandType::SetVertexBuffer);
    REQUIRE(commands[0].setVertexBuffer.buffer.id == 123);
    REQUIRE(commands[0].setVertexBuffer.offset == 256);
}

TEST_CASE("RHICommandBuffer - setVertexBuffer default offset", "[rhi][command_buffer][unit]") {
    RHICommandBuffer cmd;

    BufferHandle buffer;
    buffer.id = 5;

    cmd.setVertexBuffer(buffer); // offset defaults to 0

    REQUIRE(cmd.getCommands()[0].setVertexBuffer.offset == 0);
}

// ============================================================================
// SetIndexBuffer Command
// ============================================================================

TEST_CASE("RHICommandBuffer - setIndexBuffer 16-bit", "[rhi][command_buffer][unit]") {
    RHICommandBuffer cmd;

    BufferHandle buffer;
    buffer.id = 77;

    cmd.setIndexBuffer(buffer, 0, false);

    REQUIRE(cmd.size() == 1);
    const auto& commands = cmd.getCommands();
    REQUIRE(commands[0].type == CommandType::SetIndexBuffer);
    REQUIRE(commands[0].setIndexBuffer.buffer.id == 77);
    REQUIRE(commands[0].setIndexBuffer.offset == 0);
    REQUIRE(commands[0].setIndexBuffer.is32Bit == false);
}

TEST_CASE("RHICommandBuffer - setIndexBuffer 32-bit", "[rhi][command_buffer][unit]") {
    RHICommandBuffer cmd;

    BufferHandle buffer;
    buffer.id = 88;

    cmd.setIndexBuffer(buffer, 512, true);

    REQUIRE(cmd.getCommands()[0].setIndexBuffer.is32Bit == true);
    REQUIRE(cmd.getCommands()[0].setIndexBuffer.offset == 512);
}

// ============================================================================
// SetInstanceBuffer Command
// ============================================================================

TEST_CASE("RHICommandBuffer - setInstanceBuffer", "[rhi][command_buffer][unit]") {
    RHICommandBuffer cmd;

    BufferHandle buffer;
    buffer.id = 99;

    cmd.setInstanceBuffer(buffer, 10, 100);

    REQUIRE(cmd.size() == 1);
    const auto& commands = cmd.getCommands();
    REQUIRE(commands[0].type == CommandType::SetInstanceBuffer);
    REQUIRE(commands[0].setInstanceBuffer.buffer.id == 99);
    REQUIRE(commands[0].setInstanceBuffer.start == 10);
    REQUIRE(commands[0].setInstanceBuffer.count == 100);
}

// ============================================================================
// SetScissor Command
// ============================================================================

TEST_CASE("RHICommandBuffer - setScissor", "[rhi][command_buffer][unit]") {
    RHICommandBuffer cmd;

    cmd.setScissor(100, 200, 640, 480);

    REQUIRE(cmd.size() == 1);
    const auto& commands = cmd.getCommands();
    REQUIRE(commands[0].type == CommandType::SetScissor);
    REQUIRE(commands[0].setScissor.x == 100);
    REQUIRE(commands[0].setScissor.y == 200);
    REQUIRE(commands[0].setScissor.w == 640);
    REQUIRE(commands[0].setScissor.h == 480);
}

// ============================================================================
// Draw Commands
// ============================================================================

TEST_CASE("RHICommandBuffer - draw", "[rhi][command_buffer][unit]") {
    RHICommandBuffer cmd;

    cmd.draw(36, 0);

    REQUIRE(cmd.size() == 1);
    const auto& commands = cmd.getCommands();
    REQUIRE(commands[0].type == CommandType::Draw);
    REQUIRE(commands[0].draw.vertexCount == 36);
    REQUIRE(commands[0].draw.startVertex == 0);
}

TEST_CASE("RHICommandBuffer - draw with start vertex", "[rhi][command_buffer][unit]") {
    RHICommandBuffer cmd;

    cmd.draw(24, 100);

    REQUIRE(cmd.getCommands()[0].draw.vertexCount == 24);
    REQUIRE(cmd.getCommands()[0].draw.startVertex == 100);
}

TEST_CASE("RHICommandBuffer - drawIndexed", "[rhi][command_buffer][unit]") {
    RHICommandBuffer cmd;

    cmd.drawIndexed(1024, 512);

    REQUIRE(cmd.size() == 1);
    const auto& commands = cmd.getCommands();
    REQUIRE(commands[0].type == CommandType::DrawIndexed);
    REQUIRE(commands[0].drawIndexed.indexCount == 1024);
    REQUIRE(commands[0].drawIndexed.startIndex == 512);
}

TEST_CASE("RHICommandBuffer - drawInstanced", "[rhi][command_buffer][unit]") {
    RHICommandBuffer cmd;

    cmd.drawInstanced(6, 1000); // 1000 instances of 6-vertex mesh

    REQUIRE(cmd.size() == 1);
    const auto& commands = cmd.getCommands();
    REQUIRE(commands[0].type == CommandType::DrawInstanced);
    REQUIRE(commands[0].drawInstanced.indexCount == 6);
    REQUIRE(commands[0].drawInstanced.instanceCount == 1000);
}

// ============================================================================
// Submit Command
// ============================================================================

TEST_CASE("RHICommandBuffer - submit", "[rhi][command_buffer][unit]") {
    RHICommandBuffer cmd;

    ShaderHandle shader;
    shader.id = 555;

    cmd.submit(0, shader, 100);

    REQUIRE(cmd.size() == 1);
    const auto& commands = cmd.getCommands();
    REQUIRE(commands[0].type == CommandType::Submit);
    REQUIRE(commands[0].submit.view == 0);
    REQUIRE(commands[0].submit.shader.id == 555);
    REQUIRE(commands[0].submit.depth == 100);
}

TEST_CASE("RHICommandBuffer - submit default depth", "[rhi][command_buffer][unit]") {
    RHICommandBuffer cmd;

    ShaderHandle shader;
    shader.id = 1;

    cmd.submit(0, shader); // depth defaults to 0

    REQUIRE(cmd.getCommands()[0].submit.depth == 0);
}

// ============================================================================
// Clear & Reset
// ============================================================================

TEST_CASE("RHICommandBuffer - clear empties buffer", "[rhi][command_buffer][unit]") {
    RHICommandBuffer cmd;

    // Add some commands
    cmd.draw(100);
    cmd.draw(200);
    cmd.draw(300);

    REQUIRE(cmd.size() == 3);

    // Clear
    cmd.clear();

    REQUIRE(cmd.size() == 0);
    REQUIRE(cmd.getCommands().empty());
}

TEST_CASE("RHICommandBuffer - clear allows reuse", "[rhi][command_buffer][unit]") {
    RHICommandBuffer cmd;

    cmd.draw(10);
    cmd.clear();

    cmd.draw(20);

    REQUIRE(cmd.size() == 1);
    REQUIRE(cmd.getCommands()[0].draw.vertexCount == 20);
}

// ============================================================================
// Complex Sequences
// ============================================================================

TEST_CASE("RHICommandBuffer - typical rendering sequence", "[rhi][command_buffer][unit]") {
    RHICommandBuffer cmd;

    // Setup state
    RenderState state;
    state.blend = BlendMode::Alpha;
    cmd.setState(state);

    // Bind texture
    TextureHandle tex; tex.id = 1;
    UniformHandle sampler; sampler.id = 1;
    cmd.setTexture(0, tex, sampler);

    // Set uniform (model-view-proj matrix)
    UniformHandle mvp; mvp.id = 2;
    float matrix[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    cmd.setUniform(mvp, matrix, 4);

    // Bind buffers
    BufferHandle vb; vb.id = 10;
    BufferHandle ib; ib.id = 11;
    cmd.setVertexBuffer(vb);
    cmd.setIndexBuffer(ib);

    // Draw
    cmd.drawIndexed(36);

    // Submit
    ShaderHandle shader; shader.id = 100;
    cmd.submit(0, shader);

    // Verify sequence
    REQUIRE(cmd.size() == 7);
    REQUIRE(cmd.getCommands()[0].type == CommandType::SetState);
    REQUIRE(cmd.getCommands()[1].type == CommandType::SetTexture);
    REQUIRE(cmd.getCommands()[2].type == CommandType::SetUniform);
    REQUIRE(cmd.getCommands()[3].type == CommandType::SetVertexBuffer);
    REQUIRE(cmd.getCommands()[4].type == CommandType::SetIndexBuffer);
    REQUIRE(cmd.getCommands()[5].type == CommandType::DrawIndexed);
    REQUIRE(cmd.getCommands()[6].type == CommandType::Submit);
}

TEST_CASE("RHICommandBuffer - instanced rendering sequence", "[rhi][command_buffer][unit]") {
    RHICommandBuffer cmd;

    // Vertex buffer (quad)
    BufferHandle vb; vb.id = 1;
    cmd.setVertexBuffer(vb);

    // Instance buffer (sprite transforms)
    BufferHandle instanceBuffer; instanceBuffer.id = 2;
    cmd.setInstanceBuffer(instanceBuffer, 0, 500);

    // Texture
    TextureHandle tex; tex.id = 3;
    UniformHandle sampler; sampler.id = 4;
    cmd.setTexture(0, tex, sampler);

    // Draw instanced
    cmd.drawInstanced(6, 500); // 6 verts per quad, 500 instances

    // Submit
    ShaderHandle shader; shader.id = 5;
    cmd.submit(0, shader);

    REQUIRE(cmd.size() == 5);
    REQUIRE(cmd.getCommands()[3].type == CommandType::DrawInstanced);
    REQUIRE(cmd.getCommands()[3].drawInstanced.instanceCount == 500);
}

// ============================================================================
// Move Semantics
// ============================================================================

TEST_CASE("RHICommandBuffer - move constructor", "[rhi][command_buffer][unit]") {
    RHICommandBuffer cmd1;
    cmd1.draw(100);
    cmd1.draw(200);

    REQUIRE(cmd1.size() == 2);

    // Move construct
    RHICommandBuffer cmd2(std::move(cmd1));

    REQUIRE(cmd2.size() == 2);
    REQUIRE(cmd2.getCommands()[0].draw.vertexCount == 100);
    REQUIRE(cmd2.getCommands()[1].draw.vertexCount == 200);

    // cmd1 should be in valid but unspecified state (likely empty)
    // Don't rely on specific behavior, just ensure no crash
}

TEST_CASE("RHICommandBuffer - move assignment", "[rhi][command_buffer][unit]") {
    RHICommandBuffer cmd1;
    cmd1.draw(50);

    RHICommandBuffer cmd2;
    cmd2.draw(75);
    cmd2.draw(80);

    // Move assign
    cmd1 = std::move(cmd2);

    REQUIRE(cmd1.size() == 2);
    REQUIRE(cmd1.getCommands()[0].draw.vertexCount == 75);
    REQUIRE(cmd1.getCommands()[1].draw.vertexCount == 80);
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST_CASE("RHICommandBuffer - many commands", "[rhi][command_buffer][unit]") {
    RHICommandBuffer cmd;

    constexpr int COUNT = 1000;
    for (int i = 0; i < COUNT; ++i) {
        cmd.draw(i);
    }

    REQUIRE(cmd.size() == COUNT);
    REQUIRE(cmd.getCommands()[0].draw.vertexCount == 0);
    REQUIRE(cmd.getCommands()[COUNT - 1].draw.vertexCount == COUNT - 1);
}

TEST_CASE("RHICommandBuffer - interleaved command types", "[rhi][command_buffer][unit]") {
    RHICommandBuffer cmd;

    BufferHandle buf; buf.id = 1;
    ShaderHandle shader; shader.id = 2;

    // Interleave different command types
    cmd.setVertexBuffer(buf);
    cmd.draw(10);
    cmd.setVertexBuffer(buf, 100);
    cmd.draw(20);
    cmd.submit(0, shader);
    cmd.setVertexBuffer(buf, 200);
    cmd.draw(30);

    REQUIRE(cmd.size() == 7);
    REQUIRE(cmd.getCommands()[0].type == CommandType::SetVertexBuffer);
    REQUIRE(cmd.getCommands()[1].type == CommandType::Draw);
    REQUIRE(cmd.getCommands()[2].type == CommandType::SetVertexBuffer);
    REQUIRE(cmd.getCommands()[3].type == CommandType::Draw);
    REQUIRE(cmd.getCommands()[4].type == CommandType::Submit);
    REQUIRE(cmd.getCommands()[5].type == CommandType::SetVertexBuffer);
    REQUIRE(cmd.getCommands()[6].type == CommandType::Draw);
}
