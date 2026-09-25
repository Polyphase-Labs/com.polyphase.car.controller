#include "Runtime/CarInputSource.h"

#include "CarMath.h"
#include "EngineAPIAccess.h"

#include "Engine.h"
#include "EngineTypes.h"
#include "Log.h"
#include "Stream.h"

#include "Input/Input.h"
#include "Input/InputTypes.h"

#include "Plugins/PolyphaseEngineAPI.h"

#if LUA_ENABLED
extern "C"
{
#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"
}
#endif

#include <filesystem>

namespace CarAddon
{
    // Set true the first time the Lua PlayerInput global answers a query.
    // Reset per session; hot-reload re-evaluates it naturally.
    static bool sBridgeAvailable = false;

    // Any action returning a non-default answer means the project actually has
    // this category set up. Until that happens we stay on the raw fallback,
    // which keeps a freshly-installed addon drivable.
    static bool sBridgeAnswered = false;

    float CarInputSource::GetActionValue(const std::string& category, const std::string& action)
    {
#if LUA_ENABLED
        lua_State* L = GetLua();
        if (L == nullptr)
        {
            return 0.0f;
        }

        // Bracket every excursion into Lua with an explicit stack restore.
        // RegisterScriptFuncs may run with entries already on the stack, so
        // leaking even one slot per frame is a slow-motion stack overflow.
        const int top = lua_gettop(L);

        lua_getglobal(L, "PlayerInput");
        if (!lua_istable(L, -1))
        {
            lua_settop(L, top);
            return 0.0f;
        }

        lua_getfield(L, -1, "GetValue");
        if (!lua_isfunction(L, -1))
        {
            lua_settop(L, top);
            return 0.0f;
        }

        lua_pushstring(L, category.c_str());
        lua_pushstring(L, action.c_str());

        float value = 0.0f;
        if (lua_pcall(L, 2, 1, 0) == 0)
        {
            sBridgeAvailable = true;
            if (lua_isnumber(L, -1))
            {
                value = (float)lua_tonumber(L, -1);
                if (value != 0.0f)
                {
                    sBridgeAnswered = true;
                }
            }
        }

        lua_settop(L, top);
        return value;
#else
        (void)category;
        (void)action;
        return 0.0f;
#endif
    }

    bool CarInputSource::IsActionActive(const std::string& category, const std::string& action)
    {
#if LUA_ENABLED
        lua_State* L = GetLua();
        if (L == nullptr)
        {
            return false;
        }

        const int top = lua_gettop(L);

        lua_getglobal(L, "PlayerInput");
        if (!lua_istable(L, -1))
        {
            lua_settop(L, top);
            return false;
        }

        lua_getfield(L, -1, "IsActive");
        if (!lua_isfunction(L, -1))
        {
            lua_settop(L, top);
            return false;
        }

        lua_pushstring(L, category.c_str());
        lua_pushstring(L, action.c_str());

        bool active = false;
        if (lua_pcall(L, 2, 1, 0) == 0)
        {
            sBridgeAvailable = true;
            active = lua_toboolean(L, -1) != 0;
            if (active)
            {
                sBridgeAnswered = true;
            }
        }

        lua_settop(L, top);
        return active;
#else
        (void)category;
        (void)action;
        return false;
#endif
    }

    bool CarInputSource::IsActionBridgeAvailable()
    {
        return sBridgeAvailable;
    }

    CarInput CarInputSource::Poll(const std::string& category, int32_t gamepadIndex, float deadzone)
    {
        CarInput input;

        // --- Action-mapped path -------------------------------------------
        input.mThrottle  = Clamp01(GetActionValue(category, "Throttle"));
        input.mBrake     = Clamp01(GetActionValue(category, "Brake"));
        input.mHandbrake = Clamp01(GetActionValue(category, "Handbrake"));
        input.mBoost     = Clamp01(GetActionValue(category, "Boost"));

        // Two opposing actions rather than one signed axis: the engine's
        // AxisDirection::Negative returns a *negated positive* magnitude, so a
        // single "Steer" action could never express left.
        const float steerLeft  = Clamp01(GetActionValue(category, "SteerLeft"));
        const float steerRight = Clamp01(GetActionValue(category, "SteerRight"));
        input.mSteer = steerRight - steerLeft;

        input.mResetCar = IsActionActive(category, "ResetCar");

        // --- Raw fallback --------------------------------------------------
        // Used when the project has no actions registered for this category.
        // Keeping both paths live (rather than either/or) also means a partially
        // configured project still drives.
        if (!sBridgeAnswered)
        {
            const bool padConnected = INP_IsGamepadConnected(gamepadIndex);

            if (padConnected)
            {
                // Triggers are unipolar 0..1 across every Windows backend
                // (XInput /255, DualSense HID /255, DirectInput rescaled).
                input.mThrottle  = glm::max(input.mThrottle, Clamp01(INP_GetGamepadAxisValue(GAMEPAD_AXIS_RTRIGGER, gamepadIndex)));
                input.mBrake     = glm::max(input.mBrake,    Clamp01(INP_GetGamepadAxisValue(GAMEPAD_AXIS_LTRIGGER, gamepadIndex)));

                const float stick = ApplyDeadzone(INP_GetGamepadAxisValue(GAMEPAD_AXIS_LTHUMB_X, gamepadIndex), deadzone);
                if (fabsf(stick) > fabsf(input.mSteer))
                {
                    input.mSteer = stick;
                }

                if (INP_IsGamepadButtonDown(GAMEPAD_A, gamepadIndex))  { input.mHandbrake = 1.0f; }
                if (INP_IsGamepadButtonDown(GAMEPAD_L1, gamepadIndex)) { input.mBoost = 1.0f; }
                if (INP_IsGamepadButtonJustDown(GAMEPAD_Y, gamepadIndex)) { input.mResetCar = true; }
            }

            // Keyboard is always folded in, gamepad or not, so a dev can drive
            // without unplugging anything.
            if (INP_IsKeyDown(POLYPHASE_KEY_W))       { input.mThrottle = 1.0f; }
            if (INP_IsKeyDown(POLYPHASE_KEY_UP))      { input.mThrottle = 1.0f; }
            if (INP_IsKeyDown(POLYPHASE_KEY_S))       { input.mBrake = 1.0f; }
            if (INP_IsKeyDown(POLYPHASE_KEY_DOWN))    { input.mBrake = 1.0f; }
            if (INP_IsKeyDown(POLYPHASE_KEY_SPACE))   { input.mHandbrake = 1.0f; }
            if (INP_IsKeyDown(POLYPHASE_KEY_SHIFT_L)) { input.mBoost = 1.0f; }
            if (INP_IsKeyJustDown(POLYPHASE_KEY_R))   { input.mResetCar = true; }

            float keySteer = 0.0f;
            if (INP_IsKeyDown(POLYPHASE_KEY_A) || INP_IsKeyDown(POLYPHASE_KEY_LEFT))  { keySteer -= 1.0f; }
            if (INP_IsKeyDown(POLYPHASE_KEY_D) || INP_IsKeyDown(POLYPHASE_KEY_RIGHT)) { keySteer += 1.0f; }
            if (keySteer != 0.0f)
            {
                input.mSteer = keySteer;
            }
        }

        input.mSteer = glm::clamp(input.mSteer, -1.0f, 1.0f);
        return input;
    }

    void CarInputSource::InstallDefaultInputActions()
    {
        EngineState* engineState = GetEngineState();
        if (engineState == nullptr || engineState->mProjectDirectory.empty())
        {
            return;
        }

        // PlayerInputSystem::LoadProjectActions only ever probes
        // Assets/InputActions.oct and then <projectDir>InputActions.json. There
        // is no search for *.input.json, so the defaults have to land under that
        // exact name or they are simply never read.
        const std::string destPath = engineState->mProjectDirectory + "InputActions.json";

        std::error_code existsError;
        if (std::filesystem::exists(destPath, existsError))
        {
            // Never clobber a project's existing bindings.
            return;
        }

        const std::string srcPath = engineState->mProjectDirectory +
            "Packages/com.polyphase.car.controller/Source/Defaults/Car.input.json";

        Stream src;
        if (!src.ReadFile(srcPath.c_str(), false))
        {
            // Not fatal: the raw INP_ fallback keeps the car drivable.
            LogWarning("CarController: could not read default input actions at %s", srcPath.c_str());
            return;
        }

        std::error_code mkdirError;
        std::filesystem::create_directories(std::filesystem::path(destPath).parent_path(), mkdirError);

        if (src.WriteFile(destPath.c_str()))
        {
            LogDebug("CarController: installed default input actions to %s", destPath.c_str());
        }
    }
}
