#include "desktop_icons.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>
#include <math.h>

#include <wayland-server-core.h>

#include <wlr/types/wlr_scene.h>
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

// --- Bitmap font rendering for desktop icon labels ---
// Tiny 5x7 font packed into 8 rows (LSB on the right). Only includes common ASCII.
// Characters not present render as a small box.
static uint8_t ade_font5x7_get_row(char c, int row) {
    // row: 0..6
    // Return 5-bit row pattern in bits 4..0
    // NOTE: Hand-authored minimal font for: space, .,-,_,0-9,A-Z,a-z
    // Fallback: box.

    // Space
    if (c == ' ') {
        return 0;
    }

    // Punctuation
    if (c == '.') {
        static const uint8_t p[7] = {0,0,0,0,0,0,0b00100};
        return p[row];
    }
    if (c == '-') {
        static const uint8_t p[7] = {0,0,0,0b11111,0,0,0};
        return p[row];
    }
    if (c == '_') {
        static const uint8_t p[7] = {0,0,0,0,0,0,0b11111};
        return p[row];
    }
    if (c == '/') {
        static const uint8_t p[7] = {0b00001,0b00010,0b00100,0b01000,0b10000,0,0};
        return p[row];
    }

    // Digits 0-9
    if (c >= '0' && c <= '9') {
        switch (c) {
            case '0': { static const uint8_t p[7]={0b01110,0b10001,0b10011,0b10101,0b11001,0b10001,0b01110}; return p[row]; }
            case '1': { static const uint8_t p[7]={0b00100,0b01100,0b00100,0b00100,0b00100,0b00100,0b01110}; return p[row]; }
            case '2': { static const uint8_t p[7]={0b01110,0b10001,0b00001,0b00010,0b00100,0b01000,0b11111}; return p[row]; }
            case '3': { static const uint8_t p[7]={0b11110,0b00001,0b00001,0b01110,0b00001,0b00001,0b11110}; return p[row]; }
            case '4': { static const uint8_t p[7]={0b00010,0b00110,0b01010,0b10010,0b11111,0b00010,0b00010}; return p[row]; }
            case '5': { static const uint8_t p[7]={0b11111,0b10000,0b10000,0b11110,0b00001,0b00001,0b11110}; return p[row]; }
            case '6': { static const uint8_t p[7]={0b01110,0b10000,0b10000,0b11110,0b10001,0b10001,0b01110}; return p[row]; }
            case '7': { static const uint8_t p[7]={0b11111,0b00001,0b00010,0b00100,0b01000,0b01000,0b01000}; return p[row]; }
            case '8': { static const uint8_t p[7]={0b01110,0b10001,0b10001,0b01110,0b10001,0b10001,0b01110}; return p[row]; }
            case '9': { static const uint8_t p[7]={0b01110,0b10001,0b10001,0b01111,0b00001,0b00001,0b01110}; return p[row]; }
        }
    }

    // Uppercase A-Z
    if (c >= 'A' && c <= 'Z') {
        switch (c) {
            case 'A': { static const uint8_t p[7]={0b01110,0b10001,0b10001,0b11111,0b10001,0b10001,0b10001}; return p[row]; }
            case 'B': { static const uint8_t p[7]={0b11110,0b10001,0b10001,0b11110,0b10001,0b10001,0b11110}; return p[row]; }
            case 'C': { static const uint8_t p[7]={0b01111,0b10000,0b10000,0b10000,0b10000,0b10000,0b01111}; return p[row]; }
            case 'D': { static const uint8_t p[7]={0b11110,0b10001,0b10001,0b10001,0b10001,0b10001,0b11110}; return p[row]; }
            case 'E': { static const uint8_t p[7]={0b11111,0b10000,0b10000,0b11110,0b10000,0b10000,0b11111}; return p[row]; }
            case 'F': { static const uint8_t p[7]={0b11111,0b10000,0b10000,0b11110,0b10000,0b10000,0b10000}; return p[row]; }
            case 'G': { static const uint8_t p[7]={0b01111,0b10000,0b10000,0b10011,0b10001,0b10001,0b01111}; return p[row]; }
            case 'H': { static const uint8_t p[7]={0b10001,0b10001,0b10001,0b11111,0b10001,0b10001,0b10001}; return p[row]; }
            case 'I': { static const uint8_t p[7]={0b01110,0b00100,0b00100,0b00100,0b00100,0b00100,0b01110}; return p[row]; }
            case 'J': { static const uint8_t p[7]={0b00001,0b00001,0b00001,0b00001,0b10001,0b10001,0b01110}; return p[row]; }
            case 'K': { static const uint8_t p[7]={0b10001,0b10010,0b10100,0b11000,0b10100,0b10010,0b10001}; return p[row]; }
            case 'L': { static const uint8_t p[7]={0b10000,0b10000,0b10000,0b10000,0b10000,0b10000,0b11111}; return p[row]; }
            case 'M': { static const uint8_t p[7]={0b10001,0b11011,0b10101,0b10101,0b10001,0b10001,0b10001}; return p[row]; }
            case 'N': { static const uint8_t p[7]={0b10001,0b11001,0b10101,0b10011,0b10001,0b10001,0b10001}; return p[row]; }
            case 'O': { static const uint8_t p[7]={0b01110,0b10001,0b10001,0b10001,0b10001,0b10001,0b01110}; return p[row]; }
            case 'P': { static const uint8_t p[7]={0b11110,0b10001,0b10001,0b11110,0b10000,0b10000,0b10000}; return p[row]; }
            case 'Q': { static const uint8_t p[7]={0b01110,0b10001,0b10001,0b10001,0b10101,0b10010,0b01101}; return p[row]; }
            case 'R': { static const uint8_t p[7]={0b11110,0b10001,0b10001,0b11110,0b10100,0b10010,0b10001}; return p[row]; }
            case 'S': { static const uint8_t p[7]={0b01111,0b10000,0b10000,0b01110,0b00001,0b00001,0b11110}; return p[row]; }
            case 'T': { static const uint8_t p[7]={0b11111,0b00100,0b00100,0b00100,0b00100,0b00100,0b00100}; return p[row]; }
            case 'U': { static const uint8_t p[7]={0b10001,0b10001,0b10001,0b10001,0b10001,0b10001,0b01110}; return p[row]; }
            case 'V': { static const uint8_t p[7]={0b10001,0b10001,0b10001,0b10001,0b10001,0b01010,0b00100}; return p[row]; }
            case 'W': { static const uint8_t p[7]={0b10001,0b10001,0b10001,0b10101,0b10101,0b11011,0b10001}; return p[row]; }
            case 'X': { static const uint8_t p[7]={0b10001,0b10001,0b01010,0b00100,0b01010,0b10001,0b10001}; return p[row]; }
            case 'Y': { static const uint8_t p[7]={0b10001,0b10001,0b01010,0b00100,0b00100,0b00100,0b00100}; return p[row]; }
            case 'Z': { static const uint8_t p[7]={0b11111,0b00001,0b00010,0b00100,0b01000,0b10000,0b11111}; return p[row]; }
        }
    }

    // Lowercase a-z (simple, legible)
    if (c >= 'a' && c <= 'z') {
        switch (c) {
            case 'a': { static const uint8_t p[7]={0,0,0b01110,0b00001,0b01111,0b10001,0b01111}; return p[row]; }
            case 'b': { static const uint8_t p[7]={0b10000,0b10000,0b11110,0b10001,0b10001,0b10001,0b11110}; return p[row]; }
            case 'c': { static const uint8_t p[7]={0,0,0b01111,0b10000,0b10000,0b10000,0b01111}; return p[row]; }
            case 'd': { static const uint8_t p[7]={0b00001,0b00001,0b01111,0b10001,0b10001,0b10001,0b01111}; return p[row]; }
            case 'e': { static const uint8_t p[7]={0,0,0b01110,0b10001,0b11111,0b10000,0b01111}; return p[row]; }
            case 'f': { static const uint8_t p[7]={0b00110,0b01001,0b01000,0b11100,0b01000,0b01000,0b01000}; return p[row]; }
            case 'g': { static const uint8_t p[7]={0,0,0b01111,0b10001,0b10001,0b01111,0b00001}; return p[row]; }
            case 'h': { static const uint8_t p[7]={0b10000,0b10000,0b11110,0b10001,0b10001,0b10001,0b10001}; return p[row]; }
            case 'i': { static const uint8_t p[7]={0b00100,0,0b01100,0b00100,0b00100,0b00100,0b01110}; return p[row]; }
            case 'j': { static const uint8_t p[7]={0b00010,0,0b00110,0b00010,0b00010,0b10010,0b01100}; return p[row]; }
            case 'k': { static const uint8_t p[7]={0b10000,0b10000,0b10010,0b10100,0b11000,0b10100,0b10010}; return p[row]; }
            case 'l': { static const uint8_t p[7]={0b01100,0b00100,0b00100,0b00100,0b00100,0b00100,0b01110}; return p[row]; }
            case 'm': { static const uint8_t p[7]={0,0,0b11010,0b10101,0b10101,0b10101,0b10101}; return p[row]; }
            case 'n': { static const uint8_t p[7]={0,0,0b11110,0b10001,0b10001,0b10001,0b10001}; return p[row]; }
            case 'o': { static const uint8_t p[7]={0,0,0b01110,0b10001,0b10001,0b10001,0b01110}; return p[row]; }
            case 'p': { static const uint8_t p[7]={0,0,0b11110,0b10001,0b10001,0b11110,0b10000}; return p[row]; }
            case 'q': { static const uint8_t p[7]={0,0,0b01111,0b10001,0b10001,0b01111,0b00001}; return p[row]; }
            case 'r': { static const uint8_t p[7]={0,0,0b10111,0b11000,0b10000,0b10000,0b10000}; return p[row]; }
            case 's': { static const uint8_t p[7]={0,0,0b01111,0b10000,0b01110,0b00001,0b11110}; return p[row]; }
            case 't': { static const uint8_t p[7]={0b01000,0b01000,0b11100,0b01000,0b01000,0b01001,0b00110}; return p[row]; }
            case 'u': { static const uint8_t p[7]={0,0,0b10001,0b10001,0b10001,0b10011,0b01101}; return p[row]; }
            case 'v': { static const uint8_t p[7]={0,0,0b10001,0b10001,0b10001,0b01010,0b00100}; return p[row]; }
            case 'w': { static const uint8_t p[7]={0,0,0b10001,0b10001,0b10101,0b11011,0b10001}; return p[row]; }
            case 'x': { static const uint8_t p[7]={0,0,0b10001,0b01010,0b00100,0b01010,0b10001}; return p[row]; }
            case 'y': { static const uint8_t p[7]={0,0,0b10001,0b10001,0b10001,0b01111,0b00001}; return p[row]; }
            case 'z': { static const uint8_t p[7]={0,0,0b11111,0b00010,0b00100,0b01000,0b11111}; return p[row]; }
        }
    }

    // Fallback: small box
    static const uint8_t box[7] = {0b11111,0b10001,0b10001,0b10001,0b10001,0b10001,0b11111};
    return box[row];
}

static inline void ade_put_pixel_argb(uint8_t *dst, int stride, int x, int y, uint8_t a, uint8_t r, uint8_t g, uint8_t b) {
    uint8_t *p = dst + y * stride + x * 4;
    // ARGB32 in memory is platform-endian for Cairo, but wlroots expects WL_SHM_FORMAT_ARGB8888
    // which is 0xAARRGGBB in 32-bit. For little-endian, bytes are BB GG RR AA.
    // We'll write in that byte order to be safe on typical LE.
    p[0] = b;
    p[1] = g;
    p[2] = r;
    p[3] = a;
}

static struct wlr_buffer *make_label_buffer(const char *text, int *out_w, int *out_h) {
    if (!text || !*text) return NULL;

    // Layout: fixed-size bitmap font with optional background
    const int glyph_w = 5;
    const int glyph_h = 7;
    const int scale = 2;              // 2x scale so it doesn't look tiny
    const int gap = 1 * scale;        // gap between glyphs
    const int pad_x = 6;              // padding around text
    const int pad_y = 4;

    // Clamp length so we don't allocate silly buffers
    const size_t max_chars = 64;
    size_t n = strlen(text);
    if (n > max_chars) n = max_chars;

    const int text_w = (int)n * (glyph_w * scale) + (int)(n ? (n - 1) : 0) * gap;
    const int text_h = glyph_h * scale;

    const int W = text_w + pad_x * 2;
    const int H = text_h + pad_y * 2;

    const int stride = W * 4;
    uint8_t *pixels = calloc(1, (size_t)stride * (size_t)H);
    if (!pixels) return NULL;

    // Background: subtle dark rounded-rect vibe (rectangular for now)
    // ARGB8888 bytes: BB GG RR AA
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            ade_put_pixel_argb(pixels, stride, x, y, 140, 0, 0, 0); // ~55% black
        }
    }

    // Draw text in white with a tiny shadow for readability
    const int base_x = pad_x;
    const int base_y = pad_y;

    for (size_t i = 0; i < n; i++) {
        char c = text[i];
        int ox = base_x + (int)i * ((glyph_w * scale) + gap);
        int oy = base_y;

        for (int row = 0; row < glyph_h; row++) {
            uint8_t bits = ade_font5x7_get_row(c, row);
            for (int col = 0; col < glyph_w; col++) {
                bool on = (bits >> (glyph_w - 1 - col)) & 1;
                if (!on) continue;

                // Shadow (1px down-right)
                for (int sy = 0; sy < scale; sy++) {
                    for (int sx = 0; sx < scale; sx++) {
                        int px = ox + col * scale + sx;
                        int py = oy + row * scale + sy;
                        int shx = px + 1;
                        int shy = py + 1;
                        if (shx >= 0 && shx < W && shy >= 0 && shy < H) {
                            ade_put_pixel_argb(pixels, stride, shx, shy, 200, 0, 0, 0);
                        }
                        if (px >= 0 && px < W && py >= 0 && py < H) {
                            ade_put_pixel_argb(pixels, stride, px, py, 255, 255, 255, 255);
                        }
                    }
                }
            }
        }
    }

    struct wlr_buffer *buf = wlr_buffer_from_pixels(
        WL_SHM_FORMAT_ARGB8888,
        stride,
        W, H,
        pixels
    );

    // NOTE: wlroots may keep a reference to the provided pixel data.
    // Keep `pixels` alive for the lifetime of the buffer.
    // (Small, bounded leak for now; can be replaced later with a custom wlr_buffer impl + destroy.)
    (void)pixels;

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

        ic->icon_w = 48;
        ic->icon_h = 48;

        // Always render a label for debugging: if config label is missing, show a default.
        const char *label_text = (ic->label && *ic->label) ? ic->label : "Icon";
        int lw = 0, lh = 0;
        struct wlr_buffer *lb = make_label_buffer(label_text, &lw, &lh);
        if (lb) {
            struct wlr_scene_buffer *ls = wlr_scene_buffer_create(server->desktop_icons, lb);
            wlr_buffer_drop(lb);

            ic->label_w = lw;
            ic->label_h = lh;

            // Position using the *buffer* dimensions so the label actually centers under the icon
            int lx = ic->x + (ic->icon_w / 2) - (ic->label_w / 2);
            int ly = ic->y + ic->icon_h + 4;
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
            sel_h = ic->icon_h + 4 + ic->label_h + 16;
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
