#include "Editor/CarWizardWindow.h"

#if EDITOR

#include "Editor/CarAddonImgui.h"
#include "Editor/CarBuilder.h"
#include "Editor/CarPresets.h"

#include "CarMath.h"
#include "EngineAPIAccess.h"
#include "Runtime/CarInputSource.h"

#include "AssetManager.h"
#include "Engine.h"
#include "Log.h"
#include "World.h"

#include "Assets/StaticMesh.h"

#include "Plugins/EditorUIHooks.h"
#include "Plugins/PolyphaseEngineAPI.h"

#include "imgui.h"

#include <algorithm>
#include <string>
#include <vector>

namespace CarAddon
{
    static const char* const kWindowId = "car_setup_wizard";
    static const char* const kWindowName = "Car Setup Wizard";

    // The asset browser publishes this payload type; its data is an AssetStub**.
    // Hardcoded rather than including EditorConstants.h so the addon does not
    // depend on an editor-private header.
    static const char* const kDragDropAsset = "DND_ASSET";

    // ------------------------------------------------------------------------
    // Panel state. File-static, matching the engine's own ScriptCreatorDialog.
    // ------------------------------------------------------------------------
    struct WizardState
    {
        char mCarName[128] = "Car";
        char mSceneName[128] = "SC_Car";
        char mSearchFilter[128] = "";

        std::string mBodyMeshName;
        std::string mWheelMeshName;

        StaticMesh* mBodyMesh = nullptr;
        StaticMesh* mWheelMesh = nullptr;

        MeshAnalysis mAnalysis;

        int32_t mPresetIndex = 0;

        bool mBodyFacesBackward = false;
        bool mMirrorLeftWheels = true;
        bool mCreateCamera = true;
        bool mCreateAudio = true;
        bool mCreateSmoke = true;
        bool mMakeMainCamera = true;
        bool mSaveAsScene = false;

        float mSpawnPosition[3] = { 0.0f, 1.0f, 0.0f };

        // Overridable derived values. Kept separate from mAnalysis so
        // re-analysing a mesh can repopulate them without losing the record of
        // what the mesh actually measured.
        bool mOverrideDimensions = false;
        float mCollisionExtents[3] = { 0.9f, 0.55f, 2.0f };
        float mWheelbase = 2.5f;
        float mTrackWidth = 1.5f;
        float mWheelRadius = 0.34f;
        float mRideHeight = 0.35f;

        std::string mError;
        std::string mSuccess;
    };

    static WizardState sState;

    // ------------------------------------------------------------------------
    // Helpers
    // ------------------------------------------------------------------------

    static void ApplyAnalysisToFields(const MeshAnalysis& a)
    {
        sState.mCollisionExtents[0] = a.mCollisionHalfExtents.x;
        sState.mCollisionExtents[1] = a.mCollisionHalfExtents.y;
        sState.mCollisionExtents[2] = a.mCollisionHalfExtents.z;
        sState.mWheelbase = a.mWheelbase;
        sState.mTrackWidth = a.mTrackWidth;
        sState.mWheelRadius = a.mWheelRadius;
        sState.mRideHeight = a.mRideHeight;
    }

    static void SetBodyMesh(StaticMesh* mesh, const std::string& name)
    {
        sState.mBodyMesh = mesh;
        sState.mBodyMeshName = name;
        sState.mError.clear();
        sState.mSuccess.clear();

        sState.mAnalysis = CarBuilder::AnalyzeMesh(mesh);

        if (sState.mAnalysis.mValid)
        {
            ApplyAnalysisToFields(sState.mAnalysis);

            // Default the car's name to the mesh's, minus a leading "SM_".
            std::string carName = name;
            if (carName.rfind("SM_", 0) == 0)
            {
                carName = carName.substr(3);
            }
            if (!carName.empty())
            {
                snprintf(sState.mCarName, sizeof(sState.mCarName), "%s", carName.c_str());
                snprintf(sState.mSceneName, sizeof(sState.mSceneName), "SC_%s", carName.c_str());
            }
        }
        else if (mesh != nullptr)
        {
            sState.mError = "That mesh has no usable geometry (zero vertices).";
        }
    }

    // Hand-rolled asset slot. Polyphase::AssetRefPicker exists but carries no
    // POLYPHASE_API and is absent from Polyphase.lib, so an addon has to build
    // its own against the raw drag-drop payload.
    static bool DrawAssetSlot(const char* label, const char* id, std::string& outName, StaticMesh** outMesh)
    {
        bool changed = false;

        ImGui::TextUnformatted(label);
        ImGui::SameLine(150.0f);

        const char* display = outName.empty() ? "<drop a StaticMesh here>" : outName.c_str();

        const float width = ImGui::GetContentRegionAvail().x - 30.0f;
        const float height = ImGui::GetTextLineHeightWithSpacing() + 4.0f;
        const ImVec2 cursor = ImGui::GetCursorScreenPos();

        ImGui::InvisibleButton(id, ImVec2(glm::max(width, 60.0f), height));
        const bool hovered = ImGui::IsItemHovered();

        ImDrawList* draw = ImGui::GetWindowDrawList();
        draw->AddRect(
            cursor,
            ImVec2(cursor.x + glm::max(width, 60.0f), cursor.y + height),
            ImGui::GetColorU32(hovered ? ImGuiCol_FrameBgHovered : ImGuiCol_Border),
            3.0f);

        draw->AddText(
            ImVec2(cursor.x + 6.0f, cursor.y + 2.0f),
            ImGui::GetColorU32(outName.empty() ? ImGuiCol_TextDisabled : ImGuiCol_Text),
            display);

        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kDragDropAsset))
            {
                if (payload->Data != nullptr && size_t(payload->DataSize) == sizeof(AssetStub*))
                {
                    AssetStub* stub = *(AssetStub**)payload->Data;

                    if (stub != nullptr)
                    {
                        // The stub may not be loaded yet -- resolve by name.
                        Asset* asset = stub->mAsset;
                        if (asset == nullptr)
                        {
                            asset = ::LoadAsset(stub->mName);
                        }

                        if (asset != nullptr && asset->Is("StaticMesh"))
                        {
                            *outMesh = static_cast<StaticMesh*>(asset);
                            outName = stub->mName;
                            changed = true;
                        }
                        else
                        {
                            sState.mError = std::string("'") + stub->mName + "' is not a StaticMesh.";
                        }
                    }
                }
            }
            ImGui::EndDragDropTarget();
        }

        ImGui::SameLine();
        ImGui::PushID(id);
        if (ImGui::Button("X", ImVec2(22.0f, height)))
        {
            *outMesh = nullptr;
            outName.clear();
            changed = true;
        }
        ImGui::PopID();

        return changed;
    }

    // Fallback picker so the wizard works without dragging anything.
    static void DrawMeshBrowser()
    {
        if (!ImGui::CollapsingHeader("Browse static meshes"))
        {
            return;
        }

        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##meshfilter", "Filter by name...", sState.mSearchFilter, sizeof(sState.mSearchFilter));

        std::string filter = sState.mSearchFilter;
        std::transform(filter.begin(), filter.end(), filter.begin(), ::tolower);

        const TypeId meshType = StaticMesh::GetStaticType();

        // Collect first so the list can be sorted -- GetAssetMap is unordered
        // and an arbitrarily ordered list is miserable to scan.
        std::vector<std::string> names;
        for (auto& pair : AssetManager::Get()->GetAssetMap())
        {
            AssetStub* stub = pair.second;
            if (stub == nullptr || stub->mType != meshType)
            {
                continue;
            }

            if (!filter.empty())
            {
                std::string lower = stub->mName;
                std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
                if (lower.find(filter) == std::string::npos)
                {
                    continue;
                }
            }

            names.push_back(stub->mName);
        }

        std::sort(names.begin(), names.end());

        ImGui::BeginChild("##meshlist", ImVec2(0.0f, 160.0f), true);
        for (const std::string& name : names)
        {
            if (ImGui::Selectable(name.c_str(), sState.mBodyMeshName == name))
            {
                StaticMesh* mesh = static_cast<StaticMesh*>(::LoadAsset(name));
                if (mesh != nullptr)
                {
                    SetBodyMesh(mesh, name);
                }
            }

            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Right))
            {
                StaticMesh* mesh = static_cast<StaticMesh*>(::LoadAsset(name));
                if (mesh != nullptr)
                {
                    sState.mWheelMesh = mesh;
                    sState.mWheelMeshName = name;
                }
            }
        }

        if (names.empty())
        {
            ImGui::TextDisabled("No static meshes found.");
        }
        ImGui::EndChild();

        ImGui::TextDisabled("Click to set as body. Right-double-click to set as wheel.");
    }

    static void DrawCreateButton()
    {
        const bool ready = (sState.mBodyMesh != nullptr) && sState.mAnalysis.mValid;

        if (!ready)
        {
            ImGui::BeginDisabled();
        }

        if (ImGui::Button("Create Car", ImVec2(120.0f, 0.0f)))
        {
            sState.mError.clear();
            sState.mSuccess.clear();

            CarBuildRequest request;
            request.mCarName = sState.mCarName;
            request.mBodyMesh = sState.mBodyMesh;
            request.mWheelMesh = sState.mWheelMesh;
            request.mBodyFacesBackward = sState.mBodyFacesBackward;
            request.mMirrorLeftWheels = sState.mMirrorLeftWheels;
            request.mPresetIndex = sState.mPresetIndex;
            request.mCreateCamera = sState.mCreateCamera;
            request.mCreateAudio = sState.mCreateAudio;
            request.mCreateSmoke = sState.mCreateSmoke;
            request.mMakeMainCamera = sState.mMakeMainCamera;
            request.mSaveAsScene = sState.mSaveAsScene;
            request.mSceneAssetName = sState.mSceneName;
            request.mSpawnPosition = glm::vec3(
                sState.mSpawnPosition[0], sState.mSpawnPosition[1], sState.mSpawnPosition[2]);

            request.mAnalysis = sState.mAnalysis;

            // Fold any user overrides back into the analysis the builder sees.
            if (sState.mOverrideDimensions)
            {
                request.mAnalysis.mCollisionHalfExtents = glm::vec3(
                    sState.mCollisionExtents[0], sState.mCollisionExtents[1], sState.mCollisionExtents[2]);
                request.mAnalysis.mWheelbase = sState.mWheelbase;
                request.mAnalysis.mTrackWidth = sState.mTrackWidth;
                request.mAnalysis.mWheelRadius = sState.mWheelRadius;
                request.mAnalysis.mRideHeight = sState.mRideHeight;
            }

            PolyphaseEngineAPI* api = GetEngineAPI();

            // Group every node creation into one undo step. Null-checked: these
            // pointers only exist from plugin API v6 and are null in game builds.
            const bool grouped = (api != nullptr && api->EditorAction_BeginGroup != nullptr && api->EditorAction_EndGroup != nullptr);
            if (grouped)
            {
                api->EditorAction_BeginGroup("car.wizard.build");
            }

            std::string error;
            Node* car = CarBuilder::BuildCar(request, error);

            if (car != nullptr && sState.mSaveAsScene)
            {
                std::string sceneError;
                if (!CarBuilder::SaveCarAsScene(car, sState.mSceneName, sceneError))
                {
                    // The car itself built fine -- report the save failure
                    // without implying the whole operation failed.
                    sState.mError = "Car created, but saving the Scene asset failed: " + sceneError;
                }
            }

            if (grouped)
            {
                api->EditorAction_EndGroup();
            }

            if (car == nullptr)
            {
                sState.mError = error;
            }
            else
            {
                // There is no exported way to *select* a node from an addon
                // (EditorState::SetSelectedNode is not in Polyphase.lib and
                // EditorUIHooks offers only Selection_Clear). Clear the stale
                // selection and name the node so it can be found in the outliner.
                EditorUIHooks* hooks = (api != nullptr) ? api->editorUI : nullptr;
                if (hooks != nullptr && hooks->Selection_Clear != nullptr)
                {
                    hooks->Selection_Clear();
                }

                if (sState.mSuccess.empty() && sState.mError.empty())
                {
                    sState.mSuccess = std::string("Created '") + sState.mCarName +
                        "' in the scene. Press Play to drive it.";
                }
            }
        }

        if (!ready)
        {
            ImGui::EndDisabled();
        }

        ImGui::SameLine();
        if (ImGui::Button("Reset Form", ImVec2(100.0f, 0.0f)))
        {
            sState = WizardState();
        }
    }

    static void DrawWizard(void* /*userData*/)
    {
        // Defensive: the panel can draw before OnEditorReady on a hot reload.
        EnsureImGuiContext();

        ImGui::TextWrapped(
            "Build a drivable arcade car from a static mesh. Dimensions are measured "
            "from the mesh and can be overridden below.");
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // --- 1. Meshes -------------------------------------------------------
        ImGui::TextUnformatted("1. Meshes");
        ImGui::Spacing();

        if (DrawAssetSlot("Body Mesh *", "##bodyslot", sState.mBodyMeshName, &sState.mBodyMesh))
        {
            SetBodyMesh(sState.mBodyMesh, sState.mBodyMeshName);
        }

        ImGui::Checkbox("Body mesh faces backwards", &sState.mBodyFacesBackward);
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip(
                "Tick if the mesh was authored facing +Z. Engine forward is -Z.\n\n"
                "Applies a 180-degree yaw to the body mesh only. Do not rotate the\n"
                "car node instead -- its rotation is driven by the controller and\n"
                "is overwritten every frame. Rotating the mesh asset does not help\n"
                "either: the wizard measures the mesh's bounding box, which is\n"
                "identical either way round.\n\n"
                "The wheels do not need swapping. They are already correct.");
        }

        DrawAssetSlot("Wheel Mesh", "##wheelslot", sState.mWheelMeshName, &sState.mWheelMesh);
        ImGui::TextDisabled("Optional, and used for all four wheels. Without one the");
        ImGui::TextDisabled("wheels are empty markers and simply do not animate.");

        if (sState.mWheelMesh != nullptr)
        {
            ImGui::Checkbox("Mirror left wheels", &sState.mMirrorLeftWheels);
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip(
                    "Yaws the left pair 180 degrees so an asymmetric wheel mesh faces\n"
                    "outward on both sides. Leave on unless the mesh is symmetric or\n"
                    "already authored for the left side. Spin direction is corrected\n"
                    "automatically either way.");
            }
        }

        ImGui::Spacing();
        DrawMeshBrowser();

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // --- 2. Measurements --------------------------------------------------
        ImGui::TextUnformatted("2. Measurements");
        ImGui::Spacing();

        if (sState.mAnalysis.mValid)
        {
            const MeshAnalysis& a = sState.mAnalysis;
            ImGui::Text("Mesh size:  %.2f w  x  %.2f h  x  %.2f l  (metres)", a.mSize.x, a.mSize.y, a.mSize.z);

            const float offset = glm::length(glm::vec2(a.mBodyOffset.x, a.mBodyOffset.z));
            if (offset > 0.01f)
            {
                ImGui::TextDisabled("Pivot is off-centre by %.2fm; the body will be re-centred.", offset);
            }
        }
        else
        {
            ImGui::TextDisabled("Assign a body mesh to measure it.");
        }

        ImGui::Spacing();
        ImGui::Checkbox("Override derived dimensions", &sState.mOverrideDimensions);

        if (sState.mOverrideDimensions)
        {
            ImGui::Indent(16.0f);
            ImGui::SetNextItemWidth(220.0f);
            ImGui::DragFloat3("Collision Half Extents", sState.mCollisionExtents, 0.01f, 0.05f, 20.0f, "%.2f");
            ImGui::SetNextItemWidth(120.0f);
            ImGui::DragFloat("Wheelbase", &sState.mWheelbase, 0.01f, 0.1f, 20.0f, "%.2f");
            ImGui::SetNextItemWidth(120.0f);
            ImGui::DragFloat("Track Width", &sState.mTrackWidth, 0.01f, 0.1f, 20.0f, "%.2f");
            ImGui::SetNextItemWidth(120.0f);
            ImGui::DragFloat("Wheel Radius", &sState.mWheelRadius, 0.005f, 0.02f, 3.0f, "%.3f");
            ImGui::SetNextItemWidth(120.0f);
            ImGui::DragFloat("Ride Height", &sState.mRideHeight, 0.005f, 0.0f, 3.0f, "%.3f");

            if (ImGui::Button("Re-measure from mesh"))
            {
                if (sState.mAnalysis.mValid)
                {
                    ApplyAnalysisToFields(sState.mAnalysis);
                }
            }
            ImGui::Unindent(16.0f);
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // --- 3. Handling ------------------------------------------------------
        ImGui::TextUnformatted("3. Handling preset");
        ImGui::Spacing();

        ImGui::SetNextItemWidth(200.0f);
        ImGui::Combo("##preset", &sState.mPresetIndex, GetPresetNames(), GetPresetCount());

        const CarPreset& preset = GetPreset(sState.mPresetIndex);
        ImGui::TextWrapped("%s", preset.mDescription);
        ImGui::TextDisabled("Top speed %.0f km/h  |  grip %.1f -> %.1f while drifting",
            preset.mTopSpeed * 3.6f, preset.mBaseGrip, preset.mDriftGrip);
        ImGui::TextDisabled("Every value stays editable on the car node afterwards.");

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // --- 4. Extras --------------------------------------------------------
        ImGui::TextUnformatted("4. What to create");
        ImGui::Spacing();

        ImGui::Checkbox("Chase camera", &sState.mCreateCamera);
        if (sState.mCreateCamera)
        {
            ImGui::SameLine();
            ImGui::Checkbox("Make it the main camera", &sState.mMakeMainCamera);
        }

        ImGui::Checkbox("Engine audio node", &sState.mCreateAudio);
        ImGui::SameLine();
        ImGui::Checkbox("Tire smoke emitters", &sState.mCreateSmoke);

        ImGui::Spacing();
        ImGui::SetNextItemWidth(200.0f);
        ImGui::InputText("Car name", sState.mCarName, sizeof(sState.mCarName));

        ImGui::SetNextItemWidth(220.0f);
        ImGui::DragFloat3("Spawn position", sState.mSpawnPosition, 0.1f, -10000.0f, 10000.0f, "%.1f");

        ImGui::Checkbox("Also save as a reusable Scene asset", &sState.mSaveAsScene);
        if (sState.mSaveAsScene)
        {
            ImGui::Indent(16.0f);
            ImGui::SetNextItemWidth(200.0f);
            ImGui::InputText("Scene asset name", sState.mSceneName, sizeof(sState.mSceneName));
            ImGui::TextDisabled("Saved into the folder currently selected in the Asset Browser.");
            ImGui::Unindent(16.0f);
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // --- Status + action ---------------------------------------------------
        if (!sState.mError.empty())
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.35f, 0.35f, 1.0f));
            ImGui::TextWrapped("%s", sState.mError.c_str());
            ImGui::PopStyleColor();
        }

        if (!sState.mSuccess.empty())
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.35f, 1.0f, 0.45f, 1.0f));
            ImGui::TextWrapped("%s", sState.mSuccess.c_str());
            ImGui::PopStyleColor();
        }

        // Warn about the one failure mode the builder cannot recover from.
        World* world = GetWorld(0);
        if (world == nullptr || world->GetRootNode() == nullptr)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.8f, 0.3f, 1.0f));
            ImGui::TextWrapped("The open scene has no root node. Add any node to the scene before creating a car.");
            ImGui::PopStyleColor();
        }

        ImGui::Spacing();
        DrawCreateButton();

        ImGui::Spacing();
        if (!CarInputSource::IsActionBridgeAvailable())
        {
            ImGui::TextDisabled("Controls: WASD / arrows, Space handbrake, Shift boost, R reset. Gamepad supported.");
        }
    }

    static void OnMenuOpenWizard(void* userData)
    {
        PolyphaseEngineAPI* api = GetEngineAPI();
        if (api == nullptr || api->editorUI == nullptr || api->editorUI->OpenWindow == nullptr)
        {
            return;
        }

        const char* windowId = static_cast<const char*>(userData);
        if (windowId != nullptr)
        {
            api->editorUI->OpenWindow(windowId);
        }
    }

    void CarWizardWindow::Register(EditorUIHooks* hooks, uint64_t hookId)
    {
        if (hooks == nullptr)
        {
            return;
        }

        EnsureImGuiContext();

        if (hooks->RegisterWindow != nullptr)
        {
            hooks->RegisterWindow(hookId, kWindowName, kWindowId, &DrawWizard, nullptr);
        }

        // Registered windows default to closed and the engine adds no menu entry
        // of its own, so without this the wizard could never be opened. Passing
        // a string literal as userData is safe -- it lives in rodata.
        if (hooks->AddMenuItem != nullptr)
        {
            hooks->AddMenuItem(hookId, "Tools", "Car/Setup Wizard...",
                &OnMenuOpenWizard, (void*)kWindowId, nullptr);
        }
    }

    void CarWizardWindow::Shutdown()
    {
        // Drop cached asset pointers: they belong to the engine and would
        // dangle if the addon were reloaded across an asset purge.
        sState = WizardState();

        ResetImGuiContext();
    }
}

#endif
