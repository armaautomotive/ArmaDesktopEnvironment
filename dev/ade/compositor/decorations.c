#include "decorations.h"

#include <wlr/util/log.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_xdg_shell.h>

#if defined(__has_include)
#  if __has_include("ade-compositor.h")
#    include "ade-compositor.h"   // tinywl_server / tinywl_toplevel
#  endif
#endif

#ifndef ADE_TAB_HEIGHT
#define ADE_TAB_HEIGHT 22
#endif

// ------------------------------------------------------------
// Default BeOS-style decorations backend
// ------------------------------------------------------------

static bool dec_init(struct tinywl_server *server) {
    (void)server;
    wlr_log(WLR_INFO, "ADE: decorations backend initialized");
    return true;
}

static void dec_finish(struct tinywl_server *server) {
    (void)server;
}

static void dec_toplevel_create(struct tinywl_toplevel *toplevel) {
    if (!toplevel || !toplevel->scene_tree || !toplevel->xdg_toplevel) return;

    // Root decoration tree
    toplevel->decor_tree =
        wlr_scene_tree_create(toplevel->scene_tree);

    // BeOS-style border
    const float border_col[4] = { 0.65f, 0.65f, 0.65f, 1.0f };
    const int bw = 1;

    struct wlr_box geo = toplevel->xdg_toplevel->base->geometry;
    int w = geo.width  > 0 ? geo.width  : 800;
    int h = geo.height > 0 ? geo.height : 600;

    toplevel->border_top    = wlr_scene_rect_create(toplevel->decor_tree, w,  bw, border_col);
    toplevel->border_bottom = wlr_scene_rect_create(toplevel->decor_tree, w,  bw, border_col);
    toplevel->border_left   = wlr_scene_rect_create(toplevel->decor_tree, bw, h,  border_col);
    toplevel->border_right  = wlr_scene_rect_create(toplevel->decor_tree, bw, h,  border_col);

    if (toplevel->border_top)
        wlr_scene_node_set_position(&toplevel->border_top->node, 0, -bw);
    if (toplevel->border_bottom)
        wlr_scene_node_set_position(&toplevel->border_bottom->node, 0, h);
    if (toplevel->border_left)
        wlr_scene_node_set_position(&toplevel->border_left->node, -bw, 0);
    if (toplevel->border_right)
        wlr_scene_node_set_position(&toplevel->border_right->node, w, 0);

    // Tab
    toplevel->tab_tree = wlr_scene_tree_create(toplevel->decor_tree);
    toplevel->tab_x_px = 0;
    toplevel->tab_width_px = 180;

    if (toplevel->tab_tree)
        wlr_scene_node_set_position(&toplevel->tab_tree->node, 0, -ADE_TAB_HEIGHT);

    const float tab_col[4] = { 0.85f, 0.85f, 0.85f, 1.0f };
    toplevel->tab_rect =
        wlr_scene_rect_create(
            toplevel->tab_tree,
            toplevel->tab_width_px,
            ADE_TAB_HEIGHT,
            tab_col);
}

static void dec_toplevel_commit(struct tinywl_toplevel *toplevel) {
    if (!toplevel || !toplevel->xdg_toplevel) return;

    struct wlr_box geo = toplevel->xdg_toplevel->base->geometry;
    int w = geo.width;
    int h = geo.height;
    const int bw = 1;

    if (toplevel->border_top) {
        wlr_scene_rect_set_size(toplevel->border_top, w, bw);
        wlr_scene_node_set_position(&toplevel->border_top->node, 0, -bw);
    }
    if (toplevel->border_bottom) {
        wlr_scene_rect_set_size(toplevel->border_bottom, w, bw);
        wlr_scene_node_set_position(&toplevel->border_bottom->node, 0, h);
    }
    if (toplevel->border_left) {
        wlr_scene_rect_set_size(toplevel->border_left, bw, h);
        wlr_scene_node_set_position(&toplevel->border_left->node, -bw, 0);
    }
    if (toplevel->border_right) {
        wlr_scene_rect_set_size(toplevel->border_right, bw, h);
        wlr_scene_node_set_position(&toplevel->border_right->node, w, 0);
    }
}

static void dec_toplevel_destroy(struct tinywl_toplevel *toplevel) {
    if (!toplevel) return;

    if (toplevel->border_top)
        wlr_scene_node_destroy(&toplevel->border_top->node);
    if (toplevel->border_bottom)
        wlr_scene_node_destroy(&toplevel->border_bottom->node);
    if (toplevel->border_left)
        wlr_scene_node_destroy(&toplevel->border_left->node);
    if (toplevel->border_right)
        wlr_scene_node_destroy(&toplevel->border_right->node);
}

static void dec_toplevel_set_title(
    struct tinywl_toplevel *toplevel,
    const char *title)
{
    (void)toplevel;
    (void)title;
    // Title rendering remains in tab helpers for now
}

static void dec_toplevel_set_focused(
    struct tinywl_toplevel *toplevel,
    bool focused)
{
    if (!toplevel || !toplevel->tab_rect) return;

    const float active[4]   = { 0.95f, 0.82f, 0.20f, 1.0f }; // BeOS yellow
    const float inactive[4] = { 0.85f, 0.85f, 0.85f, 1.0f };

    wlr_scene_rect_set_color(
        toplevel->tab_rect,
        focused ? active : inactive);
}

static const struct ade_decorations_ops OPS = {
    .init = dec_init,
    .finish = dec_finish,
    .toplevel_create = dec_toplevel_create,
    .toplevel_commit = dec_toplevel_commit,
    .toplevel_destroy = dec_toplevel_destroy,
    .toplevel_set_title = dec_toplevel_set_title,
    .toplevel_set_focused = dec_toplevel_set_focused,
};

const struct ade_decorations_ops *ade_decorations_get_ops(void) {
    return &OPS;
}
