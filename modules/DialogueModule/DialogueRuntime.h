#pragma once

/**
 * grove::dialogue::DialogueRuntime — pure VN / cutscene state machine (UI framework slice 7).
 *
 * WHAT  : A data-driven dialogue graph runner. A script is a set of NODES keyed by id; each node
 *         carries its presentation (speaker/text/background/voice) and its OUTGOING edges — either a
 *         single linear `goto` (auto-advance to the next line) OR a set of `choices` (branch by pick).
 *         The runtime tracks the current node and moves along the edges: advance() / choose(i) /
 *         goToNode(id). A node with no outgoing edge is terminal (isEnd()).
 *
 * WHY   : The VN/cutscene runtime is engine-side (Alexi's call) so it's reusable beyond Drifterra —
 *         but the STATE MACHINE is pure logic (which node, which edge), fully headless-testable. Kept
 *         standalone (nlohmann only, NO IIO / renderer / SDL) exactly like grove::anim / AdaptiveMixer:
 *         the DialogueModule wraps this and drives the PRESENTATION (pushes ui:data for a bound VN
 *         screen, plays voice via sound:*) — none of that coupling leaks in here.
 *
 * HOW   : loadFromJson parses {start, nodes:{id:{speaker?,text,background?,voice?, goto? | choices:[
 *         {text,goto}]}}} into Node structs. Edges are id references (robust to reordering, and a
 *         choice can be taken by its target id — see DialogueModule, where declarative UI events can
 *         only carry strings). No expression language / conditions / variables (matches the binding
 *         doctrine) — branches are unconditional; game logic that needs gating rebuilds/reloads the
 *         script. Missing/unknown ids fail the move (return false) rather than corrupt the state.
 */

#include <nlohmann/json.hpp>

#include <string>
#include <unordered_map>
#include <vector>

namespace grove {
namespace dialogue {

// One branch out of a node: a label to show + the target node id to jump to when picked.
struct Choice {
    std::string text;
    std::string target;   // id of the node this choice leads to
};

// One dialogue node — a line of the script plus its outgoing edges.
struct Node {
    std::string id;
    std::string speaker;      // who is speaking ("" = narrator / none)
    std::string text;         // the line shown
    std::string background;   // optional background asset/scene id, passed through to the UI ("" = unchanged)
    std::string voice;        // optional voice/sfx path, played on entry ("" = none)
    std::string next;         // linear auto-advance target id ("" = none)
    std::vector<Choice> choices;   // branch options (empty = linear or terminal)

    // Terminal: no way out (neither a linear next nor any choice).
    bool isEnd() const { return next.empty() && choices.empty(); }
};

class DialogueRuntime {
public:
    // Parse a script object. Returns false (and loads nothing) if it has no usable `nodes`.
    // `start` defaults to the first node in map order when absent (nlohmann sorts object keys, so this
    // is the alphabetically-first id — provide an explicit `start`). Idempotent: replaces any prior script.
    bool loadFromJson(const nlohmann::json& script) {
        m_nodes.clear();
        m_order.clear();
        m_start.clear();
        m_currentId.clear();
        m_started = false;

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
            auto chIt = n.find("choices");
            if (chIt != n.end() && chIt->is_array()) {
                for (const auto& c : *chIt) {
                    Choice choice;
                    choice.text   = c.value("text", std::string{});
                    choice.target = c.value("goto", std::string{});
                    node.choices.push_back(std::move(choice));
                }
            }
            m_order.push_back(node.id);
            m_nodes.emplace(node.id, std::move(node));
        }

        // start: explicit, else the first-declared node.
        m_start = script.value("start", std::string{});
        if (m_start.empty() || m_nodes.find(m_start) == m_nodes.end()) m_start = m_order.front();
        return true;
    }

    // Enter the start node. Returns false if no script is loaded (or start id is missing).
    bool start() {
        auto it = m_nodes.find(m_start);
        if (it == m_nodes.end()) return false;
        m_currentId = m_start;
        m_started = true;
        return true;
    }

    // The current node, or nullptr if not started / current id somehow invalid.
    const Node* current() const {
        if (!m_started) return nullptr;
        auto it = m_nodes.find(m_currentId);
        return it == m_nodes.end() ? nullptr : &it->second;
    }

    const std::string& currentId() const { return m_currentId; }
    bool started() const { return m_started; }

    // Follow the current node's LINEAR `goto`. False if there is none (a choice node or terminal).
    bool advance() {
        const Node* c = current();
        if (!c || c->next.empty()) return false;
        return goToNode(c->next);
    }

    // Take choice `index` on the current node. False if out of range / not a choice node.
    bool choose(int index) {
        const Node* c = current();
        if (!c || index < 0 || index >= static_cast<int>(c->choices.size())) return false;
        return goToNode(c->choices[static_cast<size_t>(index)].target);
    }

    // Jump directly to a node by id (how the UI takes a choice — a declarative event carries the
    // target id string). False (state unchanged) if the id is unknown.
    bool goToNode(const std::string& id) {
        auto it = m_nodes.find(id);
        if (it == m_nodes.end()) return false;
        m_currentId = id;
        m_started = true;
        return true;
    }

    // Current node is terminal (no outgoing edge).
    bool isEnd() const {
        const Node* c = current();
        return c ? c->isEnd() : false;
    }

    size_t nodeCount() const { return m_nodes.size(); }

private:
    std::unordered_map<std::string, Node> m_nodes;
    std::vector<std::string> m_order;   // declaration order (for a default start = first node)
    std::string m_start;
    std::string m_currentId;
    bool m_started = false;
};

} // namespace dialogue
} // namespace grove
