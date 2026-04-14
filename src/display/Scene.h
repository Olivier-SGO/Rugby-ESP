#pragma once
#include "DisplayManager.h"

class Scene {
public:
    virtual ~Scene() = default;

    // Called once when this scene becomes active
    virtual void onActivate() {}

    // Called every frame (~30fps). Draw to Display.
    virtual void render() = 0;

    // Returns how long this scene should be shown (ms). 0 = use default.
    virtual uint32_t durationMs() const { return 0; }
};
