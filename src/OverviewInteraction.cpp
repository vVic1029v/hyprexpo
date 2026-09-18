#include "HyprlandConfigCompat.hpp"
#define HyprlandAPI CompatHyprlandAPI
#include "OverviewInternal.hpp"
#include "HyprexpoLogic.hpp"
#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/config/ConfigValue.hpp>
#include <hyprland/src/desktop/state/GlobalWindowController.hpp>
#include <hyprland/src/output/Monitor.hpp>
#include <hyprland/src/pointer/cursor/CursorShapeOverrideController.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>
#include <hyprland/src/managers/eventLoop/EventLoopManager.hpp>
#include <hyprland/src/managers/KeybindManager.hpp>
#include <hyprland/src/state/WorkspaceState.hpp>
#include <hyprland/src/config/shared/actions/ConfigActions.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>

using namespace std::chrono_literals;

bool COverview::selectHoveredWorkspace() {
    if (closing)
        return false;

    updateHoveredFromMouse();
    closeOnID = hoveredID >= 0 && hoveredID < (int)images.size() ? hoveredID : -1;
    return closeOnID != -1;
}

int64_t COverview::selectedWorkspaceID() const {
    const int id = closeOnID == -1 ? openedID : closeOnID;
    if (id < 0 || id >= (int)images.size())
        return WORKSPACE_INVALID;

    return images[id].workspaceID;
}

bool COverview::selectWorkspaceByID(int64_t workspaceID) {
    if (closing)
        return false;

    for (size_t i = 0; i < images.size(); ++i) {
        if (images[i].workspaceID != workspaceID)
            continue;

        closeOnID = i;
        return true;
    }

    return false;
}

bool COverview::selectVisibleIndex(size_t index) {
    if (closing)
        return false;

    size_t visible = 0;
    for (size_t i = 0; i < images.size(); ++i) {
        if (images[i].workspaceID == WORKSPACE_INVALID)
            continue;

        if (visible == index) {
            closeOnID = i;
            return true;
        }

        ++visible;
    }

    return false;
}

void COverview::updateHoveredFromMouse() {
    const auto MON = pMonitor.lock();
    if (!MON)
        return;

    const int newHoveredID = tileIndexAtPoint(lastMousePosLocal, size->value(), GAP_WIDTH, currentOuterInset(), true);
    const int newApp = drawerAppAt(lastMousePosLocal);
    if (newHoveredID == hoveredID && newApp == drawer.hoverApp)
        return;

    hoveredID = newHoveredID;
    drawer.hoverApp = newApp;
    damage();
}

void COverview::ensureKbFocusInitialized() {
    if (kbFocusID != -1)
        return;

    // try to set to current openedID
    if (openedID != -1) {
        kbFocusID = openedID;
        return;
    }

    // fallback: first valid tile
    for (size_t i = 0; i < images.size(); ++i) {
        if (isTileValid(i)) {
            kbFocusID = i;
            return;
        }
    }
}

bool COverview::isTileValid(int id) const {
    if (id < 0 || id >= (int)images.size())
        return false;
    return images[id].workspaceID != WORKSPACE_INVALID;
}

int COverview::tileForWorkspaceID(int wsid) const {
    for (size_t i = 0; i < images.size(); ++i) {
        if (images[i].workspaceID == wsid)
            return (int)i;
    }
    return -1;
}

int COverview::tileForVisibleIndex(int vIdx) const {
    if (vIdx < 0)
        return -1;
    int seen = 0;
    for (size_t i = 0; i < images.size(); ++i) {
        if (images[i].workspaceID == WORKSPACE_INVALID)
            continue;
        if (seen == vIdx)
            return (int)i;
        ++seen;
    }
    return -1;
}

Vector2D COverview::tilePointToWorkspacePoint(int id, const Vector2D& localPoint) const {
    const auto MON = pMonitor.lock();
    if (!MON)
        return {};

    const auto tileBox = tileBoxForIndex(id, size->value(), GAP_WIDTH, currentOuterInset(), true);
    const Vector2D inTile = localPoint - Vector2D{tileBox.x, tileBox.y};

    return MON->m_position + Vector2D{
        std::clamp(inTile.x / std::max(1.0, tileBox.w), 0.0, 1.0) * MON->m_size.x,
        std::clamp(inTile.y / std::max(1.0, tileBox.h), 0.0, 1.0) * MON->m_size.y,
    };
}

PHLWINDOW COverview::windowAtTilePoint(int id, const Vector2D& localPoint) const {
    if (!isTileValid(id))
        return nullptr;

    PHLWORKSPACE WORKSPACE;
    if (images[id].pWorkspace) {
        WORKSPACE = images[id].pWorkspace;
    }
    else {
        for (const auto& w : State::workspaceState()->workspacesCopy()) {
            if (w->m_id == images[id].workspaceID) {
                WORKSPACE = w;
                break;
            }
        }
    }

    if (!WORKSPACE)
        return nullptr;

    const auto POINT = tilePointToWorkspacePoint(id, localPoint);
    const auto& windows = Desktop::windowState()->windows();
    for (auto it = windows.rbegin(); it != windows.rend(); ++it) {
        const auto& window = *it;
        if (!windowVisibleOnWorkspace(window, WORKSPACE))
            continue;

        if (window->getWindowMainSurfaceBox().containsPoint(POINT))
            return window;
    }

    return nullptr;
}

void COverview::beginWindowDrag() {
    beginWindowDragAt(g_pInputManager->getMouseCoordsInternal());
}

void COverview::beginWindowDragAt(const Vector2D& global) {
    if (g_overviewDrag.state.active)
        return;

    std::vector<Hyprexpo::SGlobalTile> tiles;
    for (const auto& session : g_overviews) {
        auto* const OV = dynamic_cast<COverview*>(session.get());
        if (!OV)
            continue;
        auto overviewTiles = OV->globalTiles();
        tiles.insert(tiles.end(), overviewTiles.begin(), overviewTiles.end());
    }

    const Vector2D GLOBAL = global;
    const auto     HIT    = Hyprexpo::hitTestGlobalTile({GLOBAL.x, GLOBAL.y}, tiles);
    const auto     MON    = pMonitor.lock();
    if (!HIT || !MON || HIT->overviewKey != overviewMonitorKey(MON))
        return;

    const Vector2D LOCAL{HIT->pointLocal.x, HIT->pointLocal.y};
    const auto     WINDOW = windowAtTilePoint(HIT->tileIndex, LOCAL);
    if (!WINDOW)
        return;

    auto transition = Hyprexpo::transitionOverviewDrag(
        g_overviewDrag.state,
        {.type = Hyprexpo::EOverviewDragEventType::Press, .monitorKey = HIT->overviewKey, .tileIndex = HIT->tileIndex, .windowKey = reinterpret_cast<uint64_t>(WINDOW.get())},
        liveOverviewMonitorKeys());
    if (!transition.accepted)
        return;

    transition = Hyprexpo::transitionOverviewDrag(
        transition.next, {.type = Hyprexpo::EOverviewDragEventType::Target, .monitorKey = HIT->overviewKey, .tileIndex = HIT->tileIndex}, liveOverviewMonitorKeys());

    const auto POINT              = tilePointToWorkspacePoint(HIT->tileIndex, LOCAL);
    const auto BOX                = WINDOW->getWindowMainSurfaceBox();
    g_overviewDrag.state          = transition.next;
    g_overviewDrag.window         = WINDOW;
    g_overviewDrag.pressGlobal    = GLOBAL;
    g_overviewDrag.pointerGlobal  = GLOBAL;
    g_overviewDrag.grabOffset     = POINT - Vector2D{BOX.x, BOX.y};
    Pointer::Cursor::overrideController->setOverride("grabbing", Pointer::Cursor::CURSOR_OVERRIDE_UNKNOWN);
    damage();
}

void COverview::updateWindowDrag() {
    updateWindowDragAt(g_pInputManager->getMouseCoordsInternal());
}

void COverview::updateWindowDragAt(const Vector2D& global) {
    const auto MON = pMonitor.lock();
    if (!MON || !g_overviewDrag.state.active || g_overviewDrag.state.sourceMonitorKey != overviewMonitorKey(MON))
        return;

    const Vector2D GLOBAL = global;
    g_overviewDrag.pointerGlobal = GLOBAL;
    const auto dx = GLOBAL.x - g_overviewDrag.pressGlobal.x;
    const auto dy = GLOBAL.y - g_overviewDrag.pressGlobal.y;
    if (!g_overviewDrag.state.moved && std::hypot(dx, dy) < 12.0)
        return;

    if (!g_overviewDrag.state.moved) {
        const auto move = Hyprexpo::transitionOverviewDrag(g_overviewDrag.state, {.type = Hyprexpo::EOverviewDragEventType::Move}, liveOverviewMonitorKeys());
        if (!move.accepted)
            return;
        g_overviewDrag.state = move.next;
    }

    std::vector<Hyprexpo::SGlobalTile> tiles;
    for (const auto& session : g_overviews) {
        auto* const OV = dynamic_cast<COverview*>(session.get());
        if (!OV)
            continue;
        auto overviewTiles = OV->globalTiles();
        tiles.insert(tiles.end(), overviewTiles.begin(), overviewTiles.end());
    }

    const auto HIT = Hyprexpo::hitTestGlobalTile({GLOBAL.x, GLOBAL.y}, tiles);
    const auto target = Hyprexpo::transitionOverviewDrag(
        g_overviewDrag.state,
        HIT ? Hyprexpo::SOverviewDragEvent{.type = Hyprexpo::EOverviewDragEventType::Target, .monitorKey = HIT->overviewKey, .tileIndex = HIT->tileIndex}
            : Hyprexpo::SOverviewDragEvent{.type = Hyprexpo::EOverviewDragEventType::Target},
        liveOverviewMonitorKeys());
    if (target.accepted)
        g_overviewDrag.state = target.next;

    for (const auto key : g_overviewDrag.state.affectedMonitorKeys) {
        if (auto* const OV = overviewForMonitorKey(key))
            OV->damage();
    }
}

PHLWORKSPACE COverview::ensureWorkspaceForTile(int id) {
    if (!isTileValid(id))
        return nullptr;

    const auto MON = pMonitor.lock();
    if (!MON)
        return nullptr;

    auto& image = images[id];
    if (image.pWorkspace)
        return image.pWorkspace;

    PHLWORKSPACE workspace;
    for (const auto& w : State::workspaceState()->workspacesCopy()) {
        if (w->m_id == image.workspaceID) {
            workspace = w;
            break;
        }
    }

    if (!workspace)
        workspace = State::workspaceState()->create(image.workspaceID, MON->m_id, std::to_string(image.workspaceID), false);

    image.pWorkspace = workspace;
    return workspace;
}

bool COverview::finishWindowDrag() {
    const auto MON = pMonitor.lock();
    if (!MON || !g_overviewDrag.state.active || g_overviewDrag.state.sourceMonitorKey != overviewMonitorKey(MON))
        return false;

    const auto STATE      = g_overviewDrag.state;
    const auto TRANSITION = Hyprexpo::transitionOverviewDrag(STATE, {.type = Hyprexpo::EOverviewDragEventType::Release}, liveOverviewMonitorKeys());
    const bool CONSUMED   = STATE.moved;

    if (TRANSITION.drop && g_overviewDrag.window && reinterpret_cast<uint64_t>(g_overviewDrag.window.get()) == TRANSITION.drop->windowKey) {
        auto* const SOURCEOV  = gridOverviewForMonitorKey(TRANSITION.drop->sourceMonitorKey);
        auto* const TARGETOV  = gridOverviewForMonitorKey(TRANSITION.drop->targetMonitorKey);
        const auto  TARGETMON = TARGETOV ? TARGETOV->pMonitor.lock() : PHLMONITOR{};
        const int   SOURCE    = TRANSITION.drop->sourceTileIndex;
        const int   TARGET    = TRANSITION.drop->targetTileIndex;

        if (SOURCEOV && TARGETOV && TARGETMON && SOURCEOV->isTileValid(SOURCE) && TARGETOV->isTileValid(TARGET)) {
            PHLWORKSPACE SOURCEWS = SOURCEOV->images[SOURCE].pWorkspace;
            if (!SOURCEWS) {
                for (const auto& workspace : State::workspaceState()->workspacesCopy()) {
                    if (workspace->m_id == SOURCEOV->images[SOURCE].workspaceID) {
                        SOURCEWS = workspace;
                        break;
                    }
                }
            }

            const auto TARGETWS = TARGETOV->ensureWorkspaceForTile(TARGET);
            if (TARGETWS && TARGETWS->m_monitor.lock() != TARGETMON)
                Log::logger->log(Log::ERR, "[hyprexpo] rejected drag target workspace on the wrong monitor");
            else if (windowVisibleOnWorkspace(g_overviewDrag.window, SOURCEWS) && TARGETWS && TARGETWS != SOURCEWS) {
                const int64_t SOURCEWORKSPACEID = SOURCEOV->images[SOURCE].workspaceID;
                const int64_t TARGETWORKSPACEID = TARGETOV->images[TARGET].workspaceID;
                SOURCEOV->images[SOURCE].pWorkspace = SOURCEWS;
                Desktop::globalWindowController()->moveWindowToWorkspace(g_overviewDrag.window, TARGETWS);
                settleWorkspaceMoveAnimation(g_overviewDrag.window);
                SOURCEOV->redrawDraggedWorkspace(SOURCEWORKSPACEID);
                TARGETOV->redrawDraggedWorkspace(TARGETWORKSPACEID);
            }
        }
    }

    resetOverviewDrag(Hyprexpo::EOverviewDragEventType::Release);
    return CONSUMED;
}

// Touch hold-to-drag wrapper: mirrors the mouse press/move/release flow,
// but a touch down only arms a press — selection happens on release (tap),
// a horizontal swipe pans, and only a *still* hold picks a window up.
static constexpr uint32_t TOUCH_HOLD_MS   = 350;
static constexpr double   TOUCH_DRAG_PX   = 12.0; // same threshold as mouse drag
static constexpr double   HOLD_STILL_PX   = 8.0;  // hold must start still: drift voids pickup

COverview* COverview::touchOwner(int32_t touchID) {
    for (const auto& session : g_overviews) {
        auto* const OV = dynamic_cast<COverview*>(session.get());
        if (OV && !OV->closing && OV->touchPress.active && OV->touchPress.touchID == touchID)
            return OV;
    }
    return nullptr;
}

void COverview::cancelTouchPress() {
    touchPress.holdTimer.reset();
    touchPress.active      = false;
    touchPress.dragging    = false;
    touchPress.ribbonPanning = false;
    touchPress.touchID     = -1;
    touchPress.region      = (int)ERegion::None;
    touchPress.appIndex    = -1;
    touchPress.monitor.reset();
}

void COverview::touchPressDown(int32_t touchID, const Vector2D& global, const PHLMONITOR& monitor) {
    if (closing || !monitor)
        return;
    cancelTouchPress();
    touchPress.active      = true;
    touchPress.touchID     = touchID;
    touchPress.downGlobal  = global;
    touchPress.lastGlobal  = global;
    touchPress.dragging    = false;
    touchPress.monitor     = monitor;
    touchPress.region      = (int)regionAtPoint(global - monitor->m_position);
    touchPress.appIndex    = -1;
    if (touchPress.region == (int)ERegion::Grid)
        drawerPullBegin();
    // hover feedback under the finger while undecided
    lastMousePosLocal = global - monitor->m_position;
    updateHoveredFromMouse();
    // hold-to-drag: engage the window move if still down after the timeout
    const uint64_t KEY = overviewMonitorKey(monitor);
    const uint64_t GEN = m_sessionGeneration;
    touchPress.holdTimer  = makeShared<CEventLoopTimer>(
        std::chrono::milliseconds(TOUCH_HOLD_MS),
        [KEY, GEN, touchID](SP<CEventLoopTimer> self, void*) {
            self->cancel();
            auto* const OV = dynamic_cast<COverview*>(overviewForSession(KEY, GEN));
            if (!OV || OV->touchPress.holdTimer.get() != self.get())
                return;
            if (!OV->touchPress.active || OV->touchPress.touchID != touchID || OV->touchPress.dragging || OV->touchPress.ribbonPanning ||
                OV->closing)
                return;
            // A hold picks up only a still finger: drift means the press
            // already became a pan/scroll, so picking up now would grab a
            // window out from under a moving gesture at random.
            const auto drift = OV->touchPress.lastGlobal - OV->touchPress.downGlobal;
            if (std::hypot(drift.x, drift.y) >= HOLD_STILL_PX)
                return;
            OV->engageTouchDrag();
        },
        nullptr);
    g_pEventLoopManager->addTimer(touchPress.holdTimer);
}

void COverview::touchMotionEvent(int32_t touchID, const Vector2D& pos) {
    auto* const OWNER = touchOwner(touchID);
    if (!OWNER)
        return;
    const auto MON = OWNER->touchPress.monitor.lock();
    if (!MON)
        return;
    OWNER->touchPressMotion(touchID, MON->m_position + pos * MON->m_size);
}

void COverview::touchPressMotion(int32_t touchID, const Vector2D& global) {
    if (!touchPress.active || touchPress.touchID != touchID || closing)
        return;
    const double dx = global.x - touchPress.lastGlobal.x;
    const double dy = global.y - touchPress.lastGlobal.y;
    touchPress.lastGlobal = global;
    if (touchPress.region == (int)ERegion::Grid) {
        // Vertical finger motion pulls the drawer (release decides
        // open vs snap-back). Close permission was latched at press
        // from the scroll position; pass only the displacement.
        drawerPullBy(dy);
        const auto MON = touchPress.monitor.lock();
        if (MON) {
            const int idx = drawerAppAt(global - MON->m_position);
            if (idx != drawer.hoverApp) {
                drawer.hoverApp = idx;
                damage();
            }
        }
        return;
    }
    if (touchPress.region != (int)ERegion::Ribbon)
        return; // Search: hold still, tap focuses on release
    if (!touchPress.dragging) {
        // One rule, no races: a horizontal-dominant swipe pans the strip
        // and locks the press into panning. Anything else stays undecided
        // (hover follows) until the hold timer picks the card up — a flick
        // never grabs a window by itself, so the same gesture can't
        // sometimes pan and sometimes drag.
        if (!touchPress.ribbonPanning) {
            const auto d = global - touchPress.downGlobal;
            if (std::abs(d.x) > std::abs(d.y) && std::abs(d.x) >= TOUCH_DRAG_PX) {
                touchPress.ribbonPanning = true;
                touchPress.holdTimer.reset();
            }
        }
        if (touchPress.ribbonPanning) {
            ribbonPanBy(dx);
            const auto MON = pMonitor.lock();
            if (!MON)
                return;
            lastMousePosLocal = global - MON->m_position;
            updateHoveredFromMouse();
            return;
        }
    }
    if (touchPress.dragging) {
        updateWindowDragAt(global);
    } else {
        const auto MON = pMonitor.lock();
        if (!MON)
            return;
        lastMousePosLocal = global - MON->m_position;
        updateHoveredFromMouse();
    }
}

void COverview::engageTouchDrag() {
    if (!touchPress.active || touchPress.dragging || touchPress.ribbonPanning || closing)
        return;
    if (touchPress.region == (int)ERegion::Grid) {
        // No hold action on app tiles: the grid order stays locked, so a
        // hold is just the start of a pull (or a tap on release).
        touchPress.holdTimer.reset();
        return;
    }
    if (touchPress.region != (int)ERegion::Ribbon)
        return;
    static auto* const* PDRAGDROPENABLE = (Hyprlang::INT* const*)HyprlandAPI::getConfigValue(PHANDLE, "plugin:hyprexpo:drag_drop_enable")->getDataStaticPtr();
    if (!**PDRAGDROPENABLE)
        return;
    beginWindowDragAt(touchPress.lastGlobal);
    touchPress.dragging = g_overviewDrag.state.active;
    if (touchPress.dragging) {
        touchPress.holdTimer.reset();
        damage();
    }
}

void COverview::touchPressUp(int32_t touchID) {
    auto* const OWNER = touchOwner(touchID);
    if (!OWNER)
        return;
    const bool        wasDragging = OWNER->touchPress.dragging;
    const bool        wasPanning  = OWNER->touchPress.ribbonPanning;
    const int         region      = OWNER->touchPress.region;
    const Vector2D    upGlobal    = OWNER->touchPress.lastGlobal;
    const Vector2D    downGlobal  = OWNER->touchPress.downGlobal;
    const PHLMONITOR  mon         = OWNER->touchPress.monitor.lock();
    OWNER->cancelTouchPress();
    if (OWNER->closing || !mon)
        return;
    const Vector2D upLocal = upGlobal - mon->m_position;
    if (region == (int)COverview::ERegion::Grid) {
        OWNER->drawerDragEnd();
        const auto md = upGlobal - downGlobal;
        if (std::hypot(md.x, md.y) >= 12.0)
            return; // was a pull, not a tap
        OWNER->drawerTap(upLocal);
        return;
    }
    if (region == (int)COverview::ERegion::Search) {
        OWNER->drawer.searchFocused = true;
        OWNER->damage();
        return;
    }
    if (wasPanning)
        return; // ribbon pan: release commits nothing, never selects
    if (wasDragging) {
        // drop: move the window (or no-op) and never fall through to select
        if (auto* const SOURCE = gridOverviewForMonitorKey(g_overviewDrag.state.sourceMonitorKey))
            SOURCE->finishWindowDrag();
        return;
    }
    {
        // dead flick: drifted too far to tap but never engaged anything
        // (e.g. a vertical swipe): select nothing instead of activating
        // whatever happens to sit under the release point.
        const auto md = upGlobal - downGlobal;
        if (std::hypot(md.x, md.y) >= TOUCH_DRAG_PX)
            return;
    }
    // tap: historical behavior, evaluated at the release point
    if (!mon)
        return;
    if (OWNER->size->getPercent() < 0.05f) {
        OWNER->close(false);
        return;
    }
    OWNER->lastMousePosLocal = upGlobal - mon->m_position;
    OWNER->updateHoveredFromMouse();
    if (OWNER->selectHoveredWorkspace())
        closeOverviewsSelecting(OWNER);
}

void COverview::touchPressCancel(int32_t touchID) {
    auto* const OWNER = touchOwner(touchID);
    if (!OWNER)
        return;
    if (OWNER->touchPress.region == (int)COverview::ERegion::Grid) {
        OWNER->drawerDragEnd(false);
        // Grace window, not a snap: the driver may re-press right after a
        // cancel (stillness looks like abandonment). Stamping holds the
        // sheet so a re-press continues the pull; true abandonment still
        // springs back once the window lapses.
        OWNER->drawerPullStamp();
    }
    if (OWNER->touchPress.dragging)
        resetOverviewDrag(Hyprexpo::EOverviewDragEventType::Cancel);
    OWNER->cancelTouchPress();
}

bool COverview::moveWindowBetweenVisibleIndices(size_t sourceIndex, size_t targetIndex, const PHLWINDOW& requestedWindow) {
    if (closing)
        return false;

    const int SOURCE = tileForVisibleIndex(sourceIndex);
    const int TARGET = tileForVisibleIndex(targetIndex);
    if (!isTileValid(SOURCE) || !isTileValid(TARGET) || SOURCE == TARGET)
        return false;

    PHLWORKSPACE SOURCEWS;
    if (images[SOURCE].pWorkspace) {
        SOURCEWS = images[SOURCE].pWorkspace;
    }
    else {
        for (const auto& w : State::workspaceState()->workspacesCopy()) {
            if (w->m_id == images[SOURCE].workspaceID) {
                SOURCEWS = w;
                break;
            }
        }
    }

    const auto TARGETWS = ensureWorkspaceForTile(TARGET);

    if (!SOURCEWS || !TARGETWS || SOURCEWS == TARGETWS)
        return false;

    PHLWINDOW window = requestedWindow;
    if (window) {
        if (!windowVisibleOnWorkspace(window, SOURCEWS))
            return false;
    } else {
        const auto& windows = Desktop::windowState()->windows();
        for (auto it = windows.rbegin(); it != windows.rend(); ++it) {
            const auto& candidate = *it;
            if (!windowVisibleOnWorkspace(candidate, SOURCEWS))
                continue;

            window = candidate;
            break;
        }
    }

    if (!window)
        return false;

    images[SOURCE].pWorkspace = SOURCEWS;
    const int64_t SOURCEWORKSPACEID = images[SOURCE].workspaceID;
    const int64_t TARGETWORKSPACEID = images[TARGET].workspaceID;
    Desktop::globalWindowController()->moveWindowToWorkspace(window, TARGETWS);
    settleWorkspaceMoveAnimation(window);
    redrawDraggedWorkspace(SOURCEWORKSPACEID);
    redrawDraggedWorkspace(TARGETWORKSPACEID);
    return true;
}

void COverview::redrawDraggedWorkspace(int64_t workspaceID) {
    if (workspaceID == WORKSPACE_INVALID)
        return;

    if (std::find(settlingRedrawWorkspaceIDs.begin(), settlingRedrawWorkspaceIDs.end(), workspaceID) == settlingRedrawWorkspaceIDs.end())
        settlingRedrawWorkspaceIDs.push_back(workspaceID);
    redrawSettleTicks = 8;

    queueRedrawID(tileForWorkspaceID(workspaceID));
    flushQueuedRedraws();

    if (redrawSettleTimer)
        return;

    const auto OVERVIEWKEY = overviewMonitorKey(pMonitor.lock());
    const auto GENERATION = m_sessionGeneration;
    if (OVERVIEWKEY == 0)
        return;

    redrawSettleTimer = makeShared<CEventLoopTimer>(
        75ms,
        [OVERVIEWKEY, GENERATION](SP<CEventLoopTimer> self, void*) {
            auto* const OVERVIEW = dynamic_cast<COverview*>(overviewForSession(OVERVIEWKEY, GENERATION));
            if (!OVERVIEW || OVERVIEW->redrawSettleTimer.get() != self.get()) {
                self->cancel();
                return;
            }
            if (OVERVIEW->closing) {
                self->cancel();
                OVERVIEW->redrawSettleTimer.reset();
                return;
            }

            for (const auto workspaceID : OVERVIEW->settlingRedrawWorkspaceIDs)
                OVERVIEW->queueRedrawID(OVERVIEW->tileForWorkspaceID(workspaceID));

            OVERVIEW->flushQueuedRedraws();

            if (--OVERVIEW->redrawSettleTicks <= 0) {
                OVERVIEW->settlingRedrawWorkspaceIDs.clear();
                OVERVIEW->redrawSettleTimer.reset();
                self->cancel();
                return;
            }

            self->updateTimeout(100ms);
        },
        nullptr);
    g_pEventLoopManager->addTimer(redrawSettleTimer);
}

void COverview::queueRedrawID(int id) {
    if (!isTileValid(id))
        return;

    if (std::find(queuedRedrawIDs.begin(), queuedRedrawIDs.end(), id) == queuedRedrawIDs.end())
        queuedRedrawIDs.push_back(id);
}

void COverview::flushQueuedRedraws() {
    if (queuedRedrawIDs.empty())
        return;

    const auto ids = queuedRedrawIDs;
    queuedRedrawIDs.clear();

    for (const auto id : ids)
        redrawID(id);

    damage();
    if (const auto MON = pMonitor.lock())
        MON->scheduleFrame();
}

bool COverview::selectVisibleToken(const std::string& token) {
    if (closing)
        return false;

    static auto* const* PSELECTLABEL = (Hyprlang::INT* const*)HyprlandAPI::getConfigValue(PHANDLE, "plugin:hyprexpo:selection_label_enable")->getDataStaticPtr();
    static auto const*  PSELECTMAP   = (Hyprlang::STRING const*)HyprlandAPI::getConfigValue(PHANDLE, "plugin:hyprexpo:selection_label_token_map")->getDataStaticPtr();

    const std::string normalized = lowerString(trimString(token));
    if (normalized.empty())
        return false;

    if (**PSELECTLABEL) {
        const auto tokens = splitCommaList(std::string{*PSELECTMAP});
        for (size_t i = 0; i < tokens.size(); ++i) {
            if (tokens[i].empty() || lowerString(tokens[i]) != normalized)
                continue;

            return selectVisibleIndex(i);
        }

        return false;
    }

    const int visibleIndex = fallbackTokenToVisibleIndex(normalized);
    if (visibleIndex < 0)
        return false;

    return selectVisibleIndex(visibleIndex);
}

bool COverview::moveFocus(int dx, int dy) {
    ensureKbFocusInitialized();
    if (kbFocusID == -1)
        return false;

    const auto shape = currentGridShape();
    int x = kbFocusID % shape.cols;
    int y = kbFocusID / shape.cols;

    static auto* const* PWRAPH = (Hyprlang::INT* const*)HyprlandAPI::getConfigValue(PHANDLE, "plugin:hyprexpo:keynav_wrap_h")->getDataStaticPtr();
    static auto* const* PWRAPV = (Hyprlang::INT* const*)HyprlandAPI::getConfigValue(PHANDLE, "plugin:hyprexpo:keynav_wrap_v")->getDataStaticPtr();

    if (dx != 0) {
        static auto* const* PREADING = (Hyprlang::INT* const*)HyprlandAPI::getConfigValue(PHANDLE, "plugin:hyprexpo:keynav_reading_order")->getDataStaticPtr();
        int                 step     = dx > 0 ? 1 : -1;
        if (**PREADING) {
            // reading-order scan: proceed linearly across the grid (row-major)
            const int total = (int)images.size();
            int       idx   = kbFocusID;
            for (int tries = 0; tries < total; ++tries) {
                idx += step;
                if (idx < 0 || idx >= total) {
                    // wrap only if both wraps are enabled (edge of grid)
                    if (**PWRAPH && **PWRAPV)
                        idx = (idx + total) % total;
                    else
                        break;
                }
                if (isTileValid(idx)) {
                    kbFocusID = idx;
                    return true;
                }
            }
        } else {
            // in-row scan with optional horizontal wrap
            int nx = x;
            for (int tries = 0; tries < shape.cols; ++tries) {
                nx += step;
                if (nx < 0 || nx >= shape.cols) {
                    if (**PWRAPH)
                        nx = (nx + shape.cols) % shape.cols;
                    else
                        break;
                }
                const int nid = nx + y * shape.cols;
                if (isTileValid(nid)) {
                    kbFocusID = nid;
                    return true;
                }
            }
        }
    }

    if (dy != 0) {
        int step = dy > 0 ? 1 : -1;
        int ny   = y;
        for (int tries = 0; tries < shape.rows; ++tries) {
            ny += step;
            if (ny < 0 || ny >= shape.rows) {
                if (**PWRAPV)
                    ny = (ny + shape.rows) % shape.rows;
                else
                    break;
            }
            const int nid = x + ny * shape.cols;
            if (isTileValid(nid)) {
                kbFocusID = nid;
                return true;
            }
        }
    }
    return false;
}

bool COverview::onKbMoveFocus(const std::string& dir) {
    if (closing)
        return false;

    int                   dx = 0;
    int                   dy = 0;
    Hyprexpo::EDirection direction;
    if (dir == "left") {
        dx        = -1;
        direction = Hyprexpo::EDirection::Left;
    } else if (dir == "right") {
        dx        = 1;
        direction = Hyprexpo::EDirection::Right;
    } else if (dir == "up") {
        dy        = -1;
        direction = Hyprexpo::EDirection::Up;
    } else if (dir == "down") {
        dy        = 1;
        direction = Hyprexpo::EDirection::Down;
    } else
        return false;

    if (moveFocus(dx, dy)) {
        damage();
        return true;
    }

    return moveOverviewFocusAcrossMonitors(this, direction);
}

bool COverview::onKbConfirm() {
    if (closing)
        return false;
    ensureKbFocusInitialized();
    if (!isTileValid(kbFocusID))
        return false;
    closeOnID = kbFocusID;
    return true;
}

bool COverview::onKbSelectNumber(int num) {
    if (closing)
        return false;

    if (num == 0)
        num = 10;

    return selectWorkspaceByID(num);
}

bool COverview::onKbSelectToken(int visibleIdx) {
    if (closing)
        return false;
    if (visibleIdx < 0)
        return false;
    return selectVisibleIndex(visibleIdx);
}

static float lerpFloat(const float& from, const float& to, const float perc) {
    return (to - from) * perc + from;
}

static Vector2D lerp(const Vector2D& from, const Vector2D& to, const float perc) {
    return Vector2D{lerpFloat(from.x, to.x, perc), lerpFloat(from.y, to.y, perc)};
}

void COverview::setClosing(bool closing_) {
    closing = closing_;
}

void COverview::beginCancelSwipe() {
    closeOnID = openedID;
    closing   = true;
}

void COverview::onWindowMoveToWorkspace(const PHLWINDOW& window, const PHLWORKSPACE& workspace) {
    if (!closing || externalWorkspaceMoveDuringClose || !window)
        return;

    const auto monitor = pMonitor.lock();
    if (!monitor)
        return;

    const bool movedOnOverviewMonitor = window->m_monitor == monitor || (window->m_workspace && window->m_workspace->m_monitor == monitor) || (workspace && workspace->m_monitor == monitor);
    if (!Hyprexpo::shouldAbortOverviewCloseForWorkspaceMove(window->m_pinned, movedOnOverviewMonitor))
        return;

    externalWorkspaceMoveDuringClose = true;
    damage();
    monitor->scheduleFrame();
}

void COverview::resetSwipe() {
    swipeWasCommenced = false;
}

void COverview::onSwipeUpdate(double delta) {
    // Once the close animation is committed, ignore further swipe input so a
    // re-grabbed gesture can't warp size/pos back and replay the close.
    if (m_closeCommitted)
        return;

    m_isSwiping = true;

    const auto MON = pMonitor.lock();
    if (!MON) {
        m_isSwiping = false;
        return;
    }

    if (swipeWasCommenced)
        return;

    static auto* const* PDISTANCE = (Hyprlang::INT* const*)HyprlandAPI::getConfigValue(PHANDLE, "plugin:hyprexpo:gesture_distance")->getDataStaticPtr();
    const double        distance  = std::max<Hyprlang::INT>(1, **PDISTANCE);

    const float         PERC               = closing ? std::clamp(delta / distance, 0.0, 1.0) : 1.0 - std::clamp(delta / distance, 0.0, 1.0);
    const auto          WORKSPACE_FOCUS_ID = closing && closeOnID != -1 ? closeOnID : openedID;

    const auto          SIZEMAX = zoomSizeForCurrentGrid(MON->m_size);
    const auto          POSMAX  = zoomPosForTile(WORKSPACE_FOCUS_ID, SIZEMAX);

    const auto SIZEMIN = MON->m_size;
    const auto POSMIN  = Vector2D{0, 0};

    size->setCallbackOnEnd(nullptr);
    pos->setCallbackOnEnd(nullptr);

    size->setValueAndWarp(lerp(SIZEMIN, SIZEMAX, PERC));
    pos->setValueAndWarp(lerp(POSMIN, POSMAX, PERC));
}

void COverview::onSwipeEnd(bool switchToSelection) {
    if (m_closeCommitted)
        return;

    const auto MON = pMonitor.lock();
    if (!MON) {
        m_isSwiping       = false;
        swipeWasCommenced = false;
        closing           = true;
        destroyOverview(this);
        return;
    }

    const auto SIZEMIN = MON->m_size;
    const auto SIZEMAX = zoomSizeForCurrentGrid(MON->m_size);
    const auto span    = SIZEMAX - SIZEMIN;
    if (std::abs(span.x) <= 1e-6) {
        close(switchToSelection);
        return;
    }
    const auto PERC    = (size->value() - SIZEMIN).x / span.x;
    if (PERC > 0.5) {
        close(switchToSelection);
        return;
    }
    *size = MON->m_size;
    *pos  = {0, 0};

    size->setCallbackOnEnd([this](WP<Hyprutils::Animation::CBaseAnimatedVariable> thisptr) { redrawAll(true); });

    swipeWasCommenced = true;
    m_isSwiping       = false;
}

// The submap is compositor-global, but every overview enters and leaves it
// independently. Refcount it so opening on several monitors installs it once
// and only the last overview to close tears it down: without this the first
// monitor to close would drop keyboard navigation for the ones still open.
static int         g_submapRefs     = 0;
static std::string g_previousSubmap = "";

void enterOverviewSubmap(bool& submapActive) {
    static auto* const* PKEYNAV = (Hyprlang::INT* const*)HyprlandAPI::getConfigValue(PHANDLE, "plugin:hyprexpo:keynav_enable")->getDataStaticPtr();
    if (!**PKEYNAV || submapActive)
        return;

    if (g_submapRefs++ == 0) {
        // remember whatever submap was active so we can restore it exactly on close, instead
        // of always dropping back to Hyprland's bare default submap. Configs that nest their
        // entire keybind set inside a named submap (a common pattern for e.g. a "disable all
        // keybinds" toggle) would otherwise get silently kicked out of it every time the
        // overview closes.
        //
        // The capture is global, not per-overview: only the first overview to open sees
        // the user's real submap. The others would capture "hyprexpo" and restore that.
        g_previousSubmap = g_pKeybindManager->getCurrentSubmap().name;
        // switch to a dedicated submap for hyprexpo navigation
        (void)Config::Actions::setSubmap("hyprexpo");
    }
    submapActive = true;
}

void leaveOverviewSubmap(bool& submapActive) {
    if (!submapActive)
        return;

    if (--g_submapRefs <= 0) {
        g_submapRefs = 0;
        // Restore what was captured on the way in, not this instance's copy: the overview
        // that closes last is not necessarily the one that opened first.
        (void)Config::Actions::setSubmap(g_previousSubmap);
    }
    submapActive = false;
}

void COverview::enterSubmapIfEnabled() {
    enterOverviewSubmap(submapActive);
}

void COverview::resetSubmapIfNeeded() {
    leaveOverviewSubmap(submapActive);
}
