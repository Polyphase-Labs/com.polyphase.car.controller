#include "Editor/CarAddonImgui.h"

#if EDITOR

#include "EngineAPIAccess.h"

#include "Plugins/ImGuiPluginContext.h"
#include "Plugins/PolyphaseEngineAPI.h"

#include "imgui.h"

namespace CarAddon
{
    static bool sContextBound = false;

    void EnsureImGuiContext()
    {
        if (sContextBound)
        {
            return;
        }

        PolyphaseEngineAPI* api = GetEngineAPI();
        if (api == nullptr || api->GetImGuiContext == nullptr)
        {
            return;
        }

        ImGuiPluginContext ctx = {};
        api->GetImGuiContext(&ctx);

        if (ctx.context == nullptr)
        {
            return;
        }

        ImGui::SetCurrentContext(ctx.context);

        // Without this, memory allocated by this DLL's ImGui calls would come
        // from a different heap than the editor frees it to.
        if (ctx.allocFunc != nullptr && ctx.freeFunc != nullptr)
        {
            ImGui::SetAllocatorFunctions(ctx.allocFunc, ctx.freeFunc, ctx.allocUserData);
        }

        sContextBound = true;
    }

    void ResetImGuiContext()
    {
        sContextBound = false;
    }
}

#endif
