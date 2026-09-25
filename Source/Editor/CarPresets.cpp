#include "Editor/CarPresets.h"

#if EDITOR

namespace CarAddon
{
    // Four starting points covering the arcade spectrum. Every field stays
    // editable after a preset is applied -- these are a sane baseline, not a
    // lock-in.
    //
    // The dial that matters most is the Base/Drift grip pair: it sets how hard
    // the velocity vector chases the heading, which is the entire handling
    // model. Roughly:
    //   base 10+  -> on rails, kart-like
    //   base 8-9  -> planted street car
    //   base 6-7  -> loose, slides under provocation
    //   drift < 2 -> long lurid slides
    static const CarPreset sPresets[] =
    {
        {
            "Street",
            "Balanced and forgiving. Grips well, slides only when provoked.",
            // drive
            55.0f, 17.0f, 32.0f, 0.0016f,
            // steering
            38.0f, 5.0f, 12.0f, 0.40f, 2.4f,
            // drift
            DriftMode::Assisted, 8.5f, 1.4f, 14.0f, 0.30f, 22.0f,
            // boost
            1.32f, 1.9f,
            // visuals
            6.0f, 3.5f,
            DriveLayout::RearWheel
        },
        {
            "Drift",
            "Low grip, huge steering lock, strong yaw assist. Built to slide.",
            58.0f, 19.0f, 28.0f, 0.0014f,
            // Big lock plus a very fast countersteer rate: you can always catch it.
            62.0f, 6.0f, 16.0f, 0.55f, 2.9f,
            // Enters a drift readily and holds it -- long exit time so grip
            // eases back instead of snapping the car straight.
            DriftMode::Always, 6.0f, 0.9f, 9.0f, 0.45f, 34.0f,
            1.28f, 1.8f,
            9.0f, 4.0f,
            DriveLayout::RearWheel
        },
        {
            "Muscle",
            "Heavy and fast in a straight line, lazy to turn, easy to light up.",
            64.0f, 21.0f, 26.0f, 0.0018f,
            32.0f, 3.6f, 9.0f, 0.32f, 1.9f,
            DriftMode::Assisted, 7.2f, 1.1f, 12.0f, 0.38f, 26.0f,
            1.40f, 2.2f,
            8.0f, 5.0f,
            DriveLayout::RearWheel
        },
        {
            "Kart",
            "Light, twitchy, very high grip. Almost no slide.",
            42.0f, 24.0f, 40.0f, 0.0022f,
            46.0f, 8.0f, 16.0f, 0.62f, 3.4f,
            DriftMode::HandbrakeOnly, 12.0f, 3.0f, 20.0f, 0.20f, 16.0f,
            1.25f, 1.7f,
            4.0f, 2.0f,
            DriveLayout::AllWheel
        },
    };

    static const char* const sPresetNames[] =
    {
        "Street",
        "Drift",
        "Muscle",
        "Kart",
    };

    static const int32_t kPresetCount = int32_t(sizeof(sPresets) / sizeof(sPresets[0]));

    int32_t GetPresetCount()
    {
        return kPresetCount;
    }

    const CarPreset& GetPreset(int32_t index)
    {
        if (index < 0 || index >= kPresetCount)
        {
            index = 0;
        }
        return sPresets[index];
    }

    const char* const* GetPresetNames()
    {
        return sPresetNames;
    }
}

#endif
