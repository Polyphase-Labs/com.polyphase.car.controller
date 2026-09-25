#pragma once

#include "EngineTypes.h"
#include "Engine.h"
#include "Log.h"

#include "Nodes/CarController3D.h"

#include "LuaBindings/LuaUtils.h"
#include "LuaBindings/Node_Lua.h"

#if LUA_ENABLED

// The metatable name MUST match the DECLARE_NODE class name. Script::CallFunction
// resolves a node's metatable with luaL_getmetatable(L, node->GetClassName()),
// so any mismatch means Lua sees a bare userdata with none of these methods.
#define CAR_CONTROLLER_3D_LUA_NAME "CarController3D"
#define CAR_CONTROLLER_3D_LUA_FLAG "cfCarController3D"
#define CHECK_CAR_CONTROLLER_3D(L, arg) \
    static_cast<CarController3D*>(CheckNodeLuaType(L, arg, CAR_CONTROLLER_3D_LUA_NAME, CAR_CONTROLLER_3D_LUA_FLAG));

struct CarController3D_Lua
{
    // Queries
    static int GetSpeed(lua_State* L);
    static int GetSpeedKph(lua_State* L);
    static int GetRpm(lua_State* L);
    static int GetGear(lua_State* L);
    static int IsDrifting(lua_State* L);
    static int GetDriftAmount(lua_State* L);
    static int GetSlipAngle(lua_State* L);
    static int IsGrounded(lua_State* L);
    static int GetBoost(lua_State* L);
    static int GetTopSpeed(lua_State* L);
    static int GetBoostCapacity(lua_State* L);
    static int GetVelocity(lua_State* L);

    // External control
    static int SetInput(lua_State* L);
    static int ClearInput(lua_State* L);
    static int ResetTo(lua_State* L);
    static int ResetInPlace(lua_State* L);

    static void Bind();
};

#endif
