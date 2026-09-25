#pragma once

#if EDITOR

#include <stdint.h>

struct EditorUIHooks;

namespace CarAddon
{
    // Dockable "Car Setup Wizard" panel.
    //
    // UI only -- all mesh analysis and node construction lives in CarBuilder,
    // mirroring how the engine keeps ScriptCreatorDialog (ImGui) separate from
    // AddonCreator (logic). That split keeps the build path callable from
    // elsewhere later without dragging ImGui along.
    class CarWizardWindow
    {
    public:

        // Registers the dockable window and the menu item that opens it.
        // Registered windows start closed and the engine never surfaces them in
        // a menu on its own, so without the menu item the panel is unreachable.
        static void Register(EditorUIHooks* hooks, uint64_t hookId);

        // Clears addon-owned state before the DLL is freed. The engine removes
        // the hooks themselves via RemoveAllHooks(hookId).
        static void Shutdown();
    };
}

#endif
