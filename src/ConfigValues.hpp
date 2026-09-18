#pragma once

// Central config-value access for the plugin.
//
// Rule: system/logic code never calls HyprlandAPI::getConfigValue directly;
// it goes through these small typed helpers. Values stay live (no caching)
// so `hyprctl keyword` keeps working, lookups are null-safe (a missing key
// once took down the whole compositor), and every fallback is logged once,
// loudly, at the call site type level.
#include <string>

namespace Hyprexpo::ConfigValues {

int         getInt(const char* key, int fallback);
float       getFloat(const char* key, float fallback);
std::string getString(const char* key, const char* fallback);

} // namespace Hyprexpo::ConfigValues
