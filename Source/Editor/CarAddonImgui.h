#pragma once

#if EDITOR

namespace CarAddon
{
    // Points this DLL's ImGui at the editor's context and allocators.
    //
    // The engine patches imconfig.h so IMGUI_API becomes dllimport/dllexport
    // under POLYPHASE_IMGUI_EXPORT, and ~3600 ImGui symbols are exported from
    // the editor binary. That means the *code* is shared -- but ImGui's static
    // globals (GImGui, the allocator function pointers) are per-image, so an
    // addon still has to be told which context to use before its first ImGui
    // call, or it dereferences a null GImGui and crashes.
    //
    // Idempotent, and cheap enough to call at the top of every draw function.
    void EnsureImGuiContext();

    // Drops the one-time latch so a hot-reloaded DLL re-binds against the
    // editor's (possibly recreated) context instead of trusting a stale one.
    void ResetImGuiContext();
}

#endif
