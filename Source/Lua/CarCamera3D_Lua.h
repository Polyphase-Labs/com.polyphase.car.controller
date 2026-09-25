#pragma once

#include "EngineTypes.h"
#include "Engine.h"
#include "Log.h"

#include "Nodes/CarCamera3D.h"

#include "LuaBindings/LuaUtils.h"
#include "LuaBindings/Node_Lua.h"

#if LUA_ENABLED

#define CAR_CAMERA_3D_LUA_NAME "CarCamera3D"
#define CAR_CAMERA_3D_LUA_FLAG "cfCarCamera3D"
#define CHECK_CAR_CAMERA_3D(L, arg) \
    static_cast<CarCamera3D*>(CheckNodeLuaType(L, arg, CAR_CAMERA_3D_LUA_NAME, CAR_CAMERA_3D_LUA_FLAG));

struct CarCamera3D_Lua
{
    static int SetTargetCar(lua_State* L);
    static int GetTargetCar(lua_State* L);

    static void Bind();
};

#endif
