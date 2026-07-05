#pragma once

/**
 * DialogueModule — VN / cutscene runtime as an IModule (UI framework slice 7).
 *
 * WHAT  : Wraps the pure `dialogue::DialogueRuntime` (the node/choice/branch state machine) and drives
 *         the PRESENTATION over IIO. On entering a node it pushes the node's data as `ui:data:merge`
 *         (so a game-authored VN screen — a bound label + a choice repeater — renders it), plays the
 *         node's voice via `sound:*`, and emits `scene:node` / `scene:end` events for game logic.
 *
 * TOPICS (consumed):
 *   scene:load    { start?, nodes:{id:{speaker?,text,background?,voice?, goto? | choices:[{text,goto}]}} }
 *   scene:advance {}                 — follow the current node's linear `goto`
 *   scene:choose  { index }          — take choice N (index tolerates int OR string, for declarative UI)
 *   scene:goto    { node }           — jump to a node by id (how a choice button fires — a string arg)
 *
 * TOPICS (published):
 *   ui:data:merge { scene:{id,speaker,text,background,choices:[{text,goto}]} }  — the bound VN screen
 *   scene:node    { id, speaker, text, isEnd, choiceCount }                      — an event per node
 *   scene:end     { id }                                                        — a terminal node reached
 *   sound:sfx     { path }  (only if the node has a `voice`)                     — voice line
 *
 * WHY   : Engine-side so it's reusable; the STATE MACHINE is pure (DialogueRuntime), this only maps it
 *         onto the bus. It rides the binding engine (ui:data + repeater) rather than drawing anything
 *         itself — engine = the runtime + the machinery, game = the VN screen layout + the script.
 */

#include <grove/IModule.h>
#include <grove/IIO.h>
#include <grove/ITaskScheduler.h>
#include "DialogueRuntime.h"

#include <cstdint>
#include <memory>
#include <string>

namespace grove {

class DialogueModule : public IModule {
public:
    DialogueModule();
    ~DialogueModule() override;

    // IModule interface
    void setConfiguration(const IDataNode& config, IIO* io, ITaskScheduler* scheduler) override;
    void process(const IDataNode& input) override;
    void shutdown() override;
    std::unique_ptr<IDataNode> getState() override;
    void setState(const IDataNode& state) override;
    const IDataNode& getConfiguration() override;
    std::unique_ptr<IDataNode> getHealthStatus() override;
    std::string getType() const override { return "dialogue"; }
    bool isIdle() const override { return true; }

private:
    void handleMessage(const Message& msg);
    void loadScript(const nlohmann::json& script);   // parse + start + present the first node
    void present();                                  // publish the current node's presentation

    IIO* m_io = nullptr;
    std::unique_ptr<IDataNode> m_config;
    dialogue::DialogueRuntime m_runtime;
    nlohmann::json m_scriptJson;   // last-loaded script (preserved across hot-reload via get/setState)
    uint64_t m_nodesEntered = 0;   // health counter
};

} // namespace grove
