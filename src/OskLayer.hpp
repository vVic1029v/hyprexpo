#pragma once

// On-screen-keyboard layer passthrough.
//
// The expo overview consumes pointer/touch input while open, which would
// otherwise swallow taps on the OSK layer above it. These helpers let the
// input hooks step aside when a point lands on a known keyboard layer
// surface. Key events need no carve-out: virtual-keyboard keys arrive as
// normal key events and the existing search-key path consumes them.
#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/helpers/math/Math.hpp>

namespace Hyprexpo::Osk {

// True when the monitor-global point hits a mapped keyboard layer surface
// (configured namespaces plus anything auto-learned this session).
bool pointHitsKeyboard(PHLMONITOR monitor, const Vector2D& global);

} // namespace Hyprexpo::Osk
