#pragma once

// Live reads of the fling/commit tuning keys (plugin:hyprexpo:fling_*).
// Kept OUT of HyprexpoLogic.hpp on purpose: that header must stay free of
// the config module so the pure-logic tests keep linking without it.
#include <algorithm>

#include "ConfigValues.hpp"
#include "HyprexpoConfig.hpp"

namespace Hyprexpo::FlingConfig {

inline float minPxS() {
    return std::max(1.0F, Hyprexpo::ConfigValues::getFloat("plugin:hyprexpo:fling_min_px_s", HyprexpoConfig::FLING_MIN_PX_S_DEFAULT));
}

inline float maxPxS() {
    return std::max(100.0F, Hyprexpo::ConfigValues::getFloat("plugin:hyprexpo:fling_max_px_s", HyprexpoConfig::FLING_MAX_PX_S_DEFAULT));
}

inline float friction() {
    return std::max(0.1F, Hyprexpo::ConfigValues::getFloat("plugin:hyprexpo:fling_friction", HyprexpoConfig::FLING_FRICTION_DEFAULT));
}

inline float stopPxS() {
    return std::max(1.0F, Hyprexpo::ConfigValues::getFloat("plugin:hyprexpo:fling_stop_px_s", HyprexpoConfig::FLING_STOP_PX_S_DEFAULT));
}

inline float windowS() {
    const float w = Hyprexpo::ConfigValues::getFloat("plugin:hyprexpo:fling_window_s", HyprexpoConfig::FLING_WINDOW_S_DEFAULT);
    return std::clamp(w, 0.02F, 0.5F);
}

inline float commitProjectionS() {
    const float t = Hyprexpo::ConfigValues::getFloat("plugin:hyprexpo:fling_commit_projection_s", HyprexpoConfig::FLING_COMMIT_PROJECTION_S_DEFAULT);
    return std::clamp(t, 0.0F, 1.0F);
}

} // namespace Hyprexpo::FlingConfig
