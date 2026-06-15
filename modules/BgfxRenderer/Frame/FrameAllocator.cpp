#include "FrameAllocator.h"
#include <cstdint>

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
    // BUG FIX: align the ABSOLUTE pointer address, not the offset-from-buffer-start.
    // `new uint8_t[]` only guarantees alignof(max_align_t) (16 on x64), so aligning
    // the offset alone returned addresses that were 16-aligned even when the caller
    // asked for 32/64 (SIMD/AVX/cache-line) — i.e. the alignment contract was silently
    // violated for any alignment > the buffer's natural alignment. We round the real
    // address (base + offset) up to `alignment` and store the corresponding offset.
    // (alignment must be a power of two — the standard contract for this API.)
    const uintptr_t base = reinterpret_cast<uintptr_t>(m_buffer);
    size_t current = m_offset.load(std::memory_order_relaxed);
    size_t alignedOffset;

    do {
        const uintptr_t addr = base + current;
        const uintptr_t alignedAddr =
            (addr + alignment - 1) & ~(static_cast<uintptr_t>(alignment) - 1);
        alignedOffset = static_cast<size_t>(alignedAddr - base);

        // Check if we have enough space
        if (alignedOffset + size > m_capacity) {
            return nullptr; // Out of memory
        }
    } while (!m_offset.compare_exchange_weak(
        current, alignedOffset + size,
        std::memory_order_release,
        std::memory_order_relaxed));

    return m_buffer + alignedOffset;
}

void FrameAllocator::reset() {
    m_offset.store(0, std::memory_order_release);
}

size_t FrameAllocator::getUsed() const {
    return m_offset.load(std::memory_order_acquire);
}

} // namespace grove
