// App drawer addon: search strip + app grid on one pullable sheet.
// Session input hooks route owned-region events here through IOverviewAddon;
// pure model bits (scan/filter/locale/icons) live in Drawer.{hpp,cpp} with
// no compositor dependency.
#include "DrawerAddon.hpp"
#include "HyprexpoConfig.hpp"
#include "Overview.hpp"
#include "OverviewInternal.hpp"
#include "ConfigValues.hpp"
#include "Drawer.hpp"

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/config/ConfigValue.hpp>
#include <hyprland/src/config/values/types/IntValue.hpp>
#include <hyprland/src/config/values/types/FloatValue.hpp>
#include <hyprland/src/debug/log/Logger.hpp>
#include <hyprland/src/devices/IKeyboard.hpp>
#include <hyprland/src/managers/SeatManager.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>
#define private   public
#define protected public
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/output/Monitor.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/desktop/state/WindowState.hpp>
#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#undef private
#undef protected

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <cairo/cairo.h>
#include <xkbcommon/xkbcommon.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <dlfcn.h>
#include <format>
#include <sstream>

namespace {

// librsvg bound at runtime: optional dependency, ABI-declared by hand so
// nothing new is linked (glib/gobject already are; GError comes from their
// headers like everywhere else in this file).
struct RsvgRect {
    double x = 0.0, y = 0.0, width = 0.0, height = 0.0;
};
struct SvgRsvg {
    using NewFromFile    = void* (*)(const char*, GError**);
    using RenderDocument = int (*)(void*, void*, const void*, GError**);
    void*          handle         = nullptr;
    NewFromFile    newFromFile    = nullptr;
    RenderDocument renderDocument = nullptr;
    bool load() {
        if (handle)
            return true;
        handle = dlopen("librsvg-2.so.2", RTLD_LAZY | RTLD_LOCAL);
        if (!handle)
            return false;
        newFromFile    = (NewFromFile)dlsym(handle, "rsvg_handle_new_from_file");
        renderDocument = (RenderDocument)dlsym(handle, "rsvg_handle_render_document");
        if (!newFromFile || !renderDocument) {
            dlclose(handle);
            handle = nullptr;
            return false;
        }
        return true;
    }
};

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
// Wheel/touchpad visual wall: without a release event, pushes may travel
// one overshoot past the commit threshold so the sheet visibly strains on
// the committing push. Commit itself happens AT the threshold (same bar as
// a finger release); anything shorter springs back on idle instead of
// toggling on a small flick.
constexpr double COMMIT_OVERSHOOT_PX = 24.0;
// Touch fling tuning lives shared in Hyprexpo::Fling (same physics as the
// ribbon strip).

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

double CDrawerAddon::searchH() const {
    return (double)drawerCfgSearchH();
}

double CDrawerAddon::searchTop() const {
    // Docked: strip sits just above the single bottom row. Fitted: top.
    // The search strip is part of the sheet: it rides pullVisual exactly
    // like the app grid below it, plus the select-close dive (whole sheet
    // drops below the screen edge while the overview zooms into the tile).
    const auto MON = m_owner->pMonitor.lock();
    const double H = MON ? MON->m_size.y : 0.0;
    const double dockedY = H - 16.0 - rowH() - 12.0 - searchH();
    const double fittedY = 24.0;
    return dockedY + (fittedY - dockedY) * smooth01(state.anim) + state.pullVisual + state.divePx;
}

// Docked/fitted sheet endpoints (monitor-local). Single source for the
// geometry trio below so they can't drift apart.
void CDrawerAddon::sheetEnds(double& dockedY, double& fittedY) const {
    const auto MON = m_owner->pMonitor.lock();
    const double H = MON ? MON->m_size.y : 0.0;
    dockedY = H - 16.0 - rowH();
    fittedY = 24.0 + searchH() + 16.0;
}

double CDrawerAddon::top() const {
    // Docked: one pinned row pinned to the bottom edge (rest of the grid
    // lives below the screen and scrolls up into view when fitted).
    // pullVisual shifts the sheet with the finger while a pull drag is
    // in flight (or springing back); otherwise it is zero. divePx drops
    // the whole sheet past the screen edge on select-close.
    double dockedY, fittedY;
    sheetEnds(dockedY, fittedY);
    return dockedY + (fittedY - dockedY) * smooth01(state.anim) + state.pullVisual + state.divePx;
}

void CDrawerAddon::startDive() {
    const auto MON = m_owner->pMonitor.lock();
    if (!MON)
        return;
    // Drop distance: current sheet top to fully below the screen edge.
    // Runs at ~2x the open-animation rate: smooth, but gone before the
    // zoom lands. Idempotent: a second call mid-dive just re-arms.
    state.diveTarget = MON->m_size.y - searchTop() + 64.0;
    if (state.diveTarget < 0.0)
        state.diveTarget = 0.0;
    m_owner->damage();
}

// Full travel of the sheet between docked and fitted (always >= 0).
double CDrawerAddon::pullSpan() const {
    double dockedY, fittedY;
    sheetEnds(dockedY, fittedY);
    return std::max(0.0, dockedY - fittedY);
}

// Commit travel: a quarter of the screen height. A fixed 60px threshold
// felt like ~20px of intent on touch; a pull only counts when it covers
// real distance. drawer_expand_px remains as a floor for tiny outputs.
double CDrawerAddon::pullThreshold() const {
    const auto MON = m_owner->pMonitor.lock();
    const double H = MON ? MON->m_size.y : 0.0;
    return std::max(H / 4.0, (double)drawerCfgExpandPx());
}

int CDrawerAddon::columns() const {
    return drawerCfgCols();
}

double CDrawerAddon::tileW() const {
    const auto MON = m_owner->pMonitor.lock();
    const double W = MON ? MON->m_size.x : 0.0;
    const int    ncols = columns();
    return (W - 2.0 * 48.0 - (double)(ncols - 1) * 16.0) / (double)ncols;
}

double CDrawerAddon::rowH() const {
    return (double)drawerCfgIconPx() + 56.0; // icon + label + padding
}

double CDrawerAddon::clipH() const {
    const auto MON = m_owner->pMonitor.lock();
    const double H = MON ? MON->m_size.y : 0.0;
    const double docked = rowH();
    const double fitted = std::max(docked, H - top() - 16.0);
    const double t      = smooth01(state.anim);
    return docked + (fitted - docked) * t;
}

double CDrawerAddon::contentH() const {
    const size_t rows = (state.order.size() + (size_t)columns() - 1) / (size_t)columns();
    if (rows == 0)
        return 0.0;
    return (double)rows * rowH() + (double)(rows - 1) * 12.0 + (recentPad() ? RECENT_PAD_PX : 0.0);
}

double CDrawerAddon::maxScroll() const {
    return std::max(0.0, contentH() - clipH());
}

// Padded gap after the recent row: only with an empty query, a shown
// recent row, and more rows below it.
bool CDrawerAddon::recentPad() const {
    if (!state.query.empty() || state.recentShown <= 0 || state.order.empty())
        return false;
    const size_t rows = (state.order.size() + (size_t)columns() - 1) / (size_t)columns();
    return rows > 1;
}

int CDrawerAddon::appAt(const Vector2D& local) const {
    const auto MON = m_owner->pMonitor.lock();
    if (!drawerEnabled() || !MON || state.order.empty())
        return -1;
    const double tw = tileW();
    const double rh = rowH();
    const double lx = local.x - 48.0;
    if (lx < 0)
        return -1;
    const int col = (int)(lx / (tw + 16.0));
    if (col < 0 || col >= columns() || lx - col * (tw + 16.0) > tw)
        return -1;
    const double ly = local.y - (top() - state.scroll);
    // Band check in screen space: ly carries the scroll offset (content
    // space), so comparing it against the band height kills the bottom
    // `scroll` px of the visible band — the last rows whenever scrolled.
    const double sy = local.y - top();
    if (sy < 0 || sy >= clipH())
        return -1;
    // Row 0 is exactly the recent row; the padded gap after it is dead.
    int    row;
    double within;
    if (recentPad() && ly >= rh) {
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
    const size_t idx = (size_t)row * (size_t)columns() + (size_t)col;
    if (idx >= state.order.size() || state.order[idx] == Hyprexpo::Drawer::EMPTY_SLOT)
        return -1;
    return (int)idx;
}

CBox CDrawerAddon::tileBox(int orderIdx) const {
    if (orderIdx < 0 || orderIdx >= (int)state.order.size() || state.order[orderIdx] == Hyprexpo::Drawer::EMPTY_SLOT)
        return CBox{{0, 0}, {0, 0}};
    const double tw = tileW();
    const double rh = rowH();
    const int    row = orderIdx / columns();
    const int    col = orderIdx % columns();
    double       y   = top() + (double)row * (rh + 12.0) - state.scroll;
    if (recentPad() && row >= 1)
        y += RECENT_PAD_PX;
    return CBox{{48.0 + col * (tw + 16.0), y}, {tw, rh}};
}

// --- model glue ---

void CDrawerAddon::onOpen() {
    state.apps  = Hyprexpo::Drawer::scanApps();
    state.recent = Hyprexpo::Drawer::loadRecent();
    state.query.clear();
    state.scroll        = 0;
    state.lastPullS     = 0;
    state.pullVisual    = 0;
    state.pulling       = false;
    state.pullEngaged   = false;
    state.recentShown   = 0;
    state.hoverApp      = -1;
    state.searchFocused = false;
    state.queryDirty    = true;
    onClose();
    refilter();
}

void CDrawerAddon::refilter() {
    state.order = Hyprexpo::Drawer::filterApps(state.apps, state.query, state.recent, columns(), state.recentShown);
    state.scroll = std::clamp(state.scroll, 0.0, maxScroll());
    state.flingVel = 0.0; // content changed under the motion: stop dead
    if (state.hoverApp >= (int)state.order.size())
        state.hoverApp = -1;
    state.queryDirty = true;
    m_owner->damage();
}

void CDrawerAddon::onClose() {
    state.iconTex.clear();
    state.labelTex.clear();
    state.searchTex.reset();
}

void CDrawerAddon::setFitted(bool fitted) {
    if (state.fitted == fitted)
        return;
    state.fitted = fitted;
    state.scroll = 0;
    state.flingVel   = 0;
    state.lastPullS   = 0;
    state.pullVisual  = 0;
    state.pulling     = false;
    state.pullEngaged = false;
    if (!fitted)
        state.searchFocused = false;
    m_owner->damage();
}

double CDrawerAddon::pullStamp() {
    const double now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    state.lastPullS = now;
    return now;
}

// Scrolls the app list by a downward-positive displacement. Returns the
// overshoot pushed past the top of the list (down positive, else 0): the
// caller turns overshoot into sheet pull or rubber-band resistance.
double CDrawerAddon::listScroll(double fingerDy) {
    const double before = state.scroll;
    state.scroll       = std::clamp(before - fingerDy, 0.0, maxScroll());
    return fingerDy > 0.0 ? std::max(0.0, fingerDy - before) : 0.0;
}

// fingerDy: wheel/touchpad displacement, down positive, already scaled
// for the source (see the axis hook). Wheels have no release event, so
// they ride the same visual pull as fingers, plus a detent: a push must
// travel past the threshold AND keep pushing against the wall to commit.
// Anything shorter visibly springs back once the burst goes idle.
// Discrete touchpad open for a docked sheet: touchpads have no release
// event, so instead of the analog detent, accumulate open-direction travel
// and fire at half the pull threshold. Deliberately forgiving: wrong-way
// events are ignored (never reset progress — scroll bursts wobble), only a
// >0.5s gap restarts the burst. The sheet tracks progress live, scaled so it
// visibly strains all the way to the full threshold as the burst nears the
// fire line. Touch drags (touchMotion) and fitted list scrolls (wheel())
// never enter here.
void CDrawerAddon::wheelTouchOpen(double fingerDy) {
    if (m_owner->closeCommitted() || state.fitted)
        return;
    const double now       = pullStamp();
    const double threshold = pullThreshold();
    const double span      = pullSpan();
    if (span <= 0.0)
        return;
    const double fireAt = threshold * 0.5;
    if (now - state.wheelAccS > 0.5) {
        state.wheelAcc = 0.0;
    }
    state.wheelAccS = now;
    if (fingerDy < 0.0)
        state.wheelAcc += -fingerDy; // wrong-way events ignored, never reset
    if (state.wheelAcc >= fireAt) {
        state.wheelAcc = 0.0;
        // No visual reset: commitPull seeds the open animation from the
        // strained sheet position, so there is no jump back to docked.
        commitPull(true, span);
    } else {
        state.pullVisual = -(state.wheelAcc / fireAt) * threshold;
    }
    m_owner->damage();
}

void CDrawerAddon::wheel(double fingerDy) {
    if (m_owner->closeCommitted())
        return;
    state.flingVel = 0.0; // wheel takes over: inertia stops
    pullStamp();
    const double threshold = pullThreshold();
    const double span      = pullSpan();
    if (span <= 0.0)
        return;
    if (!state.fitted) {
        if (state.pullVisual < 0.0 || fingerDy < 0.0) {
            // A live pull tracks both directions; fresh pushes still
            // detent-gate. Only idle snaps it home, never a reversal.
            // Wheel commits AT the threshold like a finger release does
            // (endDrag): there is no release event to confirm intent, so
            // still pushing past the threshold IS the confirmation. The
            // visual wall sits one overshoot beyond so the sheet visibly
            // strains against it on the committing push.
            if (fingerDy < 0.0 && state.pullVisual <= -threshold)
                commitPull(true, span);
            else
                pushVisual(fingerDy, -(threshold + COMMIT_OVERSHOOT_PX), 0.0);
        } else {
            state.pullVisual = 0.0;
        }
        m_owner->damage();
        return;
    }
    if (state.pullVisual > 0.0) {
        // A live pull tracks both directions like a finger: reversals walk
        // the sheet back, crossing zero spills into the list. Wheel commits
        // AT the threshold like a finger release (endDrag), for the same
        // reason as the docked branch above.
        if (fingerDy > 0.0 && state.pullVisual >= threshold) {
            commitPull(false, span);
        } else {
            const double next = state.pullVisual + fingerDy;
            if (next <= 0.0) {
                state.pullVisual = 0.0;
                listScroll(next);
            } else {
                state.pullVisual = std::min(next, threshold + COMMIT_OVERSHOOT_PX);
            }
        }
        m_owner->damage();
        return;
    }
    const double before = state.scroll;
    const double over   = listScroll(fingerDy);
    // Engagement is scroll position, never screen position: only a push
    // that starts within the top row may close. Deeper pushes just scroll.
    if (fingerDy > 0.0 && over > 0.0 && before <= rowH())
        state.pullVisual = std::min(over, threshold + COMMIT_OVERSHOOT_PX);
    m_owner->damage();
}

// Grid press: latch whether THIS drag may commit open/close. The latch is
// the drawer's scroll position, not the touch point: a drag that starts
// with the first row visible ("at the top") may close; one started
// scrolled deeper can only scroll or resist, never close by mistake.
// Never clears pullVisual: a second finger (or driver re-press) joining a
// live pull continues it instead of snapping the sheet out from under the
// first finger.
void CDrawerAddon::beginPull() {
    if (m_owner->closeCommitted())
        return;
    state.pulling     = false;
    state.pullEngaged = !state.fitted || state.scroll <= rowH();
    pullStamp();
}

// Commit an open/close pull, seeding the snap animation from the live
// sheet position so there is no jump.
void CDrawerAddon::commitPull(bool open, double span) {
    if (span <= 0.0)
        return;
    if (open)
        state.anim = std::clamp((float)(-state.pullVisual / span), 0.0F, 1.0F);
    else
        state.anim = 1.0F - std::clamp((float)(state.pullVisual / span), 0.0F, 1.0F);
    state.pullVisual = 0;
    setFitted(open);
}

// Shared sheet physics: accumulate a visual offset inside [lo, hi].
void CDrawerAddon::pushVisual(double delta, double lo, double hi) {
    const double next = std::clamp(state.pullVisual + delta, lo, hi);
    if (next != state.pullVisual) {
        state.pullVisual = next;
        m_owner->damage();
    }
}

// Pull path (mouse/touch drags): the sheet follows the finger via
// pullVisual; the open/close decision happens on release (endDrag).
void CDrawerAddon::dragAdvance(double fingerDy) {
    if (m_owner->closeCommitted())
        return;
    pullStamp();
    const double span = pullSpan();
    if (span <= 0.0)
        return;
    if (!state.fitted) {
        // Docked: any Grid press may pull the sheet up to open.
        state.pulling     = true;
        state.pullEngaged = true;
        pushVisual(fingerDy, -span, 0.0);
        m_owner->damage();
        return;
    }
    state.pulling = true;
    if (state.pullEngaged) {
        // Drag started at the top. While the sheet is displaced it owns
        // the gesture in both directions, so reversing walks it back
        // instead of teleporting it home — only the release may send it
        // back. Crossing zero spills the remainder into the list.
        if (state.pullVisual > 0.0) {
            const double next = state.pullVisual + fingerDy;
            if (next <= 0.0) {
                state.pullVisual = 0.0;
                listScroll(next);
            } else {
                pushVisual(fingerDy, 0.0, span);
            }
        } else {
            const double over = listScroll(fingerDy);
            if (over > 0.0)
                pushVisual(over, 0.0, span);
        }
        m_owner->damage();
        return;
    }
    // Drag started scrolled down: plain list scroll, plus rubber-band
    // resistance when pushing past the top. Never accumulates a close.
    // A live rubber band tracks back 1:1 for the same no-teleport reason.
    if (state.pullVisual > 0.0) {
        const double next = state.pullVisual + fingerDy;
        if (next <= 0.0) {
            state.pullVisual = 0.0;
            listScroll(next);
        } else {
            state.pullVisual = std::min(next, RESIST_CAP_PX);
        }
    } else {
        const double over = listScroll(fingerDy);
        if (over > 0.0)
            pushVisual(over * (double)drawerCfgResist(), 0.0, RESIST_CAP_PX);
    }
    m_owner->damage();
}

// Release: pulled far enough -> animate to the new state; otherwise the
// sheet springs back (stepFrame decays pullVisual to zero).
void CDrawerAddon::endDrag(bool commit) {
    if (!state.pulling)
        return;
    state.pulling = false;
    const double threshold = pullThreshold();
    const double span      = pullSpan();
    if (commit && state.pullEngaged && span > 0.0) {
        if (!state.fitted && -state.pullVisual >= threshold) {
            commitPull(true, span);
            return;
        }
        if (state.fitted && state.pullVisual >= threshold) {
            commitPull(false, span);
            return;
        }
    }
    state.pullEngaged = false;
    state.lastPullS   = 0; // snap back immediately, don't wait out the idle window
    m_owner->damage();
}

void CDrawerAddon::tap(const Vector2D& local) {
    if (classifyBelowRibbon(local) == Hyprexpo::Addon::EBandRegion::Search) {
        focusSearch();
        return;
    }
    const int idx = appAt(local);
    if (idx < 0) {
        // Diagnostic for dead taps: proves whether the press reached the
        // grid at all (position, model size, scroll state).
        Log::logger->log(Log::ERR, "[hyprexpo] dead tap at {:.0f},{:.0f} (order={}, scroll={:.0f}, fitted={})", local.x, local.y,
                         (int)state.order.size(), state.scroll, state.fitted ? 1 : 0);
        state.searchFocused = false;
        m_owner->damage();
        return;
    }
    launch((size_t)idx);
}

void CDrawerAddon::launch(size_t orderIdx) {
    if (orderIdx >= state.order.size() || state.order[orderIdx] == Hyprexpo::Drawer::EMPTY_SLOT)
        return;
    const auto& app = state.apps[state.order[orderIdx]];
    // Tapping counts as use either way, so the recent row tracks it.
    Hyprexpo::Drawer::recordRecent(app.id);
    // Already open: focus it (visible feedback) instead of duplicating.
    // Terminals exempt: multi-instance tools open new, always.
    if (!app.terminal && focusIfOpen(app)) {
        state.searchFocused = false;
        m_owner->close(false);
        return;
    }
    Log::logger->log(Log::ERR, "[hyprexpo] launching {}: {}", app.id, app.exec);
    HyprlandAPI::addNotification(PHANDLE, "Launching " + app.name, CHyprColor{0.16, 0.76, 0.87, 1.0}, 2000);
    // Launch itself is detached; the close below destroys this session.
    Hyprexpo::Drawer::launchApp(app);
    state.searchFocused = false;
    // Select-workspace-first: close onto the focused (green-outlined) tile
    // so its workspace is created/selected first; the detached launch lands
    // on the now-active workspace. Falls back to a plain dismiss.
    if (m_owner->focusedWorkspaceID() != WORKSPACE_INVALID)
        m_owner->selectWorkspaceByID(m_owner->focusedWorkspaceID());
    closeOverviewsSelecting(m_owner);
}

// Focus an already-open app instead of launching a duplicate. Matches
// StartupWMClass exactly, else the exec basename against the window class
// (case-insensitive), else the flatpak app id and its last component.
// Prefers a window on the overview's workspace so a tap doesn't yank you
// elsewhere when avoidable.
bool CDrawerAddon::focusIfOpen(const Hyprexpo::Drawer::SApp& app) {
    auto lower = [](std::string s) {
        for (auto& c : s)
            c = (char)tolower((unsigned char)c);
        return s;
    };
    std::string execBase;
    {
        std::istringstream in(app.exec);
        in >> execBase;
        const auto slash = execBase.find_last_of('/');
        if (slash != std::string::npos)
            execBase = execBase.substr(slash + 1);
        execBase = lower(execBase);
    }
    std::string flatTail;
    if (!app.flatpakAppId.empty()) {
        flatTail = lower(app.flatpakAppId);
        const auto dot = flatTail.find_last_of('.');
        if (dot != std::string::npos)
            flatTail = flatTail.substr(dot + 1);
    }
    const auto MON = m_owner->pMonitor.lock();
    const int64_t here =
        MON && MON->m_activeWorkspace ? MON->m_activeWorkspace->m_id : (int64_t)WORKSPACE_INVALID;
    PHLWINDOW fallback;
    for (const auto& window : Desktop::windowState()->windows()) {
        if (!window)
            continue;
        const std::string cls = lower(window->m_class);
        const bool match      = (!app.startupWmClass.empty() && window->m_class == app.startupWmClass) || (!execBase.empty() && cls == execBase) ||
                           (!flatTail.empty() && (cls == lower(app.flatpakAppId) || cls == flatTail));
        if (!match)
            continue;
        const int64_t ws = window->m_workspace ? window->m_workspace->m_id : (int64_t)WORKSPACE_INVALID;
        if (ws == here) {
            const std::string out = HyprlandAPI::invokeHyprctlCommand(
                "dispatch", std::format("focuswindow address:{:x}", (uintptr_t)window.get()));
            if (!out.empty())
                Log::logger->log(Log::ERR, "[hyprexpo] focuswindow replied: {}", out);
            return true;
        }
        if (!fallback)
            fallback = window;
    }
    if (fallback) {
        const std::string out = HyprlandAPI::invokeHyprctlCommand(
            "dispatch", std::format("focuswindow address:{:x}", (uintptr_t)fallback.get()));
        if (!out.empty())
            Log::logger->log(Log::ERR, "[hyprexpo] focuswindow replied: {}", out);
        return true;
    }
    return false;
}

void CDrawerAddon::typeText(const std::string& text) {
    if (!state.searchFocused || text.empty())
        return;
    if (state.query.size() + text.size() > 64)
        return;
    state.query += text;
    refilter();
}

void CDrawerAddon::backspace() {
    if (!state.searchFocused || state.query.empty())
        return;
    state.query.pop_back();
    refilter();
}

void CDrawerAddon::clearQuery() {
    if (state.query.empty())
        return;
    state.query.clear();
    refilter();
}

bool CDrawerAddon::confirmTop() {
    if (!state.searchFocused || state.order.empty())
        return false;
    if (state.query.empty())
        return false; // empty box: Enter selects the workspace, never the first app
    launch(0);
    return true;
}

// --- textures ---

// Rasterize an SVG icon at exactly `target` px via runtime-bound librsvg.
// Empty texture when the library (or the file) refuses: the caller falls
// through to the glyph. No new build dependency by design.
SP<Render::ITexture> renderSvgIcon(const std::string& path, int target) {
    static SvgRsvg rsvg;
    if (!rsvg.load())
        return {};
    GError* err    = nullptr;
    void*   handle = rsvg.newFromFile(path.c_str(), &err);
    if (err)
        g_error_free(err);
    if (!handle)
        return {};
    cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, target, target);
    SP<Render::ITexture> tex;
    if (cairo_surface_status(surf) == CAIRO_STATUS_SUCCESS) {
        cairo_t* cr = cairo_create(surf);
        // transparent tile first: icons with no background stay clean
        cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.0);
        cairo_paint(cr);
        const RsvgRect viewport{0.0, 0.0, (double)target, (double)target};
        err         = nullptr;
        const int ok = rsvg.renderDocument(handle, cr, &viewport, &err);
        if (err)
            g_error_free(err);
        cairo_destroy(cr);
        if (ok) {
            cairo_surface_flush(surf);
            tex = uploadCairoSurface(surf);
        }
    }
    cairo_surface_destroy(surf);
    g_object_unref(handle);
    return tex;
}

SP<Render::ITexture> CDrawerAddon::iconTexture(const Hyprexpo::Drawer::SApp& app, int px, double scale) {
    const std::string key = app.id;
    auto it = state.iconTex.find(key);
    if (it != state.iconTex.end())
        return it->second;
    SP<Render::ITexture> tex;
    const std::string path = Hyprexpo::Drawer::resolveIconPath(app.icon, (int)(px * scale));
    const int target = std::max(16, (int)(px * scale));
    if (!path.empty() && path.size() >= 4 && path.compare(path.size() - 4, 4, ".svg") == 0)
        tex = renderSvgIcon(path, target);
    if (!tex && !path.empty()) {
        GError* err = nullptr;
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
    state.iconTex[key] = tex;
    return tex;
}

SP<Render::ITexture> CDrawerAddon::labelTexture(const Hyprexpo::Drawer::SApp& app, double scale) {
    auto it = state.labelTex.find(app.id);
    if (it != state.labelTex.end())
        return it->second;
    const int w = std::max(16, (int)(tileW() * scale));
    auto tex = renderNumberTexture(app.name, CHyprColor{0xffa9b1d6}, Vector2D{(double)w, 30.0 * scale}, 1.0, (int)(15.0 * scale));
    state.labelTex[app.id] = tex;
    return tex;
}

void CDrawerAddon::stepFrame() {
    const float target = state.fitted ? 1.f : 0.f;
    const double now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    const double dt = state.lastStepS <= 0.0 ? 0.016 : std::min(0.1, now - state.lastStepS);
    state.lastStepS = now;
    // Released short of the threshold (or a quiet wheel burst), the
    // sheet springs back to rest once input goes idle — but never while a
    // press is physically down. A resting finger sends no motion events,
    // so without the activity gate the sheet would snap back underneath
    // a held finger. Touchpad scroll bursts get the same protection on
    // their own clock: slow scrolls arrive sparse (>0.15s gaps), and the
    // idle snap must not yank the sheet down between ticks of one burst —
    // only true silence (>0.5s) ends it.
    if (!state.pulling && state.pullVisual != 0.0 && !state.touchDownActive && !state.mouseArmed && now - state.lastPullS > 0.15 &&
        now - state.wheelAccS > 0.5) {
        state.pullVisual += (0.0 - state.pullVisual) * std::min(1.0, dt * 12.0);
        if (std::abs(state.pullVisual) < 0.5)
            state.pullVisual = 0.0;
    }
    if (std::abs(state.anim - target) < 0.002f)
        state.anim = target;
    else {
        state.anim += (target - state.anim) * (float)std::min(1.0, dt * 7.0);
        if (std::abs(state.anim - target) < 0.002f)
            state.anim = target;
    }
    // Select-close dive: whole sheet below the screen edge, ~2x the open
    // rate so it clears before the zoom lands. Smooth exponential, snapped
    // at the end like everything else here.
    if (state.divePx < state.diveTarget) {
        state.divePx += (state.diveTarget - state.divePx) * std::min(1.0, dt * 14.0);
        if (state.diveTarget - state.divePx < 0.5)
            state.divePx = state.diveTarget;
    }
    if (state.anim == target && state.pullVisual == 0.0 && state.flingVel == 0.0 && state.divePx >= state.diveTarget)
        return;
    // List inertia: integrates only with no live input and no displaced
    // sheet; anything else (grab, pull, snap, state flip) owns the motion.
    if (state.flingVel != 0.0) {
        if (state.fitted && !state.pulling && !state.touchDownActive && !state.mouseArmed && state.pullVisual == 0.0) {
            const double max = maxScroll();
            state.scroll     = std::clamp(state.scroll + state.flingVel * dt, 0.0, max);
            if (state.scroll <= 0.0 || state.scroll >= max) {
                state.flingVel = 0.0; // hit an end: stop dead, no bounce
            } else {
                state.flingVel = Hyprexpo::Fling::drain(state.flingVel, dt);
            }
        } else {
            state.flingVel = 0.0;
        }
    }
    m_owner->damage();
}

void CDrawerAddon::renderPass() {
    const auto MON = m_owner->pMonitor.lock();
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

    // Sheet plate: one rect exactly matching the live sheet (search strip
    // top edge to screen bottom), so the drawer reads as a solid surface
    // rising/sinking with the pull instead of icons floating over whatever
    // is behind. searchTop() already rides anim + pullVisual, so the plate
    // tracks every path (finger, wheel, dispatcher) for free. Same
    // near-black as the search well; square corners (the sheet is
    // edge-to-edge; rounding would leave slivers at the screen edges).
    {
        const double H = MON->m_size.y;
        if (sTop < H) {
            CBox plate = toPhys(CBox{{0.0, sTop}, {W, H - sTop}});
            Render::GL::g_pHyprOpenGL->renderRect(plate, CHyprColor{0xff0a0a0a}, {});
        }
    }

    // app grid, clipped to the visible band
    const int ncols = columns();
    const double tw = tileW();
    const double rh = rowH();
    const int iconPx = drawerCfgIconPx();
    // Tiles fade out over more than their own height as they slide under the
    // search bar instead of popping whole: alpha tracks how much of the
    // tile is still below the bar's bottom edge — fading starts while the
    // top is still half an icon clear of it, gone exactly when fully
    // behind it.
    const double barBottom = sTop + sH;
    const double fadeSpan = std::max(1.0, rh + (double)iconPx / 2.0);
    for (size_t oi = 0; oi < state.order.size(); ++oi) {
        if (state.order[oi] == Hyprexpo::Drawer::EMPTY_SLOT)
            continue; // recent-row padding hole: no tile, no hit test
        CBox tile = tileBox((int)oi);
        if (tile.y + tile.h < top() || tile.y > top() + clipH())
            continue;
        const double vis = tile.h > 0.0 ? smooth01(std::clamp((tile.y + tile.h - barBottom) / fadeSpan, 0.0, 1.0)) : 1.0;
        if (vis <= 0.0)
            continue; // fully behind the search bar: despawned, not drawn
        const float alpha = (float)vis;
        const auto& app = state.apps[state.order[oi]];
        if ((int)oi == state.hoverApp) {
            const uint32_t ha = (uint32_t)(0x24 * vis);
            CBox hb = toPhys(CBox{{tile.x - 6.0, tile.y - 6.0}, {tile.w + 12.0, tile.h + 12.0}});
            Render::GL::g_pHyprOpenGL->renderRect(hb, CHyprColor{(uint64_t)((ha << 24) | 0x2ac3de)}, {.round = 12});
        }
        auto icon = iconTexture(app, iconPx, sc);
        if (icon) {
            const double isz = (double)iconPx;
            CBox ibox = toPhys(CBox{{tile.x + (tile.w - isz) / 2.0, tile.y + 8.0}, {isz, isz}});
            Render::GL::g_pHyprOpenGL->renderTextureInternal(icon, ibox, {.damage = &damage, .a = alpha, .round = 10});
        }
        auto label = labelTexture(app, sc);
        if (label) {
            CBox lbox = toPhys(CBox{{tile.x, tile.y + 8.0 + (double)iconPx + 4.0}, {tile.w, 30.0}});
            Render::GL::g_pHyprOpenGL->renderTextureInternal(label, lbox, {.damage = &damage, .a = alpha});
        }
    }

    // Search occluder: full-width plate-color backdrop from the search bar's
    // bottom edge upward, drawn over the grid but under the well, so sliding
    // tiles are hard-cut the moment they pass behind the bar instead of
    // ghosting over it. Height grows with the open animation (zero docked,
    // so no dark slab hangs over the ribbon) and always covers a full tile
    // row, so nothing can peek out above it. Same color as the sheet plate:
    // the seam is invisible.
    {
        const double ext = (rowH() + 16.0) * smooth01(state.anim);
        if (ext > 0.0) {
            CBox shade = toPhys(CBox{{0.0, sTop + sH - ext}, {W, ext}});
            Render::GL::g_pHyprOpenGL->renderRect(shade, CHyprColor{0xff0a0a0a}, {});
        }
    }

    // search strip: well + text, crisp above the occluder. No focus ring
    // by design: the box is always focused while the exposé is open, so a
    // ring would sit there permanently instead of giving feedback.
    {
        CBox well = toPhys(CBox{{48.0, sTop}, {W - 96.0, sH}});
        Render::GL::g_pHyprOpenGL->renderRect(well, CHyprColor{0xff0a0a0a}, {.round = 10});
        if (state.queryDirty || !state.searchTex) {
            const std::string text = state.query.empty() ? "Search apps…" : state.query;
            const CHyprColor col = state.query.empty() ? CHyprColor{0xff787c99} : CHyprColor{0xffa9b1d6};
            const int bw = std::max(16, (int)((W - 96.0 - 32.0) * sc));
            state.searchTex = renderNumberTexture(text, col, Vector2D{(double)bw, sH * sc}, 1.0, (int)(17.0 * sc));
            state.queryDirty = false;
        }
        if (state.searchTex) {
            CBox tbox = toPhys(CBox{{48.0 + 16.0, sTop}, {W - 96.0 - 32.0, sH}});
            Render::GL::g_pHyprOpenGL->renderTextureInternal(state.searchTex, tbox, {.damage = &damage, .a = 1.0f});
        }
    }
}

// Routes printable keys / editing keys to the focused drawer search box.
// Returns true when consumed. Latin-only v1: no IME. Session guards
// (active overview, teardown) live at the call site in main.cpp.
bool CDrawerAddon::searchKey(const IKeyboard::SKeyEvent& event) {
    if (!state.searchFocused)
        return false;
    if (event.state != WL_KEYBOARD_KEY_STATE_PRESSED)
        return false;
    const auto KEYBOARD = g_pSeatManager->m_keyboard.lock();
    if (!KEYBOARD || !KEYBOARD->m_xkbState)
        return false;
    const xkb_keysym_t sym = xkb_state_key_get_one_sym(KEYBOARD->m_xkbState, event.keycode + 8);
    if (sym == XKB_KEY_Escape) {
        if (state.query.empty())
            return false; // let the overview cancel path exit
        clearQuery();
        return true;
    }
    if (sym == XKB_KEY_BackSpace) {
        backspace();
        return true;
    }
    if (sym == XKB_KEY_Return || sym == XKB_KEY_KP_Enter) {
        return confirmTop();
    }
    char buf[8] = {};
    if (xkb_keysym_to_utf8(sym, buf, sizeof(buf)) > 0 && (unsigned char)buf[0] >= 0x20) {
        typeText(std::string(buf));
        return true;
    }
    return false;
}

// --- IOverviewAddon session/input surface (thin; the engine above does the work) ---

bool CDrawerAddon::hidesRibbon() const {
    return state.anim >= 0.5f;
}

Hyprexpo::Addon::EBandRegion CDrawerAddon::classifyBelowRibbon(const Vector2D& local) const {
    using Hyprexpo::Addon::EBandRegion;
    const auto MON = m_owner->pMonitor.lock();
    if (!drawerEnabled() || !MON)
        return EBandRegion::None;
    const double H = MON->m_size.y;
    if (local.x < 0 || local.x >= MON->m_size.x || local.y < 0 || local.y >= H)
        return EBandRegion::None;
    const double sTop = searchTop();
    if (local.y >= sTop && local.y < sTop + searchH())
        return EBandRegion::Search;
    if (local.y >= top() && local.y < top() + clipH())
        return EBandRegion::Grid;
    return EBandRegion::None;
}

void CDrawerAddon::pointerDown(const Vector2D& local, uint32_t button) {
    if (button != 0x110)
        return; // left button only; right (0x111) intentionally does nothing
    (void)local; // latch is scroll position; the press point is irrelevant
    state.mouseArmed = true;
    state.mouseMoved = false;
    state.flingVel   = 0.0; // mouse grabs the list: inertia stops
    beginPull();
}

bool CDrawerAddon::pointerDragActive() const {
    return state.mouseArmed;
}

void CDrawerAddon::pointerMove(const Vector2D& delta) {
    if (!state.mouseArmed)
        return;
    if (std::hypot(delta.x, delta.y) >= 12.0)
        state.mouseMoved = true;
    if (state.mouseMoved)
        dragAdvance(delta.y);
}

void CDrawerAddon::pointerUp(const Vector2D& local) {
    if (!state.mouseArmed)
        return;
    state.mouseArmed = false;
    endDrag();
    if (!state.mouseMoved)
        tap(local);
}

void CDrawerAddon::updateHover(const Vector2D& local) {
    const int idx = appAt(local);
    if (idx != state.hoverApp) {
        state.hoverApp = idx;
        m_owner->damage();
    }
}

void CDrawerAddon::touchDown(const Vector2D&) {
    state.touchDownActive = true;
    state.flingVel       = 0.0; // finger grabs the list: inertia stops
    state.velTrack.reset();
    beginPull(); // latch is scroll position; the touch point is irrelevant
}

void CDrawerAddon::touchMotion(double dy, double pressDist) {
    dragAdvance(dy);
    // Velocity cache: cumulative finger travel stamped per motion event.
    state.velTrack.push(pullStamp(), dy);
    if (pressDist >= Hyprexpo::Addon::TAP_SLOP_PX) {
        // Scrolling, not pressing: the highlight must not chase the
        // finger. A resting finger (inside slop) keeps the pressed-app
        // highlight set at down.
        if (state.hoverApp != -1) {
            state.hoverApp = -1;
            m_owner->damage();
        }
    }
}

double CDrawerAddon::releaseVelocity(double now) {
    return state.velTrack.slope(now);
}

void CDrawerAddon::maybeStartFling() {
    state.flingVel = 0.0;
    if (!state.fitted || state.pullVisual != 0.0)
        return;
    // Shared physics: threshold, clamp, and against-the-finger sign in one.
    state.flingVel = Hyprexpo::Fling::startVelocity(releaseVelocity(pullStamp()));
}

void CDrawerAddon::touchUp(const Vector2D& upLocal, const Vector2D& pressDelta) {
    state.touchDownActive = false;
    const bool wasFitted  = state.fitted;
    endDrag();
    if (std::hypot(pressDelta.x, pressDelta.y) < Hyprexpo::Addon::TAP_SLOP_PX) {
        tap(upLocal);
        return;
    }
    // Scroll release with momentum: fling the list, unless this same
    // release just opened/closed the sheet (momentum belongs to pulls).
    if (wasFitted)
        maybeStartFling();
}

void CDrawerAddon::touchCancel() {
    state.touchDownActive = false;
    endDrag(false);
    // Grace window, not a snap: the driver may re-press right after a
    // cancel (stillness looks like abandonment). Stamping holds the
    // sheet so a re-press continues the pull; true abandonment still
    // springs back once the window lapses.
    pullStamp();
}

void CDrawerAddon::focusSearch() {
    state.searchFocused = true;
    m_owner->damage();
}

void CDrawerAddon::onDrawerCommand(const std::string& arg) {
    if (arg == "expand")
        setFitted(true);
    else if (arg == "collapse")
        setFitted(false);
    else
        setFitted(!state.fitted);
}

// Addon-owned config keys, registered centrally from PluginConfig.
void CDrawerAddon::registerConfig() {
    using namespace HyprlandAPI;
    HyprlandAPI::addConfigValueV2(PHANDLE, makeShared<Config::Values::CIntValue>("plugin:hyprexpo:drawer_enable", "app drawer section toggle", HyprexpoConfig::DRAWER_ENABLE_DEFAULT));
    HyprlandAPI::addConfigValueV2(PHANDLE, makeShared<Config::Values::CIntValue>("plugin:hyprexpo:drawer_columns", "app grid columns", HyprexpoConfig::DRAWER_COLUMNS_DEFAULT));
    HyprlandAPI::addConfigValueV2(PHANDLE, 
        makeShared<Config::Values::CIntValue>("plugin:hyprexpo:drawer_search_h", "search strip height px", HyprexpoConfig::DRAWER_SEARCH_H_DEFAULT));
    HyprlandAPI::addConfigValueV2(PHANDLE, makeShared<Config::Values::CIntValue>("plugin:hyprexpo:drawer_expand_px", "drag distance px that expands/collapses the drawer",
                                                         HyprexpoConfig::DRAWER_EXPAND_PX_DEFAULT));
    HyprlandAPI::addConfigValueV2(PHANDLE, makeShared<Config::Values::CIntValue>("plugin:hyprexpo:drawer_icon_px", "app icon px", HyprexpoConfig::DRAWER_ICON_PX_DEFAULT));
    HyprlandAPI::addConfigValueV2(PHANDLE, makeShared<Config::Values::CFloatValue>("plugin:hyprexpo:drawer_resist", "rubber-band factor for deep drawer pulls (0-1)",
                                                           HyprexpoConfig::DRAWER_RESIST_DEFAULT));
}
