#pragma once

/**
 * grove::mapview::codec — the bit packer (map-viewer engine, slice S0a).
 *
 * WHAT  : The pure, portable bit machine under the world-document. packBits() packs N values
 *         of `bits` width each into a byte buffer; unpackBits() reverses it. No knowledge of
 *         fields, chunks, files, or meaning — just bits in, bits out.
 *
 * WHY   : This is the "free semantic compression" of the format (no wasted width) AND the layer
 *         where cross-machine portability is won: we never memcpy a multi-byte integer, we emit
 *         a DEFINED byte+bit order (LSB-first within each byte). So a chunk packed on one machine
 *         unpacks identically on any other — endianness can never leak in. Isolating it here lets
 *         a single tiny test lock the trickiest, most off-by-one-prone code in the whole format.
 *
 * HOW   : Pure std-only, header-only. Bit i of value k goes to global bit position k*bits + i,
 *         byte = pos/8, in-byte = pos%8 (LSB-first). Two fail-franc guards, no silent garbage:
 *         (1) packBits rejects a value that does not fit in `bits`; (2) unpackBits rejects a
 *         buffer too small for the requested count×width (the codec-level negative control).
 *         bits is constrained to 1..32 (one value fits a uint32); 1u<<32 is UB, so 32 is special-cased.
 */

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace grove {
namespace mapview {
namespace codec {

// ceil(numBits / 8): bytes needed to hold a bit run.
inline size_t bytesForBits(size_t numBits) {
    return (numBits + 7) / 8;
}

// Pack `values` (each using the low `bits` bits) into a byte buffer, LSB-first / little-endian-defined.
// THROWS std::out_of_range if bits is not in [1,32], or if any value does not fit in `bits` bits
// (a value that overflows its declared width is a producer bug — fail franc, never truncate silently).
inline std::vector<uint8_t> packBits(const std::vector<uint32_t>& values, uint8_t bits) {
    if (bits < 1 || bits > 32) {
        throw std::out_of_range("packBits: bits must be in [1,32], got " + std::to_string(bits));
    }
    const size_t totalBits = static_cast<size_t>(values.size()) * bits;
    std::vector<uint8_t> out(bytesForBits(totalBits), 0);

    for (size_t k = 0; k < values.size(); ++k) {
        const uint32_t v = values[k];
        // Range guard (bits==32 holds every uint32, so 1u<<32 is never evaluated).
        if (bits < 32 && v >= (1u << bits)) {
            throw std::out_of_range("packBits: value " + std::to_string(v) +
                                    " does not fit in " + std::to_string(bits) + " bits");
        }
        const size_t base = k * bits;
        for (uint8_t b = 0; b < bits; ++b) {
            if (v & (1u << b)) {
                const size_t pos = base + b;
                out[pos >> 3] |= static_cast<uint8_t>(1u << (pos & 7));
            }
        }
    }
    return out;
}

// Unpack `count` values of `bits` width each from `packed` (inverse of packBits).
// THROWS std::out_of_range if bits is out of range, or if `packed` is too small for count×bits —
// the negative control: a wrong declared width that needs MORE bytes than are present fails loudly
// instead of reading past the buffer and returning garbage.
inline std::vector<uint32_t> unpackBits(const std::vector<uint8_t>& packed, uint8_t bits, size_t count) {
    if (bits < 1 || bits > 32) {
        throw std::out_of_range("unpackBits: bits must be in [1,32], got " + std::to_string(bits));
    }
    const size_t needed = bytesForBits(static_cast<size_t>(count) * bits);
    if (packed.size() < needed) {
        throw std::out_of_range("unpackBits: buffer too small (" + std::to_string(packed.size()) +
                                " bytes) for " + std::to_string(count) + "×" + std::to_string(bits) +
                                " bits (need " + std::to_string(needed) + ")");
    }

    std::vector<uint32_t> out;
    out.reserve(count);
    for (size_t k = 0; k < count; ++k) {
        uint32_t v = 0;
        const size_t base = k * bits;
        for (uint8_t b = 0; b < bits; ++b) {
            const size_t pos = base + b;
            if (packed[pos >> 3] & (1u << (pos & 7))) {
                v |= (1u << b);
            }
        }
        out.push_back(v);
    }
    return out;
}

} // namespace codec
} // namespace mapview
} // namespace grove
