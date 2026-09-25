#pragma once

#include <stdint.h>

// Shared enums and plain-data structs for the arcade car controller.
//
// Every enum here is uint8_t-backed on purpose: the inspector exposes them as
// DatumType::Byte with an enumStrings table, and Datum writes exactly one byte
// through that path. Declaring the member as a default-int enum while the
// property says Byte (or vice-versa) is a silent memory-stomp.

namespace CarAddon
{
    // Which end of the car the drive force is modelled as coming from. In the
    // arcade model this only biases how readily the tail steps out -- there is
    // no real torque split.
    enum class DriveLayout : uint8_t
    {
        RearWheel = 0,
        FrontWheel = 1,
        AllWheel = 2,

        Count
    };

    // How eagerly the car enters a drift without the player asking for one.
    enum class DriftMode : uint8_t
    {
        HandbrakeOnly = 0,   // only the handbrake breaks traction
        Assisted = 1,        // handbrake, or a hard turn under power
        Always = 2,          // any sufficiently large slip angle

        Count
    };

    // Deliberately `const char*` and not `const char* const`: Property's
    // enumStrings parameter is `const char**`, and a const-qualified element
    // type decays to `const char* const*`, which will not convert.
    static const char* kDriveLayoutStrings[] = { "Rear Wheel", "Front Wheel", "All Wheel" };
    static const char* kDriftModeStrings[]   = { "Handbrake Only", "Assisted", "Always" };

    // One frame of driver intent. Snapshotted once per Tick and reused across
    // every fixed substep -- polling input inside the substep loop would cost
    // N Lua pcalls per frame for no benefit.
    struct CarInput
    {
        float mThrottle  = 0.0f;   // 0..1
        float mBrake     = 0.0f;   // 0..1
        float mSteer     = 0.0f;   // -1..1, negative = left
        float mHandbrake = 0.0f;   // 0..1
        float mBoost     = 0.0f;   // 0..1
        bool  mResetCar  = false;  // edge-triggered
    };
}
