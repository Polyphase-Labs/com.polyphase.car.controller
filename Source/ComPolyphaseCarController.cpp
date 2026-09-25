/**
 * @file ComPolyphaseCarController.cpp
 * @brief Native addon entry point: com.polyphase.car.controller
 *
 * Arcade car controller (Burnout / OutRun feel) plus a Car Setup Wizard that
 * builds a drivable car hierarchy from a static mesh.
 */

#include "Plugins/PolyphasePluginAPI.h"
#include "Plugins/PolyphaseEngineAPI.h"

#include "EngineAPIAccess.h"

#include "Nodes/CarCamera3D.h"
#include "Nodes/CarController3D.h"
#include "Runtime/CarInputSource.h"

#if LUA_ENABLED
#include "Lua/CarCamera3D_Lua.h"
#include "Lua/CarController3D_Lua.h"
#endif

#if EDITOR
#include "Plugins/EditorUIHooks.h"
#include "Editor/CarAddonImgui.h"
#include "Editor/CarWizardWindow.h"
#endif

namespace CarAddon
{
    static PolyphaseEngineAPI* sEngineAPI = nullptr;
    static uint64_t sOwnerHookId = 0;

    PolyphaseEngineAPI* GetEngineAPI()                        { return sEngineAPI; }
    void                SetEngineAPI(PolyphaseEngineAPI* api) { sEngineAPI = api; }

    uint64_t GetOwnerHookId()                { return sOwnerHookId; }
    void     SetOwnerHookId(uint64_t hookId) { sOwnerHookId = hookId; }
}

static int OnLoad(PolyphaseEngineAPI* api)
{
    CarAddon::SetEngineAPI(api);

    // Pull the node translation units into the link so their DEFINE_NODE static
    // initializers actually run. Without this the linker is free to drop a TU
    // whose only observable effect is factory registration, and the types then
    // silently fail to appear in the editor.
    FORCE_LINK_CALL(CarController3D);
    FORCE_LINK_CALL(CarCamera3D);

    // Bucket both nodes under "Vehicles" in the Add Node menu. Without this
    // they land in Addons/com.polyphase.car.controller, which is accurate but
    // hard to find. Safe to call here: the factory statics ran at DLL load,
    // before OnLoad.
    if (api != nullptr && api->SetNodeCategory != nullptr)
    {
        api->SetNodeCategory("CarController3D", "Vehicles");
        api->SetNodeCategory("CarCamera3D", "Vehicles");
    }

    // Ship sensible default bindings. No-op if the project already has an
    // InputActions.json -- we never clobber a user's bindings. The car stays
    // drivable via raw input polling regardless, so failure here is not fatal.
    CarAddon::CarInputSource::InstallDefaultInputActions();

    if (api != nullptr && api->LogDebug != nullptr)
    {
        api->LogDebug("Arcade Car Controller addon loaded");
    }

    return 0;
}

static void OnUnload()
{
#if EDITOR
    // Drop addon-owned editor state before the DLL is freed. The engine removes
    // our hooks via RemoveAllHooks(hookId), but anything we cached ourselves is
    // our problem -- a stale pointer here becomes a crash inside DrawWindows on
    // the next frame.
    CarAddon::CarWizardWindow::Shutdown();
#endif

    PolyphaseEngineAPI* api = CarAddon::GetEngineAPI();
    if (api != nullptr && api->LogDebug != nullptr)
    {
        api->LogDebug("Arcade Car Controller addon unloaded");
    }

    CarAddon::SetOwnerHookId(0);

    // Null the cached API pointer last: everything above may still want it.
    CarAddon::SetEngineAPI(nullptr);
}

static void RegisterTypes(void* /*nodeFactory*/)
{
    // Nothing to do. DEFINE_NODE's static initializers registered the factories
    // when the DLL loaded; the nodeFactory parameter is currently unused by the
    // engine.
}

static void RegisterScriptFuncs(lua_State* /*L*/)
{
#if LUA_ENABLED
    CarController3D_Lua::Bind();
    CarCamera3D_Lua::Bind();
#endif
}

#if EDITOR
static void RegisterEditorUI(EditorUIHooks* hooks, uint64_t hookId)
{
    CarAddon::SetOwnerHookId(hookId);
    CarAddon::CarWizardWindow::Register(hooks, hookId);
}

static void OnEditorReady()
{
    // Bind the editor's ImGui context now that the UI is fully stood up. The
    // draw callbacks also bind defensively, so this is belt-and-braces for the
    // first frame after a hot reload.
    CarAddon::EnsureImGuiContext();
}
#endif

static int FillDesc(PolyphasePluginDesc* desc)
{
    desc->apiVersion          = OCTAVE_PLUGIN_API_VERSION;
    desc->pluginName          = "ArcadeCarController";
    desc->pluginVersion       = "1.0.0";
    desc->OnLoad              = OnLoad;
    desc->OnUnload            = OnUnload;

    // The car nodes tick themselves through the node system, so the addon needs
    // no plugin-level tick.
    desc->Tick                = nullptr;
    desc->TickEditor          = nullptr;

    desc->RegisterTypes       = RegisterTypes;
    desc->RegisterScriptFuncs = RegisterScriptFuncs;
#if EDITOR
    desc->RegisterEditorUI    = RegisterEditorUI;
    desc->OnEditorReady       = OnEditorReady;
#else
    desc->RegisterEditorUI    = nullptr;
    desc->OnEditorReady       = nullptr;
#endif
    desc->OnEditorPreInit     = nullptr;
    return 0;
}

#if EDITOR
extern "C" OCTAVE_PLUGIN_API int PolyphasePlugin_GetDesc(PolyphasePluginDesc* desc)
{
    return FillDesc(desc);
}
#else
// Shipped build: each addon exports a uniquely-suffixed symbol so the editor's
// generated AddonPlugins.cpp can extern "C"-declare several of them in one exe
// without collisions.
extern "C" int PolyphasePlugin_GetDesc_com_polyphase_car_controller(PolyphasePluginDesc* desc)
{
    return FillDesc(desc);
}
#endif
