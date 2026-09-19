#pragma once

#include <cstddef>
#include <cstdint>
#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <vector>

namespace Hyprexpo {

struct SColorRGBA {
    float r = 0.F;
    float g = 0.F;
    float b = 0.F;
    float a = 1.F;
};

struct SGradientSpec {
    SColorRGBA c1;
    SColorRGBA c2;
    float      angleDeg = 0.F;
    bool       valid    = false;
};

enum class EWorkspaceMethodMode {
    Center,
    First,
};

enum class ENumberKeyMode {
    Workspace,
    Index,
    Passthrough,
};

enum class EOverviewModePreference {
    Auto,
    Grid,
};

struct SWorkspaceMethodSpec {
    bool                 valid = false;
    EWorkspaceMethodMode mode  = EWorkspaceMethodMode::Center;
    std::string          workspace;
    std::string          error;
};

// Numeric workspace IDs a single workspace rule reserves: "11" or "r[11-20]".
struct SWorkspaceIDRange {
    int64_t first = 0;
    int64_t last  = 0;

    bool    operator==(const SWorkspaceIDRange&) const = default;
};

// Result of stripping an "all monitors" qualifier off an expo dispatcher arg.
struct SExpoCommand {
    std::string command;            // the arg with the qualifier removed
    bool        allMonitors = false;
};

// Split "toggle all", "on all" or a bare "all" (which means "toggle all") into
// the underlying command plus the all-monitors flag. Args without the
// qualifier come back unchanged with allMonitors = false.
SExpoCommand parseExpoCommand(const std::string& arg);

struct SPoint {
    double x = 0.0;
    double y = 0.0;
};

struct SSize {
    double w = 0.0;
    double h = 0.0;
};

struct SRect {
    double x = 0.0;
    double y = 0.0;
    double w = 0.0;
    double h = 0.0;
};

enum class EDirection {
    Left,
    Right,
    Up,
    Down,
};

struct SGlobalTile {
    uint64_t overviewKey = 0;
    int      tileIndex   = -1;
    SRect    overviewGlobal;
    SRect    tileGlobal;
};

struct STileTarget {
    uint64_t overviewKey = 0;
    int      tileIndex   = -1;
};

struct STileHit {
    uint64_t overviewKey = 0;
    int      tileIndex   = -1;
    SPoint   pointLocal;
};

enum class EOverviewDragEventType {
    Press,
    Move,
    Target,
    Release,
    Cancel,
    MonitorDestroyed,
    AllClose,
};

struct SOverviewDragState {
    bool                  active             = false;
    bool                  moved              = false;
    uint64_t              sourceMonitorKey   = 0;
    int                   sourceTileIndex    = -1;
    uint64_t              targetMonitorKey   = 0;
    int                   targetTileIndex    = -1;
    uint64_t              windowKey          = 0;
    std::vector<uint64_t> affectedMonitorKeys;
};

struct SOverviewDragEvent {
    EOverviewDragEventType type       = EOverviewDragEventType::Move;
    uint64_t               monitorKey = 0;
    int                    tileIndex  = -1;
    uint64_t               windowKey  = 0;
};

struct SOverviewDropIntent {
    uint64_t sourceMonitorKey = 0;
    int      sourceTileIndex  = -1;
    uint64_t targetMonitorKey = 0;
    int      targetTileIndex  = -1;
    uint64_t windowKey        = 0;
};

struct SOverviewDragTransition {
    SOverviewDragState                next;
    std::optional<SOverviewDropIntent> drop;
    std::vector<uint64_t>             cleanupMonitorKeys;
    bool                              accepted = false;
    bool                              cleanup  = false;
};

struct SGridShape {
    int cols = 1;
    int rows = 1;
};

struct STileLayout {
    SRect box;
    int   row = -1;
    int   col = -1;
};

struct SDropIntentInput {
    bool   targetValid     = false;
    SPoint pointerLocal    = {};
    SRect  targetTileLocal = {};
    SSize  workspaceSize   = {};
    SSize  windowSize      = {};
    SPoint grabOffset      = {};
    double minProxySize    = 24.0;
};

struct SDropIntentGeometry {
    bool   valid               = false;
    SPoint targetWorkspacePoint = {};
    SRect  targetProxyLocal     = {};
};

struct SGestureConfig {
    int         fingers = 0;
    std::string direction;
    bool        directionValid = false;
};

struct SGestureSyncDecision {
    bool        registerGesture = false;
    std::string error;
};

std::string trimString(std::string value);
std::string lowerString(std::string value);
std::vector<std::string> splitCommaList(const std::string& value);

SGridShape               computeDynamicGridShape(int visibleCount);
SGridShape               computeFixedGridShape(int64_t columns, int64_t rows);
std::optional<std::vector<int64_t>> expandDynamicWorkspaceIDs(const std::vector<int64_t>& workspaceIDs, bool fillGaps, std::size_t maxExpandedWorkspaces);
SSize                    aspectCorrectTileSize(double screenW, double screenH, int cols, int rows, double gap);
STileLayout              computeTileLayout(int index, int visibleCount, SGridShape shape, SSize total, double gap, bool centerPartialRows);
int                      tileIndexAtPoint(double x, double y, int visibleCount, SGridShape shape, SSize total, double gap, bool centerPartialRows);
std::optional<STileTarget> selectDirectionalTile(const SRect& source, EDirection direction, const std::vector<SGlobalTile>& candidates);
std::optional<STileHit>    hitTestGlobalTile(const SPoint& point, const std::vector<SGlobalTile>& tiles);
SOverviewDragTransition    transitionOverviewDrag(const SOverviewDragState& state, const SOverviewDragEvent& event, const std::vector<uint64_t>& liveMonitorKeys);

int                      clampGridColumns(int64_t columns);
int                      gridColumnsToIncludeWorkspace(int configuredColumns, int firstWorkspaceID, int activeWorkspaceID, int maxColumns, int fixedRows = 0);
std::size_t              centeredWorkspaceBacktrack(std::size_t tileCount, int64_t activeWorkspaceID, std::optional<int64_t> lowestExistingID,
                                                    std::optional<int64_t> highestExistingID);
std::optional<SWorkspaceIDRange> workspaceRuleIDRange(const std::string& workspaceString);
int                      tileIndexFromPoint(double x, double y, double width, double height, int sideLength);
int                      numberKeyToVisibleIndex(int number);
ENumberKeyMode           numberKeyModeFromString(const std::string& mode);
EOverviewModePreference  overviewModePreferenceFromString(const std::string& mode);
bool                     shouldAbortOverviewCloseForWorkspaceMove(bool windowPinned, bool movedOnOverviewMonitor);
SDropIntentGeometry      computeDropIntentGeometry(const SDropIntentInput& input);

SGestureSyncDecision     evaluateGestureSync(const SGestureConfig& config);

std::string              decodeConfigString(const void* dataptr, bool underlyingIsStdString, const std::string& fallback);

std::string              fallbackTokenForVisibleIndex(int visibleIndex);
int                      fallbackTokenToVisibleIndex(const std::string& token);

bool                     parseHexRGBA8(const std::string& value, SColorRGBA& out);
bool                     parseSolidColorSpec(const std::string& value, SColorRGBA& out);
SGradientSpec            parseGradientSpec(const std::string& value);
bool                     isGradientBorderSpec(const std::string& value);
bool                     shouldShowWorkspaceLabel(bool labelEnabled, const std::string& labelShow, bool isHovered, bool isFocused, bool isCurrent);
std::string              resolveBorderSpec(const std::string& modernSpec, const std::string& legacySpec);
std::string              resolveLabelPosition(const std::string& modernValue, bool modernSetByUser, const std::string& legacyValue, bool legacySetByUser);
int                      resolveLabelFontSize(int modernValue, bool modernSetByUser, int legacyValue, bool legacySetByUser);

SWorkspaceMethodSpec     parseWorkspaceMethodSpec(const std::string& method);
SWorkspaceMethodSpec     resolveWorkspaceMethodForMonitor(const std::string& config, const std::string& monitorName);

namespace Osk {
// On-screen-keyboard layer matching. Pure: layer enumeration stays in the
// compositor-facing helper; everything decidable lives here testable.
struct SLayerCandidate {
    std::string ns;
    double      x = 0.0, y = 0.0, w = 0.0, h = 0.0;
};
struct SScanResult {
    bool        hit = false;
    std::string learnedNs; // newly adopted keyboard namespace, if any
};

// configured + already-learned namespaces hit-test both the global and the
// monitor-local point (layer geometry space is not contracted anywhere
// stable). The first unlisted but keyboard-shaped surface (wide, short) is
// adopted on the spot so an unset config fills on first detect.
SScanResult scanLayers(const std::vector<std::string>& configured, const std::vector<std::string>& learned,
                       const std::vector<SLayerCandidate>& layers, double monW, double monH, double gx, double gy, double lx, double ly);

} // namespace Osk

namespace Fling {
// Touch-release inertia. Pure: the session owns the sample ring, the slope
// over the trailing window lives here testable.
struct SSample {
    double t = 0.0; // steady-clock seconds
    double y = 0.0; // cumulative finger travel px
};

// Finger-space px/s over [now - windowS, now]; 0 when undersampled or
// degenerate (still finger, single sample, zero span).
double releaseSlope(const std::vector<SSample>& ordered, double now, double windowS);

// Shared inertia constants: every flinger (drawer list, ribbon strip)
// starts, clamps, and drains velocity with these, so all surfaces feel
// identical. Values are the drawer-proven ones.
inline constexpr double MIN_PX_S  = 350.0; // below: no inertia starts
inline constexpr double MAX_PX_S  = 9000.0; // release-spike clamp
inline constexpr double FRICTION  = 5.0; // exponential drain per second
inline constexpr double STOP_PX_S = 80.0; // below: stop dead
inline constexpr double WINDOW_S  = 0.1; // trailing slope window
inline constexpr int    SAMPLES   = 8; // ring size per tracker

// Cumulative finger travel + trailing ring. Push once per motion event,
// slope() once per release; reset() when a new press takes over.
struct STracker {
    double t[SAMPLES] = {};
    double y[SAMPLES] = {};
    int    count      = 0;
    double cum        = 0.0;

    void push(double now, double dy) {
        cum += dy;
        const int i = count % SAMPLES;
        t[i]        = now;
        y[i]        = cum;
        ++count;
    }

    void reset() {
        count = 0;
        cum   = 0.0;
    }

    double slope(double now) const {
        const int total = std::min(count, SAMPLES);
        std::vector<SSample> ordered;
        ordered.reserve((size_t)std::max(0, total));
        for (int k = count - total; k < count; ++k)
            ordered.push_back({t[k % SAMPLES], y[k % SAMPLES]});
        return releaseSlope(ordered, now, WINDOW_S);
    }
};

// Finger-space px/s -> scroll-space velocity (content moves against the
// finger); 0 when below the start threshold.
inline double startVelocity(double fingerVel) {
    if (std::abs(fingerVel) < MIN_PX_S)
        return 0.0;
    return std::clamp(-fingerVel, -MAX_PX_S, MAX_PX_S);
}

// One exponential drain step; 0 once below the stop threshold.
inline double drain(double vel, double dt) {
    vel *= std::exp(-FRICTION * dt);
    return std::abs(vel) < STOP_PX_S ? 0.0 : vel;
}

} // namespace Fling

namespace Ribbon {
// Deterministic workspace-strip geometry. Pure: no compositor, no config,
// plain doubles throughout so the logic suite covers it directly.
inline constexpr double TILE_ASPECT_W = 16.0;
inline constexpr double TILE_ASPECT_H = 10.0;
inline constexpr double GAP_MULTIPLIER = 2.0; // finger-sized padding vs the grid default

struct SStrip {
    double x0 = 0.0;        // strip origin (scroll offset applied)
    double y0 = 0.0;        // vertically centered above the search strip
    double tileW = 1.0;
    double tileH = 1.0;
    double rowW = 1.0;      // full strip width
    double maxScroll = 0.0; // pan range, 0 when the strip fits
};

// Tile size comes from `cols` (never stretched); the row spans `count`
// pannable slots. scrollX is clamped to the pan range internally.
SStrip layoutStrip(double totalW, double totalH, int cols, int count, double gap, double outer, double searchH, double rowH, double scrollX,
                   double scale);
// Slot index for strip-local coords, or -1 (gap/miss). Caller bounds the
// result against its tile store.
int slotIndexAtPoint(double lx, double ly, const SStrip& strip, double gap, int count);

} // namespace Ribbon

}
