// In-exposé app drawer: model, pins, search filter, icon lookup, launch.
// Rendering/input integration lives in Overview{,Render,Interaction}.cpp.
#include "Drawer.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>

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

// Minimal .desktop parser: only [Desktop Entry] keys we need. Localized
// variants are kept under their full key (Name[de_DE]); lookup resolves
// them against the session locale, plain Name last.
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
        const std::string key = trim(line.substr(0, eq));
        if (!out.count(key))
            out[key] = trim(line.substr(eq + 1));
    }
    return out;
}

// Session locale candidates, most preferred first: "de_DE.UTF-8@euro"
// yields de_DE then de. LANGUAGE is already priority-ordered; the LC_*
// and LANG fallbacks contribute a single locale each.
std::vector<std::string> localeCandidates() {
    std::vector<std::string> cands;
    auto pushTag = [&](std::string tag) {
        tag = trim(tag);
        if (tag.empty() || tag == "C" || tag == "POSIX")
            return;
        tag = tag.substr(0, tag.find('@'));
        tag = tag.substr(0, tag.find('.'));
        if (tag.empty())
            return;
        if (std::none_of(cands.begin(), cands.end(), [&](const std::string& c) { return c == tag; }))
            cands.push_back(tag);
        const auto us = tag.find('_');
        if (us != std::string::npos) {
            const std::string lang = tag.substr(0, us);
            if (std::none_of(cands.begin(), cands.end(), [&](const std::string& c) { return c == lang; }))
                cands.push_back(lang);
        }
    };
    if (const char* lang = getenv("LANGUAGE")) {
        std::istringstream in(lang);
        std::string t;
        while (std::getline(in, t, ':'))
            pushTag(t);
    }
    for (const char* k : {"LC_ALL", "LC_MESSAGES", "LANG"}) {
        if (const char* v = getenv(k))
            pushTag(v);
    }
    return cands;
}

// Freedesktop locale lookup: exact locale, bare language, plain key.
// Without this, whichever Name[xx] line a file lists first wins and every
// app shows up in a different random language.
std::string localizedValue(const std::map<std::string, std::string>& m, const std::string& base) {
    static const std::vector<std::string> cands = localeCandidates();
    for (const auto& tag : cands) {
        const auto it = m.find(base + "[" + tag + "]");
        if (it != m.end() && !it->second.empty())
            return it->second;
    }
    const auto it = m.find(base);
    return it != m.end() ? it->second : "";
}

// Flatpak app id from `flatpak run [flags] <appid> ...`, for window
// class matching (the exec basename is always just "flatpak").
std::string flatpakAppId(const std::string& exec) {
    std::istringstream in(exec);
    std::string tok;
    bool seenRun = false;
    while (in >> tok) {
        std::string base = tok;
        const auto slash = base.find_last_of('/');
        if (slash != std::string::npos)
            base = base.substr(slash + 1);
        if (!seenRun) {
            if (base == "flatpak")
                seenRun = true;
            continue;
        }
        if (tok == "run")
            continue;
        if (!tok.empty() && tok[0] == '-') {
            // Only --command takes a separate value; every other bare flag
            // (e.g. --file-forwarding) is boolean and the app id follows.
            if (tok == "--command")
                in >> tok;
            continue;
        }
        return tok;
    }
    return "";
}

// Single-quote a shell word.
std::string shellQuote(const std::string& s) {
    std::string out = "'";
    for (char c : s) {
        if (c == '\'')
            out += "'\\''";
        else
            out += c;
    }
    return out + "'";
}
std::string firstToken(const std::string& s) {
    std::istringstream in(s);
    std::string tok;
    in >> tok;
    return tok;
}

// Spec junk filter: TryExec names a binary the entry needs. Skip entries
// whose binary is gone (uninstalled leftovers, wine phantoms, ...).
bool tryExecExists(const std::string& prog) {
    if (prog.empty())
        return true;
    if (prog.find('/') != std::string::npos)
        return access(prog.c_str(), X_OK) == 0;
    if (const char* path = getenv("PATH")) {
        std::istringstream in(path);
        std::string dir;
        while (std::getline(in, dir, ':')) {
            if (dir.empty())
                continue;
            if (access((dir + "/" + prog).c_str(), X_OK) == 0)
                return true;
        }
        return false;
    }
    return true;
}

bool truthy(const std::map<std::string, std::string>& m, const std::string& key) {
    const auto it = m.find(key);
    return it != m.end() && (it->second == "true" || it->second == "1");
}

// Strip freedesktop Exec field codes (%u %U %f %F %i %c %k %d %D %n %N %v %m)
// and flatpak file-forwarding placeholders (@@u @@f ...): with no files to
// forward they make the launch fail silently.
std::string cleanExec(const std::string& exec) {
    std::ostringstream out;
    bool first = true;
    std::istringstream in(exec);
    std::string tok;
    while (in >> tok) {
        if (tok.size() == 2 && tok[0] == '%' && std::isalpha((unsigned char)tok[1]))
            continue;
        if (tok.size() >= 2 && tok[0] == '@' && tok[1] == '@')
            continue;
        if (!first)
            out << ' ';
        out << tok;
        first = false;
    }
    return out.str();
}

} // namespace

// librsvg is an optional RUNTIME dependency (dlopen, never linked): the
// raster side lives in the addon, but discovery prefers .svg up front so a
// scalable master beats an upscaled bitmap. Absent library -> PNG/XPM only,
// exactly like before.
bool haveSvgLoader() {
    static int cached = -1;
    if (cached < 0) {
        void* handle = dlopen("librsvg-2.so.2", RTLD_LAZY | RTLD_LOCAL);
        cached       = handle ? 1 : 0;
        if (handle)
            dlclose(handle);
    }
    return cached > 0;
}

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
            const auto tryExec = kv.find("TryExec");
            if (tryExec != kv.end() && !tryExecExists(firstToken(tryExec->second)))
                continue;
            const std::string name = localizedValue(kv, "Name");
            const auto execIt      = kv.find("Exec");
            if (name.empty() || execIt == kv.end() || execIt->second.empty())
                continue;
            SApp app;
            app.id   = entry.path().filename().string();
            // de-dupe by id in scan order: user dir comes first, so local
            // files win over system ones with the same id.
            if (std::any_of(apps.begin(), apps.end(), [&](const SApp& u) { return u.id == app.id; }))
                continue;
            app.name = name;
            app.exec = cleanExec(execIt->second);
            const auto wmClass = kv.find("StartupWMClass");
            app.startupWmClass = (wmClass != kv.end()) ? wmClass->second : "";
            app.flatpakAppId   = flatpakAppId(execIt->second);
            const auto workdir = kv.find("Path");
            app.workingDir      = (workdir != kv.end()) ? workdir->second : "";
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
    // Stable order: alphabetical with id tie-break (no runtime swapping),
    // then drop exact (name, exec) duplicates (installer leftovers).
    std::sort(apps.begin(), apps.end(), [](const SApp& a, const SApp& b) {
        const std::string fa = lowerFold(a.name), fb = lowerFold(b.name);
        if (fa != fb)
            return fa < fb;
        return a.id < b.id;
    });
    std::vector<SApp> uniq;
    for (auto& app : apps) {
        const bool dup = std::any_of(uniq.begin(), uniq.end(), [&](const SApp& u) { return u.name == app.name && u.exec == app.exec; });
        if (!dup)
            uniq.push_back(std::move(app));
    }
    return uniq;
}

static std::string dataFile(const char* name) {
    const char* home = getenv("HOME");
    return (home ? std::string(home) : std::string("")) + "/.config/hyprexpo/" + name;
}

static void saveIdList(const char* name, const std::vector<std::string>& ids) {
    const std::string path = dataFile(name);
    std::error_code ec;
    fs::create_directories(fs::path(path).parent_path(), ec);
    std::ofstream f(path, std::ios::trunc);
    if (!f)
        return;
    for (const auto& id : ids)
        f << id << "\n";
}

static std::vector<std::string> loadIdList(const char* name) {
    std::vector<std::string> ids;
    std::ifstream f(dataFile(name));
    std::string line;
    while (std::getline(f, line)) {
        line = trim(line);
        if (!line.empty() && line[0] != '#')
            ids.push_back(line);
    }
    return ids;
}

std::vector<std::string> loadRecent() {
    return loadIdList("drawer-recent");
}

void recordRecent(const std::string& id) {
    if (id.empty())
        return;
    auto ids = loadIdList("drawer-recent");
    ids.erase(std::remove(ids.begin(), ids.end(), id), ids.end());
    ids.insert(ids.begin(), id);
    if (ids.size() > 64)
        ids.resize(64);
    saveIdList("drawer-recent", ids);
}

std::vector<size_t> filterApps(const std::vector<SApp>& apps, const std::string& query, const std::vector<std::string>& recent, int firstRowCount,
                               int& recentShown) {
    recentShown = 0;
    const std::string q = lowerFold(query);
    if (!q.empty()) {
        // Searching: plain alphabetical matches, no sections.
        std::vector<size_t> out;
        for (size_t i = 0; i < apps.size(); ++i) {
            const std::string name = lowerFold(apps[i].name);
            const std::string exec = lowerFold(apps[i].exec);
            if (name.find(q) == std::string::npos && exec.find(q) == std::string::npos)
                continue;
            out.push_back(i);
        }
        return out;
    }
    // Locked layout: exact recent row first, then everything else in scan
    // (alphabetical) order. Holes pad a short recent row so the grid below
    // never shifts when history grows.
    std::vector<size_t> out;
    std::vector<char>   used(apps.size(), 0);
    const int cap = std::max(0, firstRowCount);
    for (const auto& rid : recent) {
        if ((int)out.size() >= cap)
            break;
        for (size_t i = 0; i < apps.size(); ++i) {
            if (!used[i] && apps[i].id == rid) {
                out.push_back(i);
                used[i] = 1;
                break;
            }
        }
    }
    recentShown = (int)out.size();
    if (recentShown > 0) {
        while ((int)out.size() < cap)
            out.push_back(EMPTY_SLOT);
    }
    for (size_t i = 0; i < apps.size(); ++i) {
        if (!used[i])
            out.push_back(i);
    }
    return out;
}

std::string resolveIconPath(const std::string& icon, int minPx) {
    if (icon.empty())
        return "";
    if (icon.front() == '/') {
        std::error_code ec;
        return fs::is_regular_file(icon, ec) ? icon : "";
    }
    // Icon roots mirror the app search dirs (<prefix>/applications sits
    // next to <prefix>/icons), so user themes, system themes, /usr/local
    // and flatpak exports (system + user) are all covered without
    // hardcoding. Plus legacy ~/.icons.
    std::vector<std::string> roots;
    auto addRoot = [&](const std::string& r) {
        if (!r.empty() && std::none_of(roots.begin(), roots.end(), [&](const std::string& u) { return u == r; }))
            roots.push_back(r);
    };
    for (const auto& dir : appSearchDirs()) {
        static const std::string suffix = "/applications";
        if (dir.size() > suffix.size() && dir.compare(dir.size() - suffix.size(), suffix.size(), suffix) == 0) {
            const std::string base = dir.substr(0, dir.size() - suffix.size());
            addRoot(base + "/icons");
            addRoot(base + "/pixmaps");
        }
    }
    if (const char* home = getenv("HOME")) {
        addRoot(std::string(home) + "/.icons");
        addRoot(std::string(home) + "/.local/share/flatpak/exports/share/icons");
    }
    addRoot("/var/lib/flatpak/exports/share/icons");
    // Some files carry their extension; try verbatim first, then themed.
    std::string stem = icon;
    for (const char* ext : {".png", ".xpm", ".svg"}) {
        const size_t len = strlen(ext);
        if (stem.size() > len && stem.compare(stem.size() - len, len, ext) == 0) {
            for (const auto& root : roots) {
                std::error_code ec;
                if (fs::is_regular_file(root + "/" + icon, ec))
                    return root + "/" + icon;
            }
            stem = stem.substr(0, stem.size() - len);
            break;
        }
    }
    static const int SIZES[] = {128, 96, 72, 64, 48, 32, 256, 512, 24, 16};
    static const char* THEMES[] = {"hicolor", "Adwaita", "gnome", "AdwaitaLegacy", "oxygen"};
    // SVG masters first: infinite scale beats any bitmap (needs the
    // runtime loader; otherwise these simply miss and PNGs win).
    if (haveSvgLoader()) {
        for (const auto& root : roots) {
            for (const auto& theme : THEMES) {
                const std::string p = root + "/" + theme + "/scalable/apps/" + stem + ".svg";
                std::error_code ec;
                if (fs::is_regular_file(p, ec))
                    return p;
            }
        }
    }
    auto themedAt = [&](int want, const std::string& stem, std::string& out) {
        const std::string sub = std::to_string(want) + "x" + std::to_string(want);
        for (const auto& root : roots) {
            for (const auto& theme : THEMES) {
                for (const auto& ext : {".png", ".xpm"}) {
                    const std::string p = root + "/" + theme + "/" + sub + "/apps/" + stem + ext;
                    std::error_code ec;
                    if (fs::is_regular_file(p, ec)) {
                        out = p;
                        return true;
                    }
                }
            }
        }
        return false;
    };
    std::string found;
    // largest-first that still meets minPx, so touch tiles stay crisp...
    for (int want : SIZES) {
        if (want < minPx)
            continue;
        if (themedAt(want, stem, found))
            return found;
    }
    // ...then largest-first below it: an upscaled small icon still beats
    // a letter tile (e.g. Steam ships 32px only). SVG-only apps stay on
    // the glyph fallback: no rasterizer on this box by design.
    for (int want : SIZES) {
        if (want >= minPx)
            continue;
        if (themedAt(want, stem, found))
            return found;
    }
    // Loose files directly under the roots (pixmaps-style drop-ins).
    const std::vector<std::string> looseExts =
        haveSvgLoader() ? std::vector<std::string>{".png", ".xpm", ".svg"} : std::vector<std::string>{".png", ".xpm"};
    for (const auto& root : roots) {
        for (const auto& ext : looseExts) {
            const std::string p = root + "/" + stem + ext;
            std::error_code ec;
            if (fs::is_regular_file(p, ec))
                return p;
        }
    }
    // AdwaitaLegacy keeps app icons one level deeper (legacy/).
    for (const auto& root : roots) {
        for (int want : SIZES) {
            const std::string sub = std::to_string(want) + "x" + std::to_string(want);
            for (const auto& ext : {".png", ".xpm"}) {
                const std::string p = root + "/AdwaitaLegacy/" + sub + "/legacy/" + stem + ext;
                std::error_code ec;
                if (fs::is_regular_file(p, ec))
                    return p;
            }
        }
    }
    return "";
}

// Terminal=true apps need a host terminal. First present wins; each entry
// knows its own exec flag style.
std::string terminalWrap(const std::string& cmd) {
    struct T {
        const char* bin;
        const char* prefix;
    };
    static const T chain[] = {{"alacritty", "alacritty -e "}, {"ghostty", "ghostty -e "}, {"kitty", "kitty "}};
    if (const char* env = getenv("TERMINAL"); env && *env)
        return std::string(env) + " -e " + cmd;
    for (const auto& t : chain) {
        if (tryExecExists(t.bin))
            return std::string(t.prefix) + cmd;
    }
    return cmd; // no terminal found: launch raw, may fail silently
}

void launchApp(const SApp& app) {
    // system() reaps the wrapper itself; the app reparents to init.
    // (Raw fork() here would leak zombies: nothing wait()s plugin children,
    // and flipping SIGCHLD globally could break the compositor's own reaping.)
    std::string cmd = app.exec;
    if (app.terminal)
        cmd = terminalWrap(cmd);
    // Honor Path=: CWD-dependent launchers (wine/proton wrappers) fail
    // silently from the compositor's working directory otherwise.
    if (!app.workingDir.empty())
        cmd = "cd " + shellQuote(app.workingDir) + " && " + cmd;
    std::system(("(" + cmd + ") >/dev/null 2>&1 &").c_str());
}

} // namespace Hyprexpo::Drawer
