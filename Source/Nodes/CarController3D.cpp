#include "Nodes/CarController3D.h"

#include "CarMath.h"
#include "Runtime/CarInputSource.h"

#include "AssetManager.h"
#include "Engine.h"
#include "Line.h"
#include "Log.h"
#include "World.h"

#include "Nodes/3D/Audio3d.h"
#include "Nodes/3D/Primitive3d.h"

using namespace CarAddon;

FORCE_LINK_DEF(CarController3D);
DEFINE_NODE(CarController3D, Node3D);

// Conventional child names. The Car Setup Wizard builds nodes with exactly
// these names, which is what lets ResolveChildRef re-bind everything after a
// PIE world clone without any node-path machinery.
static const char* const kNameCollider = "Collider";
static const char* const kNameBody    = "Body";
static const char* const kNameWheelFL = "Wheel_FL";
static const char* const kNameWheelFR = "Wheel_FR";
static const char* const kNameWheelRL = "Wheel_RL";
static const char* const kNameWheelRR = "Wheel_RR";
static const char* const kNameAudio   = "EngineAudio";
static const char* const kNameSmokeL  = "Smoke_L";
static const char* const kNameSmokeR  = "Smoke_R";

// Fixed simulation step. The engine steps Bullet with a variable delta and only
// 2 substeps, and node Tick runs *after* that step, so we cannot piggyback on
// engine timing for anything that needs to feel consistent. Running our own
// accumulator makes handling identical at 30fps and 240fps.
static const float kFixedStep = 1.0f / 120.0f;
static const int32_t kMaxSubSteps = 16;

// Steer space is positive-is-right (CarInput::mSteer, and mSteerAngle which
// tracks it). Engine yaw is a right-handed rotation about +Y, so a positive
// yaw swings the heading from -Z toward -X -- that is a *left* turn (see
// GetHeadingForward). The two conventions are opposed, so every conversion out
// of steer space and into yaw/euler space carries this flip.
static const float kSteerToYaw = -1.0f;

CarController3D::CarController3D()
{
    mName = "Car";
}

CarController3D::~CarController3D()
{
}

const char* CarController3D::GetTypeName() const
{
    return "CarController3D";
}

bool CarController3D::HandlePropChange(Datum* datum, uint32_t index, const void* newValue)
{
    Property* prop = static_cast<Property*>(datum);
    OCT_ASSERT(prop != nullptr);
    CarController3D* car = static_cast<CarController3D*>(prop->mOwner);
    OCT_ASSERT(car != nullptr);

    // Property has no min/max/slider support at plugin API v8, so clamping has
    // to happen here. We write the member ourselves and return true, which tells
    // Datum to skip its own write -- returning false would let the raw value
    // through unclamped.
    if (prop->mName == "Collision Half Extents")
    {
        glm::vec3 v = *(glm::vec3*)newValue;
        v = glm::max(v, glm::vec3(0.05f));
        car->mCollisionExtents = v;
        car->mShapeDirty = true;
        return true;
    }

    if (prop->mName == "Base Grip" || prop->mName == "Drift Grip" || prop->mName == "Air Grip")
    {
        float v = glm::max(*(float*)newValue, 0.0f);
        if (prop->mName == "Base Grip")  { car->mBaseGrip = v; }
        if (prop->mName == "Drift Grip") { car->mDriftGrip = v; }
        if (prop->mName == "Air Grip")   { car->mAirGrip = v; }
        return true;
    }

    if (prop->mName == "Drift Exit Time" || prop->mName == "Drift Enter Time")
    {
        // A zero exit time makes grip snap back, which is exactly the sharp,
        // punishing recovery this controller exists to avoid.
        float v = glm::max(*(float*)newValue, 0.01f);
        if (prop->mName == "Drift Exit Time")  { car->mDriftExitTime = v; }
        if (prop->mName == "Drift Enter Time") { car->mDriftEnterTime = v; }
        return true;
    }

    if (prop->mName == "Top Speed")
    {
        car->mTopSpeed = glm::max(*(float*)newValue, 0.1f);
        return true;
    }

    if (prop->mName == "Gear Count")
    {
        car->mGearCount = glm::clamp(*(int32_t*)newValue, 1, 12);
        return true;
    }

    if (prop->mName == "Wall Bounce")
    {
        car->mWallBounce = glm::clamp(*(float*)newValue, 0.0f, 1.0f);
        return true;
    }

    return false;
}

void CarController3D::GatherProperties(std::vector<Property>& outProps)
{
    Node3D::GatherProperties(outProps);

    // SCOPED_CATEGORY hardcodes its local variable name, so each category has
    // to live in its own brace scope.
    {
        SCOPED_CATEGORY("Car|Drive");
        outProps.push_back(Property(DatumType::Float, "Top Speed", this, &mTopSpeed, 1, HandlePropChange));
        outProps.push_back(Property(DatumType::Float, "Reverse Top Speed", this, &mReverseTopSpeed));
        outProps.push_back(Property(DatumType::Float, "Accel Rate", this, &mAccelRate));
        outProps.push_back(Property(DatumType::Float, "Accel Falloff", this, &mAccelFalloff));
        outProps.push_back(Property(DatumType::Float, "Brake Rate", this, &mBrakeRate));
        outProps.push_back(Property(DatumType::Float, "Coast Rate", this, &mCoastRate));
        outProps.push_back(Property(DatumType::Float, "Drag Coeff", this, &mDragCoeff));
        outProps.push_back(Property(DatumType::Byte, "Drive Layout", this, &mDriveLayout, 1, nullptr,
            NULL_DATUM, int32_t(DriveLayout::Count), kDriveLayoutStrings));
    }

    {
        SCOPED_CATEGORY("Car|Steering");
        outProps.push_back(Property(DatumType::Float, "Max Steer Angle", this, &mMaxSteerAngle));
        outProps.push_back(Property(DatumType::Float, "Steer Rate", this, &mSteerRate));
        outProps.push_back(Property(DatumType::Float, "Countersteer Rate", this, &mCountersteerRate));
        outProps.push_back(Property(DatumType::Float, "High Speed Steer Scale", this, &mHighSpeedSteerScale));
        outProps.push_back(Property(DatumType::Float, "Steer Exponent", this, &mSteerExponent));
        outProps.push_back(Property(DatumType::Float, "Yaw Rate Scale", this, &mYawRateScale));
        outProps.push_back(Property(DatumType::Float, "Yaw Full Speed", this, &mYawFullSpeed));
        outProps.push_back(Property(DatumType::Float, "Air Steer Scale", this, &mAirSteerScale));
        outProps.push_back(Property(DatumType::Bool, "Invert X Input", this, &mInvertSteerInput));
    }

    {
        SCOPED_CATEGORY("Car|Drift");
        outProps.push_back(Property(DatumType::Byte, "Drift Mode", this, &mDriftMode, 1, nullptr,
            NULL_DATUM, int32_t(DriftMode::Count), kDriftModeStrings));
        outProps.push_back(Property(DatumType::Float, "Base Grip", this, &mBaseGrip, 1, HandlePropChange));
        outProps.push_back(Property(DatumType::Float, "Drift Grip", this, &mDriftGrip, 1, HandlePropChange));
        outProps.push_back(Property(DatumType::Float, "Air Grip", this, &mAirGrip, 1, HandlePropChange));
        outProps.push_back(Property(DatumType::Float, "Drift Min Speed", this, &mDriftMinSpeed));
        outProps.push_back(Property(DatumType::Float, "Drift Enter Angle", this, &mDriftEnterAngle));
        outProps.push_back(Property(DatumType::Float, "Drift Exit Angle", this, &mDriftExitAngle));
        outProps.push_back(Property(DatumType::Float, "Drift Enter Time", this, &mDriftEnterTime, 1, HandlePropChange));
        outProps.push_back(Property(DatumType::Float, "Drift Exit Time", this, &mDriftExitTime, 1, HandlePropChange));
        outProps.push_back(Property(DatumType::Float, "Drift Yaw Assist", this, &mDriftYawAssist));
    }

    {
        SCOPED_CATEGORY("Car|Boost");
        outProps.push_back(Property(DatumType::Float, "Boost Speed Mult", this, &mBoostSpeedMult));
        outProps.push_back(Property(DatumType::Float, "Boost Accel Mult", this, &mBoostAccelMult));
        outProps.push_back(Property(DatumType::Bool,  "Boost Use Meter", this, &mBoostUseMeter));
        outProps.push_back(Property(DatumType::Float, "Boost Capacity", this, &mBoostCapacity));
        outProps.push_back(Property(DatumType::Float, "Boost Drain Rate", this, &mBoostDrainRate));
        outProps.push_back(Property(DatumType::Float, "Boost Refill Rate", this, &mBoostRefillRate));
    }

    {
        SCOPED_CATEGORY("Car|Ground");
        outProps.push_back(Property(DatumType::Float, "Gravity", this, &mGravity));
        outProps.push_back(Property(DatumType::Float, "Ride Height", this, &mRideHeight));
        outProps.push_back(Property(DatumType::Float, "Ground Probe Distance", this, &mGroundProbeDistance));
        outProps.push_back(Property(DatumType::Float, "Ground Snap Rate", this, &mGroundSnapRate));
        outProps.push_back(Property(DatumType::Float, "Ground Align Rate", this, &mGroundAlignRate));
        outProps.push_back(Property(DatumType::Float, "Max Ground Angle", this, &mMaxGroundAngle));
        outProps.push_back(Property(DatumType::Float, "Wall Speed Loss", this, &mWallSpeedLoss));
        outProps.push_back(Property(DatumType::Float, "Wall Bounce", this, &mWallBounce, 1, HandlePropChange));
        outProps.push_back(Property(DatumType::Vector, "Collision Half Extents", this, &mCollisionExtents, 1, HandlePropChange));
        outProps.push_back(Property(DatumType::Float, "Collider Ground Clearance", this, &mColliderGroundClearance));

        // Flag grid, matching how Primitive3D exposes its own Collision Mask.
        // Leave every group ticked unless you have a reason not to -- unticking
        // ColGroup1 in particular hides all editor-spawned level geometry.
        outProps.push_back(Property(DatumType::Byte, "Ground Collision Mask", this, &mGroundCollisionMask,
            1, nullptr, (int32_t)ByteExtra::FlagWidget));
    }

    {
        SCOPED_CATEGORY("Car|Visuals");
        outProps.push_back(Property(DatumType::Float, "Body Yaw Offset", this, &mBodyYawOffset));
        outProps.push_back(Property(DatumType::Float, "Body Roll Angle", this, &mBodyRollAngle));
        outProps.push_back(Property(DatumType::Float, "Body Pitch Angle", this, &mBodyPitchAngle));
        outProps.push_back(Property(DatumType::Float, "Body Visual Rate", this, &mBodyVisualRate));
        outProps.push_back(Property(DatumType::Float, "Wheel Radius", this, &mWheelRadius));
        outProps.push_back(Property(DatumType::Float, "Wheel Steer Visual Max", this, &mWheelSteerVisualMax));
    }

    {
        SCOPED_CATEGORY("Car|Audio");
        outProps.push_back(Property(DatumType::Float, "Engine Pitch Min", this, &mEnginePitchMin));
        outProps.push_back(Property(DatumType::Float, "Engine Pitch Max", this, &mEnginePitchMax));
        outProps.push_back(Property(DatumType::Float, "Engine Volume Min", this, &mEngineVolumeMin));
        outProps.push_back(Property(DatumType::Float, "Engine Volume Max", this, &mEngineVolumeMax));
        outProps.push_back(Property(DatumType::Integer, "Gear Count", this, &mGearCount, 1, HandlePropChange));
    }

    {
        SCOPED_CATEGORY("Car|Input");
        outProps.push_back(Property(DatumType::String, "Input Category", this, &mInputCategory));
        outProps.push_back(Property(DatumType::Integer, "Gamepad Index", this, &mGamepadIndex));
        outProps.push_back(Property(DatumType::Float, "Stick Deadzone", this, &mStickDeadzone));
    }

    {
        SCOPED_CATEGORY("Car|Nodes");
        outProps.push_back(Property(DatumType::Node, "Collider", this, &mColliderNode));
        outProps.push_back(Property(DatumType::Node, "Body", this, &mBodyNode));
        outProps.push_back(Property(DatumType::Node, "Wheel FL", this, &mWheelFL));
        outProps.push_back(Property(DatumType::Node, "Wheel FR", this, &mWheelFR));
        outProps.push_back(Property(DatumType::Node, "Wheel RL", this, &mWheelRL));
        outProps.push_back(Property(DatumType::Node, "Wheel RR", this, &mWheelRR));
        outProps.push_back(Property(DatumType::Node, "Engine Audio", this, &mEngineAudioNode));
        outProps.push_back(Property(DatumType::Node, "Smoke L", this, &mSmokeL));
        outProps.push_back(Property(DatumType::Node, "Smoke R", this, &mSmokeR));
    }

    {
        SCOPED_CATEGORY("Car|Debug");
        outProps.push_back(Property(DatumType::Bool, "Debug Draw", this, &mDebugDraw));
    }
}

void CarController3D::Create()
{
    Node3D::Create();
}

void CarController3D::Destroy()
{
    Node3D::Destroy();
}

void CarController3D::Start()
{
    Node3D::Start();

    ResolveNodeRefs();
    EnsureCollider();

    // Must happen after the refs resolve and before the first Tick writes to
    // the wheels.
    CaptureWheelBaseRotations();

    // Seed heading from the authored rotation so a car placed at an angle in
    // the editor drives off in the direction it is facing.
    const glm::vec3 euler = GetWorldRotationEuler();
    mYaw = glm::radians(euler.y);
    mSmoothedUp = glm::vec3(0.0f, 1.0f, 0.0f);

    mSpawnPosition = GetWorldPosition();
    mSpawnYaw = mYaw;
    mSpawnCaptured = true;

    mEverGrounded = false;
    mUngroundedTime = 0.0f;
    mWarnedNoGround = false;

    mBoostMeter = mBoostCapacity;

    Audio3D* audio = mEngineAudioNode.Get() ? mEngineAudioNode.Get()->As<Audio3D>() : nullptr;
    if (audio != nullptr && audio->GetSoundWave() != nullptr)
    {
        audio->SetLoop(true);
        if (!audio->IsPlaying())
        {
            audio->Play();
        }
    }
}

void CarController3D::Tick(float deltaTime)
{
    Node3D::Tick(deltaTime);

    if (deltaTime <= 0.0f)
    {
        return;
    }

    // Poll driver intent exactly once per frame. Each action read is a Lua
    // pcall; doing this inside the substep loop would multiply that cost by the
    // substep count for no gain, since input cannot change mid-frame anyway.
    if (mUseExternalInput)
    {
        mInput = mExternalInput;
    }
    else
    {
        mInput = CarInputSource::Poll(mInputCategory, mGamepadIndex, mStickDeadzone);

        // Steering-preference flip, applied once here so every consumer --
        // mSteerAngle, the drift yaw assist, the front-wheel visual -- sees the
        // same value. Deliberately not applied to mExternalInput: a script
        // driving the car already states the steer it wants, and silently
        // negating that would break AI drivers whenever the toggle is on.
        if (mInvertSteerInput)
        {
            mInput.mSteer = -mInput.mSteer;
        }
    }

    if (mInput.mResetCar)
    {
        ResetInPlace();
    }

    mPrevSpeed = mSpeed;

    mStepAccumulator += deltaTime;

    // Cap the backlog so a hitch (asset load, breakpoint) cannot trigger a
    // death spiral of catch-up steps.
    const float maxBacklog = kFixedStep * kMaxSubSteps;
    if (mStepAccumulator > maxBacklog)
    {
        mStepAccumulator = maxBacklog;
    }

    int32_t steps = 0;
    while (mStepAccumulator >= kFixedStep && steps < kMaxSubSteps)
    {
        StepCar(kFixedStep);
        mStepAccumulator -= kFixedStep;
        ++steps;
    }

    mLongitudinalAccel = (mSpeed - mPrevSpeed) / deltaTime;

    // If the car has never once touched ground, say so. Falling silently
    // forever is otherwise indistinguishable from "the controller is broken",
    // and the usual cause is a collision mask that excludes the level.
    if (mGrounded)
    {
        mEverGrounded = true;
    }
    else if (!mEverGrounded && !mWarnedNoGround)
    {
        mUngroundedTime += deltaTime;
        if (mUngroundedTime > 2.0f)
        {
            mWarnedNoGround = true;
            LogWarning(
                "CarController3D '%s': no ground found in 2s. Check that the ground has "
                "Collision enabled, and that its collision group is ticked in this car's "
                "'Ground Collision Mask' (editor-spawned static meshes use ColGroup1). "
                "Enable 'Debug Draw' to see the probe.",
                GetName().c_str());
        }
    }

    UpdateVisuals(deltaTime);
    UpdateAudio(deltaTime);
    UpdateSmoke(deltaTime);

    if (mDebugDraw)
    {
        DrawDebugLines();
    }
}

void CarController3D::StepCar(float deltaTime)
{
    ProbeGround();
    UpdateSteering(deltaTime);
    UpdateDrive(deltaTime);
    UpdateDrift(deltaTime);
    UpdateYawAndGrip(deltaTime);
    UpdateVertical(deltaTime);
    MoveAndCollide(deltaTime);
    UpdateOrientation(deltaTime);

    // Leave the collider co-located with the car. MoveAndCollide syncs it
    // *before* sweeping, which is what the sweep needs, but by the end of the
    // step both position and rotation have changed -- without this the car's
    // physical presence would trail its visual one by a full step for anything
    // else querying the world.
    SyncColliderTransform();
}

void CarController3D::ProbeGround()
{
    World* world = GetWorld();
    if (world == nullptr)
    {
        mGrounded = false;
        return;
    }

    const glm::vec3 pos = GetWorldPosition();
    const glm::vec3 worldUp = glm::vec3(0.0f, 1.0f, 0.0f);

    // Start above the chassis centre so the ray cannot begin already inside
    // whatever we are standing on.
    const glm::vec3 start = pos + worldUp * mCollisionExtents.y;
    const glm::vec3 end = pos - worldUp * (mRideHeight + mGroundProbeDistance);

    // Ignore our own collider, or the probe hits the car's own box immediately.
    Primitive3D* collider = GetCollider();
    btCollisionObject* ignore[1] = { collider ? collider->GetRigidBody() : nullptr };
    const uint32_t ignoreCount = (ignore[0] != nullptr) ? 1u : 0u;

    RayTestResult result;
    world->RayTest(start, end, GetGroundCollisionMask(), result, ignoreCount, ignore);

    // A miss leaves mHitPosition/mHitNormal untouched, so mHitNode is the only
    // trustworthy hit test -- mHitFraction reads 0 in some miss paths.
    if (result.mHitNode == nullptr)
    {
        mGrounded = false;
        mGroundNormal = worldUp;
        return;
    }

    const float slopeDot = glm::dot(result.mHitNormal, worldUp);
    const float minDot = cosf(glm::radians(glm::clamp(mMaxGroundAngle, 0.0f, 89.0f)));

    if (slopeDot < minDot)
    {
        // Too steep to stand on -- this is a wall, not ground. Let the sweep
        // handle it instead of trying to drive up it.
        mGrounded = false;
        mGroundNormal = worldUp;
        return;
    }

    mGroundHitY = result.mHitPosition.y;
    mGroundNormal = result.mHitNormal;

    const float heightAboveGround = pos.y - mGroundHitY;
    mGrounded = (heightAboveGround <= mRideHeight + 0.12f) && (mVerticalVelocity <= 0.01f);
}

void CarController3D::UpdateSteering(float deltaTime)
{
    const float maxSteerRad = glm::radians(glm::max(mMaxSteerAngle, 0.0f));

    // Exponent > 1 softens small deflections, which matters a lot on a stick.
    const float shaped = powf(fabsf(mInput.mSteer), glm::max(mSteerExponent, 0.01f)) * Sign(mInput.mSteer);

    // Reduce available lock as speed rises. Note this scales the *angle*, not
    // the rate -- the car stays responsive but stops being twitchy at 200 kph.
    const float speedScale = MapClamped(fabsf(mSpeed), 0.0f, mTopSpeed, 1.0f, mHighSpeedSteerScale);
    const float target = shaped * maxSteerRad * speedScale;

    // Crossing the centre (or fighting the current angle) uses a much faster
    // rate, so a slide is always catchable. Lifted from gevp, which is the one
    // idea in that project most responsible for its cars feeling forgiving.
    const bool crossing = (Sign(target) != 0.0f) && (Sign(mSteerAngle) != 0.0f) && (Sign(target) != Sign(mSteerAngle));
    const float rateUnits = crossing ? mCountersteerRate : mSteerRate;

    mSteerAngle = Approach(mSteerAngle, target, rateUnits * maxSteerRad, deltaTime);
}

void CarController3D::UpdateDrive(float deltaTime)
{
    // Boost meter gates the requested boost, so holding the button does not
    // grant infinite thrust.
    float boost = Clamp01(mInput.mBoost);
    if (mBoostUseMeter)
    {
        if (boost > 0.0f && mBoostMeter > 0.0f)
        {
            mBoostMeter = glm::max(0.0f, mBoostMeter - mBoostDrainRate * boost * deltaTime);
        }
        else
        {
            boost = 0.0f;
            mBoostMeter = glm::min(mBoostCapacity, mBoostMeter + mBoostRefillRate * deltaTime);
        }

        if (mBoostMeter <= 0.0f)
        {
            boost = 0.0f;
        }
    }

    const float topSpeed = mTopSpeed * (1.0f + (mBoostSpeedMult - 1.0f) * boost);

    // Acceleration tapers toward top speed so the last few kph take a while --
    // cheap, readable substitute for a torque curve.
    float accel = mAccelRate
        * MapClamped(fabsf(mSpeed), 0.0f, mTopSpeed, 1.0f, mAccelFalloff)
        * (1.0f + (mBoostAccelMult - 1.0f) * boost);

    // No drive authority in the air. Steering still works (see mAirSteerScale)
    // because losing all control mid-jump feels broken.
    if (!mGrounded)
    {
        accel = 0.0f;
    }

    const float throttle = Clamp01(mInput.mThrottle);
    const float brake = Clamp01(mInput.mBrake);

    if (throttle > 0.0f)
    {
        mSpeed += accel * throttle * deltaTime;
    }

    if (brake > 0.0f)
    {
        if (mSpeed > 0.05f)
        {
            // Braking while moving forward. Do not let a single step push us
            // through zero into reverse.
            mSpeed = glm::max(0.0f, mSpeed - mBrakeRate * brake * deltaTime);
        }
        else if (mGrounded)
        {
            // Stopped or already reversing: brake doubles as reverse.
            mSpeed -= mAccelRate * 0.55f * brake * deltaTime;
        }
    }

    if (throttle <= 0.0f && brake <= 0.0f)
    {
        const float coast = mCoastRate * deltaTime;
        mSpeed -= Sign(mSpeed) * glm::min(fabsf(mSpeed), coast);
    }

    // Quadratic drag -- the term that actually sets the practical top speed.
    mSpeed -= Sign(mSpeed) * mDragCoeff * mSpeed * mSpeed * deltaTime;

    mSpeed = glm::clamp(mSpeed, -fabsf(mReverseTopSpeed), topSpeed);
}

void CarController3D::UpdateDrift(float deltaTime)
{
    const glm::vec3 worldUp = glm::vec3(0.0f, 1.0f, 0.0f);
    const glm::vec3 forward = GetHeadingForward();

    // Slip angle: how far the velocity vector has diverged from where the car
    // is pointing. This is the drift signal.
    const glm::vec3 planarVel = glm::vec3(mVelocity.x, 0.0f, mVelocity.z);
    const float planarSpeed = glm::length(planarVel);

    if (planarSpeed > 0.5f)
    {
        const glm::vec3 velDir = planarVel / planarSpeed;
        const glm::vec3 headingDir = forward * ((mSpeed < 0.0f) ? -1.0f : 1.0f);
        mSlipAngle = SignedAngle(headingDir, velDir, worldUp);
    }
    else
    {
        mSlipAngle = 0.0f;
    }

    const float slipDeg = glm::degrees(fabsf(mSlipAngle));
    const bool fastEnough = fabsf(mSpeed) > mDriftMinSpeed;
    const bool handbrake = mInput.mHandbrake > 0.25f;

    bool wantDrift = false;

    switch (mDriftMode)
    {
    case DriftMode::HandbrakeOnly:
        wantDrift = handbrake && fastEnough;
        break;

    case DriftMode::Assisted:
        wantDrift = (handbrake && fastEnough)
            || (fastEnough && slipDeg > mDriftEnterAngle && mInput.mThrottle > 0.5f);
        break;

    case DriftMode::Always:
        wantDrift = (handbrake && fastEnough)
            || (fastEnough && slipDeg > mDriftEnterAngle);
        break;

    default:
        break;
    }

    if (mDrifting)
    {
        // Stay in the drift until both the player has let go and the car has
        // actually straightened out. Exiting purely on button release would
        // snap grip back mid-slide.
        mDrifting = wantDrift || (slipDeg > mDriftExitAngle && fastEnough);
    }
    else
    {
        mDrifting = wantDrift;
    }

    const float target = mDrifting ? 1.0f : 0.0f;
    const float time = mDrifting ? mDriftEnterTime : mDriftExitTime;
    const float rate = 1.0f / glm::max(time, 0.01f);

    mDriftAmount = Approach(mDriftAmount, target, rate, deltaTime);
}

void CarController3D::UpdateYawAndGrip(float deltaTime)
{
    const glm::vec3 worldUp = glm::vec3(0.0f, 1.0f, 0.0f);

    // --- Heading -----------------------------------------------------------
    // Rotational authority ramps in with speed: a stationary car should not be
    // able to spin on the spot.
    const float speedFactor = MapClamped(fabsf(mSpeed), 0.0f, mYawFullSpeed, 0.0f, 1.0f);
    const float directionSign = (mSpeed < 0.0f) ? -1.0f : 1.0f;

    float yawRate = kSteerToYaw * mSteerAngle * mYawRateScale * speedFactor * directionSign;

    // Extra rotation while sliding, so a drift can actually be steered rather
    // than merely survived.
    yawRate += kSteerToYaw * mDriftAmount * mInput.mSteer * glm::radians(mDriftYawAssist);

    if (!mGrounded)
    {
        yawRate *= mAirSteerScale;
    }

    mYaw += yawRate * deltaTime;

    // Keep yaw bounded so long sessions cannot accumulate float error.
    const float twoPi = 6.28318530718f;
    if (mYaw > twoPi)       { mYaw -= twoPi; }
    else if (mYaw < -twoPi) { mYaw += twoPi; }

    // --- Grip --------------------------------------------------------------
    // This is the whole handling model. The velocity direction chases the
    // heading at `grip` radians per second. Blending that rate down as drift
    // ramps up is what makes the tail step out and come back.
    const glm::vec3 forward = GetHeadingForward();
    const glm::vec3 desiredDir = forward * directionSign;

    glm::vec3 planarVel = glm::vec3(mVelocity.x, 0.0f, mVelocity.z);
    const float planarSpeed = glm::length(planarVel);

    glm::vec3 velDir;
    if (planarSpeed > 0.1f)
    {
        velDir = planarVel / planarSpeed;

        float grip = glm::mix(mBaseGrip, mDriftGrip, Clamp01(mDriftAmount));
        if (!mGrounded)
        {
            grip *= mAirGrip;
        }

        velDir = RotateTowards(velDir, desiredDir, worldUp, grip * deltaTime);
    }
    else
    {
        // Below the threshold there is no meaningful direction to preserve.
        velDir = desiredDir;
    }

    const glm::vec3 newPlanar = velDir * fabsf(mSpeed);

    // Lateral acceleration drives the visual body roll.
    const glm::vec3 right = glm::normalize(glm::cross(forward, worldUp));
    const float prevLat = glm::dot(planarVel, right);
    const float newLat = glm::dot(newPlanar, right);
    mLateralAccel = (newLat - prevLat) / glm::max(deltaTime, 0.0001f);

    mVelocity.x = newPlanar.x;
    mVelocity.z = newPlanar.z;
}

void CarController3D::UpdateVertical(float deltaTime)
{
    if (mGrounded)
    {
        mVerticalVelocity = 0.0f;
    }
    else
    {
        mVerticalVelocity += mGravity * deltaTime;
    }

    mVelocity.y = mVerticalVelocity;
}

void CarController3D::MoveAndCollide(float deltaTime)
{
    World* world = GetWorld();
    if (world == nullptr)
    {
        return;
    }

    glm::vec3 pos = GetWorldPosition();
    glm::vec3 delta = mVelocity * deltaTime;

    // The collider must be oriented before sweeping: World::SweepTest reads the
    // primitive's *local* rotation to orient the swept shape.
    SyncColliderTransform();
    Primitive3D* collider = GetCollider();

    // Sweep-and-slide. World::SweepTest takes explicit start/end and does not
    // move the node, which is what makes iterating possible --
    // Primitive3D::SweepToWorldPosition would relocate the collider on the first
    // hit and always sweep from its real position.
    const int32_t kMaxSlides = 3;
    for (int32_t i = 0; collider != nullptr && i < kMaxSlides; ++i)
    {
        if (glm::dot(delta, delta) < 1e-10f)
        {
            break;
        }

        // World::SweepTest centres the shape on the start point, so the sweep
        // has to run from where the box actually is -- offset above the car's
        // origin -- not from the origin itself. hitFraction still applies to
        // `delta` unchanged, since both ends carry the same offset.
        const glm::vec3 boxPos = pos + GetWorldRotationQuat() * GetColliderLocalOffset();

        SweepTestResult sweep;
        world->SweepTest(collider, boxPos, boxPos + delta, GetGroundCollisionMask(), sweep);

        if (sweep.mHitNode == nullptr)
        {
            pos += delta;
            break;
        }

        const glm::vec3 normal = sweep.mHitNormal;
        const glm::vec3 allowed = delta * glm::clamp(sweep.mHitFraction, 0.0f, 1.0f);

        // Small push-off along the normal; Bullet's convex sweep uses a 0.001
        // epsilon internally, so anything smaller re-collides immediately.
        pos += allowed + normal * 0.0015f;

        const float intoWall = glm::dot(delta - allowed, normal);
        glm::vec3 remaining = (delta - allowed) - normal * intoWall;

        // Scrub speed on a genuine wall hit, not on ground contact. A near-
        // horizontal normal means we hit something we cannot drive up.
        if (fabsf(normal.y) < 0.5f)
        {
            // Scale the penalty by how square-on the impact was, so clipping a
            // wall at a shallow angle barely costs anything while driving
            // straight into one nearly stops the car.
            const float headOn = Clamp01(-glm::dot(SafeNormalize(glm::vec3(delta.x, 0.0f, delta.z)), normal));
            const float keep = 1.0f - Clamp01(mWallSpeedLoss) * headOn;

            // mSpeed is the value that persists -- next step rebuilds
            // mVelocity's magnitude from it. mVelocity is scrubbed too so the
            // camera, which late-ticks after this, reacts on the same frame.
            mSpeed *= keep;
            mVelocity.x *= keep;
            mVelocity.z *= keep;

            // Wall bounce. Pure slide leaves both the velocity direction and
            // the heading pointing INTO the wall (this loop only ever moved
            // position), so a head-on car re-drove into the wall every substep
            // while the speed scrub above compounded -- with yaw authority
            // scaling off mSpeed, a nearly-stopped car couldn't even steer out:
            // a closed deadlock. Reflecting fixes all three legs of that loop.
            const float bounce = Clamp01(mWallBounce) * headOn;
            if (bounce > 0.0f)
            {
                // remaining currently holds the slide (normal component
                // removed); adding the negated normal component back scaled by
                // bounce gives the standard restitution reflection
                // r' = r - (1+b)*n*(r.n). intoWall is negative here.
                remaining -= normal * (intoWall * bounce);

                // Deflect the heading toward its own reflection about the wall
                // normal, proportional to bounce -- without this, grip re-bends
                // velocity back toward the (still wall-pinned) nose next step.
                const glm::vec3 f = GetHeadingForward();
                const glm::vec3 wallN = SafeNormalize(glm::vec3(normal.x, 0.0f, normal.z));
                const float fDotN = glm::dot(f, wallN);
                if (fDotN < 0.0f)
                {
                    const glm::vec3 rf = f - 2.0f * fDotN * wallN;
                    // Yaw from a forward vector, inverting GetHeadingForward's
                    // (-sin, 0, -cos) convention.
                    const float reflectedYaw = atan2f(-rf.x, -rf.z);
                    const float kPi = 3.14159265358979f;
                    float dYaw = reflectedYaw - mYaw;
                    while (dYaw > kPi)  { dYaw -= 2.0f * kPi; }
                    while (dYaw < -kPi) { dYaw += 2.0f * kPi; }
                    mYaw += dYaw * bounce;
                }
            }

            // Point the (already speed-scrubbed) planar velocity along the
            // post-hit direction so the next substep carries the car away from
            // the wall rather than back into it. Applies to plain slides too:
            // before this, the slide direction was applied to position only and
            // mVelocity kept its into-wall direction.
            const glm::vec3 postDir = glm::vec3(remaining.x, 0.0f, remaining.z);
            const float postLenSq = glm::dot(postDir, postDir);
            if (postLenSq > 1e-12f)
            {
                const float planarSpeed = sqrtf(mVelocity.x * mVelocity.x + mVelocity.z * mVelocity.z);
                const glm::vec3 dir = postDir / sqrtf(postLenSq);
                mVelocity.x = dir.x * planarSpeed;
                mVelocity.z = dir.z * planarSpeed;
            }
        }

        delta = remaining;
    }

    if (collider == nullptr)
    {
        // No collider (the node was hand-placed and Start has not created one
        // yet, or creation failed). Still move, just without wall collision --
        // a car that refuses to move is a worse failure than one that clips.
        pos += delta;
    }

    // Ride-height correction. Damping rather than snapping keeps slopes and
    // small bumps from reading as teleports.
    if (mGrounded)
    {
        const float targetY = mGroundHitY + mRideHeight;
        pos.y = Damp(pos.y, targetY, mGroundSnapRate, deltaTime);
    }

    SetWorldPosition(pos);
}

void CarController3D::UpdateOrientation(float deltaTime)
{
    const glm::vec3 worldUp = glm::vec3(0.0f, 1.0f, 0.0f);

    // Lean the whole chassis onto the ground plane, damped so kerbs and crests
    // do not cause an instant snap.
    const glm::vec3 targetUp = mGrounded ? mGroundNormal : worldUp;
    mSmoothedUp = SafeNormalize(Damp(mSmoothedUp, targetUp, mGroundAlignRate, deltaTime));

    if (glm::dot(mSmoothedUp, mSmoothedUp) < 0.5f)
    {
        mSmoothedUp = worldUp;
    }

    const glm::vec3 forward = GetHeadingForward();

    // Re-orthogonalise heading against the (possibly tilted) up vector.
    glm::vec3 right = glm::cross(forward, mSmoothedUp);
    if (glm::dot(right, right) < 1e-6f)
    {
        // Heading is parallel to up -- degenerate, keep the previous basis.
        return;
    }
    right = glm::normalize(right);

    const glm::vec3 orthoForward = glm::normalize(glm::cross(mSmoothedUp, right));

    // Engine convention is right-handed, Y-up, forward = -Z, so the matrix's
    // Z column is the negated forward direction.
    glm::mat3 basis;
    basis[0] = right;
    basis[1] = mSmoothedUp;
    basis[2] = -orthoForward;

    SetWorldRotation(glm::quat_cast(basis));
}

glm::vec3 CarController3D::GetHeadingForward() const
{
    // Yaw of 0 must map to the engine's forward (0, 0, -1); rotating that about
    // +Y by yaw gives (-sin, 0, -cos).
    return glm::vec3(-sinf(mYaw), 0.0f, -cosf(mYaw));
}

void CarController3D::UpdateVisuals(float deltaTime)
{
    // Body lean is applied to the Body child, never to this node -- the root
    // has to stay aligned with the collision box that the sweeps use.
    Node3D* body = mBodyNode.Get() ? mBodyNode.Get()->As<Node3D>() : nullptr;
    if (body != nullptr)
    {
        const float rollTarget = -Clamp01(fabsf(mLateralAccel) / 25.0f) * mBodyRollAngle * Sign(mLateralAccel);
        const float pitchTarget = Clamp01(fabsf(mLongitudinalAccel) / 20.0f) * mBodyPitchAngle * Sign(mLongitudinalAccel);

        mBodyRollCurrent = Damp(mBodyRollCurrent, rollTarget, mBodyVisualRate, deltaTime);
        mBodyPitchCurrent = Damp(mBodyPitchCurrent, pitchTarget, mBodyVisualRate, deltaTime);

        // Lean is applied on top of the authored pose, so a body rotated in the
        // inspector keeps that rotation, and Body Yaw Offset stacks on it.
        body->SetRotation(glm::vec3(
            mBodyBaseEuler.x + mBodyPitchCurrent,
            mBodyBaseEuler.y + mBodyYawOffset,
            mBodyBaseEuler.z + mBodyRollCurrent));
    }

    // Wheels: spin from ground speed, steer the fronts.
    const float radius = glm::max(mWheelRadius, 0.01f);
    mWheelSpinAngle += glm::degrees(mSpeed / radius) * deltaTime;
    if (mWheelSpinAngle > 360.0f)       { mWheelSpinAngle -= 360.0f; }
    else if (mWheelSpinAngle < -360.0f) { mWheelSpinAngle += 360.0f; }

    // Wheel yaw is the same +Y euler as the heading, so it needs the same flip.
    const float steerVisual = glm::clamp(
        kSteerToYaw * glm::degrees(mSteerAngle),
        -fabsf(mWheelSteerVisualMax),
        fabsf(mWheelSteerVisualMax));

    // FL, FR, RL, RR -- order must match mWheelBaseEuler.
    NodePtrWeak* wheels[4] = { &mWheelFL, &mWheelFR, &mWheelRL, &mWheelRR };

    for (int32_t i = 0; i < 4; ++i)
    {
        Node3D* wheel = wheels[i]->Get() ? wheels[i]->Get()->As<Node3D>() : nullptr;
        if (wheel == nullptr)
        {
            continue;
        }

        const glm::vec3 base = mWheelBaseEuler[i];

        // A wheel mirrored onto the left-hand side is yawed ~180 degrees, which
        // flips its local X axis. Spinning it by the same angle as the
        // right-hand wheels would make the two sides visibly counter-rotate, so
        // negate when the wheel faces backwards. cos(baseYaw) is just a cheap
        // "is this flipped" test that degrades sensibly for odd angles.
        const float spinSign = (cosf(glm::radians(base.y)) < 0.0f) ? -1.0f : 1.0f;

        // Steering is a yaw about the same axis regardless of mirroring, so it
        // simply adds to whatever the authored yaw was.
        const bool isFront = (i < 2);
        const float steer = isFront ? steerVisual : 0.0f;

        wheel->SetRotation(glm::vec3(
            base.x + mWheelSpinAngle * spinSign,
            base.y + steer,
            base.z));
    }
}

void CarController3D::UpdateAudio(float deltaTime)
{
    Audio3D* audio = mEngineAudioNode.Get() ? mEngineAudioNode.Get()->As<Audio3D>() : nullptr;
    if (audio == nullptr)
    {
        return;
    }

    // Fake a gearbox purely for the audio: split the speed range into N bands
    // and let RPM sweep within each. There is no drivetrain here -- this exists
    // so the engine note rises and drops the way a listener expects.
    const int32_t gearCount = glm::max(mGearCount, 1);
    const float speedFrac = Clamp01(fabsf(mSpeed) / glm::max(mTopSpeed, 0.01f));
    const float scaled = speedFrac * gearCount;

    mGear = glm::clamp(int32_t(scaled) + 1, 1, gearCount);

    float within = scaled - float(mGear - 1);

    // Wheelspin during a drift should rev the engine even though road speed is
    // not climbing.
    within = Clamp01(within + mDriftAmount * mInput.mThrottle * 0.35f);

    mRpm = glm::mix(0.15f, 1.0f, within);

    audio->SetPitch(glm::mix(mEnginePitchMin, mEnginePitchMax, mRpm));

    // Volume must never reach exactly zero: AudioManager drops a voice whose
    // volume is 0 and it then has to re-acquire, which clicks.
    const float volumeTarget = glm::mix(mEngineVolumeMin, mEngineVolumeMax, Clamp01(mInput.mThrottle * 0.7f + mRpm * 0.3f));
    audio->SetVolume(glm::max(volumeTarget, 0.01f));

    (void)deltaTime;
}

void CarController3D::UpdateSmoke(float deltaTime)
{
    // Particle3D carries no POLYPHASE_API, so we cannot call EnableEmission()
    // directly from a DLL. Going through the reflected "Emit" property reaches
    // the same code: Particle3D::HandlePropChange forwards it to
    // EnableEmission().
    const bool emit = mGrounded && (mDriftAmount > 0.25f || (mInput.mHandbrake > 0.5f && fabsf(mSpeed) > 2.0f));

    NodePtrWeak* emitters[2] = { &mSmokeL, &mSmokeR };
    for (int32_t i = 0; i < 2; ++i)
    {
        Node* smoke = emitters[i]->Get();
        if (smoke == nullptr)
        {
            continue;
        }

        std::vector<Property> props;
        smoke->GatherProperties(props);

        for (Property& prop : props)
        {
            if (prop.mName == "Emit")
            {
                if (prop.GetBool() != emit)
                {
                    prop.SetBool(emit);
                }
                break;
            }
        }
    }

    (void)deltaTime;
}

void CarController3D::DrawDebugLines()
{
    World* world = GetWorld();
    if (world == nullptr)
    {
        return;
    }

    const glm::vec3 pos = GetWorldPosition();
    const glm::vec3 forward = GetHeadingForward();

    // Lifetime 0 means "erased at the end of this frame", which is exactly what
    // a per-frame debug overlay wants. A negative lifetime would persist
    // forever and we would have to remove them by hand.
    const float life = 0.0f;

    // Heading (green) vs actual velocity (red). The angle between them is the
    // slip angle, so this makes the drift model visible.
    world->AddLine(Line(pos, pos + forward * 3.0f, glm::vec4(0.2f, 1.0f, 0.2f, 1.0f), life));

    const glm::vec3 planarVel = glm::vec3(mVelocity.x, 0.0f, mVelocity.z);
    if (glm::length(planarVel) > 0.1f)
    {
        world->AddLine(Line(pos, pos + SafeNormalize(planarVel) * 3.0f, glm::vec4(1.0f, 0.25f, 0.15f, 1.0f), life));
    }

    // Ground probe (blue when grounded, grey when airborne).
    const glm::vec4 groundColor = mGrounded
        ? glm::vec4(0.3f, 0.6f, 1.0f, 1.0f)
        : glm::vec4(0.5f, 0.5f, 0.5f, 1.0f);
    world->AddLine(Line(pos, pos - glm::vec3(0.0f, mRideHeight + mGroundProbeDistance, 0.0f), groundColor, life));

    if (mGrounded)
    {
        const glm::vec3 contact = glm::vec3(pos.x, mGroundHitY, pos.z);
        world->AddLine(Line(contact, contact + mGroundNormal * 1.0f, glm::vec4(1.0f, 1.0f, 0.3f, 1.0f), life));
    }
}

#if EDITOR
void CarController3D::OnDrawGizmosSelected()
{
    Node3D::OnDrawGizmosSelected();

    // Gizmos:: statics are not exported from Polyphase.lib, so an addon has to
    // draw through World::AddLine (which also works in packaged builds).
    World* world = GetWorld();
    if (world == nullptr)
    {
        return;
    }

    const glm::vec3 pos = GetWorldPosition();
    const glm::vec3 e = mCollisionExtents;
    const glm::quat rot = GetWorldRotationQuat();
    const glm::vec4 color = glm::vec4(0.9f, 0.7f, 0.2f, 1.0f);

    const glm::vec3 corners[8] =
    {
        pos + rot * glm::vec3(-e.x, -e.y, -e.z), pos + rot * glm::vec3( e.x, -e.y, -e.z),
        pos + rot * glm::vec3( e.x, -e.y,  e.z), pos + rot * glm::vec3(-e.x, -e.y,  e.z),
        pos + rot * glm::vec3(-e.x,  e.y, -e.z), pos + rot * glm::vec3( e.x,  e.y, -e.z),
        pos + rot * glm::vec3( e.x,  e.y,  e.z), pos + rot * glm::vec3(-e.x,  e.y,  e.z),
    };

    static const int32_t edges[12][2] =
    {
        {0,1},{1,2},{2,3},{3,0},
        {4,5},{5,6},{6,7},{7,4},
        {0,4},{1,5},{2,6},{3,7},
    };

    for (int32_t i = 0; i < 12; ++i)
    {
        world->AddLine(Line(corners[edges[i][0]], corners[edges[i][1]], color, 0.0f));
    }
}
#endif

void CarController3D::SetExternalInput(const CarInput& input)
{
    mExternalInput = input;
    mUseExternalInput = true;
}

void CarController3D::ResetTo(glm::vec3 position, float yawDegrees)
{
    SetWorldPosition(position);

    mYaw = glm::radians(yawDegrees);
    mVelocity = glm::vec3(0.0f);
    mSpeed = 0.0f;
    mVerticalVelocity = 0.0f;
    mSteerAngle = 0.0f;
    mDrifting = false;
    mDriftAmount = 0.0f;
    mSlipAngle = 0.0f;
    mSmoothedUp = glm::vec3(0.0f, 1.0f, 0.0f);
    mStepAccumulator = 0.0f;
    mBoostMeter = mBoostCapacity;

    SetWorldRotation(glm::vec3(0.0f, yawDegrees, 0.0f));
}

void CarController3D::ResetInPlace()
{
    if (mSpawnCaptured)
    {
        ResetTo(mSpawnPosition, glm::degrees(mSpawnYaw));
    }
    else
    {
        // Nothing was captured (node never started) -- at least right the car
        // where it stands.
        ResetTo(GetWorldPosition(), glm::degrees(mYaw));
    }
}

Node* CarController3D::ResolveChildRef(NodePtrWeak& ref, const char* conventionalName)
{
    Node* current = ref.Get();

    // Already pointing at something inside our own world: nothing to fix.
    if (current != nullptr && current->GetWorld() == GetWorld())
    {
        return current;
    }

    // Stale reference (typically the edit-world node after PIE cloned the
    // scene). Re-find by name beneath this car. FindRelativeNodePath and
    // ResolveNodePath -- the engine's own fix for this -- are not exported, but
    // clones preserve names, so a name lookup gets us to the same node.
    if (current != nullptr)
    {
        Node* rebound = FindChild(current->GetName(), true);
        if (rebound != nullptr)
        {
            ref = ResolveWeakPtr<Node>(rebound);
            return rebound;
        }
    }

    if (conventionalName != nullptr)
    {
        Node* byConvention = FindChild(conventionalName, true);
        if (byConvention != nullptr)
        {
            ref = ResolveWeakPtr<Node>(byConvention);
            return byConvention;
        }
    }

    return nullptr;
}

void CarController3D::CaptureWheelBaseRotations()
{
    Node3D* body = mBodyNode.Get() ? mBodyNode.Get()->As<Node3D>() : nullptr;
    mBodyBaseEuler = (body != nullptr) ? body->GetRotationEuler() : glm::vec3(0.0f);
    mBodyRollCurrent = 0.0f;
    mBodyPitchCurrent = 0.0f;

    NodePtrWeak* wheels[4] = { &mWheelFL, &mWheelFR, &mWheelRL, &mWheelRR };

    for (int32_t i = 0; i < 4; ++i)
    {
        Node3D* wheel = wheels[i]->Get() ? wheels[i]->Get()->As<Node3D>() : nullptr;
        mWheelBaseEuler[i] = (wheel != nullptr) ? wheel->GetRotationEuler() : glm::vec3(0.0f);
    }

    mWheelBaseCaptured = true;
}

void CarController3D::ResolveNodeRefs()
{
    ResolveChildRef(mColliderNode, kNameCollider);

    // The collider is created on demand, and audio/smoke are genuinely
    // optional, so only the visual refs are worth reporting.
    struct RefEntry { NodePtrWeak* mRef; const char* mName; const char* mProp; };
    const RefEntry entries[] =
    {
        { &mBodyNode, kNameBody,    "Body"     },
        { &mWheelFL,  kNameWheelFL, "Wheel FL" },
        { &mWheelFR,  kNameWheelFR, "Wheel FR" },
        { &mWheelRL,  kNameWheelRL, "Wheel RL" },
        { &mWheelRR,  kNameWheelRR, "Wheel RR" },
    };

    std::string unresolved;
    for (const RefEntry& entry : entries)
    {
        if (ResolveChildRef(*entry.mRef, entry.mName) == nullptr)
        {
            if (!unresolved.empty()) { unresolved += ", "; }
            unresolved += entry.mProp;
        }
    }

    ResolveChildRef(mEngineAudioNode, kNameAudio);
    ResolveChildRef(mSmokeL, kNameSmokeL);
    ResolveChildRef(mSmokeR, kNameSmokeR);

    if (!unresolved.empty())
    {
        // Most often this means the reference was assigned to a node inside a
        // child scene instance and did not survive serialization. Name-matching
        // is the fallback, so say what names would have worked.
        LogWarning(
            "CarController3D '%s': unresolved node references [%s]. Nothing will animate for those. "
            "If they were assigned to nodes inside a child scene, the reference may not have "
            "persisted -- either name the nodes '%s' / '%s' etc. so they resolve by name, or move "
            "the CarController3D inside that scene so the references stay within one scene.",
            GetName().c_str(), unresolved.c_str(), kNameBody, kNameWheelFL);
    }
}

Primitive3D* CarController3D::GetCollider() const
{
    Node* node = mColliderNode.Get();
    return (node != nullptr) ? node->As<Primitive3D>() : nullptr;
}

uint8_t CarController3D::GetGroundCollisionMask() const
{
    return mGroundCollisionMask;
}

void CarController3D::EnsureCollider()
{
    Primitive3D* collider = GetCollider();

    if (collider == nullptr)
    {
        // Box3D carries no POLYPHASE_API, so it cannot be constructed or even
        // named as a type from an addon -- CreateChild(const char*) goes through
        // the engine's factory list instead. The string is the DEFINE_FACTORY
        // class name ("Box3D"), not GetTypeName() (which returns "Box").
        Node* created = CreateChild("Box3D");
        if (created == nullptr)
        {
            LogError("CarController3D: failed to create the Box3D collider child.");
            return;
        }

        created->SetName(kNameCollider);
        mColliderNode = ResolveWeakPtr<Node>(created);
        collider = created->As<Primitive3D>();
    }

    if (collider == nullptr)
    {
        return;
    }

    // Local transform == world transform, which is what makes GetRotationQuat()
    // (what World::SweepTest orients the swept box by) the car's real heading.
    collider->SetInheritTransform(false);

    collider->EnablePhysics(false);
    collider->EnableCollision(true);
    collider->EnableOverlaps(false);

    // Deliberately NOT touching the collider's collision group or mask. Forcing
    // a group here would silently rewrite the settings on a Box3D the user
    // assigned themselves, and it buys nothing: both the ground probe and the
    // wall sweep already exclude this exact body via an explicit ignore-object,
    // so the car can never hit itself regardless of grouping.

    // Box3D's "Extents" property is the FULL box size, while mCollisionExtents
    // is half-extents -- hence the doubling. Set through reflection because
    // Box3D::SetExtents is not linkable from here; the property's change
    // handler calls it for us.
    std::vector<Property> props;
    collider->GatherProperties(props);
    for (Property& prop : props)
    {
        if (prop.mName == "Extents")
        {
            prop.SetVector(glm::max(mCollisionExtents, glm::vec3(0.05f)) * 2.0f);
            break;
        }
    }

    mShapeDirty = false;

    SyncColliderTransform();
}

glm::vec3 CarController3D::GetColliderLocalOffset() const
{
    // The box underside should sit `clearance` above the ride plane:
    //   bottom = offsetY - halfHeight = -rideHeight + clearance
    const float offsetY = mCollisionExtents.y - mRideHeight + glm::max(mColliderGroundClearance, 0.0f);
    return glm::vec3(0.0f, offsetY, 0.0f);
}

void CarController3D::SyncColliderTransform()
{
    if (mShapeDirty)
    {
        EnsureCollider();
        return;
    }

    Primitive3D* collider = GetCollider();
    if (collider == nullptr)
    {
        return;
    }

    // Inherit-transform is off, so local is world here.
    const glm::quat rot = GetWorldRotationQuat();
    collider->SetPosition(GetWorldPosition() + rot * GetColliderLocalOffset());
    collider->SetRotation(rot);
}
