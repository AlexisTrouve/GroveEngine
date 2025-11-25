#include "FrameAllocator.h"

namespace grove {

FrameAllocator::FrameAllocator(size_t size)
    : m_buffer(new uint8_t[size])
    , m_capacity(size)
    , m_offset(0)
{
}

FrameAllocator::~FrameAllocator() {
    delete[] m_buffer;
}

void* FrameAllocator::allocate(size_t size, size_t alignment) {
    size_t current = m_offset.load(std::memory_order_relaxed);
    size_t aligned;

    do {
        // Align the current offset
        aligned = (current + alignment - 1) & ~(alignment - 1);

        // Check if we have enough space
        if (aligned + size > m_capacity) {
            return nullptr; // Out of memory
        }
    } while (!m_offset.compare_exchange_weak(
        current, aligned + size,
        std::memory_order_release,
        std::memory_order_relaxed));

    return m_buffer + aligned;
}

void FrameAllocator::reset() {
    m_offset.store(0, std::memory_order_release);
}

size_t FrameAllocator::getUsed() const {
    return m_offset.load(std::memory_order_acquire);
}

} // namespace grove
