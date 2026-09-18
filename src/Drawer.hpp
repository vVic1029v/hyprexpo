#pragma once

// In-expose app drawer: model, recents, search filter, icon lookup.
// Rendering + input live on COverview (see Drawer.cpp sections); this file
// owns everything that does not need the compositor.
//
// Layout contract (locked): with an empty query the first row holds the
// most recently launched apps (recency order, exact row, holes left empty),
// then a padded gap, then everything else strictly alphabetical. Searching
// shows plain alphabetical matches. Nothing but the query reshuffles.
#include <cstddef>
#include <map>
#include <string>
#include <vector>

#include <hyprland/src/devices/IKeyboard.hpp>

namespace Hyprexpo::Drawer {

// Empty first-row slot: the recent row keeps its exact shape.
inline constexpr size_t EMPTY_SLOT = static_cast<size_t>(-1);

struct SApp {
    std::string              id;       // desktop file id, e.g. firefox.desktop
    std::string              name;     // display name
    std::string              exec;     // cleaned exec line (no % codes)
    std::string              icon;     // icon name or absolute path
    bool                     terminal  = false;
    std::vector<std::string> categories;
};

// XDG app dirs, user dir first so local .desktop files win.
std::vector<std::string> appSearchDirs();
// All launchable apps. Rescanned per overview open (cheap: ~100 files).
std::vector<SApp> scanApps();
// Launch history (~/.config/hyprexpo/drawer-recent): desktop ids,
// most-recent-first. Recorded on every drawer launch.
std::vector<std::string> loadRecent();
void                     recordRecent(const std::string& id);
// Filtered view. Empty query: recent row (exact firstRowCount slots,
// EMPTY_SLOT holes) + padded rest in scan (alphabetical) order.
// Non-empty query: plain alphabetical matches.
std::vector<size_t> filterApps(const std::vector<SApp>& apps, const std::string& query, const std::vector<std::string>& recent, int firstRowCount,
                               int& recentShown);
// Icon lookup: name -> absolute PNG path (hicolor/Adwaita/pixmaps walk).
// Empty when only SVG themed icons exist (no librsvg headers on this box).
std::string resolveIconPath(const std::string& icon, int minPx);
// Routes printable keys / editing keys to the focused drawer search box.
// Returns true when consumed. Latin-only v1: no IME.
bool drawerSearchKey(const IKeyboard::SKeyEvent& event);
// Launch: fork + sh -c, Terminal=true apps wrapped in the user terminal.
void launchApp(const SApp& app);

} // namespace Hyprexpo::Drawer
