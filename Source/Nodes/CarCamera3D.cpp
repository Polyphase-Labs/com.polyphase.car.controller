#include "Nodes/CarCamera3D.h"

#include "CarMath.h"
#include "Nodes/CarController3D.h"

#include "Engine.h"
#include "Log.h"
#include "World.h"

using namespace CarAddon;

FORCE_LINK_DEF(CarCamera3D);
DEFINE_NODE(CarCamera3D, Camera3D);

CarCamera3D::CarCamera3D()
{
    mName = "CarCamera";
    mFovY = mBaseFov;
}

CarCamera3D::~CarCamera3D()
{
}

const char* CarCamera3D::GetTypeName() const
{
    return "CarCamera3D";
}

void CarCamera3D::GatherProperties(std::vector<Property>& outProps)
{
    Camera3D::GatherProperties(outProps);

    {
        SCOPED_CATEGORY("Car Camera|Target");
        outProps.push_back(Property(DatumType::Node, "Target Car", this, &mTarget));
        outProps.push_back(Property(DatumType::Bool, "Make Main Camera", this, &mMakeMainCamera));
    }

    {
        SCOPED_CATEGORY("Car Camera|Framing");
        outProps.push_back(Property(DatumType::Float, "Follow Distance", this, &mFollowDistance));
        outProps.push_back(Property(DatumType::Float, "Follow Height", this, &mFollowHeight));
        outProps.push_back(Property(DatumType::Float, "Look At Height", this, &mLookAtHeight));
    }

    {
        SCOPED_CATEGORY("Car Camera|Smoothing");
        outProps.push_back(Property(DatumType::Float, "Position Rate", this, &mPositionRate));
        outProps.push_back(Property(DatumType::Float, "Aim Rate", this, &mAimRate));
        outProps.push_back(Property(DatumType::Float, "Up Rate", this, &mUpRate));
    }

    {
        SCOPED_CATEGORY("Car Camera|Feel");
        outProps.push_back(Property(DatumType::Float, "Base FOV", this, &mBaseFov));
        outProps.push_back(Property(DatumType::Float, "Max FOV", this, &mMaxFov));
        outProps.push_back(Property(DatumType::Float, "FOV Rate", this, &mFovRate));
        outProps.push_back(Property(DatumType::Float, "Look Ahead Time", this, &mLookAheadTime));
        outProps.push_back(Property(DatumType::Float, "Drift Aim Blend", this, &mDriftAimBlend));
    }
}

void CarCamera3D::Create()
{
    Camera3D::Create();

    // Late tick so this runs after the car has already moved this frame.
    // Without it the camera trails by a full frame, which reads as rubber-banding
    // at speed.
    EnableLateTick(true);

    // The camera positions itself in world space; inheriting the car's
    // transform would fight every smoothing term here.
    SetInheritTransform(false);
}

void CarCamera3D::Start()
{
    Camera3D::Start();

    EnableLateTick(true);
    SetInheritTransform(false);

    mSmoothedFov = mBaseFov;

    if (mMakeMainCamera)
    {
        // World::GetMainCamera never assigns its highestPriority local, so
        // priority alone does not select a camera -- the explicit flag does.
        SetIsMainCamera(true);

        World* world = GetWorld();
        if (world != nullptr)
        {
            world->SetActiveCamera(this);
        }
    }

    SnapToTarget();
}

CarController3D* CarCamera3D::ResolveTarget()
{
    Node* node = mTarget.Get();

    if (node != nullptr && node->GetWorld() == GetWorld())
    {
        return node->As<CarController3D>();
    }

    // Stale after a PIE world clone -- re-find by name from the scene root.
    // (FindRelativeNodePath / ResolveNodePath are not exported to addons.)
    if (node != nullptr)
    {
        World* world = GetWorld();
        Node* root = world ? world->GetRootNode() : nullptr;
        if (root != nullptr)
        {
            Node* rebound = (root->GetName() == node->GetName())
                ? root
                : root->FindChild(node->GetName(), true);

            if (rebound != nullptr)
            {
                mTarget = ResolveWeakPtr<Node>(rebound);
                return rebound->As<CarController3D>();
            }
        }
    }

    // No explicit target: adopt the parent if it is a car, else the first one
    // in the scene. Makes a hand-placed camera work without wiring.
    Node* parent = GetParent();
    if (parent != nullptr)
    {
        CarController3D* parentCar = parent->As<CarController3D>();
        if (parentCar != nullptr)
        {
            mTarget = ResolveWeakPtr<Node>(parent);
            return parentCar;
        }
    }

    // Full-scene scan as a last resort. Rate-limited: with no car in the scene
    // this would otherwise walk the entire graph every frame forever.
    if (mSearchCooldown > 0.0f)
    {
        return nullptr;
    }

    World* world = GetWorld();
    Node* root = world ? world->GetRootNode() : nullptr;
    if (root != nullptr)
    {
        CarController3D* found = nullptr;
        root->Traverse([&](Node* n) -> bool
        {
            if (found == nullptr)
            {
                CarController3D* car = n->As<CarController3D>();
                if (car != nullptr)
                {
                    found = car;
                }
            }
            return true;
        });

        if (found != nullptr)
        {
            mTarget = ResolveWeakPtr<Node>(found);
            return found;
        }
    }

    // Nothing found -- back off before scanning again.
    mSearchCooldown = 1.0f;
    return nullptr;
}

void CarCamera3D::SnapToTarget()
{
    CarController3D* car = ResolveTarget();
    if (car == nullptr)
    {
        return;
    }

    const glm::vec3 carPos = car->GetWorldPosition();
    const glm::vec3 forward = car->GetHeadingForward();

    SetWorldPosition(carPos - forward * mFollowDistance + glm::vec3(0.0f, mFollowHeight, 0.0f));

    mSmoothedAim = carPos + glm::vec3(0.0f, mLookAtHeight, 0.0f);
    mSmoothedUp = glm::vec3(0.0f, 1.0f, 0.0f);
    mSmoothedFov = mBaseFov;
    mInitialized = true;

    LookAt(mSmoothedAim, mSmoothedUp);
    SetFieldOfView(mSmoothedFov);
}

void CarCamera3D::Tick(float deltaTime)
{
    Camera3D::Tick(deltaTime);

    if (deltaTime <= 0.0f)
    {
        return;
    }

    if (mSearchCooldown > 0.0f)
    {
        mSearchCooldown -= deltaTime;
    }

    CarController3D* car = ResolveTarget();
    if (car == nullptr)
    {
        return;
    }

    if (!mInitialized)
    {
        SnapToTarget();
        return;
    }

    const glm::vec3 carPos = car->GetWorldPosition();
    const glm::vec3 nose = car->GetHeadingForward();
    const glm::vec3 velocity = car->GetVelocity();
    const glm::vec3 planarVel = glm::vec3(velocity.x, 0.0f, velocity.z);
    const float planarSpeed = glm::length(planarVel);

    // --- Which way is "behind"? ---------------------------------------------
    // Behind the nose keeps the car square in frame; behind the direction of
    // travel shows its flank. Blending between them by drift amount is what
    // makes a slide look good instead of just looking sideways.
    glm::vec3 chaseDir = nose;
    if (planarSpeed > 1.0f)
    {
        const glm::vec3 travelDir = planarVel / planarSpeed;

        // Reversing: keep looking at the car's front rather than whipping around.
        const glm::vec3 signedTravel = (car->GetSpeed() < 0.0f) ? -travelDir : travelDir;

        const float blend = Clamp01(car->GetDriftAmount()) * Clamp01(mDriftAimBlend);
        chaseDir = SafeNormalize(glm::mix(nose, signedTravel, blend));
    }

    if (glm::dot(chaseDir, chaseDir) < 0.5f)
    {
        chaseDir = nose;
    }

    // --- Position ------------------------------------------------------------
    // Leash is measured purely in the horizontal plane and the height is then
    // applied on top, so vertical motion never enters the follow calculation.
    const glm::vec3 desiredPos =
        carPos - chaseDir * mFollowDistance + glm::vec3(0.0f, mFollowHeight, 0.0f);

    glm::vec3 pos = GetWorldPosition();

    glm::vec3 planarDelta = glm::vec3(desiredPos.x - pos.x, 0.0f, desiredPos.z - pos.z);
    const glm::vec3 newPlanar = glm::vec3(pos.x, 0.0f, pos.z) +
        planarDelta * (1.0f - expf(-mPositionRate * deltaTime));

    pos.x = newPlanar.x;
    pos.z = newPlanar.z;
    pos.y = Damp(pos.y, desiredPos.y, mPositionRate, deltaTime);

    SetWorldPosition(pos);

    // --- Aim -----------------------------------------------------------------
    // Look ahead of the car along its velocity. Smoothed separately from
    // position so the car slides across frame rather than sitting dead centre.
    const glm::vec3 lookAhead = planarVel * mLookAheadTime;
    const glm::vec3 desiredAim = carPos + glm::vec3(0.0f, mLookAtHeight, 0.0f) + lookAhead;

    mSmoothedAim = Damp(mSmoothedAim, desiredAim, mAimRate, deltaTime);

    // Roll with the ground a little, but stay mostly world-up: fully matching
    // the car's up vector on a banked surface is nauseating.
    const glm::vec3 carUp = car->GetUpVector();
    const glm::vec3 desiredUp = SafeNormalize(glm::mix(glm::vec3(0.0f, 1.0f, 0.0f), carUp, 0.35f));
    mSmoothedUp = SafeNormalize(Damp(mSmoothedUp, desiredUp, mUpRate, deltaTime));

    if (glm::dot(mSmoothedUp, mSmoothedUp) < 0.5f)
    {
        mSmoothedUp = glm::vec3(0.0f, 1.0f, 0.0f);
    }

    // LookAt degenerates when the aim point coincides with the camera.
    if (glm::distance2(mSmoothedAim, pos) > 0.01f)
    {
        LookAt(mSmoothedAim, mSmoothedUp);
    }

    // --- FOV ------------------------------------------------------------------
    // Widening with speed is the cheapest possible sense-of-speed trick and the
    // single biggest contributor to the arcade feel.
    const float speedFrac = Clamp01(planarSpeed / glm::max(fabsf(car->GetSpeed()) + 20.0f, 1.0f));
    const float boostKick = Clamp01(car->GetDriftAmount()) * 0.25f;
    const float fovTarget = glm::mix(mBaseFov, mMaxFov, Clamp01(speedFrac + boostKick));

    mSmoothedFov = Damp(mSmoothedFov, fovTarget, mFovRate, deltaTime);
    SetFieldOfView(mSmoothedFov);
}

void CarCamera3D::SetTargetCar(CarController3D* car)
{
    mTarget = (car != nullptr) ? ResolveWeakPtr<Node>(car) : NodePtrWeak();
    mInitialized = false;
}

CarController3D* CarCamera3D::GetTargetCar() const
{
    Node* node = mTarget.Get();
    return (node != nullptr) ? node->As<CarController3D>() : nullptr;
}
