#pragma once

#include "Nodes/3D/Node3d.h"
#include "SmartPointer.h"

#include "CarTypes.h"

#include <string>

class Audio3D;
class Primitive3D;

// Arcade car controller -- Burnout / OutRun feel, deliberately not a simulation.
//
// Handling model: the chassis heading is rotated directly by steering, and the
// velocity vector *chases* that heading at a rate equal to grip. High grip means
// the car goes where it points; low grip means the tail steps out. That single
// relationship is the entire drift system, and it is why this reads as arcade
// rather than sim -- there is no tire force curve to fall off a cliff and spin
// the player out.
//
// Motion is kinematic: the node moves itself and sweeps a box for wall
// collision.
//
// The collider is a separate Box3D child rather than a shape on this node,
// because Bullet's symbols are not exported from Polyphase.lib -- an addon
// cannot construct a btBoxShape. Box3D is not exported either, so it is created
// by type name and configured through property reflection. That child carries
// inherit-transform off, which makes its *local* rotation equal its world
// rotation; World::SweepTest reads GetRotationQuat() (local), so this is what
// keeps the swept box aligned with the car's heading.
//
// No POLYPHASE_API on this class: the macro resolves to __declspec(dllimport)
// inside an addon translation unit, which would make the linker look for this
// type in Polyphase.dll.
class CarController3D : public Node3D
{
public:

    DECLARE_NODE(CarController3D, Node3D);

    CarController3D();
    virtual ~CarController3D();

    virtual const char* GetTypeName() const override;
    virtual void GatherProperties(std::vector<Property>& outProps) override;

    virtual void Create() override;
    virtual void Destroy() override;
    virtual void Start() override;
    virtual void Tick(float deltaTime) override;

#if EDITOR
    virtual void OnDrawGizmosSelected() override;
#endif

    static bool HandlePropChange(Datum* datum, uint32_t index, const void* newValue);

    // --- Runtime queries (also surfaced to Lua) ------------------------------
    float GetSpeed() const          { return mSpeed; }                  // m/s, signed
    float GetSpeedKph() const       { return fabsf(mSpeed) * 3.6f; }
    float GetRpm() const            { return mRpm; }
    int32_t GetGear() const         { return mGear; }
    bool  IsDrifting() const        { return mDrifting; }
    float GetDriftAmount() const    { return mDriftAmount; }
    float GetSlipAngle() const      { return mSlipAngle; }              // radians
    bool  IsGrounded() const        { return mGrounded; }
    float GetBoost() const          { return mBoostMeter; }
    float GetTopSpeed() const       { return mTopSpeed; }
    float GetBoostCapacity() const  { return mBoostCapacity; }
    glm::vec3 GetVelocity() const   { return mVelocity; }

    // --- Runtime control (Lua / AI / cutscenes) ------------------------------
    // Setting any of these disables action polling *permanently*, not just for
    // the frame -- mUseExternalInput is sticky and nothing resets it on its own
    // (not Start, not ResetTo/ResetInPlace). A script that calls SetInput/
    // SetExternalInput and then stops must call ClearInput/ClearExternalInput
    // when it is done, or the car is stuck on the last input it was given and
    // the player never regains control.
    void SetExternalInput(const CarAddon::CarInput& input);
    void ClearExternalInput()       { mUseExternalInput = false; }

    void ResetTo(glm::vec3 position, float yawDegrees);
    void ResetInPlace();

    // Used by CarCamera3D so it does not need to duplicate the heading math.
    glm::vec3 GetHeadingForward() const;

protected:

    void StepCar(float deltaTime);

    void ProbeGround();
    void UpdateSteering(float deltaTime);
    void UpdateDrive(float deltaTime);
    void UpdateDrift(float deltaTime);
    void UpdateYawAndGrip(float deltaTime);
    void UpdateVertical(float deltaTime);
    void MoveAndCollide(float deltaTime);
    void UpdateOrientation(float deltaTime);

    void UpdateVisuals(float deltaTime);
    void UpdateAudio(float deltaTime);
    void UpdateSmoke(float deltaTime);
    void DrawDebugLines();

    void ResolveNodeRefs();

    // Records each wheel's authored local rotation so spin and steer are applied
    // *relative* to it. Without this the controller would stomp whatever
    // orientation the artist gave the wheels on the first frame -- including the
    // 180-degree yaw that mirrors the left-hand side.
    void CaptureWheelBaseRotations();

    // Creates the Box3D collider child if absent and pushes the current
    // half-extents into it. Safe to call repeatedly.
    void EnsureCollider();
    Primitive3D* GetCollider() const;

    // Keeps the collider's transform in step with the car before a sweep.
    void SyncColliderTransform();

    // Where the collision box sits relative to the car's origin.
    //
    // The origin sits at wheel-contact level (one ride height above the ground),
    // but the box is centred on its own node, so leaving it at the origin makes
    // it rest the car `halfHeight` above the ground instead of `rideHeight`.
    // When halfHeight > rideHeight the ground probe can then never report
    // grounded, gravity fights the sweep, and -- because drive force is zeroed
    // in the air -- the car will not even accelerate.
    //
    // Lifting the box so its underside clears the ride plane hands the ground
    // entirely to the raycast and leaves the box to do only what it is good at:
    // stopping walls.
    glm::vec3 GetColliderLocalOffset() const;

    uint8_t GetGroundCollisionMask() const;

    // Re-binds a weak node reference after a PIE world clone.
    //
    // The usual engine fix (FindRelativeNodePath -> ResolveNodePath) is not
    // available: neither symbol is exported from Polyphase.lib. Instead we take
    // the referenced node's *name* and re-find it beneath this car in whatever
    // world we are actually in. Clones preserve names, so this survives the
    // edit-world -> PIE-world transition, and it also lets the wizard wire
    // things up purely by naming convention.
    Node* ResolveChildRef(NodePtrWeak& ref, const char* conventionalName);

    // ---- Drive -------------------------------------------------------------
    float mTopSpeed = 55.0f;             // m/s (~198 km/h)
    float mReverseTopSpeed = 12.0f;
    float mAccelRate = 17.0f;            // m/s^2 from standstill
    float mAccelFalloff = 0.35f;         // accel multiplier once at top speed
    float mBrakeRate = 32.0f;
    float mCoastRate = 4.5f;             // engine braking when off the pedals
    float mDragCoeff = 0.0016f;          // quadratic, dominates at high speed
    CarAddon::DriveLayout mDriveLayout = CarAddon::DriveLayout::RearWheel;

    // ---- Steering ----------------------------------------------------------
    float mMaxSteerAngle = 38.0f;        // degrees at full lock, low speed
    float mSteerRate = 5.0f;             // lock fractions per second
    float mCountersteerRate = 12.0f;     // faster, so a slide is always catchable
    float mHighSpeedSteerScale = 0.40f;  // lock multiplier at top speed
    float mSteerExponent = 1.4f;         // >1 softens small stick deflections
    float mYawRateScale = 2.4f;
    float mYawFullSpeed = 11.0f;         // m/s for full rotational authority
    float mAirSteerScale = 0.45f;
    bool mInvertSteerInput = false;      // player preference; polled input only

    // ---- Drift -------------------------------------------------------------
    CarAddon::DriftMode mDriftMode = CarAddon::DriftMode::Assisted;
    float mBaseGrip = 8.5f;              // rad/s that velocity chases heading
    float mDriftGrip = 1.4f;
    float mAirGrip = 0.6f;
    float mDriftMinSpeed = 7.0f;
    float mDriftEnterAngle = 14.0f;      // degrees of slip
    float mDriftExitAngle = 6.0f;
    float mDriftEnterTime = 0.10f;
    float mDriftExitTime = 0.30f;        // grip returns over this, never snaps
    float mDriftYawAssist = 22.0f;       // extra deg/s of rotation while sliding

    // ---- Boost -------------------------------------------------------------
    float mBoostSpeedMult = 1.32f;
    float mBoostAccelMult = 1.9f;
    bool  mBoostUseMeter = true;
    float mBoostCapacity = 2.5f;         // seconds of continuous boost
    float mBoostDrainRate = 1.0f;
    float mBoostRefillRate = 0.35f;

    // ---- Ground ------------------------------------------------------------
    float mGravity = -26.0f;             // heavier than real, standard arcade
    float mRideHeight = 0.35f;
    float mGroundProbeDistance = 1.4f;
    float mGroundSnapRate = 14.0f;
    float mGroundAlignRate = 7.0f;
    float mMaxGroundAngle = 50.0f;       // steeper than this counts as a wall
    float mWallSpeedLoss = 0.45f;

    // 0 = pure slide (legacy behavior), 1 = full reflection on a head-on hit.
    // Scaled by how square-on the impact is, so glancing contact still slides.
    float mWallBounce = 0.35f;
    glm::vec3 mCollisionExtents = glm::vec3(0.9f, 0.55f, 2.0f);

    // Gap left between the underside of the collision box and the ride plane.
    // Must stay above zero: a box flush with the ground grazes it on every
    // forward sweep, which reads as a wall and stops the car dead.
    float mColliderGroundClearance = 0.08f;

    // Which collision groups the ground probe and wall sweep can hit.
    //
    // Defaults to everything. Do NOT default this to "all except the car's own
    // group": the editor puts every static mesh it spawns on ColGroup1, so
    // masking that out silently makes the entire level invisible to the car.
    // Self-hits are already prevented by passing the car's own collider as an
    // explicit ignore-object to both queries, so no group juggling is needed.
    uint8_t mGroundCollisionMask = uint8_t(ColGroupAll);

    // ---- Visuals -----------------------------------------------------------
    // Extra yaw applied to the body mesh only. Set to 180 when the mesh was
    // authored facing +Z instead of the engine's forward (-Z).
    //
    // This has to be a body-only offset: rotating the CarController3D node
    // itself achieves nothing, because UpdateOrientation rewrites the root's
    // world rotation from the heading every frame. Rotating the mesh asset does
    // not help either -- the wizard derives its dimensions from the mesh AABB,
    // which is unchanged by a 180-degree yaw.
    float mBodyYawOffset = 0.0f;

    float mBodyRollAngle = 6.0f;         // degrees at full lateral load
    float mBodyPitchAngle = 3.5f;
    float mBodyVisualRate = 9.0f;
    float mWheelRadius = 0.34f;
    float mWheelSteerVisualMax = 30.0f;

    // ---- Audio -------------------------------------------------------------
    float mEnginePitchMin = 0.75f;
    float mEnginePitchMax = 2.10f;
    float mEngineVolumeMin = 0.35f;      // never 0: a silent voice gets dropped
    float mEngineVolumeMax = 1.0f;
    int32_t mGearCount = 6;

    // ---- Input -------------------------------------------------------------
    std::string mInputCategory = "Car";
    int32_t mGamepadIndex = 0;
    float mStickDeadzone = 0.12f;

    // ---- Debug -------------------------------------------------------------
    bool mDebugDraw = false;

    // ---- Node references ---------------------------------------------------
    NodePtrWeak mColliderNode;
    NodePtrWeak mBodyNode;
    NodePtrWeak mWheelFL;
    NodePtrWeak mWheelFR;
    NodePtrWeak mWheelRL;
    NodePtrWeak mWheelRR;
    NodePtrWeak mEngineAudioNode;
    NodePtrWeak mSmokeL;
    NodePtrWeak mSmokeR;

    // ---- Runtime state -----------------------------------------------------
    CarAddon::CarInput mInput;
    CarAddon::CarInput mExternalInput;
    bool mUseExternalInput = false;

    glm::vec3 mVelocity = glm::vec3(0.0f);
    float mSpeed = 0.0f;                 // signed speed along heading
    float mVerticalVelocity = 0.0f;
    float mYaw = 0.0f;                   // radians
    float mSteerAngle = 0.0f;            // radians, current front-wheel angle

    bool  mDrifting = false;
    float mDriftAmount = 0.0f;
    float mSlipAngle = 0.0f;

    bool  mGrounded = false;
    glm::vec3 mGroundNormal = glm::vec3(0.0f, 1.0f, 0.0f);
    glm::vec3 mSmoothedUp = glm::vec3(0.0f, 1.0f, 0.0f);
    float mGroundHitY = 0.0f;

    float mBoostMeter = 1.0f;
    float mRpm = 0.0f;
    int32_t mGear = 1;

    float mLateralAccel = 0.0f;
    float mLongitudinalAccel = 0.0f;
    float mPrevSpeed = 0.0f;
    float mWheelSpinAngle = 0.0f;

    // Authored local rotation of the body, plus the damper state for its lean.
    // Keeping the damper in members rather than reading it back off the node
    // each frame stops the body's own output feeding into its next input, and
    // leaves the authored rotation intact as the reference pose.
    glm::vec3 mBodyBaseEuler = glm::vec3(0.0f);
    float mBodyRollCurrent = 0.0f;
    float mBodyPitchCurrent = 0.0f;

    // Authored local rotation per wheel, in FL / FR / RL / RR order. A wheel
    // yawed ~180 degrees (the usual way to mirror an asymmetric wheel mesh onto
    // the left-hand side) has its local X axis pointing the opposite way, so its
    // spin has to be negated to still turn the correct way in world space.
    glm::vec3 mWheelBaseEuler[4] = {};
    bool mWheelBaseCaptured = false;

    float mStepAccumulator = 0.0f;
    bool  mShapeDirty = false;

    // One-shot "the car never found the ground" diagnostic. Silent failure here
    // is very hard to diagnose from the outside -- the car simply falls forever
    // and nothing is logged.
    bool  mEverGrounded = false;
    float mUngroundedTime = 0.0f;
    bool  mWarnedNoGround = false;
    glm::vec3 mSpawnPosition = glm::vec3(0.0f);
    float mSpawnYaw = 0.0f;
    bool  mSpawnCaptured = false;
};
