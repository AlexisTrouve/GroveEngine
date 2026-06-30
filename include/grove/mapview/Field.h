#pragma once

/**
 * grove::mapview::Field — self-describing field declarations ("bit par bit").
 *
 * WHAT  : A FieldDecl describes ONE named layer of per-cell data and exactly how it is stored
 *         on disk: its encoding and its bit-width. The codec packs/unpacks raw bit patterns;
 *         decodePhysical() recovers the physical value (metres, °C, 0..1…) from a raw pattern.
 *
 * WHY   : The whole point of the world-document is that the viewer reads ANY producer's data
 *         without knowing what it means — it reads the declaration and knows how to unpack.
 *         A flag costs 1 bit, a 19-category biome 5 bits, elevation 16 bits. No wasted width,
 *         and adding a field type is one enum value + one branch here, never a format rewrite.
 *
 * HOW   : Pure std-only, header-only. Storage is an UNSIGNED bit pattern of storageBits() width;
 *         meaning is layered on top (scale/offset map an integer to a physical range; Int uses
 *         two's-complement in the low bits; Float32 is bit-reinterpreted via memcpy — std::bit_cast
 *         is C++20, we are C++17). The raw<->physical split keeps the codec a pure bit machine
 *         that knows nothing about units. float16 is deliberately NOT in the v1 set (no native
 *         type, conversion routine deferred until a producer needs it — Theomen does not).
 */

#include <cstdint>
#include <cstring>
#include <string>

namespace grove {
namespace mapview {

// How a field's per-cell value is stored. Raw storage is always an unsigned bit pattern of
// storageBits() width; the encoding tells decodePhysical() how to interpret that pattern.
enum class Encoding : uint8_t {
    Bit,      // 1-bit boolean
    Uint,     // unsigned integer, `bits` wide (1..32)
    Int,      // signed integer, `bits` wide (1..32), two's-complement in the low bits
    Unorm8,   // 0..1 normalized, stored in 8 bits
    Unorm16,  // 0..1 normalized, stored in 16 bits
    Float32,  // IEEE-754 single, stored bit-for-bit in 32 bits
};

struct FieldDecl {
    std::string name;
    Encoding    encoding{Encoding::Uint};
    uint8_t     bits{0};       // storage width for Uint/Int; ignored (derived) for the others
    double      scale{1.0};    // physical = raw*scale + offset  (Bit/Uint/Int)
    double      offset{0.0};

    // The on-disk storage width, in bits, for this declaration.
    // POURQUOI: the codec needs one authoritative width; fixed-width encodings derive it,
    // variable-width integers carry it explicitly in `bits`.
    uint8_t storageBits() const {
        switch (encoding) {
            case Encoding::Bit:      return 1;
            case Encoding::Unorm8:   return 8;
            case Encoding::Unorm16:  return 16;
            case Encoding::Float32:  return 32;
            case Encoding::Uint:
            case Encoding::Int:      return bits;
        }
        return bits;
    }
};

// Recover the physical value from a raw bit pattern, per the declaration.
// COMMENT: Int sign-extends from `bits`; Unorm divides by its max; Float32 reinterprets the
// 32-bit pattern; scale/offset apply to the integer encodings (identity defaults elsewhere).
inline double decodePhysical(const FieldDecl& f, uint32_t raw) {
    switch (f.encoding) {
        case Encoding::Bit:
        case Encoding::Uint:
            return static_cast<double>(raw) * f.scale + f.offset;
        case Encoding::Int: {
            const uint8_t b = f.bits;
            int64_t s = static_cast<int64_t>(raw);
            // Sign-extend: if the sign bit (b-1) is set, the value is negative.
            if (b < 32 && (raw & (1u << (b - 1)))) {
                s -= (static_cast<int64_t>(1) << b);
            } else if (b == 32) {
                s = static_cast<int32_t>(raw);
            }
            return static_cast<double>(s) * f.scale + f.offset;
        }
        case Encoding::Unorm8:
            return (static_cast<double>(raw) / 255.0) * f.scale + f.offset;
        case Encoding::Unorm16:
            return (static_cast<double>(raw) / 65535.0) * f.scale + f.offset;
        case Encoding::Float32: {
            float v;
            std::memcpy(&v, &raw, sizeof(v));
            return static_cast<double>(v) * f.scale + f.offset;
        }
    }
    return 0.0;
}

} // namespace mapview
} // namespace grove
