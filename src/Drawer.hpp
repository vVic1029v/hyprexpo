#pragma once

// In-expose app drawer: model, pins, search filter, icon lookup.
// Rendering + input live on COverview (see Drawer.cpp sections); this file
// owns everything that does not need the compositor.
#include <map>
#include <string>
#include <vector>

#include <hyprland/src/devices/IKeyboard.hpp>

namespace Hyprexpo::Drawer {

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
// Pins file (~/.config/hyprexpo/drawer-pins): desktop ids, one per line.
std::vector<std::string> loadPins();
void                     savePins(const std::vector<std::string>& ids);
// Filtered view: pinned (pin-file order) first, then the rest by name.
std::vector<size_t> filterApps(const std::vector<SApp>& apps, const std::vector<std::string>& pins, const std::string& query);
// Icon lookup: name -> absolute PNG path (hicolor/Adwaita/pixmaps walk).
// Empty when only SVG themed icons exist (no librsvg headers on this box).
std::string resolveIconPath(const std::string& icon, int minPx);
// Routes printable keys / editing keys to the focused drawer search box.
// Returns true when consumed. Latin-only v1: no IME.
bool drawerSearchKey(const IKeyboard::SKeyEvent& event);
// Launch: fork + sh -c, Terminal=true apps wrapped in the user terminal.
void launchApp(const SApp& app);

} // namespace Hyprexpo::Drawer
