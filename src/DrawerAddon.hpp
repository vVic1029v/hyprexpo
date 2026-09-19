#pragma once

// In-expose app drawer, behind the addon API (see Addon.hpp): search strip
// + app grid on one pullable sheet. Owns its state, geometry, pull engine,
// textures, render pass, and search-key routing; the session reaches it
// only through IOverviewAddon plus searchH()/rowH() (which frame the
// ribbon band above the docked sheet).
#include "Addon.hpp"
#include "Drawer.hpp"
#include "HyprexpoLogic.hpp"

#include <hyprland/src/render/Texture.hpp>
#include <hyprutils/math/Box.hpp>

#include <map>
#include <memory>
#include <string>
#include <vector>

class COverview;

class CDrawerAddon final : public Hyprexpo::Addon::IOverviewAddon {
  public:
    explicit CDrawerAddon(COverview* owner) : m_owner(owner) {}

    const char* name() const override { return "drawer"; }

    void onOpen() override;
    void onClose() override;

    bool hidesRibbon() const override;

    Hyprexpo::Addon::EBandRegion classifyBelowRibbon(const Vector2D& local) const override;

    void stepFrame() override;
    void renderPass() override;

    void pointerDown(const Vector2D& local, uint32_t button) override;
    bool pointerDragActive() const override;
    void pointerMove(const Vector2D& delta) override;
    void pointerUp(const Vector2D& local) override;

    void wheel(double steps) override;
    void wheel(double fingerDy, bool discrete); // mouse wheels speak standard direction (see impl)
    // Discrete touchpad open (docked only): burst accumulator that fires at
    // the threshold. Separate from the analog wheel()/touch drag paths.
    void wheelTouchOpen(double fingerDy);
    bool isFitted() const { return state.fitted; }
    void startDive(); // select-close: drop the whole sheet below the screen edge, fast
    void updateHover(const Vector2D& local) override;

    void touchDown(const Vector2D& local) override;
    void touchMotion(double dy, double pressDist) override;
    void touchUp(const Vector2D& upLocal, const Vector2D& pressDelta) override;
    void touchCancel() override;

    void focusSearch() override;
    bool searchKey(const IKeyboard::SKeyEvent& event) override;

    void onDrawerCommand(const std::string& arg) override;

    double searchH() const override;
    double rowH() const override;

    // Addon-owned config keys (registered centrally from PluginConfig).
    static void registerConfig();

  private:
    struct State {
        bool        fitted       = false; // docked: 1 pinned row; fitted: fill + scroll
        float       anim         = 0.f;   // 0 docked -> 1 fitted, stepped per frame
        double      lastStepS    = 0.0;   // last anim step timestamp (0 = unset)
        double      scroll       = 0.0;   // content scroll px (fitted only)
        double      lastPullS    = 0.0;   // last pull input (any source); snap-back runs past idle
        double      pullVisual   = 0.0;   // live sheet offset px, follows the push
        double      wheelAcc     = 0.0;   // touchpad-open burst accumulation px
        double      wheelAccS    = 0.0;   // last touchpad-open burst event timestamp
        double      divePx       = 0.0;   // select-close sheet drop px (0 at rest)
        double      diveTarget   = 0.0;   // drop distance: sheet top to below the screen edge
        bool        pulling      = false; // a pull drag is in flight
        bool        pullEngaged  = false; // latched at press: may commit open/close
        int         hoverApp     = -1;    // filtered-list position under pointer
        bool        searchFocused = false;
        std::string query;
        bool        queryDirty = true;
        // model (rescanned per overview open) + texture caches (cleared on close)
        std::vector<Hyprexpo::Drawer::SApp>         apps;
        std::vector<std::string>                    recent; // launch history, most-recent-first
        std::vector<size_t>                         order;  // positions (EMPTY_SLOT = recent-row hole)
        int                                         recentShown = 0; // leading recent tiles in order
        std::map<std::string, SP<Render::ITexture>> iconTex;
        std::map<std::string, SP<Render::ITexture>> labelTex;
        SP<Render::ITexture>                        searchTex;
        // mouse pull press (parallel to the touch press in the session)
        bool mouseArmed = false;
        bool mouseMoved = false;
        // Grid touch press lifetime, tracked here so snap-back never runs
        // while a finger is physically down (resting fingers send no
        // motion events to refresh the idle clock).
        bool touchDownActive = false;
        // List fling (touch inertia): shared Fling tracker (same physics as
        // the ribbon strip) + scroll-space velocity.
        double flingVel = 0.0;
        Hyprexpo::Fling::STracker velTrack;
    };
    State state;

    COverview* m_owner = nullptr; // owning session: damage/monitor/close signals only

    // --- geometry (logical px, monitor-local) ---
    double top() const;       // y where the app grid starts (incl. live pull)
    double searchTop() const; // y where the search strip starts (rides the sheet)
    void   sheetEnds(double& dockedY, double& fittedY) const;
    double pullSpan() const;
    double pullThreshold() const; // commit travel: quarter screen (config floor)
    int    columns() const;
    double tileW() const;
    double clipH() const;
    double contentH() const;
    double maxScroll() const;
    bool   recentPad() const;
    int    appAt(const Vector2D& local) const;
    CBox   tileBox(int orderIdx) const;

    // --- pull engine ---
    void   beginPull();
    void   dragAdvance(double fingerDy);
    void   endDrag(bool commit = true);
    void   commitPull(bool open, double span);
    void   pushVisual(double delta, double lo, double hi);
    double pullStamp();
    double listScroll(double fingerDy);
    void   setFitted(bool fitted);
    // Release slope over the trailing motion window (finger-space px/s).
    double releaseVelocity(double now);
    // Starts list inertia when the release qualifies; no-op otherwise.
    void maybeStartFling();

    // --- model glue ---
    void refilter();
    void tap(const Vector2D& local);
    void launch(size_t orderIdx, bool forceNew = false);
    bool focusIfOpen(const Hyprexpo::Drawer::SApp& app);
    void typeText(const std::string& text);
    void backspace();
    void clearQuery();
    bool confirmTop(bool forceNew = false);

    // --- textures ---
    SP<Render::ITexture> iconTexture(const Hyprexpo::Drawer::SApp& app, int px, double scale);
    SP<Render::ITexture> labelTexture(const Hyprexpo::Drawer::SApp& app, double scale);
};
