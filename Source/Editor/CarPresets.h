#pragma once

#if EDITOR

#include "CarTypes.h"

namespace CarAddon
{
    // Handling tuning applied by the wizard. Values are distilled from the
    // Godot-Easy-Vehicle-Physics presets, converted from its N/mm suspension
    // world into this controller's direct-grip terms.
    struct CarPreset
    {
        const char* mName;
        const char* mDescription;

        float mTopSpeed;
        float mAccelRate;
        float mBrakeRate;
        float mDragCoeff;

        float mMaxSteerAngle;
        float mSteerRate;
        float mCountersteerRate;
        float mHighSpeedSteerScale;
        float mYawRateScale;

        DriftMode mDriftMode;
        float mBaseGrip;
        float mDriftGrip;
        float mDriftEnterAngle;
        float mDriftExitTime;
        float mDriftYawAssist;

        float mBoostSpeedMult;
        float mBoostAccelMult;

        float mBodyRollAngle;
        float mBodyPitchAngle;

        DriveLayout mDriveLayout;
    };

    int32_t GetPresetCount();
    const CarPreset& GetPreset(int32_t index);
    const char* const* GetPresetNames();
}

#endif
