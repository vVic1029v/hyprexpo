#include "ConfigValues.hpp"
#include "globals.hpp"

#include <hyprland/src/config/ConfigValue.hpp>
#include <hyprland/src/debug/log/Logger.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>

namespace Hyprexpo::ConfigValues {

int getInt(const char* key, int fallback) {
    const auto raw = HyprlandAPI::getConfigValue(PHANDLE, key);
    if (!raw) {
        Log::logger->log(Log::ERR, "[hyprexpo] config key missing: {}", key);
        return fallback;
    }
    auto* val = (Hyprlang::INT* const*)raw->getDataStaticPtr();
    if (!val || !*val) {
        Log::logger->log(Log::ERR, "[hyprexpo] config value null: {}", key);
        return fallback;
    }
    return (int)**val;
}

float getFloat(const char* key, float fallback) {
    const auto raw = HyprlandAPI::getConfigValue(PHANDLE, key);
    if (!raw) {
        Log::logger->log(Log::ERR, "[hyprexpo] config key missing: {}", key);
        return fallback;
    }
    auto* val = (Hyprlang::FLOAT* const*)raw->getDataStaticPtr();
    if (!val || !*val) {
        Log::logger->log(Log::ERR, "[hyprexpo] config value null: {}", key);
        return fallback;
    }
    return (float)**val;
}

std::string getString(const char* key, const char* fallback) {
    const auto raw = HyprlandAPI::getConfigValue(PHANDLE, key);
    if (!raw) {
        Log::logger->log(Log::ERR, "[hyprexpo] config key missing: {}", key);
        return fallback ? fallback : "";
    }
    auto* val = (Hyprlang::STRING const*)raw->getDataStaticPtr();
    if (!val) {
        Log::logger->log(Log::ERR, "[hyprexpo] config value null: {}", key);
        return fallback ? fallback : "";
    }
    return std::string{*val};
}

} // namespace Hyprexpo::ConfigValues
