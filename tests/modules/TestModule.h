#pragma once

#include <engine/Module.h>
#include <string>

/**
 * @brief Simple test module for race condition testing
 *
 * This module has a version string that gets modified during compilation
 * to test hot-reload during concurrent compilations.
 */
class TestModule : public grove::Module {
public:
    void initialize() override;
    void update(float deltaTime) override;
    void shutdown() override;

    const char* getName() const override { return "TestModule"; }

    std::string getVersion() const { return version_; }
    int getUpdateCount() const { return updateCount_; }

private:
    std::string version_;
    int updateCount_ = 0;
};

extern "C" {
    grove::Module* createModule();
    void destroyModule(grove::Module* module);
}
