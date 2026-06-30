#pragma once

/**
 * grove::mapview::Manifest — the world-document's readable table-of-contents (slice S0c).
 *
 * WHAT  : The Manifest struct + pure string<->JSON (parse/emit) for it. It describes the world —
 *         coordinate system (topology, cell size, bounds, chunk dims) and the ordered field schema
 *         ("bit par bit") — and where the chunk blobs live. It holds NO bulk numbers.
 *
 * WHY   : This is the human-readable, versionable contract a producer (Theomen, future games) writes
 *         and the viewer reads to know how to unpack ANY chunk without knowing what it means. Keeping
 *         it pure (string in, string out — no filesystem) makes it trivially testable and lets the disk
 *         layer (WorldDocumentDisk.h) or any other transport sit on top.
 *
 * HOW   : nlohmann/json (header-only, vendored). Encodings round-trip through a fixed name table;
 *         an unknown encoding throws (fail-franc — never a silent default). Fixed-width encodings omit
 *         `bits` (derived); variable integers carry it. scale/offset are optional (identity default).
 *         The bounds are finite here; null-per-axis (infinite worlds) is a deferred axis (mapview.md §10).
 */

#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "grove/mapview/Field.h"
#include "grove/mapview/Overlays.h"

namespace grove {
namespace mapview {

// The world's coordinate system + chunking, as declared in the manifest.
struct Coordinate {
    std::string             topology{"square"};    // square | hex | rect (only square impl'd in S1)
    std::array<double, 2>   cellSize{{1.0, 1.0}};   // world units per cell (x,y)
    std::array<int32_t, 3>  boundsMin{{0, 0, 0}};   // inclusive min cell (x,y,z)
    std::array<int32_t, 3>  boundsMax{{0, 0, 0}};   // inclusive max cell (x,y,z)
    std::array<int32_t, 3>  chunkDims{{128, 128, 1}}; // W×H×D cells per chunk (Theomen 128×128×1)
};

struct Manifest {
    int                    formatVersion{1};
    Coordinate             coordinate{};
    std::vector<FieldDecl> fields;             // ordered schema (matches the chunk presence-mask order)
    std::string            chunksDir{"chunks"}; // path (relative to the manifest) holding chunk blobs
    std::vector<Region>    regions;            // vector overlays — inline (low cardinality, no blob)
    std::vector<Marker>    markers;            // point overlays — inline
};

// --- Encoding <-> string (the on-disk field-type names). -----------------------------------------
inline std::string encodingToString(Encoding e) {
    switch (e) {
        case Encoding::Bit:     return "bit";
        case Encoding::Uint:    return "uint";
        case Encoding::Int:     return "int";
        case Encoding::Unorm8:  return "unorm8";
        case Encoding::Unorm16: return "unorm16";
        case Encoding::Float32: return "float32";
    }
    throw std::runtime_error("encodingToString: unknown encoding");
}
inline Encoding encodingFromString(const std::string& s) {
    if (s == "bit")     return Encoding::Bit;
    if (s == "uint")    return Encoding::Uint;
    if (s == "int")     return Encoding::Int;
    if (s == "unorm8")  return Encoding::Unorm8;
    if (s == "unorm16") return Encoding::Unorm16;
    if (s == "float32") return Encoding::Float32;
    throw std::runtime_error("encodingFromString: unknown encoding '" + s + "'");  // fail-franc
}

// Serialize a manifest to pretty JSON (2-space indent).
inline std::string emitManifest(const Manifest& m) {
    nlohmann::json j;
    j["formatVersion"] = m.formatVersion;
    j["coordinate"] = {
        {"topology", m.coordinate.topology},
        {"cellSize", m.coordinate.cellSize},
        {"bounds", {{"min", m.coordinate.boundsMin}, {"max", m.coordinate.boundsMax}}},
        {"chunkDims", m.coordinate.chunkDims},
    };
    nlohmann::json fields = nlohmann::json::array();
    for (const auto& f : m.fields) {
        nlohmann::json jf;
        jf["name"] = f.name;
        jf["encoding"] = encodingToString(f.encoding);
        // Only emit `bits` for the variable-width integer encodings (others derive it).
        if (f.encoding == Encoding::Uint || f.encoding == Encoding::Int) {
            jf["bits"] = f.bits;
        }
        // Only emit scale/offset when they are not the identity (keep the manifest lean).
        if (f.scale != 1.0)  jf["scale"]  = f.scale;
        if (f.offset != 0.0) jf["offset"] = f.offset;
        fields.push_back(jf);
    }
    j["fields"] = fields;
    j["chunks"] = m.chunksDir;

    // Vector overlays — inline JSON lists (low cardinality; areas belong in a categorical field, not here).
    if (!m.regions.empty()) {
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& r : m.regions) {
            nlohmann::json jr{{"cx", r.cx}, {"cy", r.cy}, {"radius", r.radius}, {"type", r.type}};
            if (r.value != 0.0) jr["value"] = r.value;
            arr.push_back(jr);
        }
        j["regions"] = arr;
    }
    if (!m.markers.empty()) {
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& mk : m.markers) {
            nlohmann::json jm{{"x", mk.x}, {"y", mk.y}, {"kind", mk.kind}};
            if (mk.angle != 0.0) jm["angle"] = mk.angle;
            if (mk.scale != 1.0) jm["scale"] = mk.scale;
            arr.push_back(jm);
        }
        j["markers"] = arr;
    }
    return j.dump(2);
}

// Parse a manifest from JSON. THROWS nlohmann::json::exception on malformed JSON, or std::runtime_error
// on an unknown encoding / unsupported version (fail-franc — no silent defaults for the contract).
inline Manifest parseManifest(const std::string& text) {
    const nlohmann::json j = nlohmann::json::parse(text);

    Manifest m;
    m.formatVersion = j.at("formatVersion").get<int>();
    if (m.formatVersion != 1) {
        throw std::runtime_error("parseManifest: unsupported formatVersion " + std::to_string(m.formatVersion));
    }

    const auto& c = j.at("coordinate");
    m.coordinate.topology  = c.at("topology").get<std::string>();
    m.coordinate.cellSize  = c.at("cellSize").get<std::array<double, 2>>();
    m.coordinate.boundsMin = c.at("bounds").at("min").get<std::array<int32_t, 3>>();
    m.coordinate.boundsMax = c.at("bounds").at("max").get<std::array<int32_t, 3>>();
    m.coordinate.chunkDims = c.at("chunkDims").get<std::array<int32_t, 3>>();

    for (const auto& jf : j.at("fields")) {
        FieldDecl f;
        f.name     = jf.at("name").get<std::string>();
        f.encoding = encodingFromString(jf.at("encoding").get<std::string>());
        f.bits     = jf.contains("bits")   ? jf.at("bits").get<uint8_t>()   : 0;
        f.scale    = jf.contains("scale")  ? jf.at("scale").get<double>()   : 1.0;
        f.offset   = jf.contains("offset") ? jf.at("offset").get<double>()  : 0.0;
        m.fields.push_back(std::move(f));
    }

    m.chunksDir = j.value("chunks", std::string{"chunks"});

    if (j.contains("regions")) {
        for (const auto& jr : j.at("regions")) {
            Region r;
            r.cx = jr.at("cx").get<double>();
            r.cy = jr.at("cy").get<double>();
            r.radius = jr.at("radius").get<double>();
            r.type = jr.value("type", static_cast<uint32_t>(0));
            r.value = jr.value("value", 0.0);
            m.regions.push_back(r);
        }
    }
    if (j.contains("markers")) {
        for (const auto& jm : j.at("markers")) {
            Marker mk;
            mk.x = jm.at("x").get<double>();
            mk.y = jm.at("y").get<double>();
            mk.kind = jm.value("kind", static_cast<uint32_t>(0));
            mk.angle = jm.value("angle", 0.0);
            mk.scale = jm.value("scale", 1.0);
            m.markers.push_back(mk);
        }
    }
    return m;
}

} // namespace mapview
} // namespace grove
