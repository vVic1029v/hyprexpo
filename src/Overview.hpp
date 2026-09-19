#pragma once

#define WLR_USE_UNSTABLE

#include "globals.hpp"
#include "Drawer.hpp"
#include "DrawerAddon.hpp"
#include "IOverviewSession.hpp"
#include "HyprexpoLogic.hpp"
#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/render/Framebuffer.hpp>
#include <hyprland/src/render/Texture.hpp>
#include <hyprland/src/helpers/AnimatedVariable.hpp>
#include <hyprland/src/helpers/signal/Signal.hpp>
#include <hyprland/src/managers/eventLoop/EventLoopTimer.hpp>
#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

// saves on resources, but is a bit broken rn with blur.
// hyprland's fault, but cba to fix.
constexpr bool ENABLE_LOWRES = false;

class COverview final : public IOverviewSession {
  public:
    // The monitor is passed in rather than resolved from the cursor: with
    // several overviews alive they would all bind to the same output.
    COverview(PHLWORKSPACE startedOn_, PHLMONITOR monitor_, bool swipe = false, uint64_t sessionGeneration = 0);
    ~COverview() override;

    void render() override;
    void damage() override;
    void onDamageReported() override;
    void onPreRender() override;
    void onConfigReload() override {
        onDamageReported();
    }
    void prepareForTeardown() override {}
    std::expected<std::string, std::string> injectScrollingInput(const std::string&) override {
        return std::unexpected("active overview is not a scrolling session");
    }

    void setClosing(bool closing) override;
    void beginCancelSwipe() override;
    // True once close() has armed the teardown animation. Further gestures must
    // be ignored until the overview is destroyed, otherwise a second swipe
    // rewinds the in-flight close animation (the close "replays" from ~80%).
    bool closeCommitted() const override {
        return m_closeCommitted;
    }
    bool shouldRenderOverviewForMonitor(const PHLMONITOR& monitor) const;
    // Animation callbacks are handed the variable that fired, not the owner.
    // With several overviews alive the owner has to be resolved from it.
    bool ownsAnimVar(const WP<Hyprutils::Animation::CBaseAnimatedVariable>& var) const;
    void onWindowMoveToWorkspace(const PHLWINDOW& window, const PHLWORKSPACE& workspace);

    void resetSwipe() override;
    void onSwipeUpdate(double delta) override;
    void onSwipeEnd(bool switchToSelection) override;

    // close without a selection
    void          close(bool switchToSelection = true);
    bool          selectHoveredWorkspace();

    // keyboard navigation interface
    bool          onKbMoveFocus(const std::string& dir);
    bool          onKbConfirm();
    bool          onKbSelectNumber(int num);
    bool          onKbSelectToken(int visibleIdx);
    bool          selectVisibleToken(const std::string& token);
    int64_t       selectedWorkspaceID() const;
    int64_t       focusedWorkspaceID() const; // kbFocus tile's ID (select-workspace-first target)
    bool          selectWorkspaceByID(int64_t workspaceID);
    bool          selectVisibleIndex(size_t index);
    bool          moveWindowBetweenVisibleIndices(size_t sourceIndex, size_t targetIndex, const PHLWINDOW& window = nullptr);
    std::optional<Hyprexpo::SGlobalTile> focusedGlobalTile() const;
    std::vector<Hyprexpo::SGlobalTile>   globalTiles() const;
    bool                                 setKeyboardFocus(int tileIndex);

    bool blocksOverviewRendering() const override { return blockOverviewRendering; }
    bool blocksDamageReporting() const override { return blockDamageReporting; }    bool isSwiping() const override { return m_isSwiping; }
    bool ownsPointerInput() const override;
    PHLMONITOR monitor() const override { return pMonitor.lock(); }
    uint64_t sessionGeneration() const override { return m_sessionGeneration; }

    bool          blockOverviewRendering = false;
    bool          blockDamageReporting   = false;

    PHLMONITORREF pMonitor;
    bool          m_isSwiping = false;

    struct SWorkspaceImage {
        SP<Render::IFramebuffer> fb;
        int64_t                  workspaceID = -1;
        bool                     isNewWorkspace = false; // trailing "+" tile: ID beyond range, created on select
        PHLWORKSPACE             pWorkspace;
        CBox                     box;
        // Label textures per state for customization
        SP<Render::ITexture>     labelTexDefault;
        SP<Render::ITexture>     labelTexHover;
        SP<Render::ITexture>     labelTexFocus;
        SP<Render::ITexture>     labelTexCurrent;
        SP<Render::ITexture>     selectionLabelTex;
        Vector2D                 labelSizeDefault = {0, 0};
        Vector2D                 labelSizeHover   = {0, 0};
        Vector2D                 labelSizeFocus   = {0, 0};
        Vector2D                 labelSizeCurrent = {0, 0};
        Vector2D                 selectionLabelSize = {0, 0};
    };

  private:
    void       redrawID(int id, bool forcelowres = false);
    void       redrawAll(bool forcelowres = false);
    void       onWorkspaceChange();
    void       fullRender() override;
    Hyprexpo::SGridShape currentGridShape() const;
    double     currentOuterInset() const;
    Hyprexpo::STileLayout tileLayoutForIndex(int id, const Vector2D& totalSize, double gap, double outerInset = 0.0, bool centerPartialRows = true) const;
    CBox       tileBoxForIndex(int id, const Vector2D& totalSize, double gap, double outerInset = 0.0, bool centerPartialRows = true) const;
    int        tileIndexAtPoint(const Vector2D& point, const Vector2D& totalSize, double gap, double outerInset = 0.0, bool centerPartialRows = true) const;
    Vector2D   tilePosForID(int id, const Vector2D& totalSize, double gap, double outerInset = 0.0, bool centerPartialRows = true) const;
    Vector2D   zoomSizeForCurrentGrid(const Vector2D& monitorSize) const;
    Vector2D   zoomPosForTile(int id, const Vector2D& canvasSize) const;
    void       updateHoveredFromMouse();
    void       ensureKbFocusInitialized();
    bool       isTileValid(int id) const;
    bool       moveFocus(int dx, int dy);
    int        tileForWorkspaceID(int wsid) const;
    int        tileForVisibleIndex(int vIdx) const;

  public:
    // Drawer + ribbon public surface: dispatchers, search-key routing and
    // tests reach in here. Everything below stays private again afterwards.
    // In-expose app drawer regions. Ribbon owns the workspace grid (top
    // band); then a thin search strip; the app grid owns everything below.
    enum class ERegion { None, Ribbon, Search, Grid };

    // App drawer addon (search + app grid sheet). Replaces the old inline
    // SDrawerState + drawer* methods: session code uses OV->drawer through
    // IOverviewAddon and never reaches past it.
    CDrawerAddon drawer;

    // Ribbon geometry: workspace tiles live in the top band only. All tile
    // math flows through tileBoxForIndex/tileIndexAtPoint, so every consumer
    // (render, hover, drag, labels, damage) follows automatically.
    // Region map: ribbon band first, then the drawer addon's bands below.
    ERegion    regionAtPoint(const Vector2D& local) const;
    double     ribbonH() const;
    int        ribbonCount() const; // pannable slots: trailing-trimmed valid tiles
    double     ribbonMaxScroll() const; // pan range px (0 when the strip fits)
    void       ribbonScrollBy(double deltaPx); // wheel: content moves against delta
    void       ribbonPanBy(double fingerDx);   // touch: content follows the finger
    void       stepRibbon();                   // per-frame strip inertia (shared fling physics)
    void       ribbonScrollToWorkspace(int wsid); // center the tile (open-time)

  private:
    // Touch hold-to-drag wrapper (classic grid path): a touch down only
    // arms a press. Release before the hold timeout / drag threshold replays
    // the historical tap (select workspace). Holding past the timeout, or
    // moving past the threshold, engages the same window-drag machinery the
    // mouse path uses, so touch can move windows between workspaces.
    // Hold timeout and drag threshold mirror the mouse feel (350ms / 12px).
    struct STouchPress {
        bool                active   = false;
        int32_t             touchID  = -1;
        Vector2D            downGlobal{};
        Vector2D            lastGlobal{};
        bool                dragging = false;
        bool                ribbonPanning = false; // horizontal swipe pans the ribbon
        int                 region   = 0; // EDrawerRegion, resolved at down
        int                 appIndex = -1;
        PHLMONITORREF       monitor;
        SP<CEventLoopTimer> holdTimer;
    };
    STouchPress touchPress;
    bool ribbonMousePan = false; // right-drag on the ribbon pans the strip (scroll only)

    static COverview* touchOwner(int32_t touchID);
    void touchPressDown(int32_t touchID, const Vector2D& global, const PHLMONITOR& monitor);
    void touchMotionEvent(int32_t touchID, const Vector2D& pos);
    void touchPressMotion(int32_t touchID, const Vector2D& global);
    void touchPressUp(int32_t touchID);
    void touchPressCancel(int32_t touchID);
    void cancelTouchPress();
    void engageTouchDrag();
    void       beginWindowDrag();
    void       beginWindowDragAt(const Vector2D& global);
    bool       finishWindowDrag();
    void       updateWindowDrag();
    void       updateWindowDragAt(const Vector2D& global);
    void       redrawDraggedWorkspace(int64_t workspaceID);
    void       queueRedrawID(int id);
    void       flushQueuedRedraws();
    PHLWINDOW  windowAtTilePoint(int id, const Vector2D& localPoint) const;
    Vector2D   tilePointToWorkspacePoint(int id, const Vector2D& localPoint) const;
    PHLWORKSPACE ensureWorkspaceForTile(int id);
    void       enterSubmapIfEnabled();
    void       resetSubmapIfNeeded();

    bool       dynamicGrid = false;
    bool       emptyTilesSelectable = false;
    Hyprexpo::SGridShape gridShape{3, 3};
    int        GAP_WIDTH   = 5;
    CHyprColor BG_COLOR    = CHyprColor{0.1, 0.1, 0.1, 0.0};

    bool       damageDirty = false;

    Vector2D                     lastMousePosLocal = Vector2D{};
    double                       ribbonScrollX = 0.0; // horizontal pan offset px
    double                       ribbonTargetX = -1.0; // animated scroll goal px (-1 = none; direct input wins)
    double                       ribbonTargetFromX = 0.0; // scroll pos when the goal was set
    double                       ribbonTargetT0 = -1.0; // steady-clock start of the goal glide
    double                       ribbonVel     = 0.0; // strip inertia, same physics as the drawer list
    double                       ribbonLastStepS = 0.0;
    Hyprexpo::Fling::STracker    ribbonTrack; // touch finger travel (finger-space) for release slope
    Hyprexpo::Fling::STracker    wheelTrack; // touchpad + mouse-drag travel (scroll-space) for release slope
    double                       wheelS = 0.0; // last touchpad ribbon event (idle = release)

    int                          openedID  = -1;
    int                          closeOnID = -1;
    int                          kbFocusID = -1;
    int                          hoveredID = -1;
    bool                         submapActive = false;

    std::vector<int>             queuedRedrawIDs;
    std::vector<int64_t>         settlingRedrawWorkspaceIDs;
    int                          redrawSettleTicks = 0;
    SP<CEventLoopTimer>          redrawSettleTimer;

    std::vector<SWorkspaceImage> images;

    PHLWORKSPACE                 startedOn;

    PHLANIMVAR<Vector2D>         size;
    PHLANIMVAR<Vector2D>         pos;

    bool                         closing = false;
    bool                         m_closeCommitted = false;
    uint64_t                     m_sessionGeneration = 0;
    bool                         externalWorkspaceMoveDuringClose = false;

    CHyprSignalListener          mouseMoveHook;
    CHyprSignalListener          mouseButtonHook;
    CHyprSignalListener          touchMoveHook;
    CHyprSignalListener          touchDownHook;
    CHyprSignalListener          touchUpHook;
    CHyprSignalListener          touchCancelHook;
    CHyprSignalListener          mouseAxisHook;
    CHyprSignalListener          workspaceMoveHook;

    bool                         swipe             = false;
    bool                         swipeWasCommenced = false;
    bool                         showWorkspaceNumbers = false;
    bool                         showWorkspaceNames = false;
    bool                         animateEntry = false;
    bool                         wallpaperBg = false;
    std::chrono::steady_clock::time_point createdAt;

    friend class COverviewPassElement;
};

struct SOverviewDragRuntime {
    Hyprexpo::SOverviewDragState state;
    Vector2D                     pressGlobal;
    Vector2D                     pointerGlobal;
    Vector2D                     grabOffset;
    PHLWINDOW                    window;
};

inline SOverviewDragRuntime g_overviewDrag;

// Grid drag indices are workspace tiles, while scrolling indices address its native scene.
COverview* gridOverviewForMonitorKey(uint64_t key);
COverview* gridOverviewForGlobalPoint(const Vector2D& point);
void resetOverviewDrag(Hyprexpo::EOverviewDragEventType type = Hyprexpo::EOverviewDragEventType::Cancel, uint64_t monitorKey = 0);
