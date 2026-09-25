#pragma once

#if EDITOR

#include "Maths.h"

#include <string>

class Node;
class StaticMesh;

namespace CarAddon
{
    // Local-space bounding box of a mesh, plus the car dimensions derived from
    // it. StaticMesh::GetBounds() only returns a *sphere* (centre + radius), so
    // anything that needs a real box has to walk the vertex array.
    struct MeshAnalysis
    {
        bool mValid = false;

        glm::vec3 mMin = glm::vec3(0.0f);
        glm::vec3 mMax = glm::vec3(0.0f);
        glm::vec3 mCenter = glm::vec3(0.0f);
        glm::vec3 mSize = glm::vec3(0.0f);

        // Derived car dimensions -- all overridable in the wizard UI.
        glm::vec3 mCollisionHalfExtents = glm::vec3(0.9f, 0.55f, 2.0f);
        float mWheelbase = 2.5f;        // front-to-rear axle separation
        float mTrackWidth = 1.5f;       // left-to-right wheel separation
        float mWheelRadius = 0.34f;
        float mRideHeight = 0.35f;
        glm::vec3 mBodyOffset = glm::vec3(0.0f);   // re-centres an off-pivot mesh
    };

    // What the wizard asks CarBuilder to produce.
    struct CarBuildRequest
    {
        std::string mCarName = "Car";

        StaticMesh* mBodyMesh = nullptr;
        StaticMesh* mWheelMesh = nullptr;   // optional

        MeshAnalysis mAnalysis;
        int32_t mPresetIndex = 0;

        // Yaws the left-hand wheels 180 degrees. Wanted for any asymmetric wheel
        // mesh (the usual case); harmless for a symmetric one. Turn off if the
        // mesh is already authored for the left side.
        // Set when the body mesh was authored facing +Z. Applied as a body-only
        // yaw offset; the wheels and the driving direction are unaffected,
        // because they were already correct.
        bool mBodyFacesBackward = false;

        bool mMirrorLeftWheels = true;

        bool mCreateCamera = true;
        bool mCreateAudio = true;
        bool mCreateSmoke = true;
        bool mMakeMainCamera = true;
        bool mSaveAsScene = false;
        std::string mSceneAssetName = "SC_Car";

        glm::vec3 mSpawnPosition = glm::vec3(0.0f, 1.0f, 0.0f);
    };

    class CarBuilder
    {
    public:

        // Walks the mesh's vertices to get a true AABB, then derives collision
        // extents, wheelbase, track width, wheel radius and ride height from it.
        // Returns a MeshAnalysis with mValid == false if the mesh is unusable.
        static MeshAnalysis AnalyzeMesh(StaticMesh* mesh);

        // Builds the car hierarchy in the currently open scene.
        //
        // Returns the car root, or nullptr with outError set. Wrapped in an
        // editor undo group by the caller so one Ctrl+Z removes the whole thing.
        static Node* BuildCar(const CarBuildRequest& request, std::string& outError);

        // Captures an already-built car into a reusable Scene asset.
        static bool SaveCarAsScene(Node* carRoot, const std::string& assetName, std::string& outError);
    };
}

#endif
