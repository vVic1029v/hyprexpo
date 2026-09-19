#include "RenderUtil.hpp"

#include <hyprland/src/render/Renderer.hpp>

namespace Hyprexpo::RenderUtil {

cairo_surface_t* surfaceFromPixbuf(GdkPixbuf* pb) {
    if (!pb)
        return nullptr;
    const int w = gdk_pixbuf_get_width(pb);
    const int h = gdk_pixbuf_get_height(pb);
    if (w <= 0 || h <= 0)
        return nullptr;
    cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    if (cairo_surface_status(surf) != CAIRO_STATUS_SUCCESS)
        return nullptr;
    unsigned char* dst       = cairo_image_surface_get_data(surf);
    const int      dstStride = cairo_image_surface_get_stride(surf);
    guchar*        src       = gdk_pixbuf_get_pixels(pb);
    const int      srcStride = gdk_pixbuf_get_rowstride(pb);
    const int      ch        = gdk_pixbuf_get_n_channels(pb);
    const bool     hasA      = gdk_pixbuf_get_has_alpha(pb);
    for (int y = 0; y < h; ++y) {
        uint32_t* row = (uint32_t*)(dst + y * dstStride);
        for (int x = 0; x < w; ++x) {
            const guchar*  p = src + y * srcStride + x * ch;
            const uint32_t a = hasA ? p[3] : 255;
            const uint32_t r = p[0] * a / 255;
            const uint32_t g = (ch > 1 ? p[1] : p[0]) * a / 255;
            const uint32_t b = (ch > 2 ? p[2] : p[0]) * a / 255;
            row[x]           = (a << 24) | (r << 16) | (g << 8) | b;
        }
    }
    cairo_surface_mark_dirty(surf);
    return surf;
}

SP<Render::ITexture> uploadCairoSurface(cairo_surface_t* surf) {
    return g_pHyprRenderer->createTexture(surf);
}

} // namespace Hyprexpo::RenderUtil
