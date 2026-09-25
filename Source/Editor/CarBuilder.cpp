#include "Editor/CarBuilder.h"

#if EDITOR

#include "Editor/CarPresets.h"

#include "CarMath.h"
#include "Nodes/CarCamera3D.h"
#include "Nodes/CarController3D.h"

#include "AssetManager.h"
#include "Engine.h"
#include "Log.h"
#include "World.h"

#include "Assets/Scene.h"
#include "Assets/StaticMesh.h"

#include "Nodes/3D/Audio3d.h"
#include "Nodes/3D/StaticMesh3d.h"

#include <vector>

namespace CarAddon
{
    // ------------------------------------------------------------------------
    // Generic property writers.
    //
    // Going through reflection rather than direct member access does two useful
    // things: it works for node types the addon cannot link against (Particle3D
    // has no POLYPHASE_API), and it routes through each type's HandlePropChange,
    // so clamping and side effects fire exactly as they would from the
    // inspector.
    // ------------------------------------------------------------------------

    static Property* FindProp(std::vector<Property>& props, const char* name)
    {
        for (Property& prop : props)
        {
            if (prop.mName == name)
            {
                return &prop;
            }
        }
        return nullptr;
    }

    static void SetFloatProp(Node* node, const char* name, float value)
    {
        std::vector<Property> props;
        node->GatherProperties(props);
        if (Property* prop = FindProp(props, name)) { prop->SetFloat(value); }
    }

    static void SetBoolProp(Node* node, const char* name, bool value)
    {
        std::vector<Property> props;
        node->GatherProperties(props);
        if (Property* prop = FindProp(props, name)) { prop->SetBool(value); }
    }

    static void SetByteProp(Node* node, const char* name, uint8_t value)
    {
        std::vector<Property> props;
        node->GatherProperties(props);
        if (Property* prop = FindProp(props, name)) { prop->SetByte(value); }
    }

    static void SetVectorProp(Node* node, const char* name, glm::vec3 value)
    {
        std::vector<Property> props;
        node->GatherProperties(props);
        if (Property* prop = FindProp(props, name)) { prop->SetVector(value); }
    }

    static void SetNodeProp(Node* node, const char* name, Node* value)
    {
        std::vector<Property> props;
        node->GatherProperties(props);
        if (Property* prop = FindProp(props, name))
        {
            prop->SetNode(ResolveWeakPtr<Node>(value));
        }
    }

    // ------------------------------------------------------------------------
    // Mesh analysis
    // ------------------------------------------------------------------------

    MeshAnalysis CarBuilder::AnalyzeMesh(StaticMesh* mesh)
    {
        MeshAnalysis result;

        if (mesh == nullptr)
        {
            return result;
        }

        // StaticMesh::GetBounds() is a bounding *sphere*, which cannot tell a
        // long car from a tall one. GetAABB() is the real box, computed by the
        // engine in ComputeBounds() during asset load -- same vertex walk this
        // used to do by hand, in the same model space, but without depending on
        // the CPU-side vertex arrays still being resident when the wizard runs.
        const AABB aabb = mesh->GetAABB();
        const glm::vec3 size = aabb.GetSize();

        // A mesh with no vertices yields a zero-size box rather than an invalid
        // one, so check both. A legitimately flat mesh (one zero axis) is still
        // usable and must not be rejected here.
        if (!aabb.IsValid() || (size.x <= 0.0f && size.y <= 0.0f && size.z <= 0.0f))
        {
            return result;
        }

        result.mValid = true;
        result.mMin = aabb.mMin;
        result.mMax = aabb.mMax;
        result.mCenter = aabb.GetCenter();
        result.mSize = glm::max(size, glm::vec3(0.01f));

        // The engine is Y-up with forward along -Z, so for a conventionally
        // authored car mesh: X = width, Y = height, Z = length.
        const float width  = result.mSize.x;
        const float height = result.mSize.y;
        const float length = result.mSize.z;

        // Collision box is the mesh AABB inset slightly. A box that exactly
        // matches the visual silhouette catches on scenery that looks like it
        // should clear. Less inset along Z so bumpers still register.
        result.mCollisionHalfExtents = aabb.GetExtents() * glm::vec3(0.92f, 0.92f, 0.96f);

        // Axles sit inboard of the bumpers; wheels sit inboard of the flanks.
        // These fractions are eyeballed from real car proportions and are the
        // first thing an artist is likely to nudge.
        result.mWheelbase = length * 0.62f;
        result.mTrackWidth = width * 0.82f;

        // Wheel radius from the smaller of height and a fraction of length, so
        // a tall van does not get monster-truck wheels.
        result.mWheelRadius = glm::clamp(glm::min(height * 0.34f, length * 0.16f), 0.08f, 1.5f);

        // How far the pivot sits above the mesh's lowest point. If the mesh is
        // authored sitting on the origin plane this is ~0, and we fall back to
        // resting the wheels on the ground instead.
        const float pivotToBottom = -aabb.mMin.y;
        result.mRideHeight = (pivotToBottom > 0.02f) ? pivotToBottom : result.mWheelRadius;

        // Re-centre a mesh whose pivot is not at its horizontal centre, so the
        // car rotates about its middle rather than about a corner.
        result.mBodyOffset = glm::vec3(-result.mCenter.x, 0.0f, -result.mCenter.z);

        return result;
    }

    // ------------------------------------------------------------------------
    // Hierarchy construction
    // ------------------------------------------------------------------------

    static StaticMesh3D* CreateWheel(
        Node* parent,
        const char* name,
        StaticMesh* wheelMesh,
        glm::vec3 position,
        float wheelRadius,
        bool mirror)
    {
        StaticMesh3D* wheel = parent->CreateChild<StaticMesh3D>(name);
        if (wheel == nullptr)
        {
            return nullptr;
        }

        wheel->SetPosition(position);

        // Wheel meshes are almost always asymmetric -- the rim face points
        // outward -- so the left-hand pair needs flipping or it shows its inner
        // face to the world. CarController3D captures this authored rotation at
        // Start and spins relative to it, negating the spin for flipped wheels
        // so both sides still turn the same way.
        if (mirror)
        {
            wheel->SetRotation(glm::vec3(0.0f, 180.0f, 0.0f));
        }

        if (wheelMesh != nullptr)
        {
            wheel->SetStaticMesh(wheelMesh);

            // Scale the supplied wheel mesh to the derived radius so a wheel
            // authored at any size still fits this car.
            MeshAnalysis wheelInfo = CarBuilder::AnalyzeMesh(wheelMesh);
            if (wheelInfo.mValid)
            {
                const float sourceRadius = glm::max(wheelInfo.mSize.y * 0.5f, 0.001f);
                const float scale = wheelRadius / sourceRadius;
                wheel->SetScale(glm::vec3(scale));
            }
        }

        // Wheels are visual only -- the chassis box does all the colliding.
        wheel->EnableCollision(false);
        wheel->EnablePhysics(false);
        wheel->EnableOverlaps(false);

        return wheel;
    }

    Node* CarBuilder::BuildCar(const CarBuildRequest& request, std::string& outError)
    {
        outError.clear();

        World* world = GetWorld(0);
        if (world == nullptr)
        {
            outError = "No world is open. Open or create a scene first.";
            return nullptr;
        }

        if (request.mBodyMesh == nullptr)
        {
            outError = "Pick a body mesh first.";
            return nullptr;
        }

        if (!request.mAnalysis.mValid)
        {
            outError = "The selected mesh has no usable geometry (zero vertices).";
            return nullptr;
        }

        // EditorState::EnsureActiveScene() is not exported, so we cannot create
        // a scene on the user's behalf. Refusing clearly beats spawning into a
        // world with no root and having the node vanish.
        Node* root = world->GetRootNode();
        if (root == nullptr)
        {
            outError = "The open scene has no root node. Add any node to the scene first, then retry.";
            return nullptr;
        }

        const MeshAnalysis& a = request.mAnalysis;
        const CarPreset& preset = GetPreset(request.mPresetIndex);

        // --- Car root --------------------------------------------------------
        CarController3D* car = root->CreateChild<CarController3D>(request.mCarName.c_str());
        if (car == nullptr)
        {
            outError = "Failed to create the CarController3D node.";
            return nullptr;
        }

        car->SetWorldPosition(request.mSpawnPosition);

        // --- Handling from the preset ---------------------------------------
        SetFloatProp(car, "Top Speed", preset.mTopSpeed);
        SetFloatProp(car, "Accel Rate", preset.mAccelRate);
        SetFloatProp(car, "Brake Rate", preset.mBrakeRate);
        SetFloatProp(car, "Drag Coeff", preset.mDragCoeff);
        SetByteProp(car, "Drive Layout", uint8_t(preset.mDriveLayout));

        SetFloatProp(car, "Max Steer Angle", preset.mMaxSteerAngle);
        SetFloatProp(car, "Steer Rate", preset.mSteerRate);
        SetFloatProp(car, "Countersteer Rate", preset.mCountersteerRate);
        SetFloatProp(car, "High Speed Steer Scale", preset.mHighSpeedSteerScale);
        SetFloatProp(car, "Yaw Rate Scale", preset.mYawRateScale);

        SetByteProp(car, "Drift Mode", uint8_t(preset.mDriftMode));
        SetFloatProp(car, "Base Grip", preset.mBaseGrip);
        SetFloatProp(car, "Drift Grip", preset.mDriftGrip);
        SetFloatProp(car, "Drift Enter Angle", preset.mDriftEnterAngle);
        SetFloatProp(car, "Drift Exit Time", preset.mDriftExitTime);
        SetFloatProp(car, "Drift Yaw Assist", preset.mDriftYawAssist);

        SetFloatProp(car, "Boost Speed Mult", preset.mBoostSpeedMult);
        SetFloatProp(car, "Boost Accel Mult", preset.mBoostAccelMult);

        SetFloatProp(car, "Body Roll Angle", preset.mBodyRollAngle);
        SetFloatProp(car, "Body Pitch Angle", preset.mBodyPitchAngle);
        SetFloatProp(car, "Body Yaw Offset", request.mBodyFacesBackward ? 180.0f : 0.0f);

        // --- Dimensions from the mesh ---------------------------------------
        SetVectorProp(car, "Collision Half Extents", a.mCollisionHalfExtents);
        SetFloatProp(car, "Ride Height", a.mRideHeight);
        SetFloatProp(car, "Wheel Radius", a.mWheelRadius);

        // --- Collider ---------------------------------------------------------
        // CarController3D creates this itself on Start if it is missing, but
        // building it here means it is visible in the outliner, adjustable, and
        // saved with the scene rather than appearing only once the game runs.
        //
        // Box3D is not dllexported, so it is created by factory name and
        // configured through reflection. Its "Extents" is the FULL box size,
        // while the controller works in half-extents -- hence the doubling.
        Node* collider = car->CreateChild("Box3D");
        if (collider != nullptr)
        {
            collider->SetName("Collider");

            Node3D* collider3d = collider->As<Node3D>();
            if (collider3d != nullptr)
            {
                // Local transform is the world transform for this node, which is
                // what keeps the swept box aligned with the car's heading.
                collider3d->SetInheritTransform(false);
                collider3d->SetWorldPosition(request.mSpawnPosition);
            }

            SetVectorProp(collider, "Extents", a.mCollisionHalfExtents * 2.0f);
            SetBoolProp(collider, "Physics", false);
            SetBoolProp(collider, "Collision", true);
            SetBoolProp(collider, "Overlaps", false);

            // Collision group and mask are left at Primitive3D's defaults
            // (ColGroup0 / everything). The car's queries exclude this body by
            // explicit ignore-object, so it needs no group of its own -- and
            // putting it on ColGroup1 would collide, conceptually, with the
            // group the editor spawns all its static meshes on.

            SetNodeProp(car, "Collider", collider);
        }

        // --- Body ------------------------------------------------------------
        StaticMesh3D* body = car->CreateChild<StaticMesh3D>("Body");
        if (body != nullptr)
        {
            body->SetStaticMesh(request.mBodyMesh);
            body->SetPosition(a.mBodyOffset);

            // Visual only: the CarController3D box owns collision, and a second
            // collider on the same car would fight the sweeps.
            body->EnableCollision(false);
            body->EnablePhysics(false);
            body->EnableOverlaps(false);

            SetNodeProp(car, "Body", body);
        }

        // --- Wheels -----------------------------------------------------------
        const float halfBase = a.mWheelbase * 0.5f;
        const float halfTrack = a.mTrackWidth * 0.5f;

        // Wheel centres sit one radius above the ground plane the chassis rides
        // at, so they visually touch the road.
        const float wheelY = -a.mRideHeight + a.mWheelRadius;

        // Forward is -Z, so the front axle is at negative Z. The left-hand pair
        // (negative X) is the side that gets mirrored.
        struct WheelDef { const char* mName; const char* mProp; glm::vec3 mPos; bool mMirror; };
        const WheelDef wheels[4] =
        {
            { "Wheel_FL", "Wheel FL", glm::vec3(-halfTrack, wheelY, -halfBase), true  },
            { "Wheel_FR", "Wheel FR", glm::vec3( halfTrack, wheelY, -halfBase), false },
            { "Wheel_RL", "Wheel RL", glm::vec3(-halfTrack, wheelY,  halfBase), true  },
            { "Wheel_RR", "Wheel RR", glm::vec3( halfTrack, wheelY,  halfBase), false },
        };

        for (int32_t i = 0; i < 4; ++i)
        {
            const bool mirror = wheels[i].mMirror && request.mMirrorLeftWheels;

            StaticMesh3D* wheel = CreateWheel(
                car, wheels[i].mName, request.mWheelMesh, wheels[i].mPos, a.mWheelRadius, mirror);

            if (wheel != nullptr)
            {
                SetNodeProp(car, wheels[i].mProp, wheel);
            }
        }

        // --- Engine audio ------------------------------------------------------
        if (request.mCreateAudio)
        {
            Audio3D* audio = car->CreateChild<Audio3D>("EngineAudio");
            if (audio != nullptr)
            {
                audio->SetLoop(true);
                audio->SetAutoPlay(true);
                audio->SetInnerRadius(4.0f);
                audio->SetOuterRadius(45.0f);

                // No SoundWave assigned: the artist drops one on the node. The
                // controller skips audio entirely until then rather than
                // guessing at an asset name that may not exist.
                SetNodeProp(car, "Engine Audio", audio);
            }
        }

        // --- Tire smoke ---------------------------------------------------------
        if (request.mCreateSmoke)
        {
            // Particle3D is not dllexported, so it has to be created by type
            // name and configured through reflection.
            const char* smokeNames[2] = { "Smoke_L", "Smoke_R" };
            const char* smokeProps[2] = { "Smoke L", "Smoke R" };
            const float smokeX[2] = { -halfTrack, halfTrack };

            for (int32_t i = 0; i < 2; ++i)
            {
                Node* smoke = car->CreateChild("Particle3D");
                if (smoke == nullptr)
                {
                    continue;
                }

                smoke->SetName(smokeNames[i]);

                Node3D* smoke3d = smoke->As<Node3D>();
                if (smoke3d != nullptr)
                {
                    smoke3d->SetPosition(glm::vec3(smokeX[i], wheelY, halfBase));
                }

                // Off until the car actually slides, and world-space so the
                // trail is left behind rather than dragged along.
                SetBoolProp(smoke, "Emit", false);
                SetBoolProp(smoke, "Auto Emit", false);
                SetBoolProp(smoke, "Use Local Space", false);

                SetNodeProp(car, smokeProps[i], smoke);
            }
        }

        // --- Chase camera --------------------------------------------------------
        if (request.mCreateCamera)
        {
            // Parented to the car for tidiness in the outliner, but with
            // inherit-transform off (set in CarCamera3D::Create) so its own
            // smoothing is not fighting the parent transform.
            CarCamera3D* camera = car->CreateChild<CarCamera3D>("CarCamera");
            if (camera != nullptr)
            {
                SetNodeProp(camera, "Target Car", car);
                SetBoolProp(camera, "Make Main Camera", request.mMakeMainCamera);

                // Frame the camera off the derived body size so a bus and a
                // go-kart both start with a sensible chase distance.
                const float length = a.mSize.z;
                SetFloatProp(camera, "Follow Distance", glm::clamp(length * 1.6f, 3.0f, 14.0f));
                SetFloatProp(camera, "Follow Height", glm::clamp(a.mSize.y * 1.5f, 1.0f, 5.0f));
                SetFloatProp(camera, "Look At Height", glm::clamp(a.mSize.y * 0.6f, 0.3f, 3.0f));

                camera->SetWorldPosition(request.mSpawnPosition + glm::vec3(0.0f, 2.5f, 6.0f));

                SetNodeProp(car, "Camera", camera);
            }
        }

        return car;
    }

    bool CarBuilder::SaveCarAsScene(Node* carRoot, const std::string& assetName, std::string& outError)
    {
        outError.clear();

        if (carRoot == nullptr)
        {
            outError = "Nothing to save.";
            return false;
        }

        AssetDir* dir = GetCurrentAssetDir();
        if (dir == nullptr)
        {
            outError = "No asset folder is selected. Pick a folder in the Asset Browser, then retry.";
            return false;
        }

        const std::string name = assetName.empty() ? std::string("SC_Car") : assetName;

        // CreateAndRegisterAsset creates, names, registers and writes the asset
        // in one call, so stub->mAsset is already populated here.
        AssetStub* stub = AssetManager::Get()->CreateAndRegisterAsset(
            Scene::GetStaticType(), dir, name, false);

        if (stub == nullptr || stub->mAsset == nullptr)
        {
            outError = "Failed to create the Scene asset.";
            return false;
        }

        Scene* scene = static_cast<Scene*>(stub->mAsset);

        // Capture with the root at the origin, mirroring what the editor's own
        // CaptureAndSaveScene does -- otherwise every instantiation inherits the
        // position the car happened to be built at.
        Node3D* root3d = carRoot->As<Node3D>();
        glm::vec3 savedPos(0.0f);
        bool restorePos = false;

        if (root3d != nullptr)
        {
            savedPos = root3d->GetPosition();
            root3d->SetPosition(glm::vec3(0.0f));
            restorePos = true;
        }

        scene->Capture(carRoot);
        carRoot->SetScene(scene);

        if (restorePos)
        {
            root3d->SetPosition(savedPos);
        }

        AssetManager::Get()->SaveAsset(*stub);

        return true;
    }
}

#endif
