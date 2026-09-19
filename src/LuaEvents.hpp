#pragma once

#include <cstdint>
#include <functional>
#include <string>

struct lua_State;

// hyprexpo.on(event, fn) input faucet: raw compositor input forwarded to
// Lua. A handler returning truthy consumes the event (the C++ default is
// skipped); falsy/none falls through to existing behavior, so unhandled
// input acts exactly as before. Threading: compositor input and Lua config
// share one thread; callbacks run synchronously via callLuaFn with a 50ms
// watchdog, same as gesture lambdas.
namespace Hyprexpo::LuaEvents {

using PushArgs = std::function<int(lua_State*)>; // builds the event table, returns 1

// Fire handlers for `event`; true = consumed.
bool fire(const std::string& event, PushArgs push);

// lua entry points (hyprexpo.on / hyprexpo.consume).
int luaOn(lua_State* L);
int luaConsume(lua_State* L);

// Event table builders (fields stable; documented in README).
void pushTouch(lua_State* L, int id, double x, double y, uint32_t t);
void pushTouchMotion(lua_State* L, int id, double x, double y, double dx, double dy, uint32_t t);
void pushPointer(lua_State* L, double x, double y, double dx, double dy);
void pushButton(lua_State* L, uint32_t button, bool pressed, double x, double y, uint32_t t);
void pushWheel(lua_State* L, const char* axis, double delta, int discrete, int source, double x, double y, uint32_t t);
void pushKey(lua_State* L, uint32_t keysym, uint32_t keycode, bool pressed);

} // namespace Hyprexpo::LuaEvents
