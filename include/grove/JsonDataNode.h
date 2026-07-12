#pragma once

#include "IDataNode.h"
#include "JsonDataValue.h"
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <memory>
#include <map>
#include <functional>
#include <cstdint>

namespace grove {

using json = nlohmann::json;

/**
 * @brief Concrete implementation of IDataNode backed by JSON
 *
 * Represents a node in the hierarchical data tree. Can have:
 * - Children nodes (map of name -> node)
 * - Own data (JSON value)
 * - Path in the tree for identification
 */
class JsonDataNode : public IDataNode {
public:
    /**
     * @brief Create a node with name and optional data
     * @param name Node name
     * @param data Optional JSON data for this node
     * @param parent Optional parent node (for path tracking)
     * @param readOnly Whether this node is read-only (for config/)
     */
    JsonDataNode(const std::string& name,
                 const json& data = json::object(),
                 JsonDataNode* parent = nullptr,
                 bool readOnly = false);

    // Out-of-line (not =default) so grove::mem can track the free when GROVE_MEM_TRACKING is on
    // (JsonDataNode is IIO's workhorse alloc). Behaviourally identical — members self-destruct.
    virtual ~JsonDataNode();

    // Tree navigation
    std::unique_ptr<IDataNode> getChild(const std::string& name) override;
    IDataNode* getChildReadOnly(const std::string& name) override;
    std::vector<std::string> getChildNames() override;
    bool hasChildren() override;
    bool hasChild(const std::string& name) const override;

    // Exact search in children
    std::vector<IDataNode*> getChildrenByName(const std::string& name) override;
    bool hasChildrenByName(const std::string& name) const override;
    IDataNode* getFirstChildByName(const std::string& name) override;

    // Pattern matching search
    std::vector<IDataNode*> getChildrenByNameMatch(const std::string& pattern) override;
    bool hasChildrenByNameMatch(const std::string& pattern) const override;
    IDataNode* getFirstChildByNameMatch(const std::string& pattern) override;

    // Query by properties
    std::vector<IDataNode*> queryByProperty(const std::string& propName,
                                           const std::function<bool(const IDataValue&)>& predicate) override;

    // Node's own data
    std::unique_ptr<IDataValue> getData() const override;
    bool hasData() const override;
    void setData(std::unique_ptr<IDataValue> data) override;

    // Typed data access
    std::string getString(const std::string& name, const std::string& defaultValue = "") const override;
    int getInt(const std::string& name, int defaultValue = 0) const override;
    double getDouble(const std::string& name, double defaultValue = 0.0) const override;
    bool getBool(const std::string& name, bool defaultValue = false) const override;
    bool hasProperty(const std::string& name) const override;

    // Typed data modification
    void setString(const std::string& name, const std::string& value) override;
    void setInt(const std::string& name, int value) override;
    void setDouble(const std::string& name, double value) override;
    void setBool(const std::string& name, bool value) override;

    // Hash system
    std::string getDataHash() override;
    std::string getTreeHash() override;
    std::string getSubtreeHash(const std::string& childPath) override;

    // Metadata
    std::string getPath() const override;
    std::string getName() const override;
    std::string getNodeType() const override;

    // Tree modification
    void setChild(const std::string& name, std::unique_ptr<IDataNode> node) override;
    bool removeChild(const std::string& name) override;
    void clearChildren() override;

    // Serialization: the node's own JSON data as a compact string (IO contract §4/§8). IIO payloads carry
    // their content here (the typed get/set operate on m_data), so this is exactly the payload a subscriber
    // reads or the replay sink captures. Const, no mutation. THROW-PROOF: a payload may carry non-UTF8 bytes
    // (a blob, or the legacy blob-in-a-string) and a plain dump() would throw -> `error_handler::replace`
    // substitutes U+FFFD instead of throwing (closes the replay/network crash). Blobs (raw, beside the json)
    // are emitted base64 under "__blobs__" so the text stays valid + round-trippable -- base64 paid ONLY here
    // (replay/network, rare), never on the hot in-process path.
    std::string serialize() const override {
        if (m_blobs.empty()) {
            return m_data.dump(-1, ' ', false, json::error_handler_t::replace);
        }
        json j = m_data;
        for (const auto& [name, bytes] : m_blobs) j["__blobs__"][name] = base64Encode(bytes);
        return j.dump(-1, ' ', false, json::error_handler_t::replace);
    }

    // --- Binary payload (blobs) : raw bytes beside the json (IDataNode override). ------------------------
    void setBlob(const std::string& name, const uint8_t* data, size_t size) override {
        m_blobs[name].assign(data, data + size);
    }
    const std::vector<uint8_t>* getBlob(const std::string& name) const override {
        auto it = m_blobs.find(name);
        return it == m_blobs.end() ? nullptr : &it->second;
    }

    // Base64 (RFC 4648) — used by serialize() to make a raw blob JSON-safe, and by a reader (replay/network
    // tier) to recover the bytes. Static + symmetric so both sides + tests share one implementation.
    static std::string base64Encode(const std::vector<uint8_t>& in) {
        static const char* T = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string out;
        out.reserve(((in.size() + 2) / 3) * 4);
        size_t i = 0;
        for (; i + 3 <= in.size(); i += 3) {
            uint32_t n = (static_cast<uint32_t>(in[i]) << 16) | (static_cast<uint32_t>(in[i + 1]) << 8) | in[i + 2];
            out.push_back(T[(n >> 18) & 63]); out.push_back(T[(n >> 12) & 63]);
            out.push_back(T[(n >> 6) & 63]);  out.push_back(T[n & 63]);
        }
        if (i < in.size()) {                                   // 1 or 2 trailing bytes -> '=' padding
            const bool two = (i + 1 < in.size());
            uint32_t n = static_cast<uint32_t>(in[i]) << 16;
            if (two) n |= static_cast<uint32_t>(in[i + 1]) << 8;
            out.push_back(T[(n >> 18) & 63]);
            out.push_back(T[(n >> 12) & 63]);
            out.push_back(two ? T[(n >> 6) & 63] : '=');
            out.push_back('=');
        }
        return out;
    }
    static std::vector<uint8_t> base64Decode(const std::string& in) {
        auto val = [](char c) -> int {
            if (c >= 'A' && c <= 'Z') return c - 'A';
            if (c >= 'a' && c <= 'z') return c - 'a' + 26;
            if (c >= '0' && c <= '9') return c - '0' + 52;
            if (c == '+') return 62;
            if (c == '/') return 63;
            return -1;                                         // '=' / whitespace / invalid -> skipped
        };
        std::vector<uint8_t> out;
        out.reserve((in.size() / 4) * 3);
        uint32_t buf = 0; int bits = 0;
        for (char c : in) {
            int v = val(c);
            if (v < 0) continue;
            buf = (buf << 6) | static_cast<uint32_t>(v);
            bits += 6;
            if (bits >= 8) { bits -= 8; out.push_back(static_cast<uint8_t>((buf >> bits) & 0xFF)); }
        }
        return out;
    }

    // Direct JSON access (for internal use by JsonDataTree)
    const json& getJsonData() const { return m_data; }
    json& getJsonData() { return m_data; }
    const std::map<std::string, std::unique_ptr<JsonDataNode>>& getChildren() const { return m_children; }

    // QUOI : sérialise l'ARBRE COMPLET (m_data scalaires + m_children RÉCURSIF) en un seul json.
    // POURQUOI : `getJsonData()` ne renvoie QUE m_data -- les enfants ajoutés par `setChild()` vivent dans
    //   m_children (map séparée) et étaient PERDUS quand IntraIO::publish() re-home le message via getJsonData()
    //   (chemin non-core-resident). Résultat : un message "batch" (render:sprite:batch construit par setChild)
    //   arrivait VIDE chez l'abonné. toFullJson() fusionne les deux -> le re-home préserve les enfants. Pour un
    //   nœud sans enfant (99% des messages) c'est == copie de m_data (zéro surcoût). Utilisé par le re-home.
    json toFullJson() const {
        json result = m_data;
        for (const auto& [name, child] : m_children) result[name] = child->toFullJson();
        return result;
    }

    // QUOI : construit le nœud payload du RE-HOME -- l'arbre json complet (toFullJson) + les blobs bruts COPIÉS,
    //   renommé. POURQUOI : IntraIO::publish() re-copie le message sur le chemin non-core-resident ; sans ça les
    //   blobs (qui ne peuvent PAS se fondre dans le json -- c'est tout l'intérêt) seraient perdus, comme les
    //   enfants l'étaient avant toFullJson. Le chemin core-resident partage le nœud tel quel (blobs inclus, zéro
    //   copie) et n'appelle pas ceci. Retourne un const partagé prêt à router.
    std::shared_ptr<const JsonDataNode> rehomed(const std::string& name) const {
        auto node = std::make_shared<JsonDataNode>(name, toFullJson());
        node->m_blobs = m_blobs;   // carry raw bytes (même classe -> accès privé)
        return node;
    }

private:
    std::string m_name;
    json m_data;
    JsonDataNode* m_parent;
    bool m_readOnly;
    std::map<std::string, std::unique_ptr<JsonDataNode>> m_children;
    std::map<std::string, std::vector<uint8_t>> m_blobs;  // named RAW binary payloads, OUTSIDE m_data (no UTF-8 abuse)

    // Helper methods
    bool matchesPattern(const std::string& text, const std::string& pattern) const;
    void collectMatchingNodes(const std::string& pattern, std::vector<IDataNode*>& results);
    void collectNodesByProperty(const std::string& propName,
                                const std::function<bool(const IDataValue&)>& predicate,
                                std::vector<IDataNode*>& results);
    std::string computeHash(const std::string& input) const;
    void checkReadOnly() const;
};

} // namespace grove
