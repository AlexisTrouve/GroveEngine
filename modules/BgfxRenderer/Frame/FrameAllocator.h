#pragma once

#include <atomic>
#include <cstdint>
#include <cstddef>
#include <new>

namespace grove {

// ============================================================================
// Frame Allocator - Lock-free linear allocator, reset each frame
// ============================================================================

class FrameAllocator {
public:
    static constexpr size_t DEFAULT_SIZE = 16 * 1024 * 1024; // 16 MB

    explicit FrameAllocator(size_t size = DEFAULT_SIZE);
    ~FrameAllocator();

    // Non-copyable
    FrameAllocator(const FrameAllocator&) = delete;
    FrameAllocator& operator=(const FrameAllocator&) = delete;

    // Thread-safe, lock-free allocation
    void* allocate(size_t size, size_t alignment = 16);

    // Typed allocation with constructor
    template<typename T, typename... Args>
    T* allocate(Args&&... args) {
        void* ptr = allocate(sizeof(T), alignof(T));
        if (!ptr) return nullptr;
        return new (ptr) T(std::forward<Args>(args)...);
    }

    // Array allocation
    template<typename T>
    T* allocateArray(size_t count) {
        void* ptr = allocate(sizeof(T) * count, alignof(T));
        if (!ptr) return nullptr;
        for (size_t i = 0; i < count; ++i) {
            new (static_cast<T*>(ptr) + i) T();
        }
        return static_cast<T*>(ptr);
    }

    // Reset (called once per frame, single-thread)
    void reset();

    // Stats
    size_t getUsed() const;
    size_t getCapacity() const { return m_capacity; }

private:
    uint8_t* m_buffer;
    size_t m_capacity;
    std::atomic<size_t> m_offset;
};

} // namespace grove
