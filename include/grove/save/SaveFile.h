#pragma once

/**
 * grove::save::SaveFile — a versioned whole-game SAVE container, persisted to a JSON file on disk.
 *
 * WHAT : a set of { moduleName -> that module's serialized state }, captured from IModule::getState() and
 *        applied back via IModule::setState() — the SAME per-module state contract hot-reload already uses and
 *        has proven round-trippable. Write it to disk (`save`), read it back later (`load`), and restore each
 *        module (`restoreInto`). Pure: std + nlohmann + the core IDataNode/JsonDataNode/IModule types — NO
 *        engine / module-system coupling. The caller feeds module states in and applies them back out; a thin
 *        engine glue (IEngine::saveState/loadState) can drive this over all registered modules.
 *
 * WHY  : every game product needs to persist and resume its world. The engine already serializes per-module
 *        state for hot-reload; this promotes that into a durable, versioned on-disk save, reusing the exact
 *        mechanism (so a module that hot-reloads correctly also saves/loads correctly).
 *
 * CROSS-DLL SAFETY (learned from the LimitsTest crash — see docs/design/limitstest-segfault-handoff.md):
 *        `capture()` DEEP-COPIES the state's JSON immediately and NEVER retains the IDataNode a hot-loaded
 *        module returned (its vtable lives in the module DLL; holding it across a reload/unload faults).
 *        `restoreInto()` builds a fresh HOST-owned JsonDataNode before setState(). So a save survives a module
 *        reload/unload. This mirrors ModuleLoader::reload()'s own re-homing (it too copies getJsonData()).
 *
 * FORMAT (on disk):
 *   { "grove_save": { "formatVersion": 1, "savedAtUnixMs": <ms>, "modules": { "<name>": <state>, ... } } }
 *
 * ROBUSTNESS: every method is fail-soft — never throws on a bad/missing file or a non-JsonDataNode state; it
 *        returns false and leaves the object in a clean state. A module present at save time but absent at load
 *        (or vice-versa) is skipped, not fatal — the game evolves.
 */

#include <grove/IDataNode.h>
#include <grove/IModule.h>
#include <grove/JsonDataNode.h>

#include <nlohmann/json.hpp>

#include <chrono>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace grove {
namespace save {

class SaveFile {
public:
    static constexpr int kFormatVersion = 1;

    // --- Capture (save side) -----------------------------------------------------------------------
    // Deep-copy a module's state into the save under `moduleName`. The source node is read NOW and not
    // retained (cross-DLL safety). A non-JsonDataNode state stores an empty object (fail-soft — that module
    // just restores to nothing). Re-capturing the same name overwrites.
    void capture(const std::string& moduleName, const IDataNode& state) {
        if (const auto* jn = dynamic_cast<const JsonDataNode*>(&state)) {
            m_modules[moduleName] = jn->getJsonData();          // deep copy of the json
        } else {
            m_modules[moduleName] = nlohmann::json::object();   // unknown node type -> empty (fail-soft)
        }
    }
    // Convenience: capture a module's CURRENT state (module.getState()) in one call.
    void captureModule(const std::string& moduleName, IModule& module) {
        auto state = module.getState();
        if (state) capture(moduleName, *state);
    }

    // --- Persist (disk) ----------------------------------------------------------------------------
    // Write the save to `path` as JSON. Stamps savedAtUnixMs from the wall clock. Returns false on any IO or
    // serialization failure (never throws).
    bool save(const std::string& path) const {
        try {
            const int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
                                    std::chrono::system_clock::now().time_since_epoch()).count();
            nlohmann::json modules = nlohmann::json::object();
            for (const auto& kv : m_modules) modules[kv.first] = kv.second;
            nlohmann::json root;
            root["grove_save"] = { {"formatVersion", kFormatVersion}, {"savedAtUnixMs", now},
                                   {"modules", std::move(modules)} };
            std::ofstream out(path, std::ios::binary | std::ios::trunc);
            if (!out) return false;
            out << root.dump(2);
            return static_cast<bool>(out);
        } catch (...) { return false; }
    }

    // Load a save from `path`. Fail-soft: returns false (and clears *this) on a missing file, malformed JSON,
    // a missing/incompatible structure, or a formatVersion NEWER than this build understands.
    bool load(const std::string& path) {
        clear();
        try {
            std::ifstream in(path, std::ios::binary);
            if (!in) return false;
            nlohmann::json root = nlohmann::json::parse(in, nullptr, /*allow_exceptions=*/false);
            if (root.is_discarded() || !root.contains("grove_save")) return false;
            const auto& gs = root["grove_save"];
            const int ver = gs.value("formatVersion", 0);
            if (ver <= 0 || ver > kFormatVersion) return false;   // unknown / future format
            m_formatVersion = ver;
            m_savedAt = gs.value("savedAtUnixMs", static_cast<int64_t>(0));
            if (auto it = gs.find("modules"); it != gs.end() && it->is_object()) {
                for (auto mit = it->begin(); mit != it->end(); ++mit) m_modules[mit.key()] = mit.value();
            }
            return true;
        } catch (...) { clear(); return false; }
    }

    // --- Restore (load side) -----------------------------------------------------------------------
    bool has(const std::string& moduleName) const { return m_modules.find(moduleName) != m_modules.end(); }
    std::vector<std::string> moduleNames() const {
        std::vector<std::string> names; names.reserve(m_modules.size());
        for (const auto& kv : m_modules) names.push_back(kv.first);
        return names;
    }
    // Read-only access to a captured state (nullptr if absent).
    const nlohmann::json* state(const std::string& moduleName) const {
        auto it = m_modules.find(moduleName);
        return it == m_modules.end() ? nullptr : &it->second;
    }

    // Apply a captured state back into a module: builds a HOST-owned JsonDataNode (safe vtable) and calls
    // module.setState(). Returns false (leaving the module untouched) if there's no captured state for it, so a
    // module added since the save keeps its current state. setState() may still throw on a corrupt state — that
    // is the module's own validation contract, deliberately not swallowed here.
    bool restoreInto(const std::string& moduleName, IModule& module) const {
        auto it = m_modules.find(moduleName);
        if (it == m_modules.end()) return false;
        JsonDataNode node("state", it->second);
        module.setState(node);
        return true;
    }

    void clear() { m_modules.clear(); m_formatVersion = kFormatVersion; m_savedAt = 0; }
    bool empty() const { return m_modules.empty(); }
    int loadedFormatVersion() const { return m_formatVersion; }
    int64_t savedAtUnixMs() const { return m_savedAt; }

private:
    std::map<std::string, nlohmann::json> m_modules;   // moduleName -> serialized state
    int m_formatVersion = kFormatVersion;
    int64_t m_savedAt = 0;
};

} // namespace save
} // namespace grove
