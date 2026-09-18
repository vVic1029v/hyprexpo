#pragma once

// Front-facing addon API for in-expose sheets (app drawer today, more
// tomorrow). Rudimentary on purpose: one internal interface, no dynamic
// loading, no ABI. The core session owns the overview lifecycle, the
// workspace ribbon, provisioning, zoom, and gestures; addons own horizontal
// bands below the ribbon (search + app grid) including their input,
// animation, rendering, config, and behavior.
//
// Rules of the seam:
// - Core routes by region and never reaches past this interface (no
//   addon-state access from session code).
// - Addons never touch session rendering or workspace state directly; the
//   owner pointer is for damage/monitor/close signals only.
// - Config registration stays central (PluginConfig calls the addon's
//   static registerConfig); dispatcher registration stays central while
//   the behavior behind it lives here.
#include <cstdint>
#include <string>

#include <hyprland/src/devices/IKeyboard.hpp>
#include <hyprutils/math/Vector2D.hpp>

class COverview;

namespace Hyprexpo::Addon {

// Region classification below the ribbon strip. Values mirror
// COverview::ERegion so the core maps them 1:1.
enum class EBandRegion {
    None   = 0,
    Search = 1,
    Grid   = 2,
};

class IOverviewAddon {
  public:
    virtual ~IOverviewAddon() = default;

    virtual const char* name() const = 0;

    // Lifecycle: model scan on open, cache drop on close/teardown.
    virtual void onOpen()  = 0;
    virtual void onClose() = 0;

    // True while the sheet covers the ribbon (fitted drawer hides it).
    virtual bool hidesRibbon() const = 0;

    // Which band, if any, owns this monitor-local point (ribbon already
    // excluded by the core).
    virtual EBandRegion classifyBelowRibbon(const Vector2D& local) const = 0;

    // Per-frame animation step + render pass, called from fullRender.
    virtual void stepFrame()  = 0;
    virtual void renderPass() = 0;

    // Pointer, routed by the core when the band owns the region.
    virtual void pointerDown(const Vector2D& local, uint32_t button) = 0;
    virtual bool pointerDragActive() const                           = 0;
    virtual void pointerMove(const Vector2D& delta)                  = 0;
    virtual void pointerUp(const Vector2D& local)                    = 0;

    // Wheel / touchpad scroll over the owned band (already source-scaled).
    virtual void wheel(double steps) = 0;

    // Hover tracking for the owned band.
    virtual void updateHover(const Vector2D& local) = 0;

    // Touch. The core owns press identity (touchID); the addon owns the
    // gesture: latch on down, pull on motion, commit-or-tap on up.
    virtual void touchDown(const Vector2D& local)                          = 0;
    virtual void touchMotion(double dy)                                    = 0;
    virtual void touchUp(const Vector2D& upLocal, const Vector2D& pressDelta) = 0;
    virtual void touchCancel()                                             = 0;

    // Search strip focus + key routing.
    virtual void focusSearch()                                = 0;
    virtual bool searchKey(const IKeyboard::SKeyEvent& event) = 0;

    // `hyprexpo:drawer expand|collapse|toggle` behavior.
    virtual void onDrawerCommand(const std::string& arg) = 0;

    // Live tunables the ribbon strip needs from the sheet (search height
    // frames the ribbon band above the docked drawer).
    virtual double searchH() const = 0;
    virtual double rowH() const    = 0;
};

} // namespace Hyprexpo::Addon
