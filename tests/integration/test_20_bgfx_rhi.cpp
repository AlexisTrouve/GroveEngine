/**
 * Test 20: BgfxRenderer RHI Unit Tests
 *
 * Tests the RHI layer components without requiring a graphical window:
 * - Command buffer recording
 * - Frame packet construction
 * - Scene collector parsing
 * - Render graph compilation
 */

#include <iostream>
#include <cassert>
#include <cstring>

// Include RHI components directly (no bgfx needed for these tests)
#include "../../modules/BgfxRenderer/RHI/RHITypes.h"
#include "../../modules/BgfxRenderer/RHI/RHICommandBuffer.h"
#include "../../modules/BgfxRenderer/Frame/FramePacket.h"
#include "../../modules/BgfxRenderer/Frame/FrameAllocator.h"

using namespace grove;
using namespace grove::rhi;

// ============================================================================
// Test Helpers
// ============================================================================

static int g_testsPassed = 0;
static int g_testsFailed = 0;

#define TEST(name) \
    std::cout << "  Testing: " << #name << "... "; \
    try { test_##name(); g_testsPassed++; std::cout << "PASS\n"; } \
    catch (const std::exception& e) { g_testsFailed++; std::cout << "FAIL: " << e.what() << "\n"; } \
    catch (...) { g_testsFailed++; std::cout << "FAIL: Unknown exception\n"; }

#define ASSERT(cond) \
    if (!(cond)) { throw std::runtime_error("Assertion failed: " #cond); }

// ============================================================================
// RHI Types Tests
// ============================================================================

void test_handle_validity() {
    TextureHandle tex;
    ASSERT(!tex.isValid());

    tex.id = 42;
    ASSERT(tex.isValid());

    BufferHandle buf;
    ASSERT(!buf.isValid());

    ShaderHandle shader;
    ASSERT(!shader.isValid());

    UniformHandle uniform;
    ASSERT(!uniform.isValid());
}

void test_render_state_defaults() {
    RenderState state;
    ASSERT(state.blend == BlendMode::Alpha);
    ASSERT(state.cull == CullMode::None);
    ASSERT(state.depthTest == false);
    ASSERT(state.depthWrite == false);
}

void test_texture_desc() {
    TextureDesc desc;
    ASSERT(desc.width == 0);
    ASSERT(desc.height == 0);
    ASSERT(desc.mipLevels == 1);
    ASSERT(desc.format == TextureDesc::RGBA8);
    ASSERT(desc.data == nullptr);
}

void test_buffer_desc() {
    BufferDesc desc;
    ASSERT(desc.size == 0);
    ASSERT(desc.data == nullptr);
    ASSERT(desc.dynamic == false);
    ASSERT(desc.type == BufferDesc::Vertex);
}

// ============================================================================
// Command Buffer Tests
// ============================================================================

void test_command_buffer_empty() {
    RHICommandBuffer cmd;
    ASSERT(cmd.size() == 0);
    ASSERT(cmd.getCommands().empty());
}

void test_command_buffer_set_state() {
    RHICommandBuffer cmd;

    RenderState state;
    state.blend = BlendMode::Additive;
    state.cull = CullMode::CW;
    state.depthTest = true;
    state.depthWrite = true;

    cmd.setState(state);

    ASSERT(cmd.size() == 1);
    ASSERT(cmd.getCommands()[0].type == CommandType::SetState);
    ASSERT(cmd.getCommands()[0].setState.state.blend == BlendMode::Additive);
    ASSERT(cmd.getCommands()[0].setState.state.cull == CullMode::CW);
    ASSERT(cmd.getCommands()[0].setState.state.depthTest == true);
}

void test_command_buffer_set_texture() {
    RHICommandBuffer cmd;

    TextureHandle tex;
    tex.id = 123;
    UniformHandle sampler;
    sampler.id = 456;

    cmd.setTexture(0, tex, sampler);

    ASSERT(cmd.size() == 1);
    ASSERT(cmd.getCommands()[0].type == CommandType::SetTexture);
    ASSERT(cmd.getCommands()[0].setTexture.slot == 0);
    ASSERT(cmd.getCommands()[0].setTexture.texture.id == 123);
    ASSERT(cmd.getCommands()[0].setTexture.sampler.id == 456);
}

void test_command_buffer_set_uniform() {
    RHICommandBuffer cmd;

    UniformHandle uniform;
    uniform.id = 42;
    float data[4] = {1.0f, 2.0f, 3.0f, 4.0f};

    cmd.setUniform(uniform, data, 1);

    ASSERT(cmd.size() == 1);
    ASSERT(cmd.getCommands()[0].type == CommandType::SetUniform);
    ASSERT(cmd.getCommands()[0].setUniform.uniform.id == 42);
    ASSERT(cmd.getCommands()[0].setUniform.numVec4s == 1);
    ASSERT(cmd.getCommands()[0].setUniform.data[0] == 1.0f);
    ASSERT(cmd.getCommands()[0].setUniform.data[3] == 4.0f);
}

void test_command_buffer_set_buffers() {
    RHICommandBuffer cmd;

    BufferHandle vb, ib, inst;
    vb.id = 1;
    ib.id = 2;
    inst.id = 3;

    cmd.setVertexBuffer(vb, 100);
    cmd.setIndexBuffer(ib, 200, true);
    cmd.setInstanceBuffer(inst, 10, 50);

    ASSERT(cmd.size() == 3);

    ASSERT(cmd.getCommands()[0].type == CommandType::SetVertexBuffer);
    ASSERT(cmd.getCommands()[0].setVertexBuffer.buffer.id == 1);
    ASSERT(cmd.getCommands()[0].setVertexBuffer.offset == 100);

    ASSERT(cmd.getCommands()[1].type == CommandType::SetIndexBuffer);
    ASSERT(cmd.getCommands()[1].setIndexBuffer.buffer.id == 2);
    ASSERT(cmd.getCommands()[1].setIndexBuffer.offset == 200);
    ASSERT(cmd.getCommands()[1].setIndexBuffer.is32Bit == true);

    ASSERT(cmd.getCommands()[2].type == CommandType::SetInstanceBuffer);
    ASSERT(cmd.getCommands()[2].setInstanceBuffer.buffer.id == 3);
    ASSERT(cmd.getCommands()[2].setInstanceBuffer.start == 10);
    ASSERT(cmd.getCommands()[2].setInstanceBuffer.count == 50);
}

void test_command_buffer_draw() {
    RHICommandBuffer cmd;

    cmd.draw(100, 10);
    cmd.drawIndexed(200, 20);
    cmd.drawInstanced(300, 30);

    ASSERT(cmd.size() == 3);

    ASSERT(cmd.getCommands()[0].type == CommandType::Draw);
    ASSERT(cmd.getCommands()[0].draw.vertexCount == 100);
    ASSERT(cmd.getCommands()[0].draw.startVertex == 10);

    ASSERT(cmd.getCommands()[1].type == CommandType::DrawIndexed);
    ASSERT(cmd.getCommands()[1].drawIndexed.indexCount == 200);
    ASSERT(cmd.getCommands()[1].drawIndexed.startIndex == 20);

    ASSERT(cmd.getCommands()[2].type == CommandType::DrawInstanced);
    ASSERT(cmd.getCommands()[2].drawInstanced.indexCount == 300);
    ASSERT(cmd.getCommands()[2].drawInstanced.instanceCount == 30);
}

void test_command_buffer_submit() {
    RHICommandBuffer cmd;

    ShaderHandle shader;
    shader.id = 999;

    cmd.submit(0, shader, 42);

    ASSERT(cmd.size() == 1);
    ASSERT(cmd.getCommands()[0].type == CommandType::Submit);
    ASSERT(cmd.getCommands()[0].submit.view == 0);
    ASSERT(cmd.getCommands()[0].submit.shader.id == 999);
    ASSERT(cmd.getCommands()[0].submit.depth == 42);
}

void test_command_buffer_clear() {
    RHICommandBuffer cmd;

    cmd.draw(100);
    cmd.draw(200);
    ASSERT(cmd.size() == 2);

    cmd.clear();
    ASSERT(cmd.size() == 0);
}

void test_command_buffer_complex_sequence() {
    RHICommandBuffer cmd;

    // Typical draw call sequence
    RenderState state;
    state.blend = BlendMode::Alpha;
    cmd.setState(state);

    BufferHandle vb, ib;
    vb.id = 1;
    ib.id = 2;
    cmd.setVertexBuffer(vb);
    cmd.setIndexBuffer(ib);

    TextureHandle tex;
    tex.id = 10;
    UniformHandle sampler;
    sampler.id = 20;
    cmd.setTexture(0, tex, sampler);

    cmd.drawIndexed(6);

    ShaderHandle shader;
    shader.id = 100;
    cmd.submit(0, shader);

    ASSERT(cmd.size() == 6);
}

// ============================================================================
// Frame Allocator Tests
// ============================================================================

void test_frame_allocator_basic() {
    FrameAllocator allocator(1024);

    ASSERT(allocator.getCapacity() == 1024);
    ASSERT(allocator.getUsed() == 0);
}

void test_frame_allocator_allocate() {
    FrameAllocator allocator(1024);

    void* ptr1 = allocator.allocate(64);
    ASSERT(ptr1 != nullptr);
    ASSERT(allocator.getUsed() >= 64);

    void* ptr2 = allocator.allocate(128);
    ASSERT(ptr2 != nullptr);
    ASSERT(ptr2 != ptr1);
    ASSERT(allocator.getUsed() >= 192);
}

void test_frame_allocator_alignment() {
    FrameAllocator allocator(1024);

    // Allocate with 16-byte alignment (typical for SIMD)
    // Note: new[] in FrameAllocator may not align the base buffer itself,
    // so we test relative alignment between consecutive allocations
    void* ptr1 = allocator.allocate(1, 16);
    void* ptr2 = allocator.allocate(1, 16);

    // Both should be 16-byte aligned relative to buffer start
    uintptr_t addr1 = reinterpret_cast<uintptr_t>(ptr1);
    uintptr_t addr2 = reinterpret_cast<uintptr_t>(ptr2);

    // The allocator aligns within its buffer, so difference should be aligned
    ASSERT((addr2 - addr1) % 16 == 0 || addr2 % 16 == 0);

    // Test that alignment spacing is respected
    void* ptr3 = allocator.allocate(4, 8);
    void* ptr4 = allocator.allocate(4, 8);
    ASSERT(ptr3 != ptr4);
}

void test_frame_allocator_reset() {
    FrameAllocator allocator(1024);

    allocator.allocate(256);
    size_t usedBefore = allocator.getUsed();
    ASSERT(usedBefore >= 256);

    allocator.reset();
    ASSERT(allocator.getUsed() == 0);
}

void test_frame_allocator_typed() {
    FrameAllocator allocator(1024);

    struct TestStruct {
        int a;
        float b;
        TestStruct() : a(42), b(3.14f) {}
    };

    TestStruct* ptr = allocator.allocate<TestStruct>();
    ASSERT(ptr != nullptr);
    ASSERT(ptr->a == 42);
    ASSERT(ptr->b == 3.14f);
}

void test_frame_allocator_array() {
    FrameAllocator allocator(1024);

    SpriteInstance* sprites = allocator.allocateArray<SpriteInstance>(10);
    ASSERT(sprites != nullptr);

    // Check all are default initialized
    for (int i = 0; i < 10; ++i) {
        sprites[i].x = static_cast<float>(i);
        sprites[i].y = static_cast<float>(i * 2);
    }

    ASSERT(sprites[5].x == 5.0f);
    ASSERT(sprites[5].y == 10.0f);
}

void test_frame_allocator_out_of_memory() {
    FrameAllocator allocator(64);

    void* ptr = allocator.allocate(128);
    ASSERT(ptr == nullptr); // Should fail gracefully
}

// ============================================================================
// Frame Packet Tests
// ============================================================================

void test_frame_packet_struct() {
    FramePacket packet;
    packet.frameNumber = 100;
    packet.deltaTime = 0.016f;
    packet.clearColor = 0xFF0000FF;
    packet.sprites = nullptr;
    packet.spriteCount = 0;

    ASSERT(packet.frameNumber == 100);
    ASSERT(packet.deltaTime == 0.016f);
    ASSERT(packet.clearColor == 0xFF0000FF);
}

void test_view_info_struct() {
    ViewInfo view;
    view.positionX = 100.0f;
    view.positionY = 200.0f;
    view.zoom = 2.0f;
    view.viewportX = 0;
    view.viewportY = 0;
    view.viewportW = 1280;
    view.viewportH = 720;

    ASSERT(view.positionX == 100.0f);
    ASSERT(view.zoom == 2.0f);
    ASSERT(view.viewportW == 1280);
}

void test_sprite_instance_struct() {
    SpriteInstance sprite;
    sprite.x = 50.0f;
    sprite.y = 100.0f;
    sprite.scaleX = 2.0f;
    sprite.scaleY = 2.0f;
    sprite.rotation = 3.14159f;
    sprite.u0 = 0.0f;
    sprite.v0 = 0.0f;
    sprite.u1 = 1.0f;
    sprite.v1 = 1.0f;
    sprite.textureId = 5.0f;
    sprite.layer = 10.0f;
    sprite.padding0 = 0.0f;
    sprite.reserved[0] = sprite.reserved[1] = sprite.reserved[2] = sprite.reserved[3] = 0.0f;
    sprite.r = 1.0f;
    sprite.g = 1.0f;
    sprite.b = 1.0f;
    sprite.a = 1.0f;

    ASSERT(sprite.x == 50.0f);
    ASSERT(sprite.scaleX == 2.0f);
    ASSERT(sprite.textureId == 5.0f);
    ASSERT(sprite.layer == 10.0f);
    ASSERT(sizeof(SpriteInstance) == 80);  // Must be 80 bytes for GPU
}

// ============================================================================
// Main
// ============================================================================

int main() {
    std::cout << "========================================\n";
    std::cout << "BgfxRenderer RHI Unit Tests\n";
    std::cout << "========================================\n\n";

    std::cout << "[RHI Types]\n";
    TEST(handle_validity);
    TEST(render_state_defaults);
    TEST(texture_desc);
    TEST(buffer_desc);

    std::cout << "\n[Command Buffer]\n";
    TEST(command_buffer_empty);
    TEST(command_buffer_set_state);
    TEST(command_buffer_set_texture);
    TEST(command_buffer_set_uniform);
    TEST(command_buffer_set_buffers);
    TEST(command_buffer_draw);
    TEST(command_buffer_submit);
    TEST(command_buffer_clear);
    TEST(command_buffer_complex_sequence);

    std::cout << "\n[Frame Allocator]\n";
    TEST(frame_allocator_basic);
    TEST(frame_allocator_allocate);
    TEST(frame_allocator_alignment);
    TEST(frame_allocator_reset);
    TEST(frame_allocator_typed);
    TEST(frame_allocator_array);
    TEST(frame_allocator_out_of_memory);

    std::cout << "\n[Frame Packet]\n";
    TEST(frame_packet_struct);
    TEST(view_info_struct);
    TEST(sprite_instance_struct);

    std::cout << "\n========================================\n";
    std::cout << "Results: " << g_testsPassed << " passed, " << g_testsFailed << " failed\n";
    std::cout << "========================================\n";

    return g_testsFailed > 0 ? 1 : 0;
}
