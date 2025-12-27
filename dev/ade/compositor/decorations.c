#include "decorations.h"
#include <wlr/util/log.h>

// Treat tinywl_toplevel as opaque for now.
// (Its full struct currently lives in ade-compositor.c.)
static bool dec_init(struct tinywl_server *server) {
    (void)server;
    wlr_log(WLR_INFO, "ADE: decorations backend init (opaque tinywl_toplevel)");
    return true;
}
static void dec_finish(struct tinywl_server *server) { (void)server; }
static void dec_toplevel_create(struct tinywl_toplevel *toplevel) { (void)toplevel; }
static void dec_toplevel_commit(struct tinywl_toplevel *toplevel) { (void)toplevel; }
static void dec_toplevel_destroy(struct tinywl_toplevel *toplevel) { (void)toplevel; }
static void dec_toplevel_set_title(struct tinywl_toplevel *toplevel, const char *title) {
    (void)toplevel; (void)title;
}
static void dec_toplevel_set_focused(struct tinywl_toplevel *toplevel, bool focused) {
    (void)toplevel; (void)focused;
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

// Helpers referenced by ade-compositor.c
int ade_decor_tab_height(void) {
    return 22;
}

void ade_decor_tab_color(bool selected, float out_rgba[4]) {
    if (!out_rgba) return;
    if (selected) {
        out_rgba[0] = 0.95f; out_rgba[1] = 0.82f; out_rgba[2] = 0.20f; out_rgba[3] = 1.0f;
    } else {
        out_rgba[0] = 0.85f; out_rgba[1] = 0.85f; out_rgba[2] = 0.85f; out_rgba[3] = 1.0f;
    }
}
