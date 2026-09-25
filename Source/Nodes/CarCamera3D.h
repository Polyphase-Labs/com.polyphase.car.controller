#pragma once

#include "Nodes/3D/Camera3d.h"
#include "SmartPointer.h"

class CarController3D;

// Chase camera for CarController3D.
//
// Blends the two ideas worth keeping from the Godot references and adds the
// parts neither has:
//
//   * Planar leash (gevp) -- the follow distance is measured with Y zeroed and
//     height applied separately, so jumps and landings do not swing the camera
//     vertically and make people motion sick.
//   * Independently smoothed position and aim (Godot-Advanced-Vehicle) -- the
//     look-at target lags the car slightly, so the car drifts across frame
//     instead of being nailed to the centre.
//   * Speed-driven FOV, velocity look-ahead, and a drift factor that swings the
//     aim from the car's nose toward its direction of travel, so a slide shows
//     the car's flank. This is the Burnout shot; neither reference does it.
//
// Runs with late tick enabled so it reads a car position already updated this
// frame rather than trailing by one.
class CarCamera3D : public Camera3D
{
public:

    DECLARE_NODE(CarCamera3D, Camera3D);

    CarCamera3D();
    virtual ~CarCamera3D();

    virtual const char* GetTypeName() const override;
    virtual void GatherProperties(std::vector<Property>& outProps) override;

    virtual void Create() override;
    virtual void Start() override;
    virtual void Tick(float deltaTime) override;

    void SetTargetCar(CarController3D* car);
    CarController3D* GetTargetCar() const;

protected:

    CarController3D* ResolveTarget();
    void SnapToTarget();

    NodePtrWeak mTarget;

    // ---- Framing -----------------------------------------------------------
    float mFollowDistance = 6.5f;
    float mFollowHeight = 2.4f;
    float mLookAtHeight = 0.9f;

    // ---- Smoothing ---------------------------------------------------------
    float mPositionRate = 6.0f;      // higher = tighter follow
    float mAimRate = 9.0f;
    float mUpRate = 4.0f;

    // ---- Feel --------------------------------------------------------------
    float mBaseFov = 65.0f;
    float mMaxFov = 88.0f;
    float mFovRate = 3.0f;
    float mLookAheadTime = 0.28f;    // seconds of velocity to aim ahead by
    float mDriftAimBlend = 0.55f;    // 0 = behind the nose, 1 = behind travel
    bool  mMakeMainCamera = true;

    // ---- Runtime state -----------------------------------------------------
    glm::vec3 mSmoothedAim = glm::vec3(0.0f);
    glm::vec3 mSmoothedUp = glm::vec3(0.0f, 1.0f, 0.0f);
    float mSmoothedFov = 65.0f;
    bool  mInitialized = false;

    // Throttles the fallback scene scan. Without it, a camera with no target
    // would walk the entire scene graph every single frame.
    float mSearchCooldown = 0.0f;
};
