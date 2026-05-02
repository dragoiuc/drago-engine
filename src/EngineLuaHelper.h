#pragma once

#include <string>

#include <lua.hpp>
#include <LuaBridge/LuaBridge.h>

namespace luabridge {

    inline lua_State* CreateLuaState()
    {
        lua_State* luaState = luaL_newstate();
        if (luaState != nullptr) {
            luaL_openlibs(luaState);
            enableExceptions(luaState);
        }

        return luaState;
    }

    inline void CloseLuaState(lua_State* luaState)
    {
        if (luaState != nullptr) {
            lua_close(luaState);
        }
    }

    inline void LoadLuaFile(lua_State* luaState, const std::string& path)
    {
        const int stackTop = lua_gettop(luaState);
        const int status = luaL_dofile(luaState, path.c_str());
        if (status != LUA_OK) {
            throw LuaException(luaState, status);
        }

        lua_settop(luaState, stackTop);
    }

    inline void EstablishInheritance(const LuaRef& instance, const LuaRef& parent)
    {
        lua_State* luaState = instance.state();
        instance.push(luaState);
        lua_newtable(luaState);
        parent.push(luaState);
        lua_setfield(luaState, -2, "__index");
        lua_setmetatable(luaState, -2);
        lua_pop(luaState, 1);
    }

} // namespace luabridge
