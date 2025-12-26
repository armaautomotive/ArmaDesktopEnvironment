#include "ade_deskbar.h"

#include <stdlib.h>
#include <string.h>

#include <wayland-server-core.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_output.h>
#include <wlr/util/log.h>

/* ------------------------------
 * Internal data structures
 * ------------------------------ */

struct ade_deskbar {
    struct tinywl_server *server;

    struct wlr_scene_tree *tree;      /* Root node for the deskbar */
    struct wlr_scene_rect *bg_rect;   /* Background rectangle */

    /* Layout */
    int x;
    int y;
    int height;
    int width;
    int padding;
    int slot_width;

    /* App tracking (simple version) */
    size_t app_count;
};

/* ------------------------------
 * Helpers
 * ------------------------------ */

static void deskbar_recompute_geometry(struct ade_deskbar *bar,
                                       struct wlr_output *output) {
    if (!bar || !output) {
        return;
    }

    int ow = output->width;

    bar->width = bar->padding * 2 + (int)bar->app_count * bar->slot_width;
    bar->x = ow - bar->width - bar->padding;
    bar->y = bar->padding;

    if (bar->bg_rect) {
        wlr_scene_rect_set_size(bar->bg_rect, bar->width, bar->height);
        wlr_scene_node_set_position(&bar->bg_rect->node, bar->x, bar->y);
    }
}

/* ------------------------------
 * Public API
 * ------------------------------ */

bool ade_deskbar_init(struct tinywl_server *server) {
    if (!server || !server->scene) {
        return false;
    }

    struct ade_deskbar *bar = calloc(1, sizeof(*bar));
    if (!bar) {
        return false;
    }

    bar->server = server;
    bar->padding = 6;
    bar->height = 22;
    bar->slot_width = 24;
    bar->app_count = 0;

    bar->tree = wlr_scene_tree_create(&server->scene->tree);
    if (!bar->tree) {
        free(bar);
        return false;
    }

    const float bg_col[4] = {0.85f, 0.85f, 0.85f, 0.95f};
    bar->bg_rect = wlr_scene_rect_create(bar->tree, 1, bar->height, bg_col);

    wlr_scene_node_raise_to_top(&bar->tree->node);

    server->deskbar = bar;

    wlr_log(WLR_INFO, "ADE deskbar initialized");
    return true;
}

void ade_deskbar_finish(struct tinywl_server *server) {
    if (!server || !server->deskbar) {
        return;
    }

    struct ade_deskbar *bar = server->deskbar;

    if (bar->tree) {
        wlr_scene_node_destroy(&bar->tree->node);
    }

    free(bar);
    server->deskbar = NULL;
}

void ade_deskbar_update_layout(struct tinywl_server *server,
                               struct wlr_output *output) {
    if (!server || !server->deskbar || !output) {
        return;
    }

    deskbar_recompute_geometry(server->deskbar, output);
}

bool ade_deskbar_handle_pointer_button(struct tinywl_server *server,
                                       uint32_t button,
                                       bool pressed) {
    (void)button;
    (void)pressed;

    if (!server || !server->deskbar) {
        return false;
    }

    /* TODO: hit-testing and click handling */
    return false;
}

void ade_deskbar_on_toplevel_mapped(struct tinywl_server *server,
                                    struct tinywl_toplevel *toplevel) {
    (void)toplevel;

    if (!server || !server->deskbar) {
        return;
    }

    server->deskbar->app_count++;
}

void ade_deskbar_on_toplevel_unmapped(struct tinywl_server *server,
                                      struct tinywl_toplevel *toplevel) {
    (void)toplevel;

    if (!server || !server->deskbar) {
        return;
    }

    if (server->deskbar->app_count > 0) {
        server->deskbar->app_count--;
    }
}

void ade_deskbar_on_focus_changed(struct tinywl_server *server,
                                  struct tinywl_toplevel *focused) {
    (void)server;
    (void)focused;
    /* Optional: highlight focused app slot */
}
