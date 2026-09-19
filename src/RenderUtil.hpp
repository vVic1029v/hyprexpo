#pragma once

// Shared raster pipeline: file pixels -> ARGB32 cairo surface -> texture.
// Used by app icons (DrawerAddon) and the expose backdrop (OverviewRender)
// so the pixbuf copy loop and the upload exist exactly once.
//
// NOTE: this header deliberately does NOT include Renderer.hpp (only
// Texture.hpp for the type): in this Hyprland snapshot, pulling Renderer
// in early flips subsequent renderTextureInternal calls in the including
// TU from public to private. The renderer access lives in RenderUtil.cpp.
#include <cairo/cairo.h>
#include <gdk-pixbuf/gdk-pixbuf.h>

#include <hyprland/src/render/Texture.hpp>

namespace Hyprexpo::RenderUtil {

// ARGB32 surface from a pixbuf (premultiplied like the rest of the
// pipeline; grayscale expands). Null on failure. Caller destroys.
cairo_surface_t* surfaceFromPixbuf(GdkPixbuf* pb);

// Upload helper (implemented in RenderUtil.cpp).
SP<Render::ITexture> uploadCairoSurface(cairo_surface_t* surf);

} // namespace Hyprexpo::RenderUtil
