#include "LuaEvents.hpp"

#include <lua.hpp>
#include <hyprland/src/config/ConfigManager.hpp>
#include <hyprland/src/config/lua/ConfigManager.hpp>

#include <map>

namespace Hyprexpo::LuaEvents {
namespace {

std::map<std::string, int> g_handlers; // event -> lua registry ref
lua_State*                 g_luaState = nullptr; // state the refs belong to
bool                       g_consume  = false; // set by hyprexpo.consume() during fire()

Config::Lua::CConfigManager* luaManager() {
    return dynamic_cast<Config::Lua::CConfigManager*>(Config::mgr().get());
}

void setStr(lua_State* L, const char* key, const std::string& value) {
    lua_pushstring(L, value.c_str());
    lua_setfield(L, -2, key);
}

void setNum(lua_State* L, const char* key, double value) {
    lua_pushnumber(L, value);
    lua_setfield(L, -2, key);
}

void setBool(lua_State* L, const char* key, bool value) {
    lua_pushboolean(L, value ? 1 : 0);
    lua_setfield(L, -2, key);
}

} // namespace

int luaOn(lua_State* L) {
    if (lua_gettop(L) < 2 || !lua_isstring(L, 1) || (!lua_isfunction(L, 2) && !lua_isnil(L, 2)))
        return luaL_error(L, "hyprexpo.on(event, fn): expected (string, function|nil)");

    const std::string event = lua_tostring(L, 1);
    const auto        old   = g_handlers.find(event);
    // Refs belong to the state that created them: only unref when it is
    // still the live one (a reload starts a fresh state where old ints
    // would alias innocent objects).
    if (old != g_handlers.end()) {
        if (g_luaState == L)
            luaL_unref(L, LUA_REGISTRYINDEX, old->second);
        g_handlers.erase(old);
    }
    if (lua_isnil(L, 2))
        return 0;

    lua_pushvalue(L, 2);
    g_handlers[event] = luaL_ref(L, LUA_REGISTRYINDEX);
    g_luaState        = L;
    return 0;
}

int luaConsume(lua_State* L) {
    (void)L;
    g_consume = true;
    return 0;
}

bool fire(const std::string& event, PushArgs push) {
    const auto it = g_handlers.find(event);
    if (it == g_handlers.end())
        return false;

    auto* const mgr = luaManager();
    if (!mgr)
        return false;

    g_consume = false;
    mgr->callLuaFn(it->second, push, 50, std::string("hyprexpo:") + event);
    return g_consume;
}

// Event table builders: one per input kind, fields stable (see README).
void pushTouch(lua_State* L, int id, double x, double y, uint32_t t) {
    lua_createtable(L, 0, 4);
    setNum(L, "id", (double)id);
    setNum(L, "x", x);
    setNum(L, "y", y);
    setNum(L, "t", (double)t);
}

void pushTouchMotion(lua_State* L, int id, double x, double y, double dx, double dy, uint32_t t) {
    lua_createtable(L, 0, 7);
    setNum(L, "id", (double)id);
    setNum(L, "x", x);
    setNum(L, "y", y);
    setNum(L, "dx", dx);
    setNum(L, "dy", dy);
    setNum(L, "t", (double)t);
}

void pushPointer(lua_State* L, double x, double y, double dx, double dy) {
    lua_createtable(L, 0, 4);
    setNum(L, "x", x);
    setNum(L, "y", y);
    setNum(L, "dx", dx);
    setNum(L, "dy", dy);
}

void pushButton(lua_State* L, uint32_t button, bool pressed, double x, double y, uint32_t t) {
    lua_createtable(L, 0, 5);
    setNum(L, "button", (double)button);
    setBool(L, "pressed", pressed);
    setNum(L, "x", x);
    setNum(L, "y", y);
    setNum(L, "t", (double)t);
}

void pushWheel(lua_State* L, const char* axis, double delta, int discrete, int source, double x, double y, uint32_t t) {
    lua_createtable(L, 0, 7);
    setStr(L, "axis", axis);
    setNum(L, "delta", delta);
    setNum(L, "discrete", (double)discrete);
    setNum(L, "source", (double)source);
    setNum(L, "x", x);
    setNum(L, "y", y);
    setNum(L, "t", (double)t);
}

void pushKey(lua_State* L, uint32_t keysym, uint32_t keycode, bool pressed) {
    lua_createtable(L, 0, 3);
    setNum(L, "keysym", (double)keysym);
    setNum(L, "keycode", (double)keycode);
    setBool(L, "pressed", pressed);
}

} // namespace Hyprexpo::LuaEvents
