#include "desktop_icons.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>
#include <math.h>

#include <wayland-server-core.h>

#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_buffer.h>
#include <wlr/util/log.h>

#include <cairo/cairo.h>
#include <pango/pangocairo.h>

// Helpers implemented in ade-compositor.c (or elsewhere)
struct wlr_buffer *ade_load_png_as_wlr_buffer(const char *path);
void ade_launch_terminal(void);
void ade_spawn_shell(const char *sh_cmd);
bool ade_node_is_or_ancestor(struct wlr_scene_node *node, struct wlr_scene_node *target);

static char g_last_path[512] = {0};
static struct ade_desktop_icons g_icons = {0};
static struct ade_desktop_icon *g_selected = NULL;

static void free_icon(struct ade_desktop_icon *ic) {
    if (!ic) return;
    free(ic->id);
    free(ic->title);
    free(ic->png_path);
    free(ic->exec_cmd);
    free(ic->label);
    ic->id = ic->title = ic->png_path = ic->exec_cmd = ic->label = NULL;
    ic->node = NULL;
    ic->label_node = NULL;
    ic->select_node = NULL;
    ic->label_w = ic->label_h = 0;
    ic->selected = false;
}

static void clear_icons(struct ade_desktop_icons *set) {
    if (!set) return;
    for (int i = 0; i < set->count; i++) free_icon(&set->items[i]);
    free(set->items);
    set->items = NULL;
    set->count = 0;
}

static char *trim(char *s) {
    while (*s && isspace((unsigned char)*s)) s++;
    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1])) end--;
    *end = '\0';
    return s;
}

// Simple config format (line-based):
// x y png_path label text... | exec command...
// Example:
// 40 60 ./icons/terminal.png Terminal | foot
static bool parse_line(char *line, struct ade_desktop_icon *out) {
    line = trim(line);
    if (line[0] == '\0' || line[0] == '#') return false;

    // Split on '|'
    char *bar = strchr(line, '|');
    if (!bar) return false;
    *bar = '\0';
    char *left = trim(line);
    char *right = trim(bar + 1);

    int x = 0, y = 0;
    char png[384];
    png[0] = '\0';

    // left: "x y png_path label text..."
    char *p = left;
    if (sscanf(p, "%d %d %383s", &x, &y, png) != 3) {
        return false;
    }

    // advance pointer past x y png
    for (int i = 0; i < 3 && *p; ) {
        if (isspace((unsigned char)*p)) {
            while (*p && isspace((unsigned char)*p)) p++;
            i++;
        } else {
            p++;
        }
    }

    char *label = trim(p);
    out->label = (*label) ? strdup(label) : NULL;

    out->x = x;
    out->y = y;
    out->png_path = strdup(png);
    out->exec_cmd = strdup(right);

    out->title = NULL;
    out->id = NULL;
    out->node = NULL;
    out->label_node = NULL;
    out->select_node = NULL;
    out->label_w = 0;
    out->label_h = 0;
    out->selected = false;
    out->icon_w = 0;
    out->icon_h = 0;

    return out->png_path && out->exec_cmd;
}

// --- Antialiased Cairo/Pango rendering for desktop icon labels ---
// Renders into a transparent ARGB8888 buffer (with a subtle shadow).
static struct wlr_buffer *make_label_buffer(const char *text, int *out_w, int *out_h) {
    if (!text || !*text) return NULL;

    // Tuning knobs
    const int pad_x = 3;
    const int pad_y = 1;
    const int shadow_dx = 1;
    const int shadow_dy = 1;
    const int max_chars = 64;

    // Clamp length so we don't allocate silly buffers
    char tmp[128];
    size_t n = strlen(text);
    if (n > (size_t)max_chars) n = (size_t)max_chars;
    memcpy(tmp, text, n);
    tmp[n] = '\0';

    // Create a tiny scratch surface to measure text
    cairo_surface_t *scratch = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
    cairo_t *cr0 = cairo_create(scratch);

    PangoLayout *layout0 = pango_cairo_create_layout(cr0);
    PangoFontDescription *desc = pango_font_description_from_string("Sans 7");
    pango_layout_set_font_description(layout0, desc);
    pango_layout_set_text(layout0, tmp, -1);
    pango_layout_set_single_paragraph_mode(layout0, TRUE);
    pango_layout_set_wrap(layout0, PANGO_WRAP_WORD_CHAR);

    int tw = 0, th = 0;
    pango_layout_get_pixel_size(layout0, &tw, &th);

    g_object_unref(layout0);
    pango_font_description_free(desc);
    cairo_destroy(cr0);
    cairo_surface_destroy(scratch);

    if (tw <= 0 || th <= 0) return NULL;

    int W = tw + pad_x * 2 + shadow_dx;
    int H = th + pad_y * 2 + shadow_dy;
    if (W < 1) W = 1;
    if (H < 1) H = 1;

    cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, W, H);
    cairo_t *cr = cairo_create(surface);

    // Ensure we start fully transparent
    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    // Make sure antialiasing is enabled
    cairo_font_options_t *fopt = cairo_font_options_create();
    cairo_font_options_set_antialias(fopt, CAIRO_ANTIALIAS_SUBPIXEL);
    cairo_font_options_set_hint_style(fopt, CAIRO_HINT_STYLE_NONE);
    cairo_font_options_set_hint_metrics(fopt, CAIRO_HINT_METRICS_OFF);
    cairo_set_font_options(cr, fopt);
    cairo_font_options_destroy(fopt);

    PangoLayout *layout = pango_cairo_create_layout(cr);
    PangoFontDescription *desc2 = pango_font_description_from_string("Sans 7");
    pango_layout_set_font_description(layout, desc2);
    pango_layout_set_text(layout, tmp, -1);

    // Center inside the label buffer
    pango_layout_set_alignment(layout, PANGO_ALIGN_CENTER);
    pango_layout_set_width(layout, (W - pad_x * 2) * PANGO_SCALE);

    // Shadow
    cairo_set_source_rgba(cr, 0, 0, 0, 0.55);
    cairo_move_to(cr, pad_x + shadow_dx, pad_y + shadow_dy);
    pango_cairo_show_layout(cr, layout);

    // Foreground (slightly off-white)
    cairo_set_source_rgba(cr, 0.95, 0.97, 1.0, 1);
    cairo_move_to(cr, pad_x, pad_y);
    pango_cairo_show_layout(cr, layout);

    g_object_unref(layout);
    pango_font_description_free(desc2);

    cairo_destroy(cr);
    cairo_surface_flush(surface);

    const int stride = cairo_image_surface_get_stride(surface);
    const uint8_t *src = cairo_image_surface_get_data(surface);

    uint8_t *pixels = malloc((size_t)stride * (size_t)H);
    if (!pixels) {
        cairo_surface_destroy(surface);
        return NULL;
    }
    memcpy(pixels, src, (size_t)stride * (size_t)H);

    struct wlr_buffer *buf = wlr_buffer_from_pixels(
        WL_SHM_FORMAT_ARGB8888,
        stride,
        W, H,
        pixels
    );

    // NOTE: wlroots may keep a reference to the provided pixel data.
    // Keep `pixels` alive for the lifetime of the buffer.
    (void)pixels;

    cairo_surface_destroy(surface);

    if (out_w) *out_w = W;
    if (out_h) *out_h = H;
    return buf;
}

static struct wlr_buffer *make_selection_lozenge_buffer(int w, int h) {
    if (w <= 0 || h <= 0) return NULL;

    cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    cairo_t *cr = cairo_create(surface);

    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    // BeOS-ish selection blue
    const double r = 41.0 / 255.0;
    const double g = 80.0 / 255.0;
    const double b = 134.0 / 255.0;
    const double a = 0.85;

    const double radius = fmin(10.0, fmin((double)w, (double)h) / 2.0);
    const double x0 = 0.0, y0 = 0.0;
    const double x1 = (double)w, y1 = (double)h;

    cairo_new_path(cr);
    cairo_arc(cr, x1 - radius, y0 + radius, radius, -M_PI / 2.0, 0.0);
    cairo_arc(cr, x1 - radius, y1 - radius, radius, 0.0, M_PI / 2.0);
    cairo_arc(cr, x0 + radius, y1 - radius, radius, M_PI / 2.0, M_PI);
    cairo_arc(cr, x0 + radius, y0 + radius, radius, M_PI, 3.0 * M_PI / 2.0);
    cairo_close_path(cr);

    cairo_set_source_rgba(cr, r, g, b, a);
    cairo_fill(cr);

    cairo_destroy(cr);
    cairo_surface_flush(surface);

    const int stride = cairo_image_surface_get_stride(surface);
    const uint8_t *src = cairo_image_surface_get_data(surface);

    uint8_t *pixels = malloc((size_t)stride * (size_t)h);
    if (!pixels) {
        cairo_surface_destroy(surface);
        return NULL;
    }
    memcpy(pixels, src, (size_t)stride * (size_t)h);

    struct wlr_buffer *buf = wlr_buffer_from_pixels(
        WL_SHM_FORMAT_ARGB8888,
        stride,
        w, h,
        pixels
    );

    // NOTE: wlroots may keep a reference to the provided pixel data.
    // Keep `pixels` alive for the lifetime of the buffer.
    (void)pixels;

    cairo_surface_destroy(surface);
    return buf;
}

static void build_icon_scene(struct tinywl_server *server, struct ade_desktop_icon *ic) {
    if (!server || !server->desktop_icons || !ic) return;

    struct wlr_buffer *buf = ade_load_png_as_wlr_buffer(ic->png_path);
    if (buf) {
        struct wlr_scene_buffer *sb = wlr_scene_buffer_create(server->desktop_icons, buf);
        wlr_buffer_drop(buf);

        wlr_scene_node_set_position(&sb->node, ic->x, ic->y);
        ic->node = &sb->node;

        ic->icon_w = (int)wlr_buffer_get_width(buf);
        ic->icon_h = (int)wlr_buffer_get_height(buf);
        if (ic->icon_w <= 0) ic->icon_w = 48;
        if (ic->icon_h <= 0) ic->icon_h = 48;

        // Always render a label for debugging: if config label is missing, show a default.
        const char *label_text = (ic->label && *ic->label) ? ic->label : "Icon";
        int lw = 0, lh = 0;
        struct wlr_buffer *lb = make_label_buffer(label_text, &lw, &lh);
        if (lb) {
            struct wlr_scene_buffer *ls = wlr_scene_buffer_create(server->desktop_icons, lb);
            wlr_buffer_drop(lb);
            wlr_scene_buffer_set_filter_mode(ls, WLR_SCALE_FILTER_BILINEAR);

            ic->label_w = lw;
            ic->label_h = lh;

            // Position using the *buffer* dimensions so the label actually centers under the icon
            int lx = ic->x + (ic->icon_w / 2) - (ic->label_w / 2);
            int ly = ic->y + ic->icon_h + 16;
            if (lx < 0) lx = 0;
            if (ly < 0) ly = 0;

            wlr_scene_node_set_position(&ls->node, lx, ly);
            ic->label_node = &ls->node;
            wlr_scene_node_raise_to_top(ic->label_node);

            wlr_log(WLR_INFO, "ADE: Desktop icon label rendered: '%s' (png=%s)",
                    label_text,
                    ic->png_path ? ic->png_path : "(null)");
        } else {
            wlr_log(WLR_ERROR, "ADE: label buffer creation FAILED for '%s' (png=%s)",
                    label_text,
                    ic->png_path ? ic->png_path : "(null)");
        }

        int sel_w = ic->icon_w + 16;
        int sel_h = ic->icon_h + 16;
        if (ic->label_h > 0) {
            // Label is positioned at (icon_h + 8), so include that offset in the selection bounds
            sel_h = ic->icon_h + 8 + ic->label_h + 16;
            int want_w = ic->label_w + 16;
            if (want_w > sel_w) sel_w = want_w;
        }

        struct wlr_buffer *lob = make_selection_lozenge_buffer(sel_w, sel_h);
        if (lob) {
            struct wlr_scene_buffer *sel = wlr_scene_buffer_create(server->desktop_icons, lob);
            wlr_buffer_drop(lob);

            int sel_x = ic->x + (ic->icon_w / 2) - (sel_w / 2);
            int sel_y = ic->y - 8;
            wlr_scene_node_set_position(&sel->node, sel_x, sel_y);

            wlr_scene_node_lower_to_bottom(&sel->node);

            ic->select_node = &sel->node;
            ic->selected = false;
            wlr_scene_node_set_enabled(ic->select_node, false);

            if (ic->node) wlr_scene_node_raise_to_top(ic->node);
            if (ic->label_node) wlr_scene_node_raise_to_top(ic->label_node);
        }

        wlr_log(WLR_INFO, "ADE: icon '%s' icon_w=%d icon_h=%d label_w=%d label_h=%d",
            (ic->label && *ic->label) ? ic->label : "(no-label)", ic->icon_w, ic->icon_h, ic->label_w, ic->label_h);

        return;
    }

    const float col[4] = { 0.75f, 0.75f, 0.75f, 1.0f };
    struct wlr_scene_rect *r = wlr_scene_rect_create(server->desktop_icons, 48, 48, col);
    wlr_scene_node_set_position(&r->node, ic->x, ic->y);
    ic->node = &r->node;
    ic->icon_w = 48;
    ic->icon_h = 48;
}

bool ade_desktop_icons_init(struct tinywl_server *server) {
    (void)server;
    g_last_path[0] = '\0';
    clear_icons(&g_icons);
    g_selected = NULL;
    return true;
}

void ade_desktop_icons_destroy(struct tinywl_server *server) {
    (void)server;
    clear_icons(&g_icons);
    g_selected = NULL;
}

bool ade_desktop_icons_reload(struct tinywl_server *server, const char *conf_path) {
    if (!server || !conf_path || !server->desktop_icons) return false;

    FILE *f = fopen(conf_path, "r");
    if (!f) {
        wlr_log(WLR_ERROR, "ADE: failed to open desktop icons config: %s", conf_path);
        return false;
    }

    clear_icons(&g_icons);
    g_selected = NULL;

    wlr_scene_node_destroy(&server->desktop_icons->node);
    //server->desktop_icons = wlr_scene_tree_create(&server->scene->tree);
    server->desktop_icons = wlr_scene_tree_create(&server->scene->tree);
    wlr_scene_node_set_position(&server->desktop_icons->node, 0, 0);
    wlr_scene_node_set_enabled(&server->desktop_icons->node, true);
    
    
    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        struct ade_desktop_icon ic = {0};
        if (!parse_line(line, &ic)) continue;

        g_icons.items = realloc(g_icons.items, sizeof(g_icons.items[0]) * (g_icons.count + 1));
        g_icons.items[g_icons.count++] = ic;
    }
    fclose(f);

    for (int i = 0; i < g_icons.count; i++) {
        build_icon_scene(server, &g_icons.items[i]);
    }

    snprintf(g_last_path, sizeof(g_last_path), "%s", conf_path);
    return true;
}

bool ade_desktop_icons_hit_test(struct tinywl_server *server,
                                struct wlr_scene_node *hit_node,
                                struct wlr_scene_node **out_icon_node) {
    if (out_icon_node) *out_icon_node = NULL;
    if (!server || !hit_node) return false;
    if (!server->desktop_icons) return false;

    if (!ade_node_is_or_ancestor(hit_node, &server->desktop_icons->node)) {
        return false;
    }

    for (int i = 0; i < g_icons.count; i++) {
        struct ade_desktop_icon *ic = &g_icons.items[i];
        if (ic->node && ade_node_is_or_ancestor(hit_node, ic->node)) {
            if (out_icon_node) *out_icon_node = ic->node;
            return true;
        }
        if (ic->label_node && ade_node_is_or_ancestor(hit_node, ic->label_node)) {
            if (out_icon_node) *out_icon_node = ic->node;
            return true;
        }
        if (ic->select_node && ade_node_is_or_ancestor(hit_node, ic->select_node)) {
            if (out_icon_node) *out_icon_node = ic->node;
            return true;
        }
    }

    if (out_icon_node) *out_icon_node = hit_node;
    return true;
}

void ade_desktop_icons_select_for_node(struct tinywl_server *server,
                                       struct wlr_scene_node *icon_node) {
    (void)server;

    if (g_selected && g_selected->select_node) {
        wlr_scene_node_set_enabled(g_selected->select_node, false);
        g_selected->selected = false;
    }
    g_selected = NULL;

    if (!icon_node) return;

    for (int i = 0; i < g_icons.count; i++) {
        struct ade_desktop_icon *ic = &g_icons.items[i];
        if (ic->node == icon_node) {
            g_selected = ic;
            if (ic->select_node) {
                wlr_scene_node_set_enabled(ic->select_node, true);
                wlr_scene_node_lower_to_bottom(ic->select_node);
            }
            ic->selected = true;
            return;
        }
    }
}

void ade_desktop_icons_launch_for_node(struct tinywl_server *server,
                                       struct wlr_scene_node *icon_node) {
    if (!server || !icon_node) return;

    for (int i = 0; i < g_icons.count; i++) {
        struct ade_desktop_icon *ic = &g_icons.items[i];
        if (ic->node == icon_node) {
            if (ic->exec_cmd && *ic->exec_cmd) {
                ade_spawn_shell(ic->exec_cmd);
            }
            return;
        }
    }

    ade_launch_terminal();
}

const char *ade_desktop_icons_last_path(struct tinywl_server *server) {
    (void)server;
    return g_last_path[0] ? g_last_path : NULL;
}
