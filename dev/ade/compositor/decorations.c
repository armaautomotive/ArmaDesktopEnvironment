


// ADE - window decorations module
// Provides BeOS/Haiku-inspired yellow title tabs and simple borders.
//
// This module is intentionally self-contained: it does not depend on tinywl/ade-compositor
// structs. The compositor owns window state and calls into this module.

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <wlr/types/wlr_scene.h>

// If you have a header, include it. If not, the compositor can `extern` these symbols.
// (Leaving the include commented avoids breaking builds if the header isn't created yet.)
// #include "decorations.h"

// ------------------------------
// Styling
// ------------------------------

static const float ADE_COL_TAB_FOCUSED[4]   = { 1.00f, 0.86f, 0.20f, 1.0f }; // warm yellow
static const float ADE_COL_TAB_UNFOCUSED[4] = { 0.78f, 0.78f, 0.78f, 1.0f }; // light grey
static const float ADE_COL_BORDER[4]        = { 0.45f, 0.45f, 0.45f, 1.0f }; // medium grey
static const float ADE_COL_BTN[4]           = { 0.18f, 0.18f, 0.18f, 1.0f }; // dark glyph/btn block

// Default sizing (caller can override via setters)
static int ADE_TAB_H_PX = 25;      // user requested 25px tall
static int ADE_TAB_PAD_L = 6;
static int ADE_TAB_PAD_R = 6;
static int ADE_BTN_SIZE = 12;      // square close/expand hit area
static int ADE_BTN_GAP  = 6;
static int ADE_BORDER_PX = 1;

// ------------------------------
// Public data structure
// ------------------------------

struct ade_decor {
    struct wlr_scene_tree *root;      // container for all decor nodes

    // Tab
    struct wlr_scene_tree *tab_tree;
    struct wlr_scene_rect *tab_rect;

    // Buttons (rects for now; compositor can draw glyphs/text separately)
    struct wlr_scene_rect *btn_close;
    struct wlr_scene_rect *btn_expand;

    // Borders
    struct wlr_scene_rect *border_top;
    struct wlr_scene_rect *border_left;
    struct wlr_scene_rect *border_right;
    struct wlr_scene_rect *border_bottom;

    // Cached geometry
    int win_w;
    int win_h;
    int tab_w;

    bool focused;
};

// ------------------------------
// Helpers
// ------------------------------

static inline int ade_max_i(int a, int b) { return a > b ? a : b; }

static void ade_scene_rect_set_color_safe(struct wlr_scene_rect *r, const float col[4]) {
    if (r == NULL) return;
    wlr_scene_rect_set_color(r, col);
}

static void ade_set_focused_colors(struct ade_decor *d, bool focused) {
    if (!d) return;
    d->focused = focused;
    ade_scene_rect_set_color_safe(d->tab_rect, focused ? ADE_COL_TAB_FOCUSED : ADE_COL_TAB_UNFOCUSED);

    // Keep borders constant for now (BeOS-like). You can change this if you want.
    ade_scene_rect_set_color_safe(d->border_top, ADE_COL_BORDER);
    ade_scene_rect_set_color_safe(d->border_left, ADE_COL_BORDER);
    ade_scene_rect_set_color_safe(d->border_right, ADE_COL_BORDER);
    ade_scene_rect_set_color_safe(d->border_bottom, ADE_COL_BORDER);

    // Buttons: make them a bit darker when unfocused
    if (focused) {
        ade_scene_rect_set_color_safe(d->btn_close, ADE_COL_BTN);
        ade_scene_rect_set_color_safe(d->btn_expand, ADE_COL_BTN);
    } else {
        static const float unf_btn[4] = { 0.28f, 0.28f, 0.28f, 1.0f };
        ade_scene_rect_set_color_safe(d->btn_close, unf_btn);
        ade_scene_rect_set_color_safe(d->btn_expand, unf_btn);
    }
}

static void ade_layout(struct ade_decor *d, int win_w, int win_h, int tab_w_override) {
    if (!d || !d->root) return;

    d->win_w = win_w;
    d->win_h = win_h;

    // Tab width: if caller provides a width, respect it; otherwise compute a sensible minimum.
    // NOTE: If you later add text rendering, you can compute width = text_width + padding + buttons.
    int min_tab = ADE_TAB_PAD_L + ADE_BTN_SIZE + ADE_BTN_GAP + ADE_TAB_PAD_R + 80; // room for title
    d->tab_w = tab_w_override > 0 ? tab_w_override : min_tab;

    // Clamp tab width so it doesn't exceed window width.
    d->tab_w = ade_max_i(ade_max_i(60, d->tab_w), 60);
    if (d->tab_w > win_w) d->tab_w = win_w;

    // Root is positioned by the compositor at the surface's top-left.

    // Tab at top-left (x=0,y=0)
    if (d->tab_tree) {
        wlr_scene_node_set_position(&d->tab_tree->node, 0, 0);
    }

    // Tab rect itself
    if (d->tab_rect) {
        // wlroots doesn't expose a universally-stable resize API across versions.
        // For 0.20, wlr_scene_rect_set_size exists.
        wlr_scene_rect_set_size(d->tab_rect, d->tab_w, ADE_TAB_H_PX);
    }

    // Close button on left
    if (d->btn_close) {
        wlr_scene_rect_set_size(d->btn_close, ADE_BTN_SIZE, ADE_BTN_SIZE);
        wlr_scene_node_set_position(&d->btn_close->node, ADE_TAB_PAD_L, (ADE_TAB_H_PX - ADE_BTN_SIZE) / 2);
    }

    // Expand button on right
    if (d->btn_expand) {
        wlr_scene_rect_set_size(d->btn_expand, ADE_BTN_SIZE, ADE_BTN_SIZE);
        int bx = d->tab_w - ADE_TAB_PAD_R - ADE_BTN_SIZE;
        wlr_scene_node_set_position(&d->btn_expand->node, bx, (ADE_TAB_H_PX - ADE_BTN_SIZE) / 2);
    }

    // Borders: simple 1px rects around the window + tab height.
    // The compositor should place the surface below the tab by ADE_TAB_H_PX.
    // We draw borders around the whole decorated region: width=win_w, height=win_h + tab_h.
    int full_h = win_h + ADE_TAB_H_PX;

    if (d->border_top) {
        wlr_scene_rect_set_size(d->border_top, win_w, ADE_BORDER_PX);
        wlr_scene_node_set_position(&d->border_top->node, 0, 0);
    }
    if (d->border_left) {
        wlr_scene_rect_set_size(d->border_left, ADE_BORDER_PX, full_h);
        wlr_scene_node_set_position(&d->border_left->node, 0, 0);
    }
    if (d->border_right) {
        wlr_scene_rect_set_size(d->border_right, ADE_BORDER_PX, full_h);
        wlr_scene_node_set_position(&d->border_right->node, win_w - ADE_BORDER_PX, 0);
    }
    if (d->border_bottom) {
        wlr_scene_rect_set_size(d->border_bottom, win_w, ADE_BORDER_PX);
        wlr_scene_node_set_position(&d->border_bottom->node, 0, full_h - ADE_BORDER_PX);
    }

    (void)full_h;
}

// ------------------------------
// Public API
// ------------------------------

// Create decorations under `parent`.
// The compositor should:
//   - create a parent tree per toplevel
//   - create a child scene_tree for the xdg surface
//   - call this to create decor in parallel
//   - position the xdg surface at y = ade_decor_get_tab_height()
struct ade_decor *ade_decor_create(struct wlr_scene_tree *parent) {
    if (parent == NULL) return NULL;

    struct ade_decor *d = calloc(1, sizeof(*d));
    if (!d) return NULL;

    d->root = wlr_scene_tree_create(parent);
    if (!d->root) {
        free(d);
        return NULL;
    }

    d->tab_tree = wlr_scene_tree_create(d->root);

    // Tab background
    d->tab_rect = wlr_scene_rect_create(d->tab_tree, 1, 1, ADE_COL_TAB_UNFOCUSED);

    // Buttons (rect hit areas; your compositor can additionally draw glyphs/text)
    d->btn_close = wlr_scene_rect_create(d->tab_tree, 1, 1, ADE_COL_BTN);
    d->btn_expand = wlr_scene_rect_create(d->tab_tree, 1, 1, ADE_COL_BTN);

    // Borders
    d->border_top = wlr_scene_rect_create(d->root, 1, 1, ADE_COL_BORDER);
    d->border_left = wlr_scene_rect_create(d->root, 1, 1, ADE_COL_BORDER);
    d->border_right = wlr_scene_rect_create(d->root, 1, 1, ADE_COL_BORDER);
    d->border_bottom = wlr_scene_rect_create(d->root, 1, 1, ADE_COL_BORDER);

    // Default geometry until first configure
    ade_layout(d, 320, 240, 0);
    ade_set_focused_colors(d, false);

    // Keep borders behind tab (optional). If you want tab behind, swap these.
    wlr_scene_node_raise_to_top(&d->tab_tree->node);

    return d;
}

void ade_decor_destroy(struct ade_decor *d) {
    if (!d) return;
    if (d->root) {
        wlr_scene_node_destroy(&d->root->node);
        d->root = NULL;
    }
    free(d);
}

void ade_decor_set_focused(struct ade_decor *d, bool focused) {
    ade_set_focused_colors(d, focused);
}

// Update sizes/positions.
// win_w/win_h are the client surface size (not including tab).
// tab_w_override: pass >0 to force a tab width; pass 0 to use defaults.
void ade_decor_set_geometry(struct ade_decor *d, int win_w, int win_h, int tab_w_override) {
    ade_layout(d, win_w, win_h, tab_w_override);
    ade_set_focused_colors(d, d->focused);
}

int ade_decor_get_tab_height(void) {
    return ADE_TAB_H_PX;
}

// Optional setters if you want to tweak in runtime.
void ade_decor_set_tab_height(int px) {
    if (px < 16) px = 16;
    if (px > 64) px = 64;
    ADE_TAB_H_PX = px;
}

void ade_decor_set_border_px(int px) {
    if (px < 0) px = 0;
    if (px > 8) px = 8;
    ADE_BORDER_PX = px;
}

// Hit testing helpers (compositor can use these for click routing).
// Returns true if (x,y) in *decor-local* coords hits the close button.
bool ade_decor_hit_close(const struct ade_decor *d, double x, double y) {
    if (!d) return false;
    int bx = ADE_TAB_PAD_L;
    int by = (ADE_TAB_H_PX - ADE_BTN_SIZE) / 2;
    return x >= bx && x < (bx + ADE_BTN_SIZE) && y >= by && y < (by + ADE_BTN_SIZE);
}

bool ade_decor_hit_expand(const struct ade_decor *d, double x, double y) {
    if (!d) return false;
    int bx = d->tab_w - ADE_TAB_PAD_R - ADE_BTN_SIZE;
    int by = (ADE_TAB_H_PX - ADE_BTN_SIZE) / 2;
    return x >= bx && x < (bx + ADE_BTN_SIZE) && y >= by && y < (by + ADE_BTN_SIZE);
}

// True if the point is inside the tab background area.
bool ade_decor_hit_tab(const struct ade_decor *d, double x, double y) {
    if (!d) return false;
    return x >= 0 && x < d->tab_w && y >= 0 && y < ADE_TAB_H_PX;
}

// True if the point is inside the client area (below the tab), for convenience.
bool ade_decor_point_in_client(const struct ade_decor *d, double x, double y) {
    if (!d) return false;
    return x >= 0 && x < d->win_w && y >= ADE_TAB_H_PX && y < (ADE_TAB_H_PX + d->win_h);
}

// Expose current tab width (useful if you compute label layout externally).
int ade_decor_get_tab_width(const struct ade_decor *d) {
    return d ? d->tab_w : 0;
}
