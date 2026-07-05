#pragma once

/**
 * grove::dialogue::DialogueRuntime — pure VN / cutscene state machine (UI framework slice 7).
 *
 * WHAT  : A data-driven dialogue graph runner. A script is a set of NODES keyed by id; each node
 *         carries its presentation (speaker/text/background/voice) and its OUTGOING edges — a linear
 *         `goto`, conditional `branch`es, or a set of `choices`. The runtime tracks the current node,
 *         a small VARIABLE store (the condition state), and moves along the edges. A node with no
 *         outgoing edge is terminal (isEnd()).
 *
 * WHY   : The VN/cutscene runtime is engine-side (Alexi's call) so it's reusable beyond Drifterra —
 *         but the STATE MACHINE is pure logic, fully headless-testable. Kept standalone (nlohmann only,
 *         NO IIO / renderer / SDL) like grove::anim / AdaptiveMixer: the DialogueModule wraps this and
 *         drives the PRESENTATION.
 *
 * CONDITIONS (declarative — NOT an expression language, matching the binding doctrine): a node can
 *         `set` variables on entry; a `choice` is OFFERED only if its `when` predicate list holds; a
 *         node's `branch` list picks the first gated `goto`. A predicate is `{var, op?, value?}` with
 *         op ∈ truthy(default)/falsy/eq/ne/gt/ge/lt/le — a single comparison on ONE variable. A `when`
 *         is a LIST of predicates that ALL must hold (AND). No OR, no nesting, no parsing: structured
 *         data. Compound/OR logic = restructure the graph or set a derived flag game-side.
 *
 * HOW   : loadFromJson parses {vars?, start, nodes:{id:{speaker?,text,background?,voice?, set?, goto? |
 *         branch:[{when?,goto}] | choices:[{text,goto,when?}]}}}. Edges are id references. Missing/
 *         unknown ids fail the move (return false) rather than corrupt the state.
 */

#include <nlohmann/json.hpp>

#include <string>
#include <unordered_map>
#include <vector>

namespace grove {
namespace dialogue {

// One branch out of a node: a label + the target node id, plus an optional `when` gate (offered only if it holds).
struct Choice {
    std::string text;
    std::string target;      // id of the node this choice leads to
    nlohmann::json when;      // predicate list; null/absent = always offered
};

// A conditional LINEAR edge: on advance(), the first branch whose `when` holds is taken.
struct Branch {
    nlohmann::json when;      // predicate list; null/absent = always matches (a default/fallthrough)
    std::string target;
};

// One dialogue node — a line of the script plus its outgoing edges + on-entry variable assignments.
struct Node {
    std::string id;
    std::string speaker;      // who is speaking ("" = narrator / none)
    std::string text;         // the line shown
    std::string background;   // optional background asset/scene id, passed through to the UI ("" = unchanged)
    std::string voice;        // optional voice/sfx path, played on entry ("" = none)
    std::string next;         // plain linear auto-advance target id ("" = none)
    std::vector<Choice> choices;    // branch-by-pick options (gated by `when`)
    std::vector<Branch> branches;   // conditional linear edges (first match on advance(), before `next`)
    nlohmann::json set;             // {var: value, ...} assigned to the variable store when this node is entered

    // Terminal: no way out (no linear next, no branches, no choices).
    bool isEnd() const { return next.empty() && choices.empty() && branches.empty(); }
};

class DialogueRuntime {
public:
    // Parse a script object. Returns false (loads nothing) if it has no usable `nodes`. `start` defaults
    // to the first node in map order when absent (nlohmann sorts keys — provide an explicit `start`).
    bool loadFromJson(const nlohmann::json& script) {
        m_nodes.clear();
        m_order.clear();
        m_start.clear();
        m_currentId.clear();
        m_started = false;
        m_vars.clear();

        auto nodesIt = script.find("nodes");
        if (nodesIt == script.end() || !nodesIt->is_object() || nodesIt->empty()) return false;

        for (auto it = nodesIt->begin(); it != nodesIt->end(); ++it) {
            const nlohmann::json& n = it.value();
            Node node;
            node.id         = it.key();
            node.speaker    = n.value("speaker", std::string{});
            node.text       = n.value("text", std::string{});
            node.background = n.value("background", std::string{});
            node.voice      = n.value("voice", std::string{});
            node.next       = n.value("goto", std::string{});

            if (auto s = n.find("set"); s != n.end() && s->is_object()) node.set = *s;

            if (auto chIt = n.find("choices"); chIt != n.end() && chIt->is_array()) {
                for (const auto& c : *chIt) {
                    Choice choice;
                    choice.text   = c.value("text", std::string{});
                    choice.target = c.value("goto", std::string{});
                    if (auto w = c.find("when"); w != c.end()) choice.when = *w;
                    node.choices.push_back(std::move(choice));
                }
            }
            if (auto brIt = n.find("branch"); brIt != n.end() && brIt->is_array()) {
                for (const auto& b : *brIt) {
                    Branch branch;
                    branch.target = b.value("goto", std::string{});
                    if (auto w = b.find("when"); w != b.end()) branch.when = *w;
                    node.branches.push_back(std::move(branch));
                }
            }

            m_order.push_back(node.id);
            m_nodes.emplace(node.id, std::move(node));
        }

        // Seed the initial variable store from `vars`.
        if (auto v = script.find("vars"); v != script.end() && v->is_object())
            for (auto it = v->begin(); it != v->end(); ++it) m_vars[it.key()] = it.value();

        m_start = script.value("start", std::string{});
        if (m_start.empty() || m_nodes.find(m_start) == m_nodes.end()) m_start = m_order.front();
        return true;
    }

    // Enter the start node (applies its `set`). False if no script is loaded (or start id is missing).
    bool start() { return enter(m_start); }

    // The current node, or nullptr if not started / current id somehow invalid.
    const Node* current() const {
        if (!m_started) return nullptr;
        auto it = m_nodes.find(m_currentId);
        return it == m_nodes.end() ? nullptr : &it->second;
    }

    const std::string& currentId() const { return m_currentId; }
    bool started() const { return m_started; }

    // Follow the current node's LINEAR edge: the first `branch` whose `when` holds, else the plain `goto`.
    bool advance() {
        const Node* c = current();
        if (!c) return false;
        for (const auto& b : c->branches)
            if (!b.target.empty() && evalWhen(b.when)) return enter(b.target);
        if (c->next.empty()) return false;
        return enter(c->next);
    }

    // Take AVAILABLE choice `index` — indexing the OFFERED list (gated-out choices are not counted).
    bool choose(int index) {
        const auto avail = availableChoices();
        if (index < 0 || index >= static_cast<int>(avail.size())) return false;
        return enter(avail[static_cast<size_t>(index)].target);
    }

    // Jump directly to a node by id (id-based choice — a declarative UI event carries the target id).
    bool goToNode(const std::string& id) { return enter(id); }

    // The choices currently OFFERED: the node's choices whose `when` holds (null/absent = always).
    std::vector<Choice> availableChoices() const {
        std::vector<Choice> out;
        const Node* c = current();
        if (!c) return out;
        for (const auto& ch : c->choices) if (evalWhen(ch.when)) out.push_back(ch);
        return out;
    }

    // Current node is terminal (no outgoing edge).
    bool isEnd() const {
        const Node* c = current();
        return c ? c->isEnd() : false;
    }

    size_t nodeCount() const { return m_nodes.size(); }

    // --- Variables (the condition state). setVar drives gates; getVar reads (null if unset). ---
    void setVar(const std::string& name, const nlohmann::json& value) { m_vars[name] = value; }
    nlohmann::json getVar(const std::string& name) const {
        auto it = m_vars.find(name);
        return it == m_vars.end() ? nlohmann::json(nullptr) : it->second;
    }
    void setVars(const nlohmann::json& obj) {
        if (obj.is_object()) for (auto it = obj.begin(); it != obj.end(); ++it) m_vars[it.key()] = it.value();
    }
    const std::unordered_map<std::string, nlohmann::json>& vars() const { return m_vars; }

private:
    // Enter a node: set current + apply its on-entry `set` assignments. False (unchanged) if id unknown.
    bool enter(const std::string& id) {
        auto it = m_nodes.find(id);
        if (it == m_nodes.end()) return false;
        m_currentId = id;
        m_started = true;
        applySet(it->second.set);
        return true;
    }
    void applySet(const nlohmann::json& set) {
        if (!set.is_object()) return;
        for (auto it = set.begin(); it != set.end(); ++it) m_vars[it.key()] = it.value();
    }

    // A `when` is a LIST of predicates that ALL must hold (AND). Missing / non-array = pass (unconditional).
    bool evalWhen(const nlohmann::json& when) const {
        if (!when.is_array()) return true;
        for (const auto& p : when) if (!evalPredicate(p)) return false;
        return true;
    }
    // A predicate {var, op?, value?} — a single comparison on ONE variable. Fail-closed on a bad shape / op.
    bool evalPredicate(const nlohmann::json& p) const {
        if (!p.is_object()) return false;
        const nlohmann::json v = getVar(p.value("var", std::string{}));
        const std::string op = p.value("op", std::string("truthy"));
        if (op == "truthy") return isTruthy(v);
        if (op == "falsy")  return !isTruthy(v);
        const nlohmann::json val = p.contains("value") ? p["value"] : nlohmann::json(nullptr);
        if (op == "eq") return v == val;
        if (op == "ne") return v != val;
        const double a = toNum(v), b = toNum(val);
        if (op == "gt") return a >  b;
        if (op == "ge" || op == "gte") return a >= b;
        if (op == "lt") return a <  b;
        if (op == "le" || op == "lte") return a <= b;
        return false;   // unknown operator
    }
    static bool isTruthy(const nlohmann::json& v) {
        if (v.is_boolean()) return v.get<bool>();
        if (v.is_number())  return v.get<double>() != 0.0;
        if (v.is_string())  return !v.get<std::string>().empty();
        return false;   // null / object / array
    }
    static double toNum(const nlohmann::json& v) {
        if (v.is_number())  return v.get<double>();
        if (v.is_boolean()) return v.get<bool>() ? 1.0 : 0.0;
        if (v.is_string())  { try { return std::stod(v.get<std::string>()); } catch (...) { return 0.0; } }
        return 0.0;
    }

    std::unordered_map<std::string, Node> m_nodes;
    std::vector<std::string> m_order;   // declaration order (for a default start = first node)
    std::string m_start;
    std::string m_currentId;
    bool m_started = false;
    std::unordered_map<std::string, nlohmann::json> m_vars;   // condition state (variables)
};

} // namespace dialogue
} // namespace grove
