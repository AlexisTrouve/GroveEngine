/**
 * Unit Tests: RenderGraph
 *
 * Tests render graph compilation and execution including:
 * - Pass registration
 * - Topological sort by priority
 * - Dependency ordering
 * - Setup/shutdown lifecycle
 *
 * Uses MockRHIDevice and mock passes
 */

#include <catch2/catch_test_macros.hpp>

#include "../../modules/BgfxRenderer/RenderGraph/RenderGraph.h"
#include "../../modules/BgfxRenderer/RenderGraph/RenderPass.h"
#include "../mocks/MockRHIDevice.h"

#include <memory>
#include <string>

using namespace grove;
using namespace grove::test;

// ============================================================================
// Mock Render Passes
// ============================================================================

class MockPass : public RenderPass {
public:
    std::string name;
    uint32_t sortOrder;
    std::vector<const char*> deps;

    // Call counters
    static inline int totalSetupCalls = 0;
    static inline int totalShutdownCalls = 0;
    static inline int totalExecuteCalls = 0;

    int setupCalls = 0;
    int shutdownCalls = 0;
    int executeCalls = 0;

    MockPass(const std::string& n, uint32_t order, std::vector<const char*> dependencies = {})
        : name(n), sortOrder(order), deps(std::move(dependencies)) {}

    const char* getName() const override { return name.c_str(); }
    uint32_t getSortOrder() const override { return sortOrder; }
    std::vector<const char*> getDependencies() const override { return deps; }

    void setup(rhi::IRHIDevice& /*device*/) override {
        setupCalls++;
        totalSetupCalls++;
    }

    void shutdown(rhi::IRHIDevice& /*device*/) override {
        shutdownCalls++;
        totalShutdownCalls++;
    }

    void execute(const FramePacket& /*frame*/, rhi::IRHIDevice& /*device*/, rhi::RHICommandBuffer& /*cmd*/) override {
        executeCalls++;
        totalExecuteCalls++;
    }

    static void resetCounters() {
        totalSetupCalls = 0;
        totalShutdownCalls = 0;
        totalExecuteCalls = 0;
    }
};

// ============================================================================
// Add Pass & Basic Operations
// ============================================================================

TEST_CASE("RenderGraph - add single pass", "[render_graph][unit]") {
    RenderGraph graph;

    auto pass = std::make_unique<MockPass>("TestPass", 100);
    graph.addPass(std::move(pass));

    REQUIRE(graph.getPassCount() == 1);
}

TEST_CASE("RenderGraph - add multiple passes", "[render_graph][unit]") {
    RenderGraph graph;

    graph.addPass(std::make_unique<MockPass>("Pass1", 100));
    graph.addPass(std::make_unique<MockPass>("Pass2", 200));
    graph.addPass(std::make_unique<MockPass>("Pass3", 300));

    REQUIRE(graph.getPassCount() == 3);
}

// ============================================================================
// Setup & Shutdown Lifecycle
// ============================================================================

TEST_CASE("RenderGraph - setup calls setup on all passes", "[render_graph][unit]") {
    MockPass::resetCounters();

    RenderGraph graph;
    MockRHIDevice device;

    graph.addPass(std::make_unique<MockPass>("Pass1", 100));
    graph.addPass(std::make_unique<MockPass>("Pass2", 200));

    graph.setup(device);

    REQUIRE(MockPass::totalSetupCalls == 2);
}

TEST_CASE("RenderGraph - shutdown calls shutdown on all passes", "[render_graph][unit]") {
    MockPass::resetCounters();

    RenderGraph graph;
    MockRHIDevice device;

    graph.addPass(std::make_unique<MockPass>("Pass1", 100));
    graph.addPass(std::make_unique<MockPass>("Pass2", 200));

    graph.setup(device);
    graph.shutdown(device);

    REQUIRE(MockPass::totalShutdownCalls == 2);
}

TEST_CASE("RenderGraph - setup then shutdown lifecycle", "[render_graph][unit]") {
    MockPass::resetCounters();

    RenderGraph graph;
    MockRHIDevice device;

    auto* pass1Ptr = new MockPass("Pass1", 100);
    auto* pass2Ptr = new MockPass("Pass2", 200);

    graph.addPass(std::unique_ptr<RenderPass>(pass1Ptr));
    graph.addPass(std::unique_ptr<RenderPass>(pass2Ptr));

    graph.setup(device);

    REQUIRE(pass1Ptr->setupCalls == 1);
    REQUIRE(pass2Ptr->setupCalls == 1);

    graph.shutdown(device);

    REQUIRE(pass1Ptr->shutdownCalls == 1);
    REQUIRE(pass2Ptr->shutdownCalls == 1);
}

// ============================================================================
// Compilation & Sorting
// ============================================================================

TEST_CASE("RenderGraph - compile sorts passes by sortOrder", "[render_graph][unit]") {
    MockPass::resetCounters();

    RenderGraph graph;
    MockRHIDevice device;

    // Add in random order
    graph.addPass(std::make_unique<MockPass>("Pass3", 300));
    graph.addPass(std::make_unique<MockPass>("Pass1", 100));
    graph.addPass(std::make_unique<MockPass>("Pass2", 200));

    graph.compile();
    graph.setup(device);

    // Create frame packet
    FramePacket frame{};
    frame.frameNumber = 1;
    frame.deltaTime = 0.016f;
    frame.spriteCount = 0;
    frame.sprites = nullptr;

    graph.execute(frame, device);

    // All should execute
    REQUIRE(MockPass::totalExecuteCalls == 3);
}

TEST_CASE("RenderGraph - passes execute in sortOrder priority", "[render_graph][unit]") {
    // This test verifies execution order indirectly via a shared counter

    struct OrderedPass : public RenderPass {
        std::string name;
        uint32_t sortOrder;
        int* executionOrderCounter;
        int myExecutionOrder = -1;

        OrderedPass(const std::string& n, uint32_t order, int* counter)
            : name(n), sortOrder(order), executionOrderCounter(counter) {}

        const char* getName() const override { return name.c_str(); }
        uint32_t getSortOrder() const override { return sortOrder; }

        void setup(rhi::IRHIDevice&) override {}
        void shutdown(rhi::IRHIDevice&) override {}

        void execute(const FramePacket&, rhi::IRHIDevice&, rhi::RHICommandBuffer&) override {
            myExecutionOrder = (*executionOrderCounter)++;
        }
    };

    RenderGraph graph;
    MockRHIDevice device;
    int executionCounter = 0;

    auto* pass1 = new OrderedPass("Pass1", 100, &executionCounter);
    auto* pass2 = new OrderedPass("Pass2", 50, &executionCounter);  // Lower priority, should execute first
    auto* pass3 = new OrderedPass("Pass3", 200, &executionCounter);

    graph.addPass(std::unique_ptr<RenderPass>(pass1));
    graph.addPass(std::unique_ptr<RenderPass>(pass2));
    graph.addPass(std::unique_ptr<RenderPass>(pass3));

    graph.compile();
    graph.setup(device);

    FramePacket frame{};
    frame.frameNumber = 1;
    graph.execute(frame, device);

    // Verify execution order: pass2 (50) -> pass1 (100) -> pass3 (200)
    REQUIRE(pass2->myExecutionOrder == 0);
    REQUIRE(pass1->myExecutionOrder == 1);
    REQUIRE(pass3->myExecutionOrder == 2);
}

// ============================================================================
// Dependencies
// ============================================================================

TEST_CASE("RenderGraph - passes with dependencies execute in correct order", "[render_graph][unit]") {
    struct OrderedPass : public RenderPass {
        std::string name;
        uint32_t sortOrder;
        std::vector<const char*> deps;
        int* executionOrderCounter;
        int myExecutionOrder = -1;

        OrderedPass(const std::string& n, uint32_t order, std::vector<const char*> dependencies, int* counter)
            : name(n), sortOrder(order), deps(std::move(dependencies)), executionOrderCounter(counter) {}

        const char* getName() const override { return name.c_str(); }
        uint32_t getSortOrder() const override { return sortOrder; }
        std::vector<const char*> getDependencies() const override { return deps; }

        void setup(rhi::IRHIDevice&) override {}
        void shutdown(rhi::IRHIDevice&) override {}

        void execute(const FramePacket&, rhi::IRHIDevice&, rhi::RHICommandBuffer&) override {
            myExecutionOrder = (*executionOrderCounter)++;
        }
    };

    RenderGraph graph;
    MockRHIDevice device;
    int executionCounter = 0;

    // PassB depends on PassA (must execute after PassA)
    auto* passA = new OrderedPass("PassA", 100, {}, &executionCounter);
    auto* passB = new OrderedPass("PassB", 50, {"PassA"}, &executionCounter); // Lower priority but depends on A

    graph.addPass(std::unique_ptr<RenderPass>(passB));
    graph.addPass(std::unique_ptr<RenderPass>(passA));

    graph.compile();
    graph.setup(device);

    FramePacket frame{};
    frame.frameNumber = 1;
    graph.execute(frame, device);

    // PassA should execute first despite PassB having lower sortOrder
    REQUIRE(passA->myExecutionOrder == 0);
    REQUIRE(passB->myExecutionOrder == 1);
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST_CASE("RenderGraph - compile with no passes", "[render_graph][unit]") {
    RenderGraph graph;

    // Should not crash
    graph.compile();

    REQUIRE(graph.getPassCount() == 0);
}

TEST_CASE("RenderGraph - execute with no passes", "[render_graph][unit]") {
    RenderGraph graph;
    MockRHIDevice device;

    graph.compile();

    FramePacket frame{};
    frame.frameNumber = 1;

    // Should not crash
    graph.execute(frame, device);
}

TEST_CASE("RenderGraph - setup without compile", "[render_graph][unit]") {
    RenderGraph graph;
    MockRHIDevice device;

    graph.addPass(std::make_unique<MockPass>("Pass1", 100));

    // Setup without compiling first - should work
    graph.setup(device);

    REQUIRE(MockPass::totalSetupCalls > 0);
}

TEST_CASE("RenderGraph - multiple executions use same compiled order", "[render_graph][unit]") {
    MockPass::resetCounters();

    RenderGraph graph;
    MockRHIDevice device;

    graph.addPass(std::make_unique<MockPass>("Pass1", 100));
    graph.addPass(std::make_unique<MockPass>("Pass2", 200));

    graph.compile();
    graph.setup(device);

    FramePacket frame{};
    frame.frameNumber = 1;

    // Execute multiple times
    graph.execute(frame, device);
    graph.execute(frame, device);
    graph.execute(frame, device);

    // Each pass should execute 3 times
    REQUIRE(MockPass::totalExecuteCalls == 6); // 2 passes * 3 frames
}

// ============================================================================
// Integration with Real Pass Count
// ============================================================================

TEST_CASE("RenderGraph - getPassCount reflects added passes", "[render_graph][unit]") {
    RenderGraph graph;

    REQUIRE(graph.getPassCount() == 0);

    graph.addPass(std::make_unique<MockPass>("Pass1", 100));
    REQUIRE(graph.getPassCount() == 1);

    graph.addPass(std::make_unique<MockPass>("Pass2", 200));
    REQUIRE(graph.getPassCount() == 2);

    graph.addPass(std::make_unique<MockPass>("Pass3", 300));
    REQUIRE(graph.getPassCount() == 3);
}
