// In-exposé app drawer: model, pins, search filter, icon lookup, launch.
// Rendering/input integration lives in Overview{,Render,Interaction}.cpp.
#include "Drawer.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace fs = std::filesystem;

namespace Hyprexpo::Drawer {

namespace {

std::string trim(const std::string& s) {
    const auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos)
        return "";
    const auto e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

// Minimal .desktop parser: only [Desktop Entry] keys we need.
std::map<std::string, std::string> parseDesktopFile(const std::string& path) {
    std::map<std::string, std::string> out;
    std::ifstream f(path);
    if (!f)
        return out;
    bool inEntry = false;
    std::string line;
    while (std::getline(f, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#')
            continue;
        if (line.front() == '[') {
            inEntry = (line == "[Desktop Entry]");
            continue;
        }
        if (!inEntry)
            continue;
        const auto eq = line.find('=');
        if (eq == std::string::npos)
            continue;
        // localized keys (Name[en_US]) lose to the plain key: first wins
        const std::string key = trim(line.substr(0, eq));
        if (key.find('[') != std::string::npos && out.count(key.substr(0, key.find('['))))
            continue;
        const std::string base = key.substr(0, key.find('['));
        if (!out.count(base))
            out[base] = trim(line.substr(eq + 1));
    }
    return out;
}

bool truthy(const std::map<std::string, std::string>& m, const std::string& key) {
    const auto it = m.find(key);
    return it != m.end() && (it->second == "true" || it->second == "1");
}

// Strip freedesktop Exec field codes (%u %U %f %F %i %c %k %d %D %n %N %v %m).
std::string cleanExec(const std::string& exec) {
    std::ostringstream out;
    bool first = true;
    std::istringstream in(exec);
    std::string tok;
    while (in >> tok) {
        if (tok.size() == 2 && tok[0] == '%' && std::isalpha((unsigned char)tok[1]))
            continue;
        if (!first)
            out << ' ';
        out << tok;
        first = false;
    }
    return out.str();
}

} // namespace

std::string lowerFold(const std::string& in) {
    std::string out = in;
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return std::tolower(c); });
    return out;
}

std::vector<std::string> appSearchDirs() {
    std::vector<std::string> dirs;
    if (const char* home = getenv("HOME"))
        dirs.push_back(std::string(home) + "/.local/share/applications");
    if (const char* xdg = getenv("XDG_DATA_DIRS")) {
        std::istringstream in(xdg);
        std::string d;
        while (std::getline(in, d, ':')) {
            if (!d.empty())
                dirs.push_back(d + "/applications");
        }
    } else {
        dirs.push_back("/usr/local/share/applications");
        dirs.push_back("/usr/share/applications");
    }
    return dirs;
}

std::vector<SApp> scanApps() {
    std::vector<SApp> apps;
    for (const auto& dir : appSearchDirs()) {
        std::error_code ec;
        if (!fs::is_directory(dir, ec))
            continue;
        for (const auto& entry : fs::directory_iterator(dir, ec)) {
            if (ec)
                break;
            if (entry.path().extension() != ".desktop")
                continue;
            const auto kv = parseDesktopFile(entry.path().string());
            const auto type = kv.find("Type");
            if (type == kv.end() || type->second != "Application")
                continue;
            if (truthy(kv, "NoDisplay") || truthy(kv, "Hidden"))
                continue;
            const auto name = kv.find("Name");
            const auto exec = kv.find("Exec");
            if (name == kv.end() || name->second.empty() || exec == kv.end() || exec->second.empty())
                continue;
            SApp app;
            app.id   = entry.path().filename().string();
            app.name = name->second;
            app.exec = cleanExec(exec->second);
            const auto icon = kv.find("Icon");
            app.icon = (icon != kv.end()) ? icon->second : "";
            app.terminal = truthy(kv, "Terminal");
            const auto cats = kv.find("Categories");
            if (cats != kv.end()) {
                std::istringstream in(cats->second);
                std::string c;
                while (std::getline(in, c, ';')) {
                    c = trim(c);
                    if (!c.empty())
                        app.categories.push_back(c);
                }
            }
            apps.push_back(std::move(app));
        }
    }
    std::sort(apps.begin(), apps.end(), [](const SApp& a, const SApp& b) { return lowerFold(a.name) < lowerFold(b.name); });
    // de-dupe by id, user dir won earlier so keep first hit
    std::vector<SApp> uniq;
    for (auto& app : apps) {
        if (std::none_of(uniq.begin(), uniq.end(), [&](const SApp& u) { return u.id == app.id; }))
            uniq.push_back(std::move(app));
    }
    return uniq;
}

static std::string pinsPath() {
    const char* home = getenv("HOME");
    return (home ? std::string(home) : std::string("")) + "/.config/hyprexpo/drawer-pins";
}

std::vector<std::string> loadPins() {
    std::vector<std::string> ids;
    std::ifstream f(pinsPath());
    std::string line;
    while (std::getline(f, line)) {
        line = trim(line);
        if (!line.empty() && line[0] != '#')
            ids.push_back(line);
    }
    return ids;
}

void savePins(const std::vector<std::string>& ids) {
    const std::string path = pinsPath();
    std::error_code ec;
    fs::create_directories(fs::path(path).parent_path(), ec);
    std::ofstream f(path, std::ios::trunc);
    if (!f)
        return;
    for (const auto& id : ids)
        f << id << "\n";
}

std::vector<size_t> filterApps(const std::vector<SApp>& apps, const std::vector<std::string>& pins, const std::string& query) {
    const std::string q = lowerFold(query);
    std::vector<size_t> pinned, rest;
    for (size_t i = 0; i < apps.size(); ++i) {
        if (!q.empty()) {
            const std::string name = lowerFold(apps[i].name);
            const std::string exec = lowerFold(apps[i].exec);
            if (name.find(q) == std::string::npos && exec.find(q) == std::string::npos)
                continue;
        }
        // pin-file order first
        auto pit = std::find(pins.begin(), pins.end(), apps[i].id);
        if (pit != pins.end())
            pinned.push_back(i);
        else
            rest.push_back(i);
    }
    // pinned entries follow pin-file order, rest stay alphabetical (scan order)
    std::sort(pinned.begin(), pinned.end(), [&](size_t a, size_t b) {
        return std::find(pins.begin(), pins.end(), apps[a].id) < std::find(pins.begin(), pins.end(), apps[b].id);
    });
    pinned.insert(pinned.end(), rest.begin(), rest.end());
    return pinned;
}

std::string resolveIconPath(const std::string& icon, int minPx) {
    if (icon.empty())
        return "";
    if (icon.front() == '/') {
        std::error_code ec;
        return fs::is_regular_file(icon, ec) ? icon : "";
    }
    std::vector<std::string> roots;
    if (const char* home = getenv("HOME")) {
        roots.push_back(std::string(home) + "/.local/share/icons");
        roots.push_back(std::string(home) + "/.icons");
    }
    roots.push_back("/usr/share/icons");
    roots.push_back("/usr/share/pixmaps");
    static const int SIZES[] = {128, 96, 72, 64, 48, 32, 256, 512, 24, 16};
    // largest-first that still meets minPx, so touch tiles stay crisp
    for (int want : SIZES) {
        if (want < minPx)
            continue;
        const std::string sub = std::to_string(want) + "x" + std::to_string(want);
        for (const auto& root : roots) {
            for (const auto& theme : {"hicolor", "Adwaita", "gnome"}) {
                for (const auto& ext : {".png", ".xpm"}) {
                    const std::string p = root + "/" + theme + "/" + sub + "/apps/" + icon + ext;
                    std::error_code ec;
                    if (fs::is_regular_file(p, ec))
                        return p;
                }
            }
        }
    }
    for (const auto& root : roots) {
        for (const auto& ext : {".png", ".xpm"}) {
            const std::string p = root + "/" + icon + ext;
            std::error_code ec;
            if (fs::is_regular_file(p, ec))
                return p;
        }
    }
    return "";
}

void launchApp(const SApp& app) {
    // system() reaps the wrapper itself; the app reparents to init.
    // (Raw fork() here would leak zombies: nothing wait()s plugin children,
    // and flipping SIGCHLD globally could break the compositor's own reaping.)
    std::string cmd = app.exec;
    if (app.terminal)
        cmd = "alacritty -e " + cmd; // user terminal; documented in DOTS.md
    std::system(("(" + cmd + ") >/dev/null 2>&1 &").c_str());
}

} // namespace Hyprexpo::Drawer
