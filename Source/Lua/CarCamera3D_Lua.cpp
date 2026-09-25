#include "Lua/CarCamera3D_Lua.h"

#if LUA_ENABLED

#include "Lua/CarController3D_Lua.h"

#include "LuaBindings/Camera3d_Lua.h"
#include "LuaBindings/LuaTypeCheck.h"

int CarCamera3D_Lua::SetTargetCar(lua_State* L)
{
    CarCamera3D* camera = CHECK_CAR_CAMERA_3D(L, 1);

    // Accept nil so a script can detach the camera from its car.
    Node* node = CheckNodeOrNilLuaType(L, 2, CAR_CONTROLLER_3D_LUA_NAME, CAR_CONTROLLER_3D_LUA_FLAG);
    camera->SetTargetCar(node != nullptr ? static_cast<CarController3D*>(node) : nullptr);
    return 0;
}

int CarCamera3D_Lua::GetTargetCar(lua_State* L)
{
    CarCamera3D* camera = CHECK_CAR_CAMERA_3D(L, 1);
    Node_Lua::Create(L, camera->GetTargetCar());
    return 1;
}

void CarCamera3D_Lua::Bind()
{
    lua_State* L = GetLua();
    if (L == nullptr)
    {
        return;
    }

    const int startTop = lua_gettop(L);

    int mtIndex = CreateClassMetatable(
        CAR_CAMERA_3D_LUA_NAME,
        CAR_CAMERA_3D_LUA_FLAG,
        CAMERA_3D_LUA_NAME);

    Node_Lua::BindCommon(L, mtIndex);

    REGISTER_TABLE_FUNC(L, mtIndex, SetTargetCar);
    REGISTER_TABLE_FUNC(L, mtIndex, GetTargetCar);

    lua_pop(L, 1);

    OCT_ASSERT(lua_gettop(L) == startTop);
    (void)startTop;
}

#endif
