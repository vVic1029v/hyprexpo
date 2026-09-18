// In-expose app drawer: layout, model glue, textures, render pass, input.
// Touch/mouse gesture routing lives with the other input code (Overview.cpp
// hooks + COverview::touchPress*/mouse handlers); this file owns everything
// the drawer draws or remembers. Pure model bits (scan/filter/pins/icons)
// live in Drawer.{hpp,cpp} with no compositor dependency.
#include "Overview.hpp"
#include "OverviewInternal.hpp"
#include "ConfigValues.hpp"
#include "Drawer.hpp"

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/config/ConfigValue.hpp>
#include <hyprland/src/debug/log/Logger.hpp>
#include <hyprland/src/devices/IKeyboard.hpp>
#include <hyprland/src/managers/SeatManager.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>
#define private   public
#define protected public
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/output/Monitor.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#undef private
#undef protected

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <cairo/cairo.h>
#include <xkbcommon/xkbcommon.h>

#include <algorithm>
#include <chrono>
#include <cmath>

namespace {

// Drawer tunables, read live through the config module (no caching, so
// `hyprctl keyword` keeps working). Clamp here so geometry never inverts.
using Hyprexpo::ConfigValues::getFloat;
using Hyprexpo::ConfigValues::getInt;

int drawerEnabled() {
    return getInt("plugin:hyprexpo:drawer_enable", 1);
}
int drawerCfgCols() {
    return std::max(1, getInt("plugin:hyprexpo:drawer_columns", 5));
}
int drawerCfgSearchH() {
    return std::max(32, getInt("plugin:hyprexpo:drawer_search_h", 64));
}
int drawerCfgExpandPx() {
    return std::max(20, getInt("plugin:hyprexpo:drawer_expand_px", 60));
}
int drawerCfgIconPx() {
    return std::max(32, getInt("plugin:hyprexpo:drawer_icon_px", 72));
}
float drawerCfgResist() {
    return std::clamp(getFloat("plugin:hyprexpo:drawer_resist", 0.25F), 0.0F, 1.0F);
}
// Rubber-band overshoot cap for deep (non-closing) pulls.
constexpr double RESIST_CAP_PX = 28.0;
// Breathing room between the recent row and the locked grid below it.
constexpr double RECENT_PAD_PX = 20.0;
// Wheel/touchpad detent: without a release event, a push must travel past
// the threshold AND keep pushing against this wall to commit. Anything
// shorter springs back on idle instead of toggling on a small flick.
constexpr double COMMIT_OVERSHOOT_PX = 24.0;

double smooth01(double t) {
    t = std::clamp(t, 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

SP<Render::ITexture> uploadCairoSurface(cairo_surface_t* surf) {
    return g_pHyprRenderer->createTexture(surf);
}

} // namespace

// --- geometry (logical px, monitor-local) ---
// ribbonH() lives in Overview.cpp next to the tile math it mirrors.

double COverview::searchH() const {
    return (double)drawerCfgSearchH();
}

double COverview::searchTop() const {
    // Docked: strip sits just above the single bottom row. Fitted: top.
    // The search strip is part of the sheet: it rides pullVisual exactly
    // like the app grid below it.
    const auto MON = pMonitor.lock();
    const double H = MON ? MON->m_size.y : 0.0;
    const double dockedY = H - 16.0 - drawerRowH() - 12.0 - searchH();
    const double fittedY = 24.0;
    return dockedY + (fittedY - dockedY) * smooth01(drawer.anim) + drawer.pullVisual;
}

double COverview::drawerTop() const {
    // Docked: one pinned row pinned to the bottom edge (rest of the grid
    // lives below the screen and scrolls up into view when fitted).
    // pullVisual shifts the sheet with the finger while a pull drag is
    // in flight (or springing back); otherwise it is zero.
    const auto MON = pMonitor.lock();
    const double H = MON ? MON->m_size.y : 0.0;
    const double dockedY = H - 16.0 - drawerRowH();
    const double fittedY = 24.0 + searchH() + 16.0;
    return dockedY + (fittedY - dockedY) * smooth01(drawer.anim) + drawer.pullVisual;
}

// Full travel of the sheet between docked and fitted (always >= 0).
double COverview::drawerPullSpan() const {
    const auto MON = pMonitor.lock();
    const double H = MON ? MON->m_size.y : 0.0;
    const double dockedY = H - 16.0 - drawerRowH();
    const double fittedY = 24.0 + searchH() + 16.0;
    return std::max(0.0, dockedY - fittedY);
}

// Commit travel: a quarter of the screen height. A fixed 60px threshold
// felt like ~20px of intent on touch; a pull only counts when it covers
// real distance. drawer_expand_px remains as a floor for tiny outputs.
double COverview::drawerPullThreshold() const {
    const auto MON = pMonitor.lock();
    const double H = MON ? MON->m_size.y : 0.0;
    return std::max(H / 4.0, (double)drawerCfgExpandPx());
}

COverview::ERegion COverview::regionAtPoint(const Vector2D& local) const {
    const auto MON = pMonitor.lock();
    if (!drawerEnabled() || !MON)
        return ERegion::None;
    const double H = MON->m_size.y;
    if (local.x < 0 || local.x >= MON->m_size.x || local.y < 0 || local.y >= H)
        return ERegion::None;
    if (drawer.anim < 0.5f) {
        if (local.y < ribbonH())
            return ERegion::Ribbon;
    }
    const double sTop = searchTop();
    if (local.y >= sTop && local.y < sTop + searchH())
        return ERegion::Search;
    if (local.y >= drawerTop() && local.y < drawerTop() + drawerClipH())
        return ERegion::Grid;
    return ERegion::None;
}

int COverview::drawerCols() const {
    return drawerCfgCols();
}

double COverview::drawerTileW() const {
    const auto MON = pMonitor.lock();
    const double W = MON ? MON->m_size.x : 0.0;
    const int    cols = drawerCols();
    return (W - 2.0 * 48.0 - (double)(cols - 1) * 16.0) / (double)cols;
}

double COverview::drawerRowH() const {
    return (double)drawerCfgIconPx() + 56.0; // icon + label + padding
}

double COverview::drawerClipH() const {
    const auto MON = pMonitor.lock();
    const double H = MON ? MON->m_size.y : 0.0;
    const double docked = drawerRowH();
    const double fitted = std::max(docked, H - drawerTop() - 16.0);
    const double t      = smooth01(drawer.anim);
    return docked + (fitted - docked) * t;
}

double COverview::drawerContentH() const {
    const size_t rows = (drawer.order.size() + (size_t)drawerCols() - 1) / (size_t)drawerCols();
    if (rows == 0)
        return 0.0;
    return (double)rows * drawerRowH() + (double)(rows - 1) * 12.0 + (drawerRecentPad() ? RECENT_PAD_PX : 0.0);
}

double COverview::drawerMaxScroll() const {
    return std::max(0.0, drawerContentH() - drawerClipH());
}

// Padded gap after the recent row: only with an empty query, a shown
// recent row, and more rows below it.
bool COverview::drawerRecentPad() const {
    if (!drawer.query.empty() || drawer.recentShown <= 0 || drawer.order.empty())
        return false;
    const size_t rows = (drawer.order.size() + (size_t)drawerCols() - 1) / (size_t)drawerCols();
    return rows > 1;
}

int COverview::drawerAppAt(const Vector2D& local) const {
    const auto MON = pMonitor.lock();
    if (!drawerEnabled() || !MON || drawer.order.empty())
        return -1;
    const double tw = drawerTileW();
    const double rh = drawerRowH();
    const double lx = local.x - 48.0;
    if (lx < 0)
        return -1;
    const int col = (int)(lx / (tw + 16.0));
    if (col < 0 || col >= drawerCols() || lx - col * (tw + 16.0) > tw)
        return -1;
    const double ly = local.y - (drawerTop() - drawer.scroll);
    if (ly < 0 || ly >= drawerClipH())
        return -1;
    // Row 0 is exactly the recent row; the padded gap after it is dead.
    int    row;
    double within;
    if (drawerRecentPad() && ly >= rh) {
        const double afterFirst = ly - (rh + 12.0 + RECENT_PAD_PX);
        if (afterFirst < 0)
            return -1;
        row    = 1 + (int)(afterFirst / (rh + 12.0));
        within = afterFirst - (double)(row - 1) * (rh + 12.0);
    } else {
        row    = (int)(ly / (rh + 12.0));
        within = ly - (double)row * (rh + 12.0);
    }
    if (within < 0 || within > rh)
        return -1;
    const size_t idx = (size_t)row * (size_t)drawerCols() + (size_t)col;
    if (idx >= drawer.order.size() || drawer.order[idx] == Hyprexpo::Drawer::EMPTY_SLOT)
        return -1;
    return (int)idx;
}

CBox COverview::drawerTileBox(int orderIdx) const {
    if (orderIdx < 0 || orderIdx >= (int)drawer.order.size() || drawer.order[orderIdx] == Hyprexpo::Drawer::EMPTY_SLOT)
        return CBox{{0, 0}, {0, 0}};
    const double tw = drawerTileW();
    const double rh = drawerRowH();
    const int    row = orderIdx / drawerCols();
    const int    col = orderIdx % drawerCols();
    double       y   = drawerTop() + (double)row * (rh + 12.0) - drawer.scroll;
    if (drawerRecentPad() && row >= 1)
        y += RECENT_PAD_PX;
    return CBox{{48.0 + col * (tw + 16.0), y}, {tw, rh}};
}

// --- model glue ---

void COverview::drawerRescan() {
    drawer.apps  = Hyprexpo::Drawer::scanApps();
    drawer.recent = Hyprexpo::Drawer::loadRecent();
    drawer.query.clear();
    drawer.scroll        = 0;
    drawer.lastPullS     = 0;
    drawer.pullVisual    = 0;
    drawer.pulling       = false;
    drawer.pullEngaged   = false;
    drawer.recentShown   = 0;
    drawer.hoverApp      = -1;
    drawer.searchFocused = false;
    drawer.queryDirty    = true;
    drawerClearCaches();
    drawerRefilter();
}

void COverview::drawerRefilter() {
    drawer.order = Hyprexpo::Drawer::filterApps(drawer.apps, drawer.query, drawer.recent, drawerCols(), drawer.recentShown);
    drawer.scroll = std::clamp(drawer.scroll, 0.0, drawerMaxScroll());
    if (drawer.hoverApp >= (int)drawer.order.size())
        drawer.hoverApp = -1;
    drawer.queryDirty = true;
    damage();
}

void COverview::drawerClearCaches() {
    drawer.iconTex.clear();
    drawer.labelTex.clear();
    drawer.searchTex.reset();
}

void COverview::drawerSetFitted(bool fitted) {
    if (drawer.fitted == fitted)
        return;
    drawer.fitted = fitted;
    drawer.scroll = 0;
    drawer.lastPullS   = 0;
    drawer.pullVisual  = 0;
    drawer.pulling     = false;
    drawer.pullEngaged = false;
    if (!fitted)
        drawer.searchFocused = false;
    damage();
}

double COverview::drawerPullStamp() {
    const double now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    drawer.lastPullS = now;
    return now;
}

// Scrolls the app list by a downward-positive displacement. Returns the
// overshoot pushed past the top of the list (down positive, else 0): the
// caller turns overshoot into sheet pull or rubber-band resistance.
double COverview::drawerListScroll(double fingerDy) {
    const double before = drawer.scroll;
    drawer.scroll       = std::clamp(before - fingerDy, 0.0, drawerMaxScroll());
    return fingerDy > 0.0 ? std::max(0.0, fingerDy - before) : 0.0;
}

// fingerDy: wheel/touchpad displacement, down positive, already scaled
// for the source (see the axis hook). Wheels have no release event, so
// they ride the same visual pull as fingers, plus a detent: a push must
// travel past the threshold AND keep pushing against the wall to commit.
// Anything shorter visibly springs back once the burst goes idle.
void COverview::drawerScrollBy(double fingerDy) {
    if (closing)
        return;
    drawerPullStamp();
    const double threshold = drawerPullThreshold();
    const double span      = drawerPullSpan();
    if (span <= 0.0)
        return;
    if (!drawer.fitted) {
        if (fingerDy >= 0.0) {
            drawer.pullVisual = 0.0;
            return;
        }
        if (drawer.pullVisual <= -(threshold + COMMIT_OVERSHOOT_PX))
            commitDrawerPull(true, span);
        else
            drawer.pullVisual = std::clamp(drawer.pullVisual + fingerDy, -(threshold + COMMIT_OVERSHOOT_PX), 0.0);
        damage();
        return;
    }
    const double before = drawer.scroll;
    const double over   = drawerListScroll(fingerDy);
    // Engagement is scroll position, never screen position: only a push
    // that starts within the top row may close. Deeper pushes just scroll.
    if (fingerDy > 0.0 && over > 0.0 && before <= drawerRowH()) {
        if (drawer.pullVisual >= threshold + COMMIT_OVERSHOOT_PX)
            commitDrawerPull(false, span);
        else
            drawer.pullVisual = std::clamp(drawer.pullVisual + over, 0.0, threshold + COMMIT_OVERSHOOT_PX);
    } else {
        drawer.pullVisual = 0.0;
    }
    damage();
}

// Grid press: latch whether THIS drag may commit open/close. The latch is
// the drawer's scroll position, not the touch point: a drag that starts
// with the first row visible ("at the top") may close; one started
// scrolled deeper can only scroll or resist, never close by mistake.
// Never clears pullVisual: a second finger (or driver re-press) joining a
// live pull continues it instead of snapping the sheet out from under the
// first finger.
void COverview::drawerPullBegin() {
    if (closing)
        return;
    drawer.pulling     = false;
    drawer.pullEngaged = !drawer.fitted || drawer.scroll <= drawerRowH();
    drawerPullStamp();
}

// Commit an open/close pull, seeding the snap animation from the live
// sheet position so there is no jump.
void COverview::commitDrawerPull(bool open, double span) {
    if (span <= 0.0)
        return;
    if (open)
        drawer.anim = std::clamp((float)(-drawer.pullVisual / span), 0.0F, 1.0F);
    else
        drawer.anim = 1.0F - std::clamp((float)(drawer.pullVisual / span), 0.0F, 1.0F);
    drawer.pullVisual = 0;
    drawerSetFitted(open);
}

// Pull path (mouse/touch drags): the sheet follows the finger via
// pullVisual; the open/close decision happens on release (drawerDragEnd).
void COverview::drawerPullBy(double fingerDy) {
    if (closing)
        return;
    drawerPullStamp();
    const double span = drawerPullSpan();
    if (span <= 0.0)
        return;
    if (!drawer.fitted) {
        // Docked: any Grid press may pull the sheet up to open.
        drawer.pulling     = true;
        drawer.pullEngaged = true;
        drawer.pullVisual  = std::clamp(drawer.pullVisual + fingerDy, -span, 0.0);
        damage();
        return;
    }
    drawer.pulling = true;
    if (drawer.pullEngaged) {
        // Drag started at the top: the list absorbs what it can, the
        // overshoot pulls the sheet toward close.
        const double over = drawerListScroll(fingerDy);
        if (over > 0.0)
            drawer.pullVisual = std::clamp(drawer.pullVisual + over, 0.0, span);
        else
            drawer.pullVisual = 0.0;
        damage();
        return;
    }
    // Drag started scrolled down: plain list scroll, plus rubber-band
    // resistance when pushing past the top. Never accumulates a close.
    const double over = drawerListScroll(fingerDy);
    if (over > 0.0)
        drawer.pullVisual = std::clamp(drawer.pullVisual + over * (double)drawerCfgResist(), 0.0, RESIST_CAP_PX);
    else
        drawer.pullVisual = 0.0;
    damage();
}

// Release: pulled far enough -> animate to the new state; otherwise the
// sheet springs back (drawerStepAnim decays pullVisual to zero).
void COverview::drawerDragEnd(bool commit) {
    if (!drawer.pulling)
        return;
    drawer.pulling = false;
    const double threshold = drawerPullThreshold();
    const double span      = drawerPullSpan();
    if (commit && drawer.pullEngaged && span > 0.0) {
        if (!drawer.fitted && -drawer.pullVisual >= threshold) {
            commitDrawerPull(true, span);
            return;
        }
        if (drawer.fitted && drawer.pullVisual >= threshold) {
            commitDrawerPull(false, span);
            return;
        }
    }
    drawer.pullEngaged = false;
    drawer.lastPullS   = 0; // snap back immediately, don't wait out the idle window
    damage();
}

void COverview::drawerTap(const Vector2D& local) {
    if (regionAtPoint(local) == ERegion::Search) {
        drawer.searchFocused = true;
        damage();
        return;
    }
    const int idx = drawerAppAt(local);
    if (idx < 0) {
        drawer.searchFocused = false;
        damage();
        return;
    }
    drawerLaunch((size_t)idx);
}

void COverview::drawerLaunch(size_t orderIdx) {
    if (orderIdx >= drawer.order.size() || drawer.order[orderIdx] == Hyprexpo::Drawer::EMPTY_SLOT)
        return;
    // Record first: this launch tops the recent row next open. Launch
    // itself is detached; close() below destroys this session.
    Hyprexpo::Drawer::recordRecent(drawer.apps[drawer.order[orderIdx]].id);
    Hyprexpo::Drawer::launchApp(drawer.apps[drawer.order[orderIdx]]);
    drawer.searchFocused = false;
    close(false);
}

void COverview::drawerTypeText(const std::string& text) {
    if (!drawer.searchFocused || text.empty())
        return;
    if (drawer.query.size() + text.size() > 64)
        return;
    drawer.query += text;
    drawerRefilter();
}

void COverview::drawerBackspace() {
    if (!drawer.searchFocused || drawer.query.empty())
        return;
    drawer.query.pop_back();
    drawerRefilter();
}

void COverview::drawerClearQuery() {
    if (drawer.query.empty())
        return;
    drawer.query.clear();
    drawerRefilter();
}

bool COverview::drawerConfirmTop() {
    if (!drawer.searchFocused || drawer.order.empty())
        return false;
    drawerLaunch(0);
    return true;
}

// --- textures ---

SP<Render::ITexture> COverview::drawerIconTexture(const Hyprexpo::Drawer::SApp& app, int px, double scale) {
    const std::string key = app.id;
    auto it = drawer.iconTex.find(key);
    if (it != drawer.iconTex.end())
        return it->second;
    SP<Render::ITexture> tex;
    const std::string path = Hyprexpo::Drawer::resolveIconPath(app.icon, (int)(px * scale));
    if (!path.empty()) {
        GError* err = nullptr;
        const int target = std::max(16, (int)(px * scale));
        GdkPixbuf* pb = gdk_pixbuf_new_from_file_at_size(path.c_str(), target, target, &err);
        if (err)
            g_error_free(err);
        if (pb) {
            const int w = gdk_pixbuf_get_width(pb);
            const int h = gdk_pixbuf_get_height(pb);
            cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
            unsigned char* dst = cairo_image_surface_get_data(surf);
            const int dstStride = cairo_image_surface_get_stride(surf);
            guchar* src = gdk_pixbuf_get_pixels(pb);
            const int srcStride = gdk_pixbuf_get_rowstride(pb);
            const int ch = gdk_pixbuf_get_n_channels(pb);
            const bool hasA = gdk_pixbuf_get_has_alpha(pb);
            for (int y = 0; y < h; ++y) {
                uint32_t* row = (uint32_t*)(dst + y * dstStride);
                for (int x = 0; x < w; ++x) {
                    const guchar* p = src + y * srcStride + x * ch;
                    const uint32_t a = hasA ? p[3] : 255;
                    const uint32_t r = p[0] * a / 255;
                    const uint32_t g = (ch > 1 ? p[1] : p[0]) * a / 255;
                    const uint32_t b = (ch > 2 ? p[2] : p[0]) * a / 255;
                    row[x] = (a << 24) | (r << 16) | (g << 8) | b;
                }
            }
            cairo_surface_mark_dirty(surf);
            tex = uploadCairoSurface(surf);
            cairo_surface_destroy(surf);
            g_object_unref(pb);
        }
    }
    if (!tex) {
        // glyph fallback: first character on a tinted tile (UTF-8 aware:
        // never split a multibyte codepoint, only ASCII gets uppercased)
        std::string letter = "?";
        if (!app.name.empty()) {
            const unsigned char lead = (unsigned char)app.name[0];
            size_t len = 1;
            if ((lead & 0x80) == 0)
                len = 1;
            else if ((lead & 0xE0) == 0xC0)
                len = 2;
            else if ((lead & 0xF0) == 0xE0)
                len = 3;
            else if ((lead & 0xF8) == 0xF0)
                len = 4;
            letter = app.name.substr(0, std::min(len, app.name.size()));
            if (letter.size() == 1)
                letter[0] = (char)toupper((unsigned char)letter[0]);
        }
        const int s = std::max(16, (int)(px * scale));
        tex = renderNumberTexture(letter, CHyprColor{0xffa9b1d6}, Vector2D{(double)s, (double)s}, 1.0, (int)(s * 0.45));
    }
    drawer.iconTex[key] = tex;
    return tex;
}

SP<Render::ITexture> COverview::drawerLabelTexture(const Hyprexpo::Drawer::SApp& app, double scale) {
    auto it = drawer.labelTex.find(app.id);
    if (it != drawer.labelTex.end())
        return it->second;
    const int w = std::max(16, (int)(drawerTileW() * scale));
    auto tex = renderNumberTexture(app.name, CHyprColor{0xffa9b1d6}, Vector2D{(double)w, 30.0 * scale}, 1.0, (int)(15.0 * scale));
    drawer.labelTex[app.id] = tex;
    return tex;
}

void COverview::drawerStepAnim() {
    const float target = drawer.fitted ? 1.f : 0.f;
    const double now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    const double dt = drawer.lastStepS <= 0.0 ? 0.016 : std::min(0.1, now - drawer.lastStepS);
    drawer.lastStepS = now;
    // Released short of the threshold (or a quiet wheel burst), the
    // sheet springs back to rest once input goes idle — but never while a
    // press is physically down. A resting finger sends no motion events,
    // so without the activity gate the sheet would snap back underneath
    // a held finger.
    if (!drawer.pulling && drawer.pullVisual != 0.0 && !touchPress.active && !drawer.mouseArmed && now - drawer.lastPullS > 0.15) {
        drawer.pullVisual += (0.0 - drawer.pullVisual) * std::min(1.0, dt * 12.0);
        if (std::abs(drawer.pullVisual) < 0.5)
            drawer.pullVisual = 0.0;
    }
    if (std::abs(drawer.anim - target) < 0.002f)
        drawer.anim = target;
    else {
        drawer.anim += (target - drawer.anim) * (float)std::min(1.0, dt * 7.0);
        if (std::abs(drawer.anim - target) < 0.002f)
            drawer.anim = target;
    }
    if (drawer.anim == target && drawer.pullVisual == 0.0)
        return;
    damage();
}

void COverview::renderDrawerPass() {
    const auto MON = pMonitor.lock();
    if (!MON || !drawerEnabled())
        return;
    const double sc = MON->m_scale;
    const double sTop = searchTop();
    const double sH = searchH();
    const double W = MON->m_size.x;

    CRegion damage{0, 0, INT16_MAX, INT16_MAX};
    auto toPhys = [&](CBox b) {
        b.scale(sc);
        return b;
    };

    // search strip: well + text + focus ring
    {
        CBox well = toPhys(CBox{{48.0, sTop}, {W - 96.0, sH}});
        Render::GL::g_pHyprOpenGL->renderRect(well, CHyprColor{0xff0a0a0a}, {.round = 10});
        if (drawer.searchFocused) {
            CBox ring = toPhys(CBox{{46.0, sTop - 2.0}, {W - 92.0, sH + 4.0}});
            Render::GL::g_pHyprOpenGL->renderRect(ring, CHyprColor{0xff2ac3de}, {});
            Render::GL::g_pHyprOpenGL->renderRect(well, CHyprColor{0xff0a0a0a}, {.round = 10});
        }
        if (drawer.queryDirty || !drawer.searchTex) {
            const std::string text = drawer.query.empty() ? "Search apps…" : drawer.query;
            const CHyprColor col = drawer.query.empty() ? CHyprColor{0xff787c99} : CHyprColor{0xffa9b1d6};
            const int bw = std::max(16, (int)((W - 96.0 - 32.0) * sc));
            drawer.searchTex = renderNumberTexture(text, col, Vector2D{(double)bw, sH * sc}, 1.0, (int)(17.0 * sc));
            drawer.queryDirty = false;
        }
        if (drawer.searchTex) {
            CBox tbox = toPhys(CBox{{48.0 + 16.0, sTop}, {W - 96.0 - 32.0, sH}});
            Render::GL::g_pHyprOpenGL->renderTextureInternal(drawer.searchTex, tbox, {.damage = &damage, .a = 1.0f});
        }
    }

    // app grid, clipped to the visible band
    const int cols = drawerCols();
    const double tw = drawerTileW();
    const double rh = drawerRowH();
    const int iconPx = drawerCfgIconPx();
    for (size_t oi = 0; oi < drawer.order.size(); ++oi) {
        if (drawer.order[oi] == Hyprexpo::Drawer::EMPTY_SLOT)
            continue; // recent-row padding hole: no tile, no hit test
        CBox tile = drawerTileBox((int)oi);
        if (tile.y + tile.h < drawerTop() || tile.y > drawerTop() + drawerClipH())
            continue;
        // Apps disappear underneath the search bar instead of rendering
        // on top of it: anything reaching into the strip is culled whole.
        if (tile.y < sTop + sH)
            continue;
        const auto& app = drawer.apps[drawer.order[oi]];
        if ((int)oi == drawer.hoverApp) {
            CBox hb = toPhys(CBox{{tile.x - 6.0, tile.y - 6.0}, {tile.w + 12.0, tile.h + 12.0}});
            Render::GL::g_pHyprOpenGL->renderRect(hb, CHyprColor{0x242ac3de}, {.round = 12});
        }
        auto icon = drawerIconTexture(app, iconPx, sc);
        if (icon) {
            const double isz = (double)iconPx;
            CBox ibox = toPhys(CBox{{tile.x + (tile.w - isz) / 2.0, tile.y + 8.0}, {isz, isz}});
            Render::GL::g_pHyprOpenGL->renderTextureInternal(icon, ibox, {.damage = &damage, .a = 1.0f, .round = 10});
        }
        auto label = drawerLabelTexture(app, sc);
        if (label) {
            CBox lbox = toPhys(CBox{{tile.x, tile.y + 8.0 + (double)iconPx + 4.0}, {tile.w, 30.0}});
            Render::GL::g_pHyprOpenGL->renderTextureInternal(label, lbox, {.damage = &damage, .a = 1.0f});
        }
    }
}

// Routes printable keys / editing keys to the focused drawer search box.
// Returns true when consumed. Latin-only v1: no IME.
namespace Hyprexpo::Drawer {
bool drawerSearchKey(const IKeyboard::SKeyEvent& event) {
    auto* const session = activeOverview();
    auto* const OV = dynamic_cast<COverview*>(session);
    if (!OV || OV->closeCommitted() || !OV->drawer.searchFocused)
        return false;
    if (event.state != WL_KEYBOARD_KEY_STATE_PRESSED)
        return false;
    const auto KEYBOARD = g_pSeatManager->m_keyboard.lock();
    if (!KEYBOARD || !KEYBOARD->m_xkbState)
        return false;
    const xkb_keysym_t sym = xkb_state_key_get_one_sym(KEYBOARD->m_xkbState, event.keycode + 8);
    if (sym == XKB_KEY_Escape) {
        if (OV->drawer.query.empty())
            return false; // let the overview cancel path exit
        OV->drawerClearQuery();
        return true;
    }
    if (sym == XKB_KEY_BackSpace) {
        OV->drawerBackspace();
        return true;
    }
    if (sym == XKB_KEY_Return || sym == XKB_KEY_KP_Enter) {
        return OV->drawerConfirmTop();
    }
    char buf[8] = {};
    if (xkb_keysym_to_utf8(sym, buf, sizeof(buf)) > 0 && (unsigned char)buf[0] >= 0x20) {
        OV->drawerTypeText(std::string(buf));
        return true;
    }
    return false;
}
} // namespace Hyprexpo::Drawer
