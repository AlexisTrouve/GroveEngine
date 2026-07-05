#include "DialogueModule.h"

#include <grove/IDataNode.h>
#include <grove/JsonDataNode.h>

namespace grove {

DialogueModule::DialogueModule()
    : m_config(std::make_unique<JsonDataNode>("config")) {}

DialogueModule::~DialogueModule() = default;

void DialogueModule::setConfiguration(const IDataNode& config, IIO* io, ITaskScheduler* /*scheduler*/) {
    m_io = io;

    // Subscribe to the INPUT topics only (not scene:.*), so the module never receives its own
    // scene:node / scene:end publications back.
    if (m_io) {
        auto cb = [this](const Message& msg) { handleMessage(msg); };
        m_io->subscribe("scene:load", cb);
        m_io->subscribe("scene:advance", cb);
        m_io->subscribe("scene:choose", cb);
        m_io->subscribe("scene:goto", cb);
    }

    // Optional: a host may boot with an inline script under config's "script" object.
    if (auto* jn = dynamic_cast<const JsonDataNode*>(&config)) {
        const auto& j = jn->getJsonData();
        auto it = j.find("script");
        if (it != j.end() && it->is_object()) loadScript(*it);
    }
}

void DialogueModule::process(const IDataNode& /*input*/) {
    // Event-driven: drain everything queued this frame; each message hits handleMessage (which may
    // publish ui:data / scene:node in response — causedBy is stamped from inside the handler).
    while (m_io && m_io->hasMessages() > 0) {
        m_io->pullAndDispatch();
    }
}

void DialogueModule::handleMessage(const Message& msg) {
    if (!msg.data) return;
    const IDataNode& d = *msg.data;

    if (msg.topic == "scene:load") {
        // The script is a nested {start, nodes:{...}} object — it must ride in the node's JSON
        // (IIO transports only m_data), so read it directly off the JsonDataNode.
        if (auto* jn = dynamic_cast<const JsonDataNode*>(msg.data.get())) loadScript(jn->getJsonData());
    }
    else if (msg.topic == "scene:advance") {
        if (m_runtime.advance()) present();   // no-op if the current node has no linear `goto`
    }
    else if (msg.topic == "scene:choose") {
        // index tolerates int (a game payload) OR string (a declarative UI event can only carry strings).
        int index = d.getInt("index", -1);
        if (index < 0) {
            const std::string s = d.getString("index", "");
            if (!s.empty()) { try { index = std::stoi(s); } catch (...) { index = -1; } }
        }
        if (m_runtime.choose(index)) present();
    }
    else if (msg.topic == "scene:goto") {
        const std::string node = d.getString("node", "");
        if (!node.empty() && m_runtime.goToNode(node)) present();
    }
}

void DialogueModule::loadScript(const nlohmann::json& script) {
    if (!m_runtime.loadFromJson(script)) return;
    m_scriptJson = script;
    if (m_runtime.start()) present();   // present the first node immediately on load
}

void DialogueModule::present() {
    const dialogue::Node* n = m_runtime.current();
    if (!n || !m_io) return;
    ++m_nodesEntered;

    // 1. Push the node data to the bound VN screen. The nested `choices` array MUST live in the JSON
    //    (a setChild-assembled array arrives empty — IIO carries only m_data), so build the json here.
    nlohmann::json j;
    auto& scene = j["scene"];
    scene["id"]         = n->id;
    scene["speaker"]    = n->speaker;
    scene["text"]       = n->text;
    scene["background"] = n->background;
    scene["isEnd"]      = n->isEnd();
    scene["choices"]    = nlohmann::json::array();
    for (const auto& c : n->choices) scene["choices"].push_back({{"text", c.text}, {"goto", c.target}});
    m_io->publish("ui:data:merge", std::make_unique<JsonDataNode>("scene_data", j));

    // 2. Voice line (if any) via the sound module.
    if (!n->voice.empty()) {
        auto v = std::make_unique<JsonDataNode>("voice");
        v->setString("path", n->voice);
        m_io->publish("sound:sfx", std::move(v));
    }

    // 3. Node event for game logic (scalars only).
    auto ev = std::make_unique<JsonDataNode>("node");
    ev->setString("id", n->id);
    ev->setString("speaker", n->speaker);
    ev->setString("text", n->text);
    ev->setBool("isEnd", n->isEnd());
    ev->setInt("choiceCount", static_cast<int>(n->choices.size()));
    m_io->publish("scene:node", std::move(ev));

    // 4. Terminal node -> scene:end.
    if (n->isEnd()) {
        auto end = std::make_unique<JsonDataNode>("end");
        end->setString("id", n->id);
        m_io->publish("scene:end", std::move(end));
    }
}

void DialogueModule::shutdown() {}

std::unique_ptr<IDataNode> DialogueModule::getState() {
    // Preserve the script + the current node across hot-reload.
    nlohmann::json j;
    j["script"]    = m_scriptJson;
    j["currentId"] = m_runtime.currentId();
    return std::make_unique<JsonDataNode>("state", j);
}

void DialogueModule::setState(const IDataNode& state) {
    if (auto* jn = dynamic_cast<const JsonDataNode*>(&state)) {
        const auto& j = jn->getJsonData();
        auto scriptIt = j.find("script");
        if (scriptIt != j.end() && scriptIt->is_object() && m_runtime.loadFromJson(*scriptIt)) {
            m_scriptJson = *scriptIt;
            const std::string cur = j.value("currentId", std::string{});
            if (!cur.empty()) m_runtime.goToNode(cur);
            else              m_runtime.start();
        }
    }
}

const IDataNode& DialogueModule::getConfiguration() {
    return *m_config;
}

std::unique_ptr<IDataNode> DialogueModule::getHealthStatus() {
    auto health = std::make_unique<JsonDataNode>("health");
    health->setString("status", "ok");
    health->setBool("scriptLoaded", m_runtime.nodeCount() > 0);
    health->setString("currentNode", m_runtime.currentId());
    health->setInt("nodesEntered", static_cast<int>(m_nodesEntered));
    return health;
}

} // namespace grove
