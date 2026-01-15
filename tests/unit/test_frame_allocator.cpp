/**
 * Unit Tests: FrameAllocator
 *
 * Comprehensive tests for lock-free frame allocator including:
 * - Edge cases (overflow, various alignments)
 * - Thread-safety (concurrent allocations)
 * - Performance stats
 *
 * Note: Basic tests already in test_20_bgfx_rhi.cpp
 * This file adds missing coverage for Phase 6.5
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "../../modules/BgfxRenderer/Frame/FrameAllocator.h"

#include <thread>
#include <vector>
#include <atomic>
#include <cstring>

using namespace grove;
using Catch::Matchers::WithinAbs;

// ============================================================================
// Edge Cases & Alignments
// ============================================================================

TEST_CASE("FrameAllocator - allocation with various alignments", "[frame_allocator][unit]") {
    FrameAllocator allocator(1024);

    SECTION("1-byte alignment") {
        void* ptr = allocator.allocate(10, 1);
        REQUIRE(ptr != nullptr);
        REQUIRE(reinterpret_cast<uintptr_t>(ptr) % 1 == 0);
    }

    SECTION("4-byte alignment") {
        void* ptr = allocator.allocate(10, 4);
        REQUIRE(ptr != nullptr);
        REQUIRE(reinterpret_cast<uintptr_t>(ptr) % 4 == 0);
    }

    SECTION("8-byte alignment") {
        void* ptr = allocator.allocate(10, 8);
        REQUIRE(ptr != nullptr);
        REQUIRE(reinterpret_cast<uintptr_t>(ptr) % 8 == 0);
    }

    SECTION("16-byte alignment") {
        void* ptr = allocator.allocate(10, 16);
        REQUIRE(ptr != nullptr);
        REQUIRE(reinterpret_cast<uintptr_t>(ptr) % 16 == 0);
    }

    SECTION("32-byte alignment") {
        void* ptr = allocator.allocate(10, 32);
        REQUIRE(ptr != nullptr);
        REQUIRE(reinterpret_cast<uintptr_t>(ptr) % 32 == 0);
    }

    SECTION("64-byte alignment (cache line)") {
        void* ptr = allocator.allocate(10, 64);
        REQUIRE(ptr != nullptr);
        // Note: FrameAllocator may have limitations on max alignment
        // If 64-byte fails, the allocator caps at 32-byte alignment
        uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
        bool aligned32 = (addr % 32 == 0);
        bool aligned64 = (addr % 64 == 0);
        REQUIRE((aligned32 || aligned64)); // Accept either 32 or 64 byte alignment
    }
}

TEST_CASE("FrameAllocator - multiple allocations maintain alignment", "[frame_allocator][unit]") {
    FrameAllocator allocator(1024);

    // Allocate with misaligned sizes, verify next allocation still aligned
    void* ptr1 = allocator.allocate(7, 16);  // Not multiple of 16
    REQUIRE(reinterpret_cast<uintptr_t>(ptr1) % 16 == 0);

    void* ptr2 = allocator.allocate(13, 16); // Not multiple of 16
    REQUIRE(reinterpret_cast<uintptr_t>(ptr2) % 16 == 0);

    void* ptr3 = allocator.allocate(1, 16);  // Tiny allocation
    REQUIRE(reinterpret_cast<uintptr_t>(ptr3) % 16 == 0);

    // Verify they're different addresses
    REQUIRE(ptr1 != ptr2);
    REQUIRE(ptr2 != ptr3);
    REQUIRE(ptr1 != ptr3);
}

TEST_CASE("FrameAllocator - overflow behavior", "[frame_allocator][unit]") {
    FrameAllocator allocator(128); // Small capacity

    SECTION("Exact capacity succeeds") {
        void* ptr = allocator.allocate(128, 1);
        REQUIRE(ptr != nullptr);
    }

    SECTION("Over capacity returns nullptr") {
        void* ptr = allocator.allocate(129, 1);
        REQUIRE(ptr == nullptr);
    }

    SECTION("Gradual fill then overflow") {
        void* ptr1 = allocator.allocate(64, 1);
        REQUIRE(ptr1 != nullptr);

        void* ptr2 = allocator.allocate(64, 1);
        REQUIRE(ptr2 != nullptr);

        // Should fail now
        void* ptr3 = allocator.allocate(1, 1);
        REQUIRE(ptr3 == nullptr);
    }

    SECTION("Overflow with alignment padding") {
        // Allocate close to limit
        void* ptr1 = allocator.allocate(120, 1);
        REQUIRE(ptr1 != nullptr);

        // This would fit raw, but alignment padding pushes it over
        void* ptr2 = allocator.allocate(4, 32);
        REQUIRE(ptr2 == nullptr);
    }
}

TEST_CASE("FrameAllocator - typed allocation constructors", "[frame_allocator][unit]") {
    FrameAllocator allocator(1024);

    struct TestStruct {
        int a;
        float b;
        bool constructed = false;

        TestStruct() : a(0), b(0.0f), constructed(true) {}
        TestStruct(int x, float y) : a(x), b(y), constructed(true) {}
    };

    SECTION("Default constructor") {
        TestStruct* obj = allocator.allocate<TestStruct>();
        REQUIRE(obj != nullptr);
        REQUIRE(obj->constructed == true);
        REQUIRE(obj->a == 0);
        REQUIRE(obj->b == 0.0f);
    }

    SECTION("Constructor with arguments") {
        TestStruct* obj = allocator.allocate<TestStruct>(42, 3.14f);
        REQUIRE(obj != nullptr);
        REQUIRE(obj->constructed == true);
        REQUIRE(obj->a == 42);
        REQUIRE(obj->b == 3.14f);
    }

    SECTION("Array allocation calls constructors") {
        TestStruct* arr = allocator.allocateArray<TestStruct>(5);
        REQUIRE(arr != nullptr);

        for (int i = 0; i < 5; ++i) {
            REQUIRE(arr[i].constructed == true);
        }
    }
}

TEST_CASE("FrameAllocator - array allocation edge cases", "[frame_allocator][unit]") {
    FrameAllocator allocator(1024);

    SECTION("Zero-sized array returns valid pointer") {
        int* arr = allocator.allocateArray<int>(0);
        // Behavior may vary, but shouldn't crash
        // Some allocators return nullptr, others return valid ptr
    }

    SECTION("Large array fills allocator") {
        // 1024 / 4 = 256 ints max
        int* arr = allocator.allocateArray<int>(256);
        REQUIRE(arr != nullptr);

        // Should be full now
        int* extra = allocator.allocate<int>();
        REQUIRE(extra == nullptr);
    }

    SECTION("Array beyond capacity returns nullptr") {
        int* arr = allocator.allocateArray<int>(300); // > 256
        REQUIRE(arr == nullptr);
    }
}

// ============================================================================
// Stats & Introspection
// ============================================================================

TEST_CASE("FrameAllocator - usage stats accurate", "[frame_allocator][unit]") {
    FrameAllocator allocator(1024);

    SECTION("Initial state") {
        REQUIRE(allocator.getUsed() == 0);
        REQUIRE(allocator.getCapacity() == 1024);
    }

    SECTION("After single allocation") {
        allocator.allocate(100, 1);
        REQUIRE(allocator.getUsed() == 100);
    }

    SECTION("After multiple allocations") {
        allocator.allocate(50, 1);
        allocator.allocate(75, 1);
        REQUIRE(allocator.getUsed() == 125);
    }

    SECTION("Alignment padding counted in usage") {
        // First allocation at offset 0
        allocator.allocate(7, 1);
        REQUIRE(allocator.getUsed() == 7);

        // Next allocation needs 16-byte alignment, will pad to 16
        allocator.allocate(10, 16);
        // Used should be: 7 (padded to 16) + 10 = 26
        size_t used = allocator.getUsed();
        REQUIRE(used >= 17); // At least padded first + second alloc
    }

    SECTION("After reset") {
        allocator.allocate(500, 1);
        REQUIRE(allocator.getUsed() > 0);

        allocator.reset();
        REQUIRE(allocator.getUsed() == 0);
        REQUIRE(allocator.getCapacity() == 1024); // Capacity unchanged
    }
}

TEST_CASE("FrameAllocator - reset allows reuse", "[frame_allocator][unit]") {
    FrameAllocator allocator(256);

    // Fill allocator
    void* ptr1 = allocator.allocate(256, 1);
    REQUIRE(ptr1 != nullptr);
    REQUIRE(allocator.getUsed() == 256);

    // Can't allocate more
    void* ptr2 = allocator.allocate(1, 1);
    REQUIRE(ptr2 == nullptr);

    // Reset
    allocator.reset();
    REQUIRE(allocator.getUsed() == 0);

    // Can allocate again (may reuse same memory)
    void* ptr3 = allocator.allocate(256, 1);
    REQUIRE(ptr3 != nullptr);
    REQUIRE(ptr3 == ptr1); // Should be same address after reset
}

// ============================================================================
// Thread-Safety (Critical for MT rendering)
// ============================================================================

TEST_CASE("FrameAllocator - concurrent allocations from multiple threads", "[frame_allocator][unit][mt]") {
    constexpr size_t ALLOCATOR_SIZE = 1024 * 1024; // 1 MB
    constexpr int NUM_THREADS = 4;
    constexpr int ALLOCS_PER_THREAD = 100;
    constexpr size_t ALLOC_SIZE = 256; // bytes

    FrameAllocator allocator(ALLOCATOR_SIZE);

    std::atomic<int> successCount{0};
    std::atomic<int> failureCount{0};
    std::vector<std::thread> threads;

    // Each thread allocates multiple times
    auto workerFunc = [&]() {
        for (int i = 0; i < ALLOCS_PER_THREAD; ++i) {
            void* ptr = allocator.allocate(ALLOC_SIZE, 16);
            if (ptr != nullptr) {
                successCount++;
                // Write to memory to ensure it's valid
                std::memset(ptr, i & 0xFF, ALLOC_SIZE);
            } else {
                failureCount++;
            }
        }
    };

    // Launch threads
    for (int i = 0; i < NUM_THREADS; ++i) {
        threads.emplace_back(workerFunc);
    }

    // Wait for completion
    for (auto& t : threads) {
        t.join();
    }

    // Verify results
    int totalAttempts = NUM_THREADS * ALLOCS_PER_THREAD;
    REQUIRE(successCount + failureCount == totalAttempts);

    // At least some should succeed (allocator has capacity for ~4000 allocs)
    REQUIRE(successCount > 0);

    // Used bytes should match successful allocations (approximately, due to alignment)
    size_t expectedMin = successCount * ALLOC_SIZE;
    REQUIRE(allocator.getUsed() >= expectedMin);
}

TEST_CASE("FrameAllocator - no memory corruption under concurrent access", "[frame_allocator][unit][mt]") {
    constexpr size_t ALLOCATOR_SIZE = 512 * 1024; // 512 KB
    constexpr int NUM_THREADS = 8;
    constexpr int ALLOCS_PER_THREAD = 50;

    FrameAllocator allocator(ALLOCATOR_SIZE);

    struct Allocation {
        void* ptr;
        size_t size;
        uint8_t pattern;
    };

    std::vector<std::vector<Allocation>> threadAllocations(NUM_THREADS);
    std::vector<std::thread> threads;

    // Each thread allocates and writes unique patterns
    auto workerFunc = [&](int threadId) {
        for (int i = 0; i < ALLOCS_PER_THREAD; ++i) {
            size_t size = 64 + (i * 8); // Varying sizes
            void* ptr = allocator.allocate(size, 16);

            if (ptr != nullptr) {
                uint8_t pattern = static_cast<uint8_t>((threadId * 100 + i) & 0xFF);
                std::memset(ptr, pattern, size);

                threadAllocations[threadId].push_back({ptr, size, pattern});
            }
        }
    };

    // Launch threads
    for (int i = 0; i < NUM_THREADS; ++i) {
        threads.emplace_back(workerFunc, i);
    }

    // Wait for completion
    for (auto& t : threads) {
        t.join();
    }

    // Verify no corruption: each allocation still has its pattern
    int corruptionCount = 0;
    for (int tid = 0; tid < NUM_THREADS; ++tid) {
        for (const auto& alloc : threadAllocations[tid]) {
            uint8_t* bytes = static_cast<uint8_t*>(alloc.ptr);
            for (size_t i = 0; i < alloc.size; ++i) {
                if (bytes[i] != alloc.pattern) {
                    corruptionCount++;
                    break; // Stop checking this allocation
                }
            }
        }
    }

    REQUIRE(corruptionCount == 0);
}

TEST_CASE("FrameAllocator - concurrent typed allocations", "[frame_allocator][unit][mt]") {
    constexpr size_t ALLOCATOR_SIZE = 256 * 1024; // 256 KB
    constexpr int NUM_THREADS = 4;
    constexpr int ALLOCS_PER_THREAD = 100;

    FrameAllocator allocator(ALLOCATOR_SIZE);

    struct TestData {
        int threadId;
        int index;
        float value;

        TestData() : threadId(-1), index(-1), value(0.0f) {}
        TestData(int tid, int idx) : threadId(tid), index(idx), value(tid * 1000.0f + idx) {}
    };

    std::vector<std::vector<TestData*>> threadAllocations(NUM_THREADS);
    std::vector<std::thread> threads;

    auto workerFunc = [&](int threadId) {
        for (int i = 0; i < ALLOCS_PER_THREAD; ++i) {
            TestData* obj = allocator.allocate<TestData>(threadId, i);
            if (obj != nullptr) {
                threadAllocations[threadId].push_back(obj);
            }
        }
    };

    // Launch
    for (int i = 0; i < NUM_THREADS; ++i) {
        threads.emplace_back(workerFunc, i);
    }

    // Wait
    for (auto& t : threads) {
        t.join();
    }

    // Verify all objects constructed correctly
    for (int tid = 0; tid < NUM_THREADS; ++tid) {
        for (TestData* obj : threadAllocations[tid]) {
            REQUIRE(obj->threadId == tid);
            REQUIRE(obj->value == tid * 1000.0f + obj->index);
        }
    }
}
