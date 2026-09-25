#pragma once

#include "CarTypes.h"

#include <string>

namespace CarAddon
{
    // Reads one frame of driver intent.
    //
    // Preferred path is the engine's action-mapping system, reached through the
    // Lua `PlayerInput` global. `class PlayerInputSystem` carries no
    // POLYPHASE_API and does not appear in Polyphase.lib, so an addon DLL
    // genuinely cannot call GetActionValue() from C++ -- the Lua bridge is the
    // only way in. This is the same route the first-party OrgeController3D
    // takes for exactly the same reason.
    //
    // If the bridge is missing, or the project has no actions registered under
    // the requested category, every read falls back to raw INP_* polling
    // (exported, always available) so the car is drivable the moment the addon
    // loads and without any project setup.
    class CarInputSource
    {
    public:

        // Poll every action once. Call this once per Tick and reuse the result
        // across fixed substeps -- each Lua-backed read is a pcall, and doing
        // that N times per frame buys nothing.
        static CarInput Poll(const std::string& category, int32_t gamepadIndex, float deadzone);

        // True when the Lua PlayerInput global exists and answered at least one
        // query this session. Purely informational (the wizard surfaces it).
        static bool IsActionBridgeAvailable();

        // Copies the addon's Defaults/Car.input.json to <Project>/InputActions.json
        // when that file does not already exist. The loader only ever reads that
        // exact filename -- there is no glob for *.input.json -- so shipping the
        // defaults under any other name would silently do nothing.
        static void InstallDefaultInputActions();

    private:

        static float GetActionValue(const std::string& category, const std::string& action);
        static bool  IsActionActive(const std::string& category, const std::string& action);
    };
}
