#include "Overview.hpp"
#include <any>
#include <map>
#include "HyprlandConfigCompat.hpp"
#include "HyprexpoConfig.hpp"
#include "ConfigValues.hpp"
#include "OskLayer.hpp"
#include "OverviewInternal.hpp"
#include "OverviewCapture.hpp"
#include "HyprexpoLogic.hpp"
#include <hyprland/src/event/EventBus.hpp>
#define private   public
#define protected public
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/config/ConfigValue.hpp>
#include <hyprland/src/config/ConfigManager.hpp>
#include <hyprland/src/config/shared/actions/ConfigActions.hpp>
#include <hyprland/src/config/shared/animation/AnimationTree.hpp>
#include <hyprland/src/config/shared/workspace/WorkspaceRuleManager.hpp>
#include <hyprland/src/desktop/state/FocusState.hpp>
#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/layout/LayoutManager.hpp>
#include <hyprland/src/layout/space/Space.hpp>
#include <hyprland/src/animation/AnimationManager.hpp>
#include <hyprland/src/animation/WorkspaceAnimationController.hpp>
#include <hyprland/src/pointer/cursor/CursorShapeOverrideController.hpp>
#include <hyprland/src/managers/eventLoop/EventLoopManager.hpp>
#include <hyprland/src/managers/eventLoop/EventLoopTimer.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>
#include <hyprland/src/helpers/time/Time.hpp>
#include <hyprland/src/helpers/varlist/VarList.hpp>
#include <hyprland/src/helpers/Format.hpp>
#include <hyprland/src/state/MonitorState.hpp>
#include <hyprland/src/state/WorkspaceState.hpp>
#include <drm_fourcc.h>
#undef private
#undef protected
#include "OverviewPassElement.hpp"
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/config/shared/complex/ComplexDataTypes.hpp>
#include <pango/pangocairo.h>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <optional>
#include <string>
#include <vector>
#include <hyprutils/utils/ScopeGuard.hpp>

using namespace std::chrono_literals;

namespace CompatHyprlandAPI {
static bool isStringConfig(const std::string& name) {
    static const std::map<std::string, bool> STRING_CONFIGS = {
        {"plugin:hyprexpo:workspace_method", true},
        {"plugin:hyprexpo:border_color", true},
        {"plugin:hyprexpo:border_color_current", true},
        {"plugin:hyprexpo:border_color_focus", true},
        {"plugin:hyprexpo:border_color_hover", true},
        {"plugin:hyprexpo:border_style", true},
        {"plugin:hyprexpo:drag_drop_proxy_border_color", true},
        {"plugin:hyprexpo:drag_drop_source_border_color", true},
        {"plugin:hyprexpo:label_text_mode", true},
        {"plugin:hyprexpo:label_token_map", true},
        {"plugin:hyprexpo:label_position", true},
        {"plugin:hyprexpo:selection_label_token_map", true},
        {"plugin:hyprexpo:selection_label_position", true},
        {"plugin:hyprexpo:label_show", true},
        {"plugin:hyprexpo:label_bg_shape", true},
        {"plugin:hyprexpo:label_font_family", true},
        {"plugin:hyprexpo:cancel_key", true},
        {"plugin:hyprexpo:gesture_direction", true},
        {"plugin:hyprexpo:border_grad_current", true},
        {"plugin:hyprexpo:border_grad_focus", true},
        {"plugin:hyprexpo:border_grad_hover", true},
    };

    return STRING_CONFIGS.contains(name);
}

static bool isFloatConfig(const std::string& name) {
    return name == "plugin:hyprexpo:tile_rounding_power" ||
           name == "plugin:hyprexpo:label_scale_hover" ||
           name == "plugin:hyprexpo:label_scale_focus";
}

static Config::STRING stringDefault(const std::string& name) {
    static const std::map<std::string, Config::STRING> DEFAULTS = {
        {"plugin:hyprexpo:workspace_method", HyprexpoConfig::WORKSPACE_METHOD_DEFAULT},
        {"plugin:hyprexpo:border_color", HyprexpoConfig::BORDER_COLOR_DEFAULT},
        {"plugin:hyprexpo:border_color_current", HyprexpoConfig::BORDER_COLOR_CURRENT_DEFAULT},
        {"plugin:hyprexpo:border_color_focus", HyprexpoConfig::BORDER_COLOR_FOCUS_DEFAULT},
        {"plugin:hyprexpo:border_color_hover", HyprexpoConfig::BORDER_COLOR_HOVER_DEFAULT},
        {"plugin:hyprexpo:border_style", HyprexpoConfig::BORDER_STYLE_DEFAULT},
        {"plugin:hyprexpo:drag_drop_proxy_border_color", HyprexpoConfig::DRAG_DROP_PROXY_BORDER_COLOR_DEFAULT},
        {"plugin:hyprexpo:drag_drop_source_border_color", HyprexpoConfig::DRAG_DROP_SOURCE_BORDER_COLOR_DEFAULT},
        {"plugin:hyprexpo:label_text_mode", HyprexpoConfig::LABEL_TEXT_MODE_DEFAULT},
        {"plugin:hyprexpo:label_token_map", HyprexpoConfig::LABEL_TOKEN_MAP_DEFAULT},
        {"plugin:hyprexpo:label_position", HyprexpoConfig::LABEL_POSITION_DEFAULT},
        {"plugin:hyprexpo:selection_label_token_map", HyprexpoConfig::SELECTION_LABEL_TOKEN_MAP_DEFAULT},
        {"plugin:hyprexpo:selection_label_position", HyprexpoConfig::SELECTION_LABEL_POSITION_DEFAULT},
        {"plugin:hyprexpo:label_show", HyprexpoConfig::LABEL_SHOW_DEFAULT},
        {"plugin:hyprexpo:label_bg_shape", HyprexpoConfig::LABEL_BG_SHAPE_DEFAULT},
        {"plugin:hyprexpo:label_font_family", HyprexpoConfig::LABEL_FONT_FAMILY_DEFAULT},
        {"plugin:hyprexpo:cancel_key", HyprexpoConfig::CANCEL_KEY_DEFAULT},
        {"plugin:hyprexpo:gesture_direction", HyprexpoConfig::GESTURE_DIRECTION_DEFAULT},
        {"plugin:hyprexpo:border_grad_current", HyprexpoConfig::BORDER_GRAD_CURRENT_DEFAULT},
        {"plugin:hyprexpo:border_grad_focus", HyprexpoConfig::BORDER_GRAD_FOCUS_DEFAULT},
        {"plugin:hyprexpo:border_grad_hover", HyprexpoConfig::BORDER_GRAD_HOVER_DEFAULT},
    };

    if (const auto it = DEFAULTS.find(name); it != DEFAULTS.end())
        return it->second;
    return "";
}

static Config::FLOAT floatDefault(const std::string& name) {
    if (name == "plugin:hyprexpo:tile_rounding_power")
        return HyprexpoConfig::TILE_ROUNDING_POWER_DEFAULT;
    if (name == "plugin:hyprexpo:label_scale_hover")
        return HyprexpoConfig::LABEL_SCALE_HOVER_DEFAULT;
    if (name == "plugin:hyprexpo:label_scale_focus")
        return HyprexpoConfig::LABEL_SCALE_FOCUS_DEFAULT;
    return 0.0F;
}

static Config::INTEGER intDefault(const std::string& name) {
    static const std::map<std::string, Config::INTEGER> DEFAULTS = {
        {"plugin:hyprexpo:columns", HyprexpoConfig::COLUMNS_DEFAULT},
        {"plugin:hyprexpo:rows", HyprexpoConfig::ROWS_DEFAULT},
        {"plugin:hyprexpo:gaps_in", HyprexpoConfig::GAPS_IN_DEFAULT},
        {"plugin:hyprexpo:bg_col", HyprexpoConfig::BG_COL_DEFAULT},
        {"plugin:hyprexpo:gesture_distance", HyprexpoConfig::GESTURE_DISTANCE_DEFAULT},
        {"plugin:hyprexpo:gesture_fingers", HyprexpoConfig::GESTURE_FINGERS_DEFAULT},
        {"plugin:hyprexpo:show_cursor", HyprexpoConfig::SHOW_CURSOR_DEFAULT},
        {"plugin:hyprexpo:show_pinned_windows", HyprexpoConfig::SHOW_PINNED_WINDOWS_DEFAULT},
        {"plugin:hyprexpo:drag_drop_enable", HyprexpoConfig::DRAG_DROP_ENABLE_DEFAULT},
        {"plugin:hyprexpo:max_workspace", HyprexpoConfig::MAX_WORKSPACE_DEFAULT},
        {"plugin:hyprexpo:show_workspace_numbers", HyprexpoConfig::SHOW_WORKSPACE_NUMBERS_DEFAULT},
        {"plugin:hyprexpo:workspace_number_color", HyprexpoConfig::WORKSPACE_NUMBER_COLOR_DEFAULT},
        {"plugin:hyprexpo:keynav_enable", HyprexpoConfig::KEYNAV_ENABLE_DEFAULT},
        {"plugin:hyprexpo:border_width", HyprexpoConfig::BORDER_WIDTH_DEFAULT},
        {"plugin:hyprexpo:drag_drop_proxy_color", HyprexpoConfig::DRAG_DROP_PROXY_COLOR_DEFAULT},
        {"plugin:hyprexpo:drag_drop_proxy_active_color", HyprexpoConfig::DRAG_DROP_PROXY_ACTIVE_COLOR_DEFAULT},
        {"plugin:hyprexpo:drag_drop_proxy_border_width", HyprexpoConfig::DRAG_DROP_PROXY_BORDER_WIDTH_DEFAULT},
        {"plugin:hyprexpo:drag_drop_proxy_rounding", HyprexpoConfig::DRAG_DROP_PROXY_ROUNDING_DEFAULT},
        {"plugin:hyprexpo:drag_drop_source_border_width", HyprexpoConfig::DRAG_DROP_SOURCE_BORDER_WIDTH_DEFAULT},
        {"plugin:hyprexpo:label_enable", HyprexpoConfig::LABEL_ENABLE_DEFAULT},
        {"plugin:hyprexpo:label_color", HyprexpoConfig::LABEL_COLOR_DEFAULT_LEGACY},
        {"plugin:hyprexpo:label_font_size", HyprexpoConfig::LABEL_FONT_SIZE_DEFAULT},
        {"plugin:hyprexpo:label_color_default", HyprexpoConfig::LABEL_COLOR_DEFAULT},
        {"plugin:hyprexpo:label_color_hover", HyprexpoConfig::LABEL_COLOR_HOVER_DEFAULT},
        {"plugin:hyprexpo:label_color_focus", HyprexpoConfig::LABEL_COLOR_FOCUS_DEFAULT},
        {"plugin:hyprexpo:label_color_current", HyprexpoConfig::LABEL_COLOR_CURRENT_DEFAULT},
        {"plugin:hyprexpo:label_bg_enable", HyprexpoConfig::LABEL_BG_ENABLE_DEFAULT},
        {"plugin:hyprexpo:label_bg_color", HyprexpoConfig::LABEL_BG_COLOR_DEFAULT},
        {"plugin:hyprexpo:label_bg_rounding", HyprexpoConfig::LABEL_BG_ROUNDING_DEFAULT},
        {"plugin:hyprexpo:label_padding", HyprexpoConfig::LABEL_PADDING_DEFAULT},
        {"plugin:hyprexpo:label_pixel_snap", HyprexpoConfig::LABEL_PIXEL_SNAP_DEFAULT},
        {"plugin:hyprexpo:selection_label_enable", HyprexpoConfig::SELECTION_LABEL_ENABLE_DEFAULT},
        {"plugin:hyprexpo:selection_label_offset_x", HyprexpoConfig::SELECTION_LABEL_OFFSET_X_DEFAULT},
        {"plugin:hyprexpo:selection_label_offset_y", HyprexpoConfig::SELECTION_LABEL_OFFSET_Y_DEFAULT},
        {"plugin:hyprexpo:selection_label_color", HyprexpoConfig::SELECTION_LABEL_COLOR_DEFAULT},
        {"plugin:hyprexpo:keynav_wrap_h", HyprexpoConfig::KEYNAV_WRAP_H_DEFAULT},
        {"plugin:hyprexpo:keynav_wrap_v", HyprexpoConfig::KEYNAV_WRAP_V_DEFAULT},
        {"plugin:hyprexpo:gaps_out", HyprexpoConfig::GAPS_OUT_DEFAULT},
        {"plugin:hyprexpo:keynav_reading_order", HyprexpoConfig::KEYNAV_READING_ORDER_DEFAULT},
        {"plugin:hyprexpo:tile_rounding", HyprexpoConfig::TILE_ROUNDING_DEFAULT},
        {"plugin:hyprexpo:tile_rounding_focus", HyprexpoConfig::TILE_ROUNDING_FOCUS_DEFAULT},
        {"plugin:hyprexpo:tile_rounding_current", HyprexpoConfig::TILE_ROUNDING_CURRENT_DEFAULT},
        {"plugin:hyprexpo:tile_rounding_hover", HyprexpoConfig::TILE_ROUNDING_HOVER_DEFAULT},
        {"plugin:hyprexpo:label_offset_x", HyprexpoConfig::LABEL_OFFSET_X_DEFAULT},
        {"plugin:hyprexpo:label_offset_y", HyprexpoConfig::LABEL_OFFSET_Y_DEFAULT},
        {"plugin:hyprexpo:label_font_bold", HyprexpoConfig::LABEL_FONT_BOLD_DEFAULT},
        {"plugin:hyprexpo:label_font_italic", HyprexpoConfig::LABEL_FONT_ITALIC_DEFAULT},
        {"plugin:hyprexpo:label_text_underline", HyprexpoConfig::LABEL_TEXT_UNDERLINE_DEFAULT},
        {"plugin:hyprexpo:label_text_strikethrough", HyprexpoConfig::LABEL_TEXT_STRIKETHROUGH_DEFAULT},
        {"plugin:hyprexpo:label_center_adjust_x", HyprexpoConfig::LABEL_CENTER_ADJUST_X_DEFAULT},
        {"plugin:hyprexpo:label_center_adjust_y", HyprexpoConfig::LABEL_CENTER_ADJUST_Y_DEFAULT},
    };

    if (const auto it = DEFAULTS.find(name); it != DEFAULTS.end())
        return it->second;
    return 0;
}

SConfigValueCompat* getConfigValue(HANDLE, const std::string& name) {
    static std::map<std::string, SConfigValueCompat> VALUES;
    auto& compat = VALUES[name];

    if (isStringConfig(name)) {
        compat.string = stringDefault(name);
        compat.ptr = &compat.string;
    } else if (isFloatConfig(name)) {
        compat.floating = floatDefault(name);
        compat.floatingPtr = &compat.floating;
        compat.ptr = &compat.floatingPtr;
    } else {
        compat.integer = intDefault(name);
        compat.integerPtr = &compat.integer;
        compat.ptr = &compat.integerPtr;
    }

    const auto VALUE = Config::mgr()->getConfigValue(name);
    if (VALUE.dataptr && VALUE.type && *VALUE.type == typeid(Config::STRING)) {
        auto* ptr = reinterpret_cast<Config::STRING* const*>(VALUE.dataptr);
        compat.ptr = ptr && *ptr ? *ptr : &compat.string;
        return &compat;
    }

    if (VALUE.dataptr) {
        compat.ptr = const_cast<void*>(static_cast<const void*>(VALUE.dataptr));
        return &compat;
    }

    return &compat;
}

bool configValueSetByUser(const std::string& name) {
    return Config::mgr()->getConfigValue(name).setByUser;
}

Config::INTEGER intValue(const std::string& name) {
    const auto VALUE = Config::mgr()->getConfigValue(name);
    if (!VALUE.dataptr || !VALUE.type || *VALUE.type != typeid(Config::INTEGER))
        return intDefault(name);

    auto* const* ptr = reinterpret_cast<Config::INTEGER* const*>(VALUE.dataptr);
    return ptr && *ptr ? **ptr : intDefault(name);
}

Config::STRING stringValue(const std::string& name) {
    const auto VALUE = Config::mgr()->getConfigValue(name);
    return Hyprexpo::decodeConfigString(VALUE.dataptr, VALUE.type && *VALUE.type == typeid(Config::STRING), stringDefault(name));
}
}

#define HyprlandAPI CompatHyprlandAPI

void clearWithColor(const CHyprColor& color) {
    glClearColor(color.r, color.g, color.b, color.a);
    glClear(GL_COLOR_BUFFER_BIT);
}

uint32_t framebufferFormatWithAlpha(uint32_t drmFormat) {
    const auto alphaFormat = NFormatUtils::alphaFormat(drmFormat);
    return alphaFormat == 0 ? DRM_FORMAT_ABGR8888 : alphaFormat;
}

bool isTransformRotated(wl_output_transform t) {
    return t == WL_OUTPUT_TRANSFORM_90 || t == WL_OUTPUT_TRANSFORM_270 ||
           t == WL_OUTPUT_TRANSFORM_FLIPPED_90 || t == WL_OUTPUT_TRANSFORM_FLIPPED_270;
}

std::string trimString(std::string value) {
    return Hyprexpo::trimString(std::move(value));
}

std::vector<std::string> splitCommaList(const std::string& value) {
    return Hyprexpo::splitCommaList(value);
}

std::string lowerString(std::string value) {
    return Hyprexpo::lowerString(std::move(value));
}

std::string fallbackTokenForVisibleIndex(int visibleIndex) {
    return Hyprexpo::fallbackTokenForVisibleIndex(visibleIndex);
}

int fallbackTokenToVisibleIndex(const std::string& token) {
    return Hyprexpo::fallbackTokenToVisibleIndex(token);
}

SHyprGradientSpec parseGradientSpec(const std::string& inRaw) {
    SHyprGradientSpec spec;
    const auto        parsed = Hyprexpo::parseGradientSpec(inRaw);
    if (!parsed.valid)
        return spec;

    spec.c1       = CHyprColor{parsed.c1.r, parsed.c1.g, parsed.c1.b, parsed.c1.a};
    spec.c2       = CHyprColor{parsed.c2.r, parsed.c2.g, parsed.c2.b, parsed.c2.a};
    spec.angleDeg = parsed.angleDeg;
    spec.valid    = true;
    return spec;
}

// Helper to detect if a border config string is a gradient or solid color
bool isGradientBorderSpec(const std::string& borderSpec) {
    return Hyprexpo::isGradientBorderSpec(borderSpec);
}

SP<Render::ITexture> renderNumberTexture(const std::string& text, const CHyprColor& color, const Vector2D& bufferSize, const float scale, const int fontSize) {
    const auto CAIROSURFACE = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, bufferSize.x, bufferSize.y);
    const auto CAIRO        = cairo_create(CAIROSURFACE);

    cairo_save(CAIRO);
    cairo_set_operator(CAIRO, CAIRO_OPERATOR_CLEAR);
    cairo_paint(CAIRO);
    cairo_restore(CAIRO);

    PangoLayout* layout = pango_cairo_create_layout(CAIRO);
    pango_layout_set_text(layout, text.c_str(), -1);

    // font options from config
    static auto* const PFONTFAM = (Hyprlang::STRING const*)HyprlandAPI::getConfigValue(PHANDLE, "plugin:hyprexpo:label_font_family")->getDataStaticPtr();
    static auto* const PFONTB   = (Hyprlang::INT* const*)HyprlandAPI::getConfigValue(PHANDLE, "plugin:hyprexpo:label_font_bold")->getDataStaticPtr();
    static auto* const PFONTI   = (Hyprlang::INT* const*)HyprlandAPI::getConfigValue(PHANDLE, "plugin:hyprexpo:label_font_italic")->getDataStaticPtr();
    static auto* const PTUNDER  = (Hyprlang::INT* const*)HyprlandAPI::getConfigValue(PHANDLE, "plugin:hyprexpo:label_text_underline")->getDataStaticPtr();
    static auto* const PTSTRIKE = (Hyprlang::INT* const*)HyprlandAPI::getConfigValue(PHANDLE, "plugin:hyprexpo:label_text_strikethrough")->getDataStaticPtr();

    PangoFontDescription* fontDesc = pango_font_description_from_string(*PFONTFAM);
    pango_font_description_set_size(fontDesc, fontSize * scale * PANGO_SCALE);
    pango_font_description_set_weight(fontDesc, **PFONTB ? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_NORMAL);
    pango_font_description_set_style(fontDesc, **PFONTI ? PANGO_STYLE_ITALIC : PANGO_STYLE_NORMAL);
    pango_layout_set_font_description(layout, fontDesc);
    pango_font_description_free(fontDesc);

    if (**PTUNDER || **PTSTRIKE) {
        PangoAttrList* attrs = pango_attr_list_new();
        if (**PTUNDER) {
            pango_attr_list_insert(attrs, pango_attr_underline_new(PANGO_UNDERLINE_SINGLE));
        }
        if (**PTSTRIKE) {
            pango_attr_list_insert(attrs, pango_attr_strikethrough_new(TRUE));
        }
        pango_layout_set_attributes(layout, attrs);
        pango_attr_list_unref(attrs);
    }

    pango_layout_set_width(layout, bufferSize.x * PANGO_SCALE);
    pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_NONE);

    cairo_set_source_rgba(CAIRO, color.r, color.g, color.b, color.a);

    PangoRectangle ink_rect, logical_rect;
    pango_layout_get_extents(layout, &ink_rect, &logical_rect);

    // center inside the provided buffer using ink rect (accounts for glyph bearings)
    const int    inkW   = std::max(0, ink_rect.width / PANGO_SCALE);
    const int    inkH   = std::max(0, ink_rect.height / PANGO_SCALE);
    const int    inkX   = ink_rect.x / PANGO_SCALE; // can be negative
    const int    inkY   = ink_rect.y / PANGO_SCALE; // can be negative
    static auto* const* PCENTERADJX = (Hyprlang::INT* const*)HyprlandAPI::getConfigValue(PHANDLE, "plugin:hyprexpo:label_center_adjust_x")->getDataStaticPtr();
    static auto* const* PCENTERADJY = (Hyprlang::INT* const*)HyprlandAPI::getConfigValue(PHANDLE, "plugin:hyprexpo:label_center_adjust_y")->getDataStaticPtr();
    const double xOffset = (bufferSize.x - inkW) / 2.0 - inkX + **PCENTERADJX;
    const double yOffset = (bufferSize.y - inkH) / 2.0 - inkY + **PCENTERADJY;

    cairo_move_to(CAIRO, xOffset, yOffset);
    pango_cairo_show_layout(CAIRO, layout);
    g_object_unref(layout);

    cairo_surface_flush(CAIROSURFACE);

    auto tex = g_pHyprRenderer->createTexture(CAIROSURFACE);
    if (tex) {
        tex->setTexParameter(GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        tex->setTexParameter(GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    }

    cairo_destroy(CAIRO);
    cairo_surface_destroy(CAIROSURFACE);

    return tex;
}

static void damageMonitor(WP<Hyprutils::Animation::CBaseAnimatedVariable> thisptr) {
    if (auto* const OV = overviewForAnimVar(thisptr))
        OV->damage();
}

SWorkspacePreviewState applyWorkspacePreviewState(const PHLWORKSPACE& workspace) {
    SWorkspacePreviewState state;
    if (!workspace)
        return state;

    state.visible        = workspace->m_visible;
    state.forceRendering = workspace->m_forceRendering;
    state.alphaValue     = workspace->m_alpha->value();
    state.alphaGoal      = workspace->m_alpha->goal();
    state.offsetValue    = workspace->m_renderOffset->value();
    state.offsetGoal     = workspace->m_renderOffset->goal();

    workspace->m_visible        = true;
    workspace->m_forceRendering = true;
    workspace->m_alpha->setValueAndWarp(1.F);
    *workspace->m_alpha = 1.F;
    workspace->m_renderOffset->setValueAndWarp(Vector2D{});
    *workspace->m_renderOffset = Vector2D{};

    return state;
}

void restoreWorkspacePreviewState(const PHLWORKSPACE& workspace, const SWorkspacePreviewState& state) {
    if (!workspace)
        return;

    workspace->m_visible        = state.visible;
    workspace->m_forceRendering = state.forceRendering;
    workspace->m_alpha->setValueAndWarp(state.alphaValue);
    *workspace->m_alpha = state.alphaGoal;
    workspace->m_renderOffset->setValueAndWarp(state.offsetValue);
    *workspace->m_renderOffset = state.offsetGoal;
}

std::vector<std::pair<PHLWORKSPACE, SWorkspacePreviewState>> applyExclusiveWorkspacePreviewState(const PHLWORKSPACE& targetWorkspace) {
    std::vector<std::pair<PHLWORKSPACE, SWorkspacePreviewState>> states;

    for (const auto& workspaceRef : State::workspaceState()->workspaces()) {
        const auto workspace = workspaceRef.lock();
        if (!workspace)
            continue;

        states.push_back({
            workspace,
            {
                .visible        = workspace->m_visible,
                .forceRendering = workspace->m_forceRendering,
                .alphaValue     = workspace->m_alpha->value(),
                .alphaGoal      = workspace->m_alpha->goal(),
                .offsetValue    = workspace->m_renderOffset->value(),
                .offsetGoal     = workspace->m_renderOffset->goal(),
            },
        });

        if (workspace == targetWorkspace) {
            workspace->m_visible        = true;
            workspace->m_forceRendering = true;
            workspace->m_alpha->setValueAndWarp(1.F);
            *workspace->m_alpha = 1.F;
        } else {
            workspace->m_visible        = false;
            workspace->m_forceRendering = false;
            workspace->m_alpha->setValueAndWarp(0.F);
            *workspace->m_alpha = 0.F;
        }

        workspace->m_renderOffset->setValueAndWarp(Vector2D{});
        *workspace->m_renderOffset = Vector2D{};
    }

    return states;
}

void restoreWorkspacePreviewStates(const std::vector<std::pair<PHLWORKSPACE, SWorkspacePreviewState>>& states) {
    for (const auto& [workspace, state] : states)
        restoreWorkspacePreviewState(workspace, state);
}

void normalizeMonitorWorkspaceRenderState(PHLMONITOR monitor) {
    if (!monitor || !monitor->m_activeWorkspace)
        return;

    for (const auto& workspaceRef : State::workspaceState()->workspaces()) {
        const auto workspace = workspaceRef.lock();
        if (!workspace || workspace->m_monitor != monitor || workspace->m_isSpecialWorkspace)
            continue;

        const bool active = workspace == monitor->m_activeWorkspace;
        workspace->m_forceRendering = false;

        if (active) {
            workspace->m_visible = true;
            Animation::Workspace::startAnimation(workspace, Animation::Workspace::ANIMATION_TYPE_IN, true, true);
        } else if (!workspace->m_alpha->isBeingAnimated() && !workspace->m_renderOffset->isBeingAnimated()) {
            workspace->m_visible = false;
        }
    }

    for (const auto& window : Desktop::windowState()->windows()) {
        if (!window || !window->m_isMapped || window->isHidden() || window->m_pinned || !window->m_workspace || window->m_workspace->m_monitor != monitor)
            continue;

        window->alpha(Desktop::View::WINDOW_ALPHA_MOVE_FROM_WORKSPACE)->setValueAndWarp(1.F);
        *window->alpha(Desktop::View::WINDOW_ALPHA_MOVE_FROM_WORKSPACE) = 1.F;
    }
}

bool showPinnedWindowsInPreview() {
    static auto* const* PSHOWPINNED = (Hyprlang::INT* const*)HyprlandAPI::getConfigValue(PHANDLE, "plugin:hyprexpo:show_pinned_windows")->getDataStaticPtr();
    return **PSHOWPINNED != 0;
}

std::vector<SPinnedWindowPreviewState> applyPinnedWindowPreviewState(bool showPinnedWindows) {
    std::vector<SPinnedWindowPreviewState> states;
    if (showPinnedWindows)
        return states;

    for (const auto& window : Desktop::windowState()->windows()) {
        if (!window || !window->m_isMapped || !window->m_pinned)
            continue;

        states.push_back({
            .window = window,
            .workspace = window->m_workspace,
            .pinned = window->m_pinned,
        });

        window->m_pinned = false;
        window->m_workspace.reset();
    }

    return states;
}

void restorePinnedWindowPreviewState(const std::vector<SPinnedWindowPreviewState>& states) {
    for (const auto& state : states) {
        if (!state.window)
            continue;

        state.window->m_workspace = state.workspace;
        state.window->m_pinned    = state.pinned;
    }
}

CPinnedWindowPreviewGuard::CPinnedWindowPreviewGuard(bool showPinnedWindows) : m_states(applyPinnedWindowPreviewState(showPinnedWindows)) {}

CPinnedWindowPreviewGuard::~CPinnedWindowPreviewGuard() {
    restorePinnedWindowPreviewState(m_states);
}

bool windowVisibleOnWorkspace(const PHLWINDOW& window, const PHLWORKSPACE& workspace) {
    return window && workspace && window->m_workspace == workspace && window->m_isMapped && !window->isHidden() && !window->m_pinned;
}

void settleWorkspaceMoveAnimation(const PHLWINDOW& window) {
    if (!window)
        return;

    window->alpha(Desktop::View::WINDOW_ALPHA_MOVE_TO_WORKSPACE)->resetAllCallbacks();
    window->alpha(Desktop::View::WINDOW_ALPHA_MOVE_TO_WORKSPACE)->setValueAndWarp(1.F);
    *window->alpha(Desktop::View::WINDOW_ALPHA_MOVE_TO_WORKSPACE) = 1.F;
    window->alpha(Desktop::View::WINDOW_ALPHA_MOVE_FROM_WORKSPACE)->setValueAndWarp(1.F);
    *window->alpha(Desktop::View::WINDOW_ALPHA_MOVE_FROM_WORKSPACE) = 1.F;
    window->m_monitorMovedFrom                                      = -1;
}

void settleWorkspaceMoveAnimations() {
    for (const auto& window : Desktop::windowState()->windows()) {
        if (!window)
            continue;

        const bool movingWorkspace = window->m_monitorMovedFrom != -1 || window->alpha(Desktop::View::WINDOW_ALPHA_MOVE_TO_WORKSPACE)->isBeingAnimated() ||
            window->alpha(Desktop::View::WINDOW_ALPHA_MOVE_FROM_WORKSPACE)->isBeingAnimated();
        if (!movingWorkspace)
            continue;

        settleWorkspaceMoveAnimation(window);
    }
}

std::vector<SWindowPreviewState> applyWorkspaceWindowGoalState(const PHLWORKSPACE& workspace) {
    std::vector<SWindowPreviewState> states;
    if (!workspace)
        return states;

    for (const auto& window : Desktop::windowState()->windows()) {
        if (!windowVisibleOnWorkspace(window, workspace))
            continue;

        states.push_back({
            .window        = window,
            .positionValue = window->m_realPosition->value(),
            .positionGoal  = window->m_realPosition->goal(),
            .sizeValue     = window->m_realSize->value(),
            .sizeGoal      = window->m_realSize->goal(),
        });

        window->m_realPosition->setValueAndWarp(window->m_realPosition->goal());
        window->m_realSize->setValueAndWarp(window->m_realSize->goal());
    }

    return states;
}

void restoreWorkspaceWindowGoalState(const std::vector<SWindowPreviewState>& states) {
    for (const auto& state : states) {
        if (!state.window)
            continue;

        state.window->m_realPosition->setValueAndWarp(state.positionValue);
        *state.window->m_realPosition = state.positionGoal;
        state.window->m_realSize->setValueAndWarp(state.sizeValue);
        *state.window->m_realSize = state.sizeGoal;
    }
}

static void recalculateWorkspaceLayout(const PHLWORKSPACE& workspace) {
    if (workspace && workspace->m_space)
        workspace->m_space->recalculate(Layout::RECALCULATE_REASON_WORKSPACE_CHANGE);
}

PHLWORKSPACE activateWorkspaceForPreview(PHLMONITOR monitor, const PHLWORKSPACE& workspace) {
    if (!monitor)
        return nullptr;

    const auto previousWorkspace = monitor->m_activeWorkspace;
    if (!workspace)
        return previousWorkspace;

    monitor->m_activeWorkspace = workspace;
    if (g_layoutManager)
        g_layoutManager->recalculateMonitor(monitor);
    recalculateWorkspaceLayout(workspace);

    return previousWorkspace;
}

void restoreActiveWorkspaceAfterPreview(PHLMONITOR monitor, const PHLWORKSPACE& workspace) {
    if (!monitor || !workspace)
        return;

    monitor->m_activeWorkspace = workspace;
    if (g_layoutManager)
        g_layoutManager->recalculateMonitor(monitor);
    recalculateWorkspaceLayout(workspace);
}

bool COverview::ownsAnimVar(const WP<Hyprutils::Animation::CBaseAnimatedVariable>& var) const {
    // Animated variables live in CUniquePointers, and locking a CWeakPointer
    // over a unique pointer asserts. get() reads the raw pointer without that
    // check, and CGenericAnimatedVariable derives from CBaseAnimatedVariable
    // through plain single inheritance, so the addresses compare directly.
    const Hyprutils::Animation::CBaseAnimatedVariable* const RAW = var.get();
    if (!RAW)
        return false;

    return RAW == size.get() || RAW == pos.get();
}

IOverviewSession* overviewForAnimVar(const WP<Hyprutils::Animation::CBaseAnimatedVariable>& var) {
    for (const auto& OV : g_overviews) {
        if (!OV)
            continue;
        if (OV->ownsAnimVar(var))
            return OV.get();
    }

    return nullptr;
}

IOverviewSession* overviewForMonitor(const PHLMONITOR& monitor) {
    if (!monitor)
        return nullptr;

    for (const auto& OV : g_overviews) {
        if (!OV)
            continue;
        if (OV->monitor() == monitor)
            return OV.get();
    }

    return nullptr;
}

uint64_t overviewMonitorKey(const PHLMONITOR& monitor) {
    return monitor ? static_cast<uint64_t>(monitor->m_id) + 1 : 0;
}

IOverviewSession* overviewForMonitorKey(uint64_t key) {
    if (key == 0)
        return nullptr;

    for (const auto& OV : g_overviews) {
        if (!OV)
            continue;
        const auto MON = OV->monitor();
        if (overviewMonitorKey(MON) == key)
            return OV.get();
    }

    return nullptr;
}

IOverviewSession* overviewForGlobalPoint(const Vector2D& point) {
    for (const auto& OV : g_overviews) {
        if (!OV)
            continue;
        const auto MON = OV->monitor();
        if (!MON)
            continue;
        if (point.x >= MON->m_position.x && point.x < MON->m_position.x + MON->m_size.x && point.y >= MON->m_position.y && point.y < MON->m_position.y + MON->m_size.y)
            return OV.get();
    }
    return nullptr;
}

IOverviewSession* overviewForSession(uint64_t monitorKey, uint64_t generation) {
    auto* const session = overviewForMonitorKey(monitorKey);
    return session && session->sessionGeneration() == generation ? session : nullptr;
}

COverview* gridOverviewForMonitorKey(uint64_t key) {
    return dynamic_cast<COverview*>(overviewForMonitorKey(key));
}

COverview* gridOverviewForGlobalPoint(const Vector2D& point) {
    return dynamic_cast<COverview*>(overviewForGlobalPoint(point));
}

bool overviewRegistered(const IOverviewSession* overview) {
    return overview && std::any_of(g_overviews.begin(), g_overviews.end(), [overview](const auto& entry) { return entry.get() == overview; });
}

IOverviewSession* pointerOverview() {
    for (const auto& session : g_overviews) {
        if (session && session->ownsPointerInput())
            return session.get();
    }
    return overviewForGlobalPoint(g_pInputManager->getMouseCoordsInternal());
}

bool COverview::ownsPointerInput() const {
    return g_overviewDrag.state.active && g_overviewDrag.state.sourceMonitorKey == overviewMonitorKey(monitor());
}

IOverviewSession* createOverview(const PHLMONITOR& monitor, bool swipe) {
    if (!monitor || !monitor->m_activeWorkspace)
        return nullptr;

    if (overviewForMonitor(monitor))
        return nullptr;

    auto session = createOverviewSession(monitor->m_activeWorkspace, monitor, swipe);
    if (!session)
        return nullptr;
    g_overviews.push_back(std::move(session));
    return g_overviews.back().get();
}

bool overviewOpen() {
    return std::any_of(g_overviews.begin(), g_overviews.end(), [](const auto& entry) { return static_cast<bool>(entry); });
}

IOverviewSession* activeOverview() {
    if (g_overviews.empty())
        return nullptr;

    if (g_overviews.size() == 1)
        return g_overviews.front().get();

    const auto KEYBOARD = g_keyboardOverviewMonitor.lock();
    if (auto* const OWNED = overviewForMonitor(KEYBOARD))
        return OWNED;

    const auto FOCUS = Desktop::focusState();
    if (auto* const FOCUSED = FOCUS ? overviewForMonitor(FOCUS->monitor()) : nullptr)
        return FOCUSED;

    if (auto* const HOVERED = overviewForMonitor(State::monitorState()->query().vec(g_pInputManager->getMouseCoordsInternal()).run()))
        return HOVERED;

    const auto FIRST = std::find_if(g_overviews.begin(), g_overviews.end(), [](const auto& entry) { return static_cast<bool>(entry); });
    return FIRST == g_overviews.end() ? nullptr : FIRST->get();
}

std::optional<Hyprexpo::SGlobalTile> COverview::focusedGlobalTile() const {
    const auto MON = pMonitor.lock();
    if (!MON || !isTileValid(kbFocusID))
        return std::nullopt;

    const auto& BOX = images[kbFocusID].box;
    return Hyprexpo::SGlobalTile{
        .overviewKey   = overviewMonitorKey(MON),
        .tileIndex     = kbFocusID,
        .overviewGlobal = {MON->m_position.x, MON->m_position.y, MON->m_size.x, MON->m_size.y},
        .tileGlobal     = {MON->m_position.x + BOX.x, MON->m_position.y + BOX.y, BOX.w, BOX.h},
    };
}

std::vector<Hyprexpo::SGlobalTile> COverview::globalTiles() const {
    std::vector<Hyprexpo::SGlobalTile> tiles;
    const auto                         MON = pMonitor.lock();
    if (!MON)
        return tiles;

    tiles.reserve(images.size());
    for (size_t i = 0; i < images.size(); ++i) {
        if (!isTileValid(i))
            continue;
        const auto& BOX = images[i].box;
        tiles.push_back({
            .overviewKey   = overviewMonitorKey(MON),
            .tileIndex     = static_cast<int>(i),
            .overviewGlobal = {MON->m_position.x, MON->m_position.y, MON->m_size.x, MON->m_size.y},
            .tileGlobal     = {MON->m_position.x + BOX.x, MON->m_position.y + BOX.y, BOX.w, BOX.h},
        });
    }
    return tiles;
}

bool COverview::setKeyboardFocus(int tileIndex) {
    if (closing || !isTileValid(tileIndex))
        return false;
    kbFocusID = tileIndex;
    damage();
    return true;
}

bool moveOverviewFocusAcrossMonitors(IOverviewSession* source, Hyprexpo::EDirection direction) {
    if (!overviewRegistered(source))
        return false;

    const auto SOURCE = source->focusedGlobalTile();
    if (!SOURCE)
        return false;

    std::vector<Hyprexpo::SGlobalTile> candidates;
    for (const auto& OV : g_overviews) {
        if (!OV)
            continue;
        if (OV.get() == source)
            continue;
        auto tiles = OV->globalTiles();
        candidates.insert(candidates.end(), tiles.begin(), tiles.end());
    }

    const auto DESTINATION = Hyprexpo::selectDirectionalTile(SOURCE->tileGlobal, direction, candidates);
    if (!DESTINATION)
        return false;

    auto* const TARGET = overviewForMonitorKey(DESTINATION->overviewKey);
    if (!TARGET || !TARGET->setKeyboardFocus(DESTINATION->tileIndex))
        return false;

    g_keyboardOverviewMonitor = TARGET->monitor();
    source->damage();
    return true;
}

void forEachOverview(const std::function<void(IOverviewSession&)>& fn) {
    std::vector<std::pair<uint64_t, uint64_t>> snapshot;
    snapshot.reserve(g_overviews.size());
    for (const auto& OV : g_overviews) {
        if (OV)
            snapshot.emplace_back(overviewMonitorKey(OV->monitor()), OV->sessionGeneration());
    }

    for (const auto& [monitorKey, generation] : snapshot) {
        // A callback may destroy an entry and create a replacement on the same monitor.
        auto* const OV = overviewForSession(monitorKey, generation);
        if (!OV)
            continue;

        fn(*OV);
    }
}

std::vector<uint64_t> liveOverviewMonitorKeys() {
    std::vector<uint64_t> keys;
    keys.reserve(g_overviews.size());
    for (const auto& OV : g_overviews) {
        if (OV)
            keys.push_back(overviewMonitorKey(OV->monitor()));
    }
    return keys;
}

void resetOverviewDrag(Hyprexpo::EOverviewDragEventType type, uint64_t monitorKey) {
    const auto transition = Hyprexpo::transitionOverviewDrag(g_overviewDrag.state, {.type = type, .monitorKey = monitorKey}, liveOverviewMonitorKeys());
    g_overviewDrag.state  = transition.next;
    if (!transition.cleanup)
        return;

    g_overviewDrag.window      = nullptr;
    g_overviewDrag.pressGlobal = {};
    g_overviewDrag.pointerGlobal = {};
    g_overviewDrag.grabOffset  = {};
    Pointer::Cursor::overrideController->setOverride("left_ptr", Pointer::Cursor::CURSOR_OVERRIDE_UNKNOWN);

    for (const auto key : transition.cleanupMonitorKeys) {
        auto* const OV = overviewForMonitorKey(key);
        if (!OV)
            continue;
        OV->damage();
        if (const auto MON = OV->monitor())
            g_pHyprRenderer->damageMonitor(MON);
    }
}

void closeOverviewsSelecting(IOverviewSession* selecting) {
    resetOverviewDrag(Hyprexpo::EOverviewDragEventType::AllClose);
    g_keyboardOverviewMonitor.reset();
    forEachOverview([selecting](IOverviewSession& overview) { overview.close(&overview == selecting); });
}

void closeOverviews(bool switchToSelection) {
    resetOverviewDrag(Hyprexpo::EOverviewDragEventType::AllClose);
    g_keyboardOverviewMonitor.reset();
    forEachOverview([switchToSelection](IOverviewSession& overview) { overview.close(switchToSelection); });
}

void destroyOverview(IOverviewSession* overview) {
    if (!overview)
        return;

    const auto IT = std::find_if(g_overviews.begin(), g_overviews.end(), [overview](const auto& entry) { return entry && entry.get() == overview; });
    if (IT == g_overviews.end())
        return;

    const auto MON = overview->monitor();
    resetOverviewDrag(Hyprexpo::EOverviewDragEventType::MonitorDestroyed, overviewMonitorKey(MON));
    if (g_keyboardOverviewMonitor.lock() == MON)
        g_keyboardOverviewMonitor.reset();

    auto OWNER = std::move(*IT);
    g_overviews.erase(IT);
    OWNER->prepareForTeardown();
    OWNER.reset();
}

void destroyAllOverviews() {
    resetOverviewDrag(Hyprexpo::EOverviewDragEventType::AllClose);
    g_keyboardOverviewMonitor.reset();

    std::vector<std::unique_ptr<IOverviewSession>> OWNERS;
    OWNERS.swap(g_overviews);
    for (const auto& owner : OWNERS)
        owner->prepareForTeardown();
    OWNERS.clear();
}

void removeOverview(WP<Hyprutils::Animation::CBaseAnimatedVariable> thisptr) {
    auto* const OV = overviewForAnimVar(thisptr);
    if (!OV)
        return;

    const auto MON = OV->monitor();
    destroyOverview(OV);

    if (!MON)
        return;

    // Force one normal compositor frame after the overview pass is removed.
    // Idle/empty workspaces may not produce their own damage immediately.
    g_pHyprRenderer->damageMonitor(MON);
    MON->scheduleFrame();
}

static bool shouldShowCursorDuringOverview() {
    static auto* const* PSHOWCURSOR = (Hyprlang::INT* const*)HyprlandAPI::getConfigValue(PHANDLE, "plugin:hyprexpo:show_cursor")->getDataStaticPtr();
    return **PSHOWCURSOR;
}

static void ensureOverviewCursorVisible(bool forceOverviewShape = false, bool refreshPosition = false) {
    if (!shouldShowCursorDuringOverview())
        return;

    g_pHyprRenderer->setCursorHidden(false);
    if (forceOverviewShape)
        Pointer::Cursor::overrideController->setOverride("left_ptr", Pointer::Cursor::CURSOR_OVERRIDE_UNKNOWN);
    if (refreshPosition)
        g_pInputManager->simulateMouseMovement();
}

WORKSPACEID workspaceIDForMonitor(const PHLMONITOR& monitor, const std::string& selector) {
    const auto FOCUS = Desktop::focusState();
    if (!FOCUS || !monitor)
        return WORKSPACE_INVALID;

    const auto previousMonitor = FOCUS->m_focusMonitor;
    Hyprutils::Utils::CScopeGuard restoreFocus{[&]() { FOCUS->m_focusMonitor = previousMonitor; }};
    // Hyprland's selector API reads global focus; enumeration must not emit focus events.
    FOCUS->m_focusMonitor = monitor;
    return getWorkspaceIDNameFromString(selector).id;
}

WORKSPACEID nextEmptyWorkspaceIDForMonitor(const PHLMONITOR& monitor) {
    if (!monitor)
        return WORKSPACE_INVALID;

    const auto workspaces = State::workspaceState()->workspacesCopy();
    // Unlike emptynm, r+ excludes existing foreign-owned workspaces as well as
    // foreign bindings. At most workspaces.size() candidates can be occupied.
    for (size_t step = 1; step <= workspaces.size() + 1; ++step) {
        const auto id = workspaceIDForMonitor(monitor, "r+" + std::to_string(step));
        if (id == WORKSPACE_INVALID)
            break;
        if (id <= 0 || id <= monitor->activeWorkspaceID())
            continue;

        const auto workspace = std::ranges::find_if(workspaces, [&](const auto& ws) { return ws->m_id == id; });
        if (workspace == workspaces.end() || ((*workspace)->m_monitor == monitor && (*workspace)->getWindowCount() == 0))
            return id;
    }
    return WORKSPACE_INVALID;
}

Hyprexpo::SGridShape COverview::currentGridShape() const {
    return gridShape;
}

double COverview::currentOuterInset() const {
    const auto MON = pMonitor.lock();
    if (!MON)
        return 0.0;

    static auto* const* PGAPSO = (Hyprlang::INT* const*)HyprlandAPI::getConfigValue(PHANDLE, "plugin:hyprexpo:gaps_out")->getDataStaticPtr();
    const double        percent = closing ? (1.0 - size->getPercent()) : size->getPercent();
    return std::max<Hyprlang::INT>(0, **PGAPSO) * percent;
}

Hyprexpo::STileLayout COverview::tileLayoutForIndex(int id, const Vector2D& totalSize, double gap, double outerInset, bool centerPartialRows) const {
    const auto shape = currentGridShape();
    const Hyprexpo::SSize total{std::max(0.0, totalSize.x - outerInset * 2.0), std::max(0.0, totalSize.y - outerInset * 2.0)};
    auto                  layout = Hyprexpo::computeTileLayout(id, (int)images.size(), shape, total, gap, centerPartialRows);
    layout.box.x += outerInset;
    layout.box.y += outerInset;
    return layout;
}

namespace {
// Ribbon: workspace tiles live in the top slice only; the search strip
// and app drawer own everything below it. Tiles are deterministic 16:10
// slots (never stretched), scaled up for touch, vertically centered in the
// space above the docked search strip. Geometry itself is pure logic
// (Hyprexpo::Ribbon, unit-tested); only the scale factor is read live so
// `hyprctl keyword` keeps working.
float ribbonScale() {
    const float scale = Hyprexpo::ConfigValues::getFloat("plugin:hyprexpo:ribbon_scale", HyprexpoConfig::RIBBON_SCALE_DEFAULT);
    return scale > 0.0F ? scale : HyprexpoConfig::RIBBON_SCALE_DEFAULT;
}
// Thin adapter: Vector2D in, pure strip out.
Hyprexpo::Ribbon::SStrip ribbonStrip(const Vector2D& total, int cols, int count, double gap, double outer, double searchH, double rowH, double scrollX) {
    return Hyprexpo::Ribbon::layoutStrip(total.x, total.y, cols, count, gap, outer, searchH, rowH, scrollX, (double)ribbonScale());
}
} // namespace

// Trailing-trimmed valid tiles: provisioning lays out a leading-contiguous
// range (center/backtrack + forward scan, or the dynamic set) and leaves
// the rest WORKSPACE_INVALID. The ribbon shows the whole range.
int COverview::ribbonCount() const {
    int count = 0;
    for (size_t i = 0; i < images.size(); ++i) {
        if (images[i].workspaceID != WORKSPACE_INVALID)
            count = (int)i + 1;
    }
    return count;
}

// Region map: ribbon band first, then the drawer addon's bands below.
// Session code never looks past the addon interface beyond this mapping.
COverview::ERegion COverview::regionAtPoint(const Vector2D& local) const {
    const auto MON = pMonitor.lock();
    if (!MON)
        return ERegion::None;
    const double H = MON->m_size.y;
    if (local.x < 0 || local.x >= MON->m_size.x || local.y < 0 || local.y >= H)
        return ERegion::None;
    if (!drawer.hidesRibbon() && local.y < ribbonH())
        return ERegion::Ribbon;
    switch (drawer.classifyBelowRibbon(local)) {
        case Hyprexpo::Addon::EBandRegion::Search: return ERegion::Search;
        case Hyprexpo::Addon::EBandRegion::Grid:   return ERegion::Grid;
        default:                                   return ERegion::None;
    }
}

double COverview::ribbonMaxScroll() const {
    const auto MON = pMonitor.lock();
    if (!MON)
        return 0.0;
    return ribbonStrip(MON->m_size, std::max(1, currentGridShape().cols), ribbonCount(), (double)GAP_WIDTH, currentOuterInset(), drawer.searchH(), drawer.rowH(),
                       ribbonScrollX)
        .maxScroll;
}

void COverview::ribbonScrollBy(double deltaPx) {
    if (closing)
        return;
    const double max  = ribbonMaxScroll();
    const double next = max <= 0.0 ? 0.0 : std::clamp(ribbonScrollX + deltaPx, 0.0, max);
    if (next != ribbonScrollX) {
        ribbonScrollX = next;
        damage();
    }
}

void COverview::ribbonPanBy(double fingerDx) {
    // Touch: content follows the finger (drag right -> strip moves right).
    // Samples feed the release fling (shared drawer physics).
    ribbonTrack.push(std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(), fingerDx);
    ribbonScrollBy(-fingerDx);
}

void COverview::stepRibbon() {
    const double now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    const double dt  = ribbonLastStepS <= 0.0 ? 0.016 : std::min(0.1, now - ribbonLastStepS);
    ribbonLastStepS  = now;
    if (ribbonVel == 0.0)
        return;
    if (closing || drawer.hidesRibbon()) {
        ribbonVel = 0.0; // tearing down or covered: no motion to show
        return;
    }
    const double max = ribbonMaxScroll();
    ribbonScrollX    = max <= 0.0 ? 0.0 : std::clamp(ribbonScrollX + ribbonVel * dt, 0.0, max);
    if (ribbonScrollX <= 0.0 || ribbonScrollX >= max)
        ribbonVel = 0.0; // hit an end: stop dead, no bounce
    else
        ribbonVel = Hyprexpo::Fling::drain(ribbonVel, dt);
    damage();
}

// Open-time: center the active workspace's tile in the strip. Runs in the
// ctor with final geometry (outer read straight from config: the size
// animation does not exist yet, so currentOuterInset() is unsafe here).
void COverview::ribbonScrollToWorkspace(int wsid) {
    const auto MON = pMonitor.lock();
    if (!MON)
        return;
    const int id = tileForWorkspaceID(wsid);
    if (id < 0)
        return;
    const double outer = (double)std::max(0, Hyprexpo::ConfigValues::getInt("plugin:hyprexpo:gaps_out", HyprexpoConfig::GAPS_OUT_DEFAULT));
    const int    cols  = std::max(1, currentGridShape().cols);
    const int    count = ribbonCount();
    if (count <= 0)
        return;
    const auto   strip  = ribbonStrip(MON->m_size, cols, count, (double)GAP_WIDTH, outer, drawer.searchH(), drawer.rowH(), 0.0);
    const double center = strip.x0 + (double)id * (strip.tileW + Hyprexpo::Ribbon::GAP_MULTIPLIER * (double)GAP_WIDTH) + strip.tileW / 2.0;
    ribbonScrollX       = strip.maxScroll <= 0.0 ? 0.0 : std::clamp(center - MON->m_size.x / 2.0, 0.0, strip.maxScroll);
}

double COverview::ribbonH() const {
    const auto MON = pMonitor.lock();
    if (!MON)
        return 0.0;
    const auto strip =
        ribbonStrip(MON->m_size, std::max(1, currentGridShape().cols), ribbonCount(), (double)GAP_WIDTH, currentOuterInset(), drawer.searchH(), drawer.rowH(),
                    ribbonScrollX);
    return strip.y0 + strip.tileH;
}

CBox COverview::tileBoxForIndex(int id, const Vector2D& totalSize, double gap, double outerInset, bool centerPartialRows) const {
    // Ribbon slots (see above). Every tile consumer — render, hover, drag,
    // labels, damage — flows through here and follows.
    (void)centerPartialRows;
    const int cols = std::max(1, currentGridShape().cols);
    if (id < 0 || id >= ribbonCount())
        return CBox{{0, 0}, {0, 0}};
    const auto   strip = ribbonStrip(totalSize, cols, ribbonCount(), gap, outerInset, drawer.searchH(), drawer.rowH(), ribbonScrollX);
    const double rgap  = Hyprexpo::Ribbon::GAP_MULTIPLIER * gap;
    return CBox{{strip.x0 + id * (strip.tileW + rgap), strip.y0}, {strip.tileW, strip.tileH}};
}

int COverview::tileIndexAtPoint(const Vector2D& point, const Vector2D& totalSize, double gap, double outerInset, bool centerPartialRows) const {
    (void)centerPartialRows;
    const int  cols  = std::max(1, currentGridShape().cols);
    const auto strip = ribbonStrip(totalSize, cols, ribbonCount(), gap, outerInset, drawer.searchH(), drawer.rowH(), ribbonScrollX);
    const int  slot  = Hyprexpo::Ribbon::slotIndexAtPoint(point.x - strip.x0, point.y - strip.y0, strip, gap, ribbonCount());
    if (slot < 0 || slot >= (int)images.size())
        return -1;
    return slot;
}

Vector2D COverview::tilePosForID(int id, const Vector2D& totalSize, double gap, double outerInset, bool centerPartialRows) const {
    const auto box = tileBoxForIndex(id, totalSize, gap, outerInset, centerPartialRows);
    return {box.x, box.y};
}

// Ribbon-aware zoom canvas: a virtual size at which the tiles keep their
// exact ribbon geometry while one tile covers the screen. The old grid
// math sized tiles to the screen aspect, but ribbon tiles are fixed
// 16:10 * ribbon_scale, so it framed them 1.5x too large (over-zoom)
// and the end of the animation snapped back to the real layout.
Vector2D COverview::zoomSizeForCurrentGrid(const Vector2D& monitorSize) const {
    if (monitorSize.x <= 0.0 || monitorSize.y <= 0.0)
        return monitorSize;
    const int    cols   = std::max(1, currentGridShape().cols);
    const double coverW = std::max(monitorSize.x, monitorSize.y * 16.0 / 10.0);
    const double scale  = std::max(0.01F, ribbonScale());
    return {cols * coverW / (double)scale, monitorSize.y};
}

// Physical-px offset that centers tile `id` (laid out on `canvasSize`)
// on the screen. Start frame of the open animation / end frame of close.
Vector2D COverview::zoomPosForTile(int id, const Vector2D& canvasSize) const {
    const auto MON = pMonitor.lock();
    if (!MON)
        return {0, 0};
    const int count = std::max(1, ribbonCount());
    id              = std::clamp(id, 0, count - 1);
    const auto strip =
        ribbonStrip(canvasSize, std::max(1, currentGridShape().cols), count, 0.0, 0.0, drawer.searchH(), drawer.rowH(), ribbonScrollX);
    const double cx = strip.x0 + (double)id * strip.tileW + strip.tileW / 2.0; // gap is 0 at zoom framing
    const double cy = strip.y0 + strip.tileH / 2.0;
    return {(MON->m_size.x / 2.0 - cx) * MON->m_scale, (MON->m_size.y / 2.0 - cy) * MON->m_scale};
}

COverview::~COverview() {
    if (redrawSettleTimer) {
        redrawSettleTimer->cancel();
        redrawSettleTimer.reset();
    }
    Render::GL::g_pHyprOpenGL->makeEGLCurrent();
    images.clear(); // otherwise we get a vram leak
    drawer.onClose();
    Pointer::Cursor::overrideController->unsetOverride(Pointer::Cursor::CURSOR_OVERRIDE_UNKNOWN);
    ensureOverviewCursorVisible(false, true);
    if (const auto MON = pMonitor.lock()) {
        normalizeMonitorWorkspaceRenderState(MON);
        MON->m_blurFBDirty = true;
    }
    resetSubmapIfNeeded();
}

COverview::COverview(PHLWORKSPACE startedOn_, PHLMONITOR monitor_, bool swipe_, uint64_t sessionGeneration) : startedOn(startedOn_), m_sessionGeneration(sessionGeneration), swipe(swipe_), drawer(this) {
    const auto PMONITOR = monitor_;
    pMonitor            = PMONITOR;

    static auto* const* PCOLUMNS  = (Hyprlang::INT* const*)HyprlandAPI::getConfigValue(PHANDLE, "plugin:hyprexpo:columns")->getDataStaticPtr();
    static auto* const* PROWS     = (Hyprlang::INT* const*)HyprlandAPI::getConfigValue(PHANDLE, "plugin:hyprexpo:rows")->getDataStaticPtr();
    static auto* const* PGAPS     = (Hyprlang::INT* const*)HyprlandAPI::getConfigValue(PHANDLE, "plugin:hyprexpo:gaps_in")->getDataStaticPtr();
    static auto* const* PCOL      = (Hyprlang::INT* const*)HyprlandAPI::getConfigValue(PHANDLE, "plugin:hyprexpo:bg_col")->getDataStaticPtr();
    static auto* const* PSHOWNUM  = (Hyprlang::INT* const*)HyprlandAPI::getConfigValue(PHANDLE, "plugin:hyprexpo:show_workspace_numbers")->getDataStaticPtr();
    static auto* const* PDYNAMIC  = (Hyprlang::INT* const*)HyprlandAPI::getConfigValue(PHANDLE, "plugin:hyprexpo:dynamic_grid")->getDataStaticPtr();
    static auto* const* PFILLGAPS = (Hyprlang::INT* const*)HyprlandAPI::getConfigValue(PHANDLE, "plugin:hyprexpo:fill_gaps")->getDataStaticPtr();
    static auto* const* PMRUSORT  = (Hyprlang::INT* const*)HyprlandAPI::getConfigValue(PHANDLE, "plugin:hyprexpo:mru_sort")->getDataStaticPtr();
    static auto* const* PSHNAMES  = (Hyprlang::INT* const*)HyprlandAPI::getConfigValue(PHANDLE, "plugin:hyprexpo:show_workspace_names")->getDataStaticPtr();
    static auto* const* PANIMATE  = (Hyprlang::INT* const*)HyprlandAPI::getConfigValue(PHANDLE, "plugin:hyprexpo:animate_entry")->getDataStaticPtr();
    static auto* const* PWALLBG   = (Hyprlang::INT* const*)HyprlandAPI::getConfigValue(PHANDLE, "plugin:hyprexpo:wallpaper_bg")->getDataStaticPtr();
    static auto* const* PDRAGDROPENABLE = (Hyprlang::INT* const*)HyprlandAPI::getConfigValue(PHANDLE, "plugin:hyprexpo:drag_drop_enable")->getDataStaticPtr();

    createdAt            = std::chrono::steady_clock::now();
    gridShape            = Hyprexpo::computeFixedGridShape(**PCOLUMNS, **PROWS);
    GAP_WIDTH            = std::max<Hyprlang::INT>(0, **PGAPS);
    BG_COLOR             = **PCOL;
    showWorkspaceNumbers = **PSHOWNUM;
    showWorkspaceNames   = **PSHNAMES;
    animateEntry         = **PANIMATE;
    wallpaperBg          = **PWALLBG;
    dynamicGrid          = **PDYNAMIC;

    // Ribbon range: consecutive workspaces 1..M. M is the highest
    // in-use ID: occupied workspaces plus the opened one (an empty
    // focused workspace counts). Trailing dead empties above that vanish;
    // in-between empties always show. No methods, no skipping, no caps,
    // no sliding window: nothing wraps, nothing in use disappears. Tile
    // sizing still comes from columns/rows above; the strip pans when the
    // range overflows the output.
    int64_t highestID = (startedOn && startedOn->m_id >= 1) ? startedOn->m_id : 1;
    for (const auto& workspace : State::workspaceState()->workspacesCopy()) {
        if (!workspace || workspace->m_isSpecialWorkspace || workspace->m_id < 1)
            continue;
        if (workspace->getWindowCount() > 0)
            highestID = std::max(highestID, workspace->m_id);
    }
    emptyTilesSelectable = false; // every tile names a real ID now
    images.resize((size_t)std::max<int64_t>(1, highestID));
    for (size_t i = 0; i < images.size(); ++i)
        images[i].workspaceID = (int64_t)i + 1;

    if (dynamicGrid) {
        std::vector<int64_t> visibleWorkspaceIDs;
        const auto MON = pMonitor.lock();
        const int64_t currentWorkspaceID = startedOn ? startedOn->m_id : (MON ? MON->activeWorkspaceID() : WORKSPACE_INVALID);

        for (const auto& workspace : State::workspaceState()->workspacesCopy()) {
            if (!workspace || workspace->m_isSpecialWorkspace || workspace->m_monitor != MON || workspace->getWindowCount() <= 0)
                continue;

            visibleWorkspaceIDs.push_back(workspace->m_id);
        }

        if (visibleWorkspaceIDs.empty() && currentWorkspaceID != WORKSPACE_INVALID)
            visibleWorkspaceIDs.push_back(currentWorkspaceID);

        const auto expandedWorkspaceIDs =
            Hyprexpo::expandDynamicWorkspaceIDs(visibleWorkspaceIDs, **PFILLGAPS, HyprexpoConfig::DYNAMIC_GRID_MAX_TILES);
        if (expandedWorkspaceIDs)
            visibleWorkspaceIDs = *expandedWorkspaceIDs;
        else {
            visibleWorkspaceIDs = *Hyprexpo::expandDynamicWorkspaceIDs(visibleWorkspaceIDs, false, HyprexpoConfig::DYNAMIC_GRID_MAX_TILES);
            Log::logger->log(Log::ERR, "[hyprexpo] fill_gaps range exceeds {} tiles; using sparse workspace IDs", HyprexpoConfig::DYNAMIC_GRID_MAX_TILES);
        }

        if (**PMRUSORT) {
            const auto it = std::find(visibleWorkspaceIDs.begin(), visibleWorkspaceIDs.end(), currentWorkspaceID);
            if (it != visibleWorkspaceIDs.end() && it != visibleWorkspaceIDs.begin()) {
                const auto current = *it;
                visibleWorkspaceIDs.erase(it);
                visibleWorkspaceIDs.insert(visibleWorkspaceIDs.begin(), current);
            }
        }

        gridShape = Hyprexpo::computeDynamicGridShape((int)visibleWorkspaceIDs.size());
        images.resize(visibleWorkspaceIDs.size());
        for (size_t i = 0; i < visibleWorkspaceIDs.size(); ++i)
            images[i].workspaceID = visibleWorkspaceIDs[i];
    }

    // Trailing "+" tile, always one past whatever was provisioned (fixed
    // range or dynamic grid): selecting it focuses an ID that does not
    // exist yet, and the close path creates it (same helper as anchored
    // nonexistent tiles) — a new workspace from the exposé. Appended last
    // so it stays at the end under MRU sorts too.
    {
        int64_t topID = 0;
        for (const auto& image : images)
            topID = std::max(topID, image.workspaceID);
        images.push_back(SWorkspaceImage{});
        images.back().workspaceID    = topID + 1;
        images.back().isNewWorkspace = true;
    }

    // Ribbon opens scrolled to the active workspace; the rest of the
    // provisioned range pans in from either side.
    if (startedOn)
        ribbonScrollToWorkspace((int)startedOn->m_id);

    Render::GL::g_pHyprOpenGL->makeEGLCurrent();

    const auto tileSize = Hyprexpo::aspectCorrectTileSize(pMonitor->m_size.x, pMonitor->m_size.y, gridShape.cols, gridShape.rows, 0.0);
    CBox       monbox{0, 0, tileSize.w * 2, tileSize.h * 2};

    if (!ENABLE_LOWRES)
        monbox = {{0, 0}, pMonitor->m_pixelSize};

    int          currentid = 0;

    settleWorkspaceMoveAnimations();

    startedOn->m_visible = false;

    for (size_t i = 0; i < images.size(); ++i) {
        COverview::SWorkspaceImage& image = images[i];

        PHLWORKSPACE PWORKSPACE;
        for (const auto& w : State::workspaceState()->workspacesCopy()) {
            if (w->m_id == image.workspaceID) {
                PWORKSPACE = w;
                break;
            }
        }

        if (PWORKSPACE == startedOn)
            currentid = i;

        image.pWorkspace = PWORKSPACE;
        Hyprexpo::Capture::captureWorkspacePreview({
            .monitor              = PMONITOR,
            .workspace            = PWORKSPACE,
            .startedOn            = startedOn,
            .box                  = monbox,
            .showPinnedWindows    = showPinnedWindowsInPreview(),
            .blockSurfaceFeedback = true,
        }, image.fb);

        image.box = tileBoxForIndex((int)i, pMonitor->m_size, GAP_WIDTH, 0.0, true); // stock: literal inset; currentOuterInset() is unsafe here (size anim not created yet -> null deref)
    }
    PMONITOR->m_activeWorkspace        = startedOn;
    startedOn->m_visible               = true;
    Animation::Workspace::startAnimation(startedOn, Animation::Workspace::ANIMATION_TYPE_IN, true, true);

    const auto initSize = zoomSizeForCurrentGrid(pMonitor->m_size);
    Animation::mgr()->createAnimation(initSize, size, Config::animationTree()->getAnimationPropertyConfig("windowsMove"), AVARDAMAGE_NONE);
    Animation::mgr()->createAnimation(zoomPosForTile(currentid, initSize), pos, Config::animationTree()->getAnimationPropertyConfig("windowsMove"), AVARDAMAGE_NONE);

    size->setUpdateCallback(damageMonitor);
    pos->setUpdateCallback(damageMonitor);

    if (!swipe) {
        *size = pMonitor->m_size;
        *pos  = {0, 0};

        size->setCallbackOnEnd([this](auto) { redrawAll(true); });
    }

    openedID = currentid;

    ensureOverviewCursorVisible(true, true);

    lastMousePosLocal = g_pInputManager->getMouseCoordsInternal() - pMonitor->m_position;
    updateHoveredFromMouse();
    kbFocusID = openedID;

    drawer.onOpen();

    auto onCursorMove = [this](Event::SCallbackInfo& info) {
        if (closing)
            return;

        const Vector2D GLOBAL = g_pInputManager->getMouseCoordsInternal();
        for (const auto& session : g_overviews) {
            auto* const OV = dynamic_cast<COverview*>(session.get());
            if (!OV)
                continue;
            const auto MON = OV->monitor();
            if (!MON || OV->closing)
                continue;
            const Vector2D newLocal = GLOBAL - MON->m_position;
            const Vector2D dd       = newLocal - OV->lastMousePosLocal;
            OV->lastMousePosLocal   = newLocal;
            if (Hyprexpo::Osk::pointHitsKeyboard(MON, GLOBAL))
                continue; // on the keyboard layer: hands off entirely
            if (OV->drawer.pointerDragActive())
                OV->drawer.pointerMove(dd);
            if (OV->ribbonMousePan)
                OV->ribbonScrollBy(-dd.x); // content follows the cursor, like a touch drag
            OV->updateHoveredFromMouse();
        }

        if (info.cancelled)
            return;
        auto* const POV = dynamic_cast<COverview*>(pointerOverview());
        if (!POV)
            return;
        if (const auto PMON = POV->monitor()) {
            if (Hyprexpo::Osk::pointHitsKeyboard(PMON, GLOBAL))
                return; // on the keyboard layer: hands off entirely
        }
        info.cancelled = true;
        ensureOverviewCursorVisible();

        if (auto* const SOURCE = gridOverviewForMonitorKey(g_overviewDrag.state.sourceMonitorKey))
            SOURCE->updateWindowDrag();
    };

    auto onCursorSelect = [this](const IPointer::SButtonEvent& event, Event::SCallbackInfo& info) {
        if (closing || info.cancelled)
            return;

        if (!dynamic_cast<COverview*>(pointerOverview()))
            return;

        const Vector2D GLOBAL = g_pInputManager->getMouseCoordsInternal();
        auto* const    TARGET = gridOverviewForGlobalPoint(GLOBAL);
        auto* const    SOURCE = gridOverviewForMonitorKey(g_overviewDrag.state.sourceMonitorKey);
        if (!TARGET && !SOURCE)
            return;
        if (TARGET) {
            if (const auto TMON = TARGET->monitor()) {
                if (Hyprexpo::Osk::pointHitsKeyboard(TMON, GLOBAL))
                    return; // keyboard layer: hands off before consuming
            }
        }
        info.cancelled = true;

        // If expo hasn't animated in enough to be visible, close silently without
        // consuming the event. This prevents phantom triggers (e.g. a bouncing
        // mouse side-button firing the expo keybind) from swallowing real clicks.
        if (TARGET && TARGET->size->getPercent() < 0.05f) {
            TARGET->close(false);
            return;
        }

        // Drawer region first: taps launch/focus, presses arm for
        // pull — the window-drag path below must not see them.
        if (TARGET) {
            if (const auto TMON = TARGET->monitor()) {
                const auto local  = GLOBAL - TMON->m_position;
                const auto region = TARGET->regionAtPoint(local);
                if (region == COverview::ERegion::Search) {
                    TARGET->drawer.focusSearch();
                    return;
                }
                if (region == COverview::ERegion::Grid) {
                    if (event.state == WL_POINTER_BUTTON_STATE_PRESSED) {
                        TARGET->drawer.pointerDown(local, event.button);
                        return;
                    }
                    // A release only belongs to the drawer when it armed
                    // the pull; otherwise a window drag may end here.
                    if (TARGET->drawer.pointerDragActive()) {
                        TARGET->drawer.pointerUp(local);
                        return;
                    }
                }
            }
        }

        if (event.state == WL_POINTER_BUTTON_STATE_PRESSED) {
            // Right-drag on the ribbon pans the strip touch-style: scroll
            // only, never grabs windows, never selects. The press is already
            // cancelled above; the matching release is consumed below.
            if (TARGET && event.button == 0x111) {
                if (const auto TMON = TARGET->monitor()) {
                    if (TARGET->regionAtPoint(GLOBAL - TMON->m_position) == COverview::ERegion::Ribbon) {
                        TARGET->ribbonMousePan = true;
                        return;
                    }
                }
            }
            if (**PDRAGDROPENABLE && TARGET)
                TARGET->beginWindowDrag();
            return;
        }

        // A right-drag release that panned the ribbon ends here: consumed,
        // never falls through to workspace select (press may have started
        // off-ribbon after a re-grab, so the flag — not the region — decides).
        if (TARGET && TARGET->ribbonMousePan) {
            TARGET->ribbonMousePan = false;
            return;
        }

        if (**PDRAGDROPENABLE && SOURCE) {
            const bool CONSUMED = SOURCE->finishWindowDrag();
            if (CONSUMED)
                return;
        }

        if (TARGET && TARGET->selectHoveredWorkspace())
            closeOverviewsSelecting(TARGET);
    };

    // Touch hold-to-drag wrapper (classic grid path): a touch down only arms
    // a press on the session under the finger. Releasing before the hold
    // timeout replays the historical tap below; holding past it (or moving
    // past the drag threshold) engages the window-drag machinery instead.
    auto onTouchDown = [](const ITouch::SDownEvent& event, Event::SCallbackInfo& info) {
        if (info.cancelled)
            return;

        auto MON = event.device && !event.device->m_boundOutput.empty() ? State::monitorState()->query().name(event.device->m_boundOutput).run() : PHLMONITOR{};
        if (!MON)
            MON = Desktop::focusState()->monitor();

        auto* const TARGET = dynamic_cast<COverview*>(overviewForMonitor(MON));
        if (!TARGET || TARGET->closing)
            return;

        const Vector2D downGlobal = MON->m_position + event.pos * MON->m_size;
        if (Hyprexpo::Osk::pointHitsKeyboard(MON, downGlobal))
            return; // keyboard layer: no press, no consume, delivery proceeds
        info.cancelled = true;
        TARGET->touchPressDown(event.touchID, downGlobal, MON);
    };

    mouseMoveHook = Event::bus()->m_events.input.mouse.move.listen([onCursorMove](const Vector2D&, Event::SCallbackInfo& info) { onCursorMove(info); });
    touchMoveHook = Event::bus()->m_events.input.touch.motion.listen([onCursorMove](const ITouch::SMotionEvent& event, Event::SCallbackInfo& info) {
        if (auto* const OWNER = COverview::touchOwner(event.touchID)) {
            OWNER->touchMotionEvent(event.touchID, event.pos);
            return;
        }
        for (const auto& session : g_overviews) {
            if (session && session->ownsTouchInput(event.touchID))
                return;
        }
        // Unowned touch over the keyboard layer (press went straight to
        // the client): keep hands off so down/move/up stay consistent.
        // event.pos is output-normalized; test it against every overview
        // monitor (exact with one monitor, harmless approximation after).
        bool overOsk = false;
        for (const auto& session : g_overviews) {
            auto* const SOV = dynamic_cast<COverview*>(session.get());
            if (!SOV)
                continue;
            const auto SMON = SOV->monitor();
            if (!SMON)
                continue;
            if (Hyprexpo::Osk::pointHitsKeyboard(SMON, SMON->m_position + event.pos * SMON->m_size)) {
                overOsk = true;
                break;
            }
        }
        if (overOsk)
            return;
        onCursorMove(info);
    });
    mouseButtonHook = Event::bus()->m_events.input.mouse.button.listen([onCursorSelect](const IPointer::SButtonEvent& event, Event::SCallbackInfo& info) { onCursorSelect(event, info); });
    touchDownHook = Event::bus()->m_events.input.touch.down.listen([onTouchDown](const ITouch::SDownEvent& event, Event::SCallbackInfo& info) { onTouchDown(event, info); });
    touchUpHook = Event::bus()->m_events.input.touch.up.listen([](const ITouch::SUpEvent& event, Event::SCallbackInfo& info) {
        if (auto* const OWNER = COverview::touchOwner(event.touchID)) {
            info.cancelled = true;
            OWNER->touchPressUp(event.touchID);
        }
    });
    touchCancelHook = Event::bus()->m_events.input.touch.cancel.listen([](const ITouch::SCancelEvent& event, Event::SCallbackInfo& info) {
        if (auto* const OWNER = COverview::touchOwner(event.touchID)) {
            info.cancelled = true;
            OWNER->touchPressCancel(event.touchID);
        }
    });
    mouseAxisHook = Event::bus()->m_events.input.mouse.axis.listen([](const IPointer::SAxisEvent& event, Event::SCallbackInfo& info) {
        // Wheel / touchpad scroll while the exposé is open. Vertical over
        // the drawer pulls it (or snaps it docked<->fitted); horizontal over
        // the ribbon pans the workspace strip; vertical over the ribbon does
        // nothing. Everything the exposé doesn't act on is still CAPTURED,
        // never passed to the apps behind: an open exposé owns scroll input.
        // Only the on-screen keyboard falls through untouched.
        const Vector2D GLOBAL = g_pInputManager->getMouseCoordsInternal();
        bool expoCapture = false;
        for (const auto& session : g_overviews) {
            auto* const OV = dynamic_cast<COverview*>(session.get());
            if (!OV || OV->closing)
                continue;
            const auto MON = OV->monitor();
            if (!MON)
                continue;
            if (Hyprexpo::Osk::pointHitsKeyboard(MON, GLOBAL))
                continue; // keyboard layer: falls through untouched
            if (GLOBAL.x < MON->m_position.x || GLOBAL.y < MON->m_position.y || GLOBAL.x >= MON->m_position.x + MON->m_size.x ||
                GLOBAL.y >= MON->m_position.y + MON->m_size.y)
                continue; // another monitor's business, not ours
            expoCapture = true;
            // Latch the region per scroll burst like a touch press latches
            // it at down: the sheet slides under a stationary cursor while
            // pulling (top() rides pullVisual but the docked clip stays one
            // row), so re-classifying every event would flip Grid->None
            // mid-pull and starve the drawer. Only real silence relatches.
            static ERegion      latchedRegion = ERegion::None;
            static double       latchS        = 0.0;
            const double        nowEv =
                std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
            if (nowEv - latchS > 0.5)
                latchedRegion = OV->regionAtPoint(GLOBAL - MON->m_position);
            latchS         = nowEv;
            const auto region = latchedRegion;
            // Mouse wheels tick in coarse steps; touchpads stream pixel-true
            // deltas, boosted so a short flick still travels: the ribbon
            // visibly pans, and one downward flick pulls the drawer past its
            // detent. Touchscreen drags never reach this path (touchMotion),
            // so finger feel is untouched by every factor below.
            const bool discrete = event.source == WL_POINTER_AXIS_SOURCE_WHEEL || event.deltaDiscrete != 0;
            double     raw      = event.delta;
            if (!discrete && event.axis == WL_POINTER_AXIS_VERTICAL_SCROLL)
                raw = -raw; // touchpads report natural-scroll deltas (content
                            // follows fingers); the drawer speaks finger
                            // down-positive like the touchscreen, so un-invert
                            // here. (A setting read would be nicer, but the
                            // compat layer only sees Config::INTEGER and the
                            // live key is bool — it silently reads 0. This
                            // machine runs natural_scroll on; revisit if that
                            // ever changes.) Horizontal is already correct.
            if (region == COverview::ERegion::Ribbon) {
                // The strip pans horizontally: touchpad two-finger / tilt
                // wheel as before, plus the mouse wheel (vertical ticks pan
                // the strip too: down goes toward higher workspaces, mirroring
                // SUPER+Right). Touchpad vertical stays untouched (diagonal
                // scrolls must not yank the strip); it falls to capture.
                if (event.axis == WL_POINTER_AXIS_HORIZONTAL_SCROLL)
                    OV->ribbonScrollBy(raw * (discrete ? 4.0 : 2.5));
                else if (discrete)
                    OV->ribbonScrollBy(raw * 4.0);
                else
                    continue;
                info.cancelled = true;
                return;
            }
            if (region != COverview::ERegion::Grid)
                continue;
            if (event.axis == WL_POINTER_AXIS_HORIZONTAL_SCROLL)
                continue;
            // wheel has no press point: engagement is scroll position only
            // (top of the list may close, deeper never does). A docked sheet
            // takes the discrete open path (burst accumulator that fires at
            // half threshold — touchpad and mouse alike, each scaled); a
            // fitted sheet rides the analog detent (list scroll + close
            // pull) as before. Mouse closing ticks get an extra boost so a
            // few notches commit the close (downward mouse scrolls list a
            // bit faster as accepted collateral; touchpad untouched).
            if (!OV->drawer.isFitted())
                OV->drawer.wheelTouchOpen(raw * (discrete ? 4.0 : 2.5));
            else if (discrete && raw > 0.0)
                OV->drawer.wheel(raw * 8.0);
            else
                OV->drawer.wheel(raw * (discrete ? 4.0 : 2.5));
            // The pointer didn't move, so the highlight would sit on the
            // wrong app after the content shifted: refresh it from the
            // cursor like a mouse move would.
            OV->drawer.updateHover(GLOBAL - MON->m_position);
            info.cancelled = true;
            return;
        }
        if (expoCapture)
            info.cancelled = true;
    });
    workspaceMoveHook = Event::bus()->m_events.window.moveToWorkspace.listen([this](PHLWINDOW window, PHLWORKSPACE workspace) { onWindowMoveToWorkspace(window, workspace); });

    enterSubmapIfEnabled();
}
