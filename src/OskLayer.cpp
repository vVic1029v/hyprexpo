#include "OskLayer.hpp"
#include "ConfigValues.hpp"
#include "HyprexpoLogic.hpp"
#include "globals.hpp"

#include <hyprland/src/debug/log/Logger.hpp>
#include <hyprland/src/output/Monitor.hpp>
#include <hyprland/src/desktop/view/LayerSurface.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>

#include <algorithm>
#include <optional>
#include <sstream>
#include <vector>

namespace Hyprexpo::Osk {
namespace {

// Namespaces adopted at runtime (see below). Process lifetime is fine:
// layers are re-enumerated per call, this only remembers names.
std::vector<std::string>& learnedNamespaces() {
    static std::vector<std::string> learned;
    return learned;
}

std::vector<std::string> splitNames(const std::string& csv) {
    std::vector<std::string> out;
    std::istringstream in(csv);
    std::string tok;
    while (std::getline(in, tok, ',')) {
        const auto b = tok.find_first_not_of(" \t");
        if (b == std::string::npos)
            continue;
        const auto e = tok.find_last_not_of(" \t");
        out.push_back(tok.substr(b, e - b + 1));
    }
    return out;
}

} // namespace

bool pointHitsKeyboard(PHLMONITOR monitor, const Vector2D& global) {
    if (!monitor)
        return false;
    const auto configured = splitNames(ConfigValues::getString("plugin:hyprexpo:osk_namespaces", "wvkbd"));
    auto&      learned    = learnedNamespaces();

    const double monW = monitor->m_size.x;
    const double monH = monitor->m_size.y;
    const double lx   = global.x - monitor->m_position.x;
    const double ly   = global.y - monitor->m_position.y;

    std::vector<Hyprexpo::Osk::SLayerCandidate> layers;
    for (const auto& level : monitor->m_layerSurfaceLayers) {
        for (const auto& weak : level) {
            const auto surf = weak.lock();
            if (!surf || !surf->m_mapped)
                continue;
            const CBox box = surf->logicalBox().value_or(surf->m_geometry);
            layers.push_back({surf->m_namespace, box.x, box.y, box.w, box.h});
        }
    }

    const auto result =
        Hyprexpo::Osk::scanLayers(configured, learned, layers, monW, monH, global.x, global.y, lx, ly);
    if (!result.learnedNs.empty() &&
        std::find(learned.begin(), learned.end(), result.learnedNs) == learned.end()) {
        learned.push_back(result.learnedNs);
        Log::logger->log(Log::ERR, "[hyprexpo] OSK auto-learn: '{}' (add osk_namespaces = {} to keep it)", result.learnedNs,
                         result.learnedNs);
    }
    return result.hit;
}

} // namespace Hyprexpo::Osk
