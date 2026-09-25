#pragma once

#include <stdint.h>

// Cached PolyphaseEngineAPI* accessor for the car controller addon. OnLoad
// stashes the pointer here so any addon source file can reach the API without
// threading it through constructors. Mirrors the pattern used by the
// GaussianSplatting and VideoPlayer addons.

struct PolyphaseEngineAPI;

namespace CarAddon
{
    PolyphaseEngineAPI* GetEngineAPI();
    void                SetEngineAPI(PolyphaseEngineAPI* api);

    uint64_t GetOwnerHookId();
    void     SetOwnerHookId(uint64_t hookId);
}
