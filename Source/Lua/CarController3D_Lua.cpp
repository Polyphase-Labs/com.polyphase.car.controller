#include "Lua/CarController3D_Lua.h"

#if LUA_ENABLED

#include "LuaBindings/LuaTypeCheck.h"
#include "LuaBindings/Node3d_Lua.h"


using namespace CarAddon;

int CarController3D_Lua::GetSpeed(lua_State* L)
{
    CarController3D* car = CHECK_CAR_CONTROLLER_3D(L, 1);
    lua_pushnumber(L, car->GetSpeed());
    return 1;
}

int CarController3D_Lua::GetSpeedKph(lua_State* L)
{
    CarController3D* car = CHECK_CAR_CONTROLLER_3D(L, 1);
    lua_pushnumber(L, car->GetSpeedKph());
    return 1;
}

int CarController3D_Lua::GetRpm(lua_State* L)
{
    CarController3D* car = CHECK_CAR_CONTROLLER_3D(L, 1);
    lua_pushnumber(L, car->GetRpm());
    return 1;
}

int CarController3D_Lua::GetGear(lua_State* L)
{
    CarController3D* car = CHECK_CAR_CONTROLLER_3D(L, 1);
    lua_pushinteger(L, car->GetGear());
    return 1;
}

int CarController3D_Lua::IsDrifting(lua_State* L)
{
    CarController3D* car = CHECK_CAR_CONTROLLER_3D(L, 1);
    lua_pushboolean(L, car->IsDrifting());
    return 1;
}

int CarController3D_Lua::GetDriftAmount(lua_State* L)
{
    CarController3D* car = CHECK_CAR_CONTROLLER_3D(L, 1);
    lua_pushnumber(L, car->GetDriftAmount());
    return 1;
}

int CarController3D_Lua::GetSlipAngle(lua_State* L)
{
    CarController3D* car = CHECK_CAR_CONTROLLER_3D(L, 1);
    // Degrees on the Lua side -- radians are a C++ implementation detail and
    // every HUD wants degrees anyway.
    lua_pushnumber(L, glm::degrees(car->GetSlipAngle()));
    return 1;
}

int CarController3D_Lua::IsGrounded(lua_State* L)
{
    CarController3D* car = CHECK_CAR_CONTROLLER_3D(L, 1);
    lua_pushboolean(L, car->IsGrounded());
    return 1;
}

int CarController3D_Lua::GetBoost(lua_State* L)
{
    CarController3D* car = CHECK_CAR_CONTROLLER_3D(L, 1);
    lua_pushnumber(L, car->GetBoost());
    return 1;
}

int CarController3D_Lua::GetTopSpeed(lua_State* L)
{
    CarController3D* car = CHECK_CAR_CONTROLLER_3D(L, 1);
    lua_pushnumber(L, car->GetTopSpeed());
    return 1;
}

int CarController3D_Lua::GetBoostCapacity(lua_State* L)
{
    CarController3D* car = CHECK_CAR_CONTROLLER_3D(L, 1);
    lua_pushnumber(L, car->GetBoostCapacity());
    return 1;
}

int CarController3D_Lua::GetVelocity(lua_State* L)
{
    CarController3D* car = CHECK_CAR_CONTROLLER_3D(L, 1);
    const glm::vec3 velocity = car->GetVelocity();

    // Returns three numbers rather than a Vector userdata: Vector_Lua::Create
    // is not exported from Polyphase.lib, so an addon cannot construct the
    // engine's vector type.
    //   local vx, vy, vz = car:GetVelocity()
    lua_pushnumber(L, velocity.x);
    lua_pushnumber(L, velocity.y);
    lua_pushnumber(L, velocity.z);
    return 3;
}

int CarController3D_Lua::SetInput(lua_State* L)
{
    CarController3D* car = CHECK_CAR_CONTROLLER_3D(L, 1);

    // Table form keeps the call site readable and lets fields be omitted:
    //   car:SetInput({ throttle = 1.0, steer = -0.5 })
    luaL_checktype(L, 2, LUA_TTABLE);

    CarInput input;

    auto readNumber = [&](const char* key, float& out)
    {
        lua_getfield(L, 2, key);
        if (lua_isnumber(L, -1))
        {
            out = (float)lua_tonumber(L, -1);
        }
        lua_pop(L, 1);
    };

    readNumber("throttle", input.mThrottle);
    readNumber("brake", input.mBrake);
    readNumber("steer", input.mSteer);
    readNumber("handbrake", input.mHandbrake);
    readNumber("boost", input.mBoost);

    lua_getfield(L, 2, "reset");
    input.mResetCar = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);

    car->SetExternalInput(input);
    return 0;
}

int CarController3D_Lua::ClearInput(lua_State* L)
{
    CarController3D* car = CHECK_CAR_CONTROLLER_3D(L, 1);
    car->ClearExternalInput();
    return 0;
}

int CarController3D_Lua::ResetTo(lua_State* L)
{
    CarController3D* car = CHECK_CAR_CONTROLLER_3D(L, 1);
    float x = (float)CHECK_NUMBER(L, 2);
    float y = (float)CHECK_NUMBER(L, 3);
    float z = (float)CHECK_NUMBER(L, 4);
    float yaw = (lua_gettop(L) >= 5) ? (float)lua_tonumber(L, 5) : 0.0f;

    car->ResetTo(glm::vec3(x, y, z), yaw);
    return 0;
}

int CarController3D_Lua::ResetInPlace(lua_State* L)
{
    CarController3D* car = CHECK_CAR_CONTROLLER_3D(L, 1);
    car->ResetInPlace();
    return 0;
}

void CarController3D_Lua::Bind()
{
    lua_State* L = GetLua();
    if (L == nullptr)
    {
        return;
    }

    // Engine Bind() functions assert lua_gettop(L) == 0, but an addon's
    // RegisterScriptFuncs can run with entries already on the stack. Assert
    // net-zero against where we started instead.
    const int startTop = lua_gettop(L);

    // Parent metatable must mirror the real C++ base (Node3D), so Lua inherits
    // every Node3D / Node method and nothing the class does not actually have.
    int mtIndex = CreateClassMetatable(
        CAR_CONTROLLER_3D_LUA_NAME,
        CAR_CONTROLLER_3D_LUA_FLAG,
        NODE_3D_LUA_NAME);

    Node_Lua::BindCommon(L, mtIndex);

    REGISTER_TABLE_FUNC(L, mtIndex, GetSpeed);
    REGISTER_TABLE_FUNC(L, mtIndex, GetSpeedKph);
    REGISTER_TABLE_FUNC(L, mtIndex, GetRpm);
    REGISTER_TABLE_FUNC(L, mtIndex, GetGear);
    REGISTER_TABLE_FUNC(L, mtIndex, IsDrifting);
    REGISTER_TABLE_FUNC(L, mtIndex, GetDriftAmount);
    REGISTER_TABLE_FUNC(L, mtIndex, GetSlipAngle);
    REGISTER_TABLE_FUNC(L, mtIndex, IsGrounded);
    REGISTER_TABLE_FUNC(L, mtIndex, GetBoost);
    REGISTER_TABLE_FUNC(L, mtIndex, GetTopSpeed);
    REGISTER_TABLE_FUNC(L, mtIndex, GetBoostCapacity);
    REGISTER_TABLE_FUNC(L, mtIndex, GetVelocity);
    REGISTER_TABLE_FUNC(L, mtIndex, SetInput);
    REGISTER_TABLE_FUNC(L, mtIndex, ClearInput);
    REGISTER_TABLE_FUNC(L, mtIndex, ResetTo);
    REGISTER_TABLE_FUNC(L, mtIndex, ResetInPlace);

    lua_pop(L, 1);

    OCT_ASSERT(lua_gettop(L) == startTop);
    (void)startTop;
}

#endif
