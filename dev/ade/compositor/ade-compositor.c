/**
 *
 *
 * left: 139, 240, 217, 196, 139
 * yellow tab: top: (254, 236, 166 ) bot( 254 , 194  , 7)
 */
// Feature-test macros (must be defined before any system headers)
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE 1
#endif
#include <time.h>
#include <stdlib.h>
#include <string.h>

// wlroots buffer interface (needed for wlr_buffer_impl + wlr_buffer_init)
#include <wlr/interfaces/wlr_buffer.h>

// stb (install on Arch with: pacman -S stb)
#define STB_IMAGE_IMPLEMENTATION
#include <stb/stb_image.h>

#include <drm_fourcc.h>
#include <wlr/types/wlr_buffer.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>

#include <linux/input-event-codes.h>

#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <ctype.h>
#include <time.h>
#include <unistd.h>
#include <getopt.h>
#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_shell.h>

#include <wlr/util/log.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-keysyms.h>

#include <wlr/render/wlr_texture.h>

#include "desktop_icons.h"

#define ADE_TAB_HEIGHT 27
// Vertical offset (in pixels) for text and buttons inside the yellow tab
#define ADE_TAB_CONTENT_Y_OFFSET 6
#define ADE_TAB_GAP_ABOVE_FRAME 5
// Tab bottom is 6px above the top border (border is 1px)
#define ADE_TAB_Y (-(ADE_TAB_HEIGHT + ADE_TAB_GAP_ABOVE_FRAME + 0))
#define ADE_LEFT_RESIZE_GRIP_W 6

#define ADE_TOP_RESIZE_GRIP_H 6


/* For brevity's sake, struct members are annotated where they are used. */
enum tinywl_cursor_mode {
    TINYWL_CURSOR_PASSTHROUGH,
    TINYWL_CURSOR_MOVE,
    TINYWL_CURSOR_RESIZE,
    TINYWL_CURSOR_TABDRAG,
    TINYWL_CURSOR_ICONDRAG,
};


struct tinywl_server {
    struct wl_display *wl_display;
    struct wlr_backend *backend;
    struct wlr_renderer *renderer;
    struct wlr_allocator *allocator;
    struct wlr_scene *scene;
    struct wlr_scene_output_layout *scene_layout;

    struct wlr_xdg_shell *xdg_shell;
    struct wl_listener new_xdg_toplevel;
    struct wl_listener new_xdg_popup;
    struct wl_list toplevels;
    /* Layer shell support */
    struct wlr_layer_shell_v1 *layer_shell;
    struct wl_listener new_layer_surface;

    struct wlr_cursor *cursor;
    struct wlr_xcursor_manager *cursor_mgr;
    struct wl_listener cursor_motion;
    struct wl_listener cursor_motion_absolute;
    struct wl_listener cursor_button;
    struct wl_listener cursor_axis;
    struct wl_listener cursor_frame;

    struct wlr_seat *seat;
    struct wl_listener new_input;
    struct wl_listener request_cursor;
    struct wl_listener pointer_focus_change;
    struct wl_listener request_set_selection;
    struct wl_list keyboards;
    enum tinywl_cursor_mode cursor_mode;
    struct tinywl_toplevel *grabbed_toplevel;
    double grab_x, grab_y;
    struct wlr_box grab_geobox;
    uint32_t resize_edges;

    struct wlr_output_layout *output_layout;
    struct wl_list outputs;
    struct wl_listener new_output;
    
    // Window cascading state (applied on first map)
    int cascade_index;
    int cascade_step;
    
    struct wlr_scene_rect *bg_rect;

    // Desktop icons
    struct wlr_scene_tree *desktop_icons;
    struct wlr_scene_buffer *terminal_icon_scene;
    struct wlr_scene_rect *terminal_icon_rect;
    
    // Desktop icon double-click tracking
    uint32_t last_icon_click_msec;
    int icon_click_count;
    
    // Desktop icon drag state
    struct wlr_scene_node *dragged_icon_node;
    double icon_grab_dx;
    double icon_grab_dy;
    bool icon_drag_candidate;
    bool icon_dragging;
    double icon_press_x;
    double icon_press_y;
    
    // Deskbar (Haiku-style top-right bar)
    struct wlr_scene_tree *deskbar_tree;
    struct wlr_scene_tree *deskbar_apps_tree;
    struct wlr_scene_rect *deskbar_bg;
    int deskbar_app_count;
    int deskbar_width;
    int deskbar_height;
    
    struct ade_desktop_icons desktop_icons_state;
    char desktop_icons_conf_path[512];
    
    struct ade_desktop_icon *last_clicked_icon;
    
    struct wlr_xdg_decoration_manager_v1 *xdg_decoration_manager;
    struct wl_listener new_xdg_decoration;
    
    struct wl_listener backend_destroy;
    
    bool backend_destroyed;
};



struct tinywl_output {
    struct wl_list link;
    struct tinywl_server *server;
    struct wlr_output *wlr_output;
    struct wlr_scene_output *scene_output;
    struct wl_listener frame;
    struct wl_listener request_state;
    struct wl_listener destroy;
};

struct tinywl_toplevel {
    struct wl_list link;
    struct tinywl_server *server;
    struct wlr_xdg_toplevel *xdg_toplevel;
    struct wlr_scene_tree *scene_tree;
    
    /* Simple server-side decoration: BeOS-style tab */
    struct wlr_scene_tree *decor_tree;
    struct wlr_scene_tree *tab_tree;      // container for the whole tab (rect + text + close)
    struct wlr_scene_rect *tab_rect;
    struct wlr_scene_tree *tab_grad_tree;
    // Subtle vertical detail lines on the left/right edges of the tab
    struct wlr_scene_rect *tab_line_left;
    struct wlr_scene_rect *tab_line_right;
    struct wlr_scene_rect *tab_line_top;
    struct wlr_scene_tree *tab_text_tree; // children are tiny rects forming bitmap text
    int tab_width_px;                     // current tab width (for clamping)
    int tab_x_px;                         // current tab X offset (sliding along top)
    
    // Close button (right side of the tab)
    struct wlr_scene_tree *close_tree;
    struct wlr_scene_rect *close_bg;
    struct wlr_scene_tree *close_x_tree;

    // Simple gray window border (BeOS-style)
    struct wlr_scene_rect *border_top;
    struct wlr_scene_rect *border_bottom;
    struct wlr_scene_rect *border_left; // dep
    struct wlr_scene_rect *border_right;
    
    // Left-side resize grip (pink, draggable)
    struct wlr_scene_rect *left_resize_grip;
    // Detail line on the grip (thin red vertical line)
    struct wlr_scene_rect *left_resize_grip_redline; // rename red to left_side
    struct wlr_scene_rect *left_resize_grip_redline2; // rename redline2 to right_side
    
    // Top resize grip (6px high, draggable)
    struct wlr_scene_rect *top_resize_grip;
    struct wlr_scene_rect *top_resize_grip_line_top;
    struct wlr_scene_rect *top_resize_grip_line_bottom;

    // Right resize grip (6px wide, draggable)
    struct wlr_scene_rect *right_resize_grip;
    struct wlr_scene_rect *right_resize_grip_redline;
    struct wlr_scene_rect *right_resize_grip_redline2;

    // Bottom resize grip (6px high, draggable)
    struct wlr_scene_rect *bottom_resize_grip;
    struct wlr_scene_rect *bottom_resize_grip_line_top;
    struct wlr_scene_rect *bottom_resize_grip_line_bottom;
    
   

    // Bottom-right corner resize grip (6x6 outside the window)
    struct wlr_scene_rect *corner_rl_resize_grip; //
    // Grey detail line at bottom of the corner resize grip
    struct wlr_scene_rect *corner_rl_resize_grip_line_bottom;
    // Grey detail line at right edge of the corner resize grip
    struct wlr_scene_rect *corner_rl_resize_grip_line_right;
    
    struct wlr_scene_rect *corner_ru_resize_grip; //
    // Grey detail line at top of the corner resize grip
    struct wlr_scene_rect *corner_ru_resize_grip_line_top;
    // Grey detail line at right edge of the corner resize grip
    struct wlr_scene_rect *corner_ru_resize_grip_line_right;
    
    struct wlr_scene_rect *corner_lu_resize_grip; //
    // Grey detail line at top of the corner resize grip
    struct wlr_scene_rect *corner_lu_resize_grip_line_top;
    // Grey detail line at left edge of the corner resize grip
    struct wlr_scene_rect *corner_lu_resize_grip_line_left;
    
    // Bottom-left corner resize grip (6x6 outside the window)
    struct wlr_scene_rect *corner_bl_resize_grip; //
    // Grey detail line at bottom of the corner resize grip
    struct wlr_scene_rect *corner_bl_resize_grip_line_bottom;
    // Grey detail line at left edge of the corner resize grip
    struct wlr_scene_rect *corner_bl_resize_grip_line_left;
    
    
    
    struct wl_listener map;
    struct wl_listener unmap;
    struct wl_listener commit;
    struct wl_listener destroy;
    struct wl_listener request_move;
    struct wl_listener request_resize;
    struct wl_listener request_maximize;
    struct wl_listener request_fullscreen;
    
    struct wlr_scene_tree *expand_tree;
    struct wlr_scene_rect *expand_bg;
    struct wlr_scene_tree *expand_plus_tree;
    
    struct wlr_xdg_toplevel_decoration_v1 *xdg_decoration;
    struct wl_listener xdg_decoration_request_mode;
    struct wl_listener xdg_decoration_destroy;
};

struct tinywl_popup {
    struct wlr_xdg_popup *xdg_popup;
    struct wl_listener commit;
    struct wl_listener destroy;
};

struct tinywl_keyboard {
    struct wl_list link;
    struct tinywl_server *server;
    struct wlr_keyboard *wlr_keyboard;

    struct wl_listener modifiers;
    struct wl_listener key;
    struct wl_listener destroy;
};


static void ade_spawn(const char *cmd) {
    pid_t pid = fork();
    if (pid == 0) {
        // Child
        setsid();
        execlp(cmd, cmd, (char *)NULL);
        _exit(1); // exec failed
    }
}

static void ade_spawn_argv(char *const argv[]) {
    pid_t pid = fork();
    if (pid == 0) {
        setsid();
        execvp(argv[0], argv);
        perror("ADE execvp failed");
        _exit(1);
    }
}

static void ade_spawn_shell(const char *sh_cmd) {
    pid_t pid = fork();
    if (pid == 0) {
        setsid();
        execl("/bin/sh", "/bin/sh", "-lc", sh_cmd, (char *)NULL);
        perror("ADE execl(/bin/sh) failed");
        _exit(1);
    }
}

// Forward declarations
static void ade_update_background(struct tinywl_server *server);

// Ensure the xcursor theme is loaded at the output scale.
// If the cursor theme is only loaded at scale=1 on a HiDPI output,
// the hotspot can be wrong (cursor appears offset; clicks don't land).
static void ade_reload_cursor_for_output(struct tinywl_server *server, struct wlr_output *out) {
    if (server == NULL || server->cursor_mgr == NULL || server->cursor == NULL) {
        return;
    }

    int scale = 1;
    if (out != NULL) {
        float s = out->scale;
        scale = (int)(s + 0.999f); // ceil without needing math.h
        if (scale < 1) {
            scale = 1;
        }
    }

    wlr_xcursor_manager_load(server->cursor_mgr, scale);
    wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "default");
}


static bool ade_node_is_or_ancestor(struct wlr_scene_node *node, struct wlr_scene_node *target) {
    if (!node || !target) return false;
    struct wlr_scene_node *cur = node;
    while (cur) {
        if (cur == target) return true;
        if (!cur->parent) break;
        cur = &cur->parent->node;
    }
    return false;
}

static void ade_launch_terminal(void) {
    // Prefer foot; fall back to xterm; last resort a shell
    const char *cmd =
        "(command -v foot >/dev/null 2>&1 && exec foot) "
        "|| (command -v xterm >/dev/null 2>&1 && exec xterm) "
        "|| exec /bin/sh";
    ade_spawn_shell(cmd);
}


// -----------------------------------------------------------------------------
// PNG -> wlr_buffer helper (wlroots 0.20)
// -----------------------------------------------------------------------------
struct ade_pixel_buffer {
    struct wlr_buffer base;
    int width;
    int height;
    size_t stride;
    uint32_t format; // DRM_FORMAT_*
    uint8_t *data;
};

static void ade_pixel_buffer_destroy(struct wlr_buffer *wlr_buffer) {
    struct ade_pixel_buffer *buf = wl_container_of(wlr_buffer, buf, base);
    free(buf->data);
    free(buf);
}

static bool ade_pixel_buffer_begin_data_ptr_access(struct wlr_buffer *wlr_buffer,
        uint32_t flags, void **data, uint32_t *format, size_t *stride) {
    (void)flags;
    struct ade_pixel_buffer *buf = wl_container_of(wlr_buffer, buf, base);
    if (data) *data = buf->data;
    if (format) *format = buf->format;
    if (stride) *stride = buf->stride;
    return true;
}

static void ade_pixel_buffer_end_data_ptr_access(struct wlr_buffer *wlr_buffer) {
    (void)wlr_buffer;
}

static const struct wlr_buffer_impl ade_pixel_buffer_impl = {
    .destroy = ade_pixel_buffer_destroy,
    .begin_data_ptr_access = ade_pixel_buffer_begin_data_ptr_access,
    .end_data_ptr_access = ade_pixel_buffer_end_data_ptr_access,
};

static struct wlr_buffer *ade_load_png_as_wlr_buffer(const char *path) {
    if (path == NULL) {
        return NULL;
    }

    int w = 0, h = 0, n = 0;
    // Force 4 channels (RGBA)
    unsigned char *rgba = stbi_load(path, &w, &h, &n, 4);
    if (!rgba || w <= 0 || h <= 0) {
        wlr_log(WLR_ERROR, "ADE: failed to load PNG %s (%s)", path, stbi_failure_reason());
        if (rgba) stbi_image_free(rgba);
        return NULL;
    }

    struct ade_pixel_buffer *buf = calloc(1, sizeof(*buf));
    if (!buf) {
        stbi_image_free(rgba);
        return NULL;
    }

    buf->width = w;
    buf->height = h;
    buf->stride = (size_t)w * 4;
    // stb gives RGBA bytes; DRM_FORMAT_ABGR8888 matches that byte layout on little-endian
    buf->format = DRM_FORMAT_ABGR8888;
    buf->data = malloc(buf->stride * (size_t)h);
    if (!buf->data) {
        free(buf);
        stbi_image_free(rgba);
        return NULL;
    }

    // --- Transparency handling ---
    // Many icon PNGs already contain an alpha channel. If yours has a solid
    // background baked in, we can treat the top-left pixel as a "color key"
    // and make matching pixels fully transparent.
    const uint8_t key_r = rgba[0];
    const uint8_t key_g = rgba[1];
    const uint8_t key_b = rgba[2];
    const uint8_t key_a = rgba[3];

    // Copy into our buffer, optionally apply color-key transparency, and
    // premultiply alpha so blending looks correct.
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            size_t i = (size_t)(y * w + x) * 4;
            uint8_t r = rgba[i + 0];
            uint8_t g = rgba[i + 1];
            uint8_t b = rgba[i + 2];
            uint8_t a = rgba[i + 3];

            // If the PNG has an opaque background color, key it out.
            // We only key when the sampled key pixel is opaque, to avoid
            // breaking images which already use alpha.
            if (key_a == 255 && a == 255 && r == key_r && g == key_g && b == key_b) {
                a = 0;
            }

            // Premultiply alpha for proper blending.
            if (a < 255) {
                r = (uint8_t)((uint16_t)r * (uint16_t)a / 255);
                g = (uint8_t)((uint16_t)g * (uint16_t)a / 255);
                b = (uint8_t)((uint16_t)b * (uint16_t)a / 255);
            }

            buf->data[i + 0] = r;
            buf->data[i + 1] = g;
            buf->data[i + 2] = b;
            buf->data[i + 3] = a;
        }
    }

    stbi_image_free(rgba);

    wlr_buffer_init(&buf->base, &ade_pixel_buffer_impl, (size_t)w, (size_t)h);
    return &buf->base;
}



static void ade_show_help(void) {
    // Show key hints in a terminal so the user doesn't need to remember commands.
    // Using foot if available; fallback to xterm if installed.
    const char *cmd =
        "(command -v foot >/dev/null 2>&1 && exec foot -T ADE-Help -e /bin/sh -lc 'printf \"ADE – Arma Desktop Environment\\n\\nF1   = Terminal\\nF2   = Launcher\\nF9   = Help\\nF12  = Quit\\nAlt+Esc = Quit\\n\\n(Press Enter to close this help)\\n\"; read _') "
        "|| (command -v xterm >/dev/null 2>&1 && exec xterm -T ADE-Help -e /bin/sh -lc 'printf \"ADE – Arma Desktop Environment\\n\\nF1   = Terminal\\nF2   = Launcher\\nF9   = Help\\nF12  = Quit\\nAlt+Esc = Quit\\n\\n(Press Enter to close this help)\\n\"; read _')";
    ade_spawn_shell(cmd);
}


// -----------------------------------------------------------------------------
// Title sanitization
// wlroots titles are UTF-8. Our tiny 5x7 font only supports basic ASCII.
// Convert unknown/non-ASCII bytes to '?' so we never render random glyphs.
// -----------------------------------------------------------------------------
#include <string.h>
static void ade_sanitize_title_ascii(const char *in, char *out, size_t out_sz) {
    if (out == NULL || out_sz == 0) {
        return;
    }
    out[0] = '\0';

    if (in == NULL || in[0] == '\0') {
        snprintf(out, out_sz, "%s", "APP");
        return;
    }

    size_t oi = 0;
    for (size_t i = 0; in[i] != '\0' && oi + 1 < out_sz; i++) {
        unsigned char c = (unsigned char)in[i];

        // Replace control chars; SKIP non-ASCII UTF-8 bytes entirely.
        if (c < 0x20 || c == 0x7F) {
            // Convert tabs/newlines to space
            if (c == '\t' || c == '\n' || c == '\r') {
                c = ' ';
            } else {
                c = ' ';
            }
        } else if (c >= 0x80) {
            // Most app titles are UTF-8. Our tiny font is ASCII-only.
            // Skip UTF-8 bytes so we keep any ASCII parts instead of rendering garbage.
            continue;
        }

        // Normalize to uppercase for our glyph table
        if (c >= 'a' && c <= 'z') {
            c = (unsigned char)(c - 'a' + 'A');
        }

        out[oi++] = (char)c;
    }
    out[oi] = '\0';

    // Trim trailing spaces
    while (oi > 0 && out[oi - 1] == ' ') {
        out[--oi] = '\0';
    }

    // Trim leading spaces
    size_t start = 0;
    while (out[start] == ' ') start++;
    if (start > 0) {
        memmove(out, out + start, strlen(out + start) + 1);
    }

    // Collapse consecutive spaces
    size_t ri = 0;
    size_t wi = 0;
    bool prev_space = false;
    while (out[ri] != '\0') {
        char ch = out[ri++];
        if (ch == ' ') {
            if (prev_space) continue;
            prev_space = true;
        } else {
            prev_space = false;
        }
        out[wi++] = ch;
        if (wi + 1 >= out_sz) break;
    }
    out[wi] = '\0';

    // Trim trailing space again after collapsing
    while (wi > 0 && out[wi - 1] == ' ') {
        out[--wi] = '\0';
    }

    // If we ended up empty, fall back
    if (out[0] == '\0') {
        snprintf(out, out_sz, "%s", "APP");
    }
}

// -----------------------------------------------------------------------------
// Very small built-in bitmap font renderer for the yellow BeOS-style tab.
// We draw text as tiny rectangles ("pixels") so we don't need extra deps.
// 5x7 font, only a minimal subset we care about right now.
// -----------------------------------------------------------------------------

static uint8_t ade_font5x7_get(char c, int col) {
    // Each glyph is 5 columns wide. Return a 7-bit column mask (MSB of the 7 bits = top).
    // Missing glyphs fall back to '?'.
    // NOTE: These are intentionally simple/rough.
    if (col < 0 || col >= 5) {
        return 0;
    }

    // Normalize
    if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');

    switch (c) {
    case ' ': {
        static const uint8_t g[5] = {0,0,0,0,0};
        return g[col];
    }
    case '-': {
        static const uint8_t g[5] = {0,0,0b0010000,0,0};
        return g[col];
    }
    case '.': {
        static const uint8_t g[5] = {0,0,0,0b1000000,0b1000000};
        return g[col];
    }
    case '?': {
        // A simple 5x7 question mark
        static const uint8_t g[5] = {
            0b0100000,
            0b1010000,
            0b0001000,
            0b0000000,
            0b0001000
        };
        return g[col];
    }
    case '0': { static const uint8_t g[5] = {0b0111110,0b1000001,0b1000001,0b1000001,0b0111110}; return g[col]; }
    case '1': { static const uint8_t g[5] = {0b0000000,0b0100001,0b1111111,0b0000001,0b0000000}; return g[col]; }
    case '2': { static const uint8_t g[5] = {0b0100011,0b1000101,0b1001001,0b1010001,0b0100001}; return g[col]; }
    case '3': { static const uint8_t g[5] = {0b0100010,0b1000001,0b1001001,0b1001001,0b0110110}; return g[col]; }
    case '4': { static const uint8_t g[5] = {0b0001100,0b0010100,0b0100100,0b1111111,0b0000100}; return g[col]; }
    case '5': { static const uint8_t g[5] = {0b1110010,0b1010001,0b1010001,0b1010001,0b1001110}; return g[col]; }
    case '6': { static const uint8_t g[5] = {0b0011110,0b0101001,0b1001001,0b1001001,0b0000110}; return g[col]; }
    case '7': { static const uint8_t g[5] = {0b1000000,0b1000111,0b1001000,0b1010000,0b1100000}; return g[col]; }
    case '8': { static const uint8_t g[5] = {0b0110110,0b1001001,0b1001001,0b1001001,0b0110110}; return g[col]; }
    case '9': { static const uint8_t g[5] = {0b0110000,0b1001001,0b1001001,0b1001010,0b0111100}; return g[col]; }
    case 'A': { static const uint8_t g[5] = {0b0111111,0b1001000,0b1001000,0b1001000,0b0111111}; return g[col]; }
    case 'B': { static const uint8_t g[5] = {0b1111111,0b1001001,0b1001001,0b1001001,0b0110110}; return g[col]; }
    case 'C': { static const uint8_t g[5] = {0b0111110,0b1000001,0b1000001,0b1000001,0b0100010}; return g[col]; }
    case 'D': { static const uint8_t g[5] = {0b1111111,0b1000001,0b1000001,0b0100010,0b0011100}; return g[col]; }
    case 'E': { static const uint8_t g[5] = {0b1111111,0b1001001,0b1001001,0b1001001,0b1000001}; return g[col]; }
    case 'F': { static const uint8_t g[5] = {0b1111111,0b1001000,0b1001000,0b1001000,0b1000000}; return g[col]; }
    case 'G': { static const uint8_t g[5] = {0b0111110,0b1000001,0b1001001,0b1001001,0b0101110}; return g[col]; }
    case 'H': { static const uint8_t g[5] = {0b1111111,0b0001000,0b0001000,0b0001000,0b1111111}; return g[col]; }
    case 'I': { static const uint8_t g[5] = {0,0b1000001,0b1111111,0b1000001,0}; return g[col]; }
    case 'J': { static const uint8_t g[5] = {0b0000010,0b0000001,0b1000001,0b1111110,0b1000000}; return g[col]; }
    case 'K': { static const uint8_t g[5] = {0b1111111,0b0001000,0b0010100,0b0100010,0b1000001}; return g[col]; }
    case 'L': { static const uint8_t g[5] = {0b1111111,0b0000001,0b0000001,0b0000001,0b0000001}; return g[col]; }
    case 'M': { static const uint8_t g[5] = {0b1111111,0b0100000,0b0011000,0b0100000,0b1111111}; return g[col]; }
    case 'N': { static const uint8_t g[5] = {0b1111111,0b0100000,0b0010000,0b0001000,0b1111111}; return g[col]; }
    case 'O': { static const uint8_t g[5] = {0b0111110,0b1000001,0b1000001,0b1000001,0b0111110}; return g[col]; }
    case 'P': { static const uint8_t g[5] = {0b1111111,0b1001000,0b1001000,0b1001000,0b0110000}; return g[col]; }
    case 'Q': { static const uint8_t g[5] = {0b0111110,0b1000001,0b1000101,0b1000010,0b0111101}; return g[col]; }
    case 'R': { static const uint8_t g[5] = {0b1111111,0b1001000,0b1001100,0b1001010,0b0110001}; return g[col]; }
    case 'S': { static const uint8_t g[5] = {0b0110010,0b1001001,0b1001001,0b1001001,0b0100110}; return g[col]; }
    case 'T': { static const uint8_t g[5] = {0b1000000,0b1000000,0b1111111,0b1000000,0b1000000}; return g[col]; }
    case 'U': { static const uint8_t g[5] = {0b1111110,0b0000001,0b0000001,0b0000001,0b1111110}; return g[col]; }
    case 'V': { static const uint8_t g[5] = {0b1111000,0b0000110,0b0000001,0b0000110,0b1111000}; return g[col]; }
    case 'W': { static const uint8_t g[5] = {0b1111110,0b0000001,0b0001110,0b0000001,0b1111110}; return g[col]; }
    case 'X': { static const uint8_t g[5] = {0b1100011,0b0010100,0b0001000,0b0010100,0b1100011}; return g[col]; }
    case 'Y': { static const uint8_t g[5] = {0b1100000,0b0010000,0b0001111,0b0010000,0b1100000}; return g[col]; }
    case 'Z': { static const uint8_t g[5] = {0b1000011,0b1000101,0b1001001,0b1010001,0b1100001}; return g[col]; }
    default:
        return ade_font5x7_get('?', col);
    }
}

// -----------------------------------------------------------------------------
// "Soft" (pseudo anti-aliased) text rendering
//
// We don't have a real font rasterizer dependency yet. Instead, we render our
// 5x7 bitmap font but add a subtle edge "halo" using alpha rectangles.
// This makes the text look much less jagged, similar to classic BeOS UI.
// -----------------------------------------------------------------------------

static float ade_glyph_coverage_5x7(char c, int x, int y) {
    // Returns 1.0 for a solid pixel, otherwise a small alpha if we're near an edge.
    // Coordinates are in glyph pixels: x=[0..4], y=[0..6].
    if (x < 0 || x >= 5 || y < 0 || y >= 7) {
        return 0.0f;
    }

    // Build "on" mask for this glyph
    bool on[7][5] = {{false}};
    for (int col = 0; col < 5; col++) {
        uint8_t mask = ade_font5x7_get(c, col);
        for (int row = 0; row < 7; row++) {
            // Our glyph columns are encoded with the TOP pixel in the MSB of the 7-bit column.
            // Convert to row-major with row=0 at the top.
            int bit = 6 - row;
            on[row][col] = ((mask >> bit) & 0x1) != 0;
        }
    }

    if (on[y][x]) {
        return 1.0f;
    }

    // 4-neighbor edge softening
    const int dx4[4] = { -1, 1, 0, 0 };
    const int dy4[4] = { 0, 0, -1, 1 };
    for (int i = 0; i < 4; i++) {
        int nx = x + dx4[i];
        int ny = y + dy4[i];
        if (nx >= 0 && nx < 5 && ny >= 0 && ny < 7 && on[ny][nx]) {
            return 0.35f;
        }
    }

    // diagonal edge softening
    const int dxd[4] = { -1, -1, 1, 1 };
    const int dyd[4] = { -1, 1, -1, 1 };
    for (int i = 0; i < 4; i++) {
        int nx = x + dxd[i];
        int ny = y + dyd[i];
        if (nx >= 0 && nx < 5 && ny >= 0 && ny < 7 && on[ny][nx]) {
            return 0.20f;
        }
    }

    return 0.0f;
}

static void ade_tab_draw_glyph_soft(struct wlr_scene_tree *parent,
        char c, int origin_x, int origin_y, int px, const float base_rgb[3]) {
    // Draw a 5x7 glyph using rectangles, but with a soft halo for anti-alias-like edges.
    // base_rgb is RGB (0..1). Alpha is controlled per "pixel".
    for (int gy = 0; gy < 7; gy++) {
        for (int gx = 0; gx < 5; gx++) {
            float cov = ade_glyph_coverage_5x7(c, gx, gy);
            if (cov <= 0.0f) {
                continue;
            }
            float ink[4] = { base_rgb[0], base_rgb[1], base_rgb[2], cov };
            struct wlr_scene_rect *dot = wlr_scene_rect_create(parent, px, px, ink);
            wlr_scene_node_set_position(&dot->node,
                origin_x + gx * px,
                origin_y + gy * px);
        }
    }
}

static int ade_title_measure_px(const char *safe_title, int px) {
    const int glyph_w = 5;
    const int glyph_spacing = 1;
    int title_len = 0;
    while (safe_title[title_len] != '\0' && title_len < 32) {
        title_len++;
    }
    int char_advance = (glyph_w + glyph_spacing) * px;
    return title_len * char_advance;
}

static void ade_tab_clear_text(struct tinywl_toplevel *toplevel) {
    if (toplevel->tab_text_tree != NULL) {
        wlr_scene_node_destroy(&toplevel->tab_text_tree->node);
        toplevel->tab_text_tree = NULL;
    }
}

static void ade_tab_clear_close(struct tinywl_toplevel *toplevel) {
    if (toplevel->close_x_tree != NULL) {
        wlr_scene_node_destroy(&toplevel->close_x_tree->node);
        toplevel->close_x_tree = NULL;
    }
    if (toplevel->close_bg != NULL) {
        wlr_scene_node_destroy(&toplevel->close_bg->node);
        toplevel->close_bg = NULL;
    }
    if (toplevel->close_tree != NULL) {
        wlr_scene_node_destroy(&toplevel->close_tree->node);
        toplevel->close_tree = NULL;
    }
}

static void ade_tab_clear_expand(struct tinywl_toplevel *toplevel) {
    if (toplevel->expand_plus_tree != NULL) {
        wlr_scene_node_destroy(&toplevel->expand_plus_tree->node);
        toplevel->expand_plus_tree = NULL;
    }
    if (toplevel->expand_bg != NULL) {
        wlr_scene_node_destroy(&toplevel->expand_bg->node);
        toplevel->expand_bg = NULL;
    }
    if (toplevel->expand_tree != NULL) {
        wlr_scene_node_destroy(&toplevel->expand_tree->node);
        toplevel->expand_tree = NULL;
    }
}


static void ade_tab_clear_gradient(struct tinywl_toplevel *toplevel) {
    if (toplevel && toplevel->tab_grad_tree) {
        wlr_scene_node_destroy(&toplevel->tab_grad_tree->node);
        toplevel->tab_grad_tree = NULL;
    }
}

static void ade_tab_build_gradient(struct tinywl_toplevel *toplevel, int tab_width) {
    if (!toplevel || !toplevel->tab_tree) return;

    ade_tab_clear_gradient(toplevel);

    if (tab_width < 1) tab_width = 1;

    toplevel->tab_grad_tree = wlr_scene_tree_create(toplevel->tab_tree);
    if (!toplevel->tab_grad_tree) return;

    // Top:    RGB(254,236,166)
    // Bottom: RGB(254,194,7)
    const float top_col[3]    = { 254.0f/255.0f, 236.0f/255.0f, 166.0f/255.0f };
    const float bottom_col[3] = { 254.0f/255.0f, 194.0f/255.0f,   7.0f/255.0f };

    for (int y = 0; y < ADE_TAB_HEIGHT; y++) {
        float t = (ADE_TAB_HEIGHT <= 1) ? 0.0f : (float)y / (float)(ADE_TAB_HEIGHT - 1);
        float col[4] = {
            top_col[0]    * (1.0f - t) + bottom_col[0] * t,
            top_col[1]    * (1.0f - t) + bottom_col[1] * t,
            top_col[2]    * (1.0f - t) + bottom_col[2] * t,
            1.0f
        };
        struct wlr_scene_rect *row =
            wlr_scene_rect_create(toplevel->tab_grad_tree, tab_width, 1, col);
        if (row) wlr_scene_node_set_position(&row->node, 0, y);
    }

    // Keep gradient behind everything else in tab_tree
    wlr_scene_node_lower_to_bottom(&toplevel->tab_grad_tree->node);
}



static void ade_tab_build_close(struct tinywl_toplevel *toplevel, int tab_width) {
    if (toplevel == NULL || toplevel->tab_tree == NULL) {
        return;
    }

    ade_tab_clear_close(toplevel);

    // Button geometry within the tab (LEFT side)
    const int margin_left = 10;
    const int btn_size = 16;  // square button
    const int btn_y = ADE_TAB_CONTENT_Y_OFFSET;
    int btn_x = margin_left;

    toplevel->close_tree = wlr_scene_tree_create(toplevel->tab_tree);

    // Background: slightly darker yellow
    float bg[4] = { 0.92f, 0.72f, 0.12f, 1.0f };
    toplevel->close_bg = wlr_scene_rect_create(toplevel->close_tree, btn_size, btn_size, bg);
    wlr_scene_node_set_position(&toplevel->close_bg->node, btn_x, btn_y);

    // Draw the “X” as tiny rectangles
    toplevel->close_x_tree = wlr_scene_tree_create(toplevel->close_tree);
    float ink[4] = { 0.25f, 0.18f, 0.05f, 1.0f };
    const int px = 2;
    const int x0 = btn_x + 4;
    const int y0 = btn_y + 4;

    for (int i = 0; i < 6; i++) {
        struct wlr_scene_rect *d1 = wlr_scene_rect_create(toplevel->close_x_tree, px, px, ink);
        wlr_scene_node_set_position(&d1->node, x0 + i * px, y0 + i * px);

        struct wlr_scene_rect *d2 = wlr_scene_rect_create(toplevel->close_x_tree, px, px, ink);
        wlr_scene_node_set_position(&d2->node, x0 + (5 - i) * px, y0 + i * px);
    }

    wlr_scene_node_raise_to_top(&toplevel->close_tree->node);
}

static void ade_tab_build_expand(struct tinywl_toplevel *toplevel, int tab_width) {
    if (toplevel == NULL || toplevel->tab_tree == NULL) return;

    ade_tab_clear_expand(toplevel);

    const int margin_right = 10;
    const int btn_size = 16;
    const int btn_y = ADE_TAB_CONTENT_Y_OFFSET;
    int btn_x = tab_width - margin_right - btn_size;
    if (btn_x < 0) btn_x = 0;

    toplevel->expand_tree = wlr_scene_tree_create(toplevel->tab_tree);

    float bg[4] = { 0.92f, 0.72f, 0.12f, 1.0f };
    toplevel->expand_bg = wlr_scene_rect_create(toplevel->expand_tree, btn_size, btn_size, bg);
    wlr_scene_node_set_position(&toplevel->expand_bg->node, btn_x, btn_y);

    toplevel->expand_plus_tree = wlr_scene_tree_create(toplevel->expand_tree);
    float ink[4] = { 0.25f, 0.18f, 0.05f, 1.0f };
    const int px = 2;

    const int cx = btn_x + 8;
    const int cy = btn_y + 8;

    for (int i = -2; i <= 2; i++) {
        struct wlr_scene_rect *d = wlr_scene_rect_create(toplevel->expand_plus_tree, px, px, ink);
        wlr_scene_node_set_position(&d->node, cx + i * px, cy);
    }
    for (int i = -2; i <= 2; i++) {
        struct wlr_scene_rect *d = wlr_scene_rect_create(toplevel->expand_plus_tree, px, px, ink);
        wlr_scene_node_set_position(&d->node, cx, cy + i * px);
    }

    wlr_scene_node_raise_to_top(&toplevel->expand_tree->node);
}

static void ade_tab_render_title(struct tinywl_toplevel *toplevel, const char *title) {
    if (toplevel == NULL || toplevel->tab_tree == NULL) {
        return;
    }

    // Remove previous text nodes
    ade_tab_clear_text(toplevel);

    char safe_title[128];
    ade_sanitize_title_ascii(title, safe_title, sizeof(safe_title));

    // Create a subtree for the text so we can destroy/recreate easily.
    toplevel->tab_text_tree = wlr_scene_tree_create(toplevel->tab_tree);

    // Render settings
    // Use a smaller pixel size; the soft halo makes this read well and feel more "font-like".
    const int px = 2;

    // Position inside the tab
    // Leave space for the close button on the left, plus a small gap.
    const int close_btn_w = 16;
    const int close_btn_pad_left = 10;
    const int close_btn_gap = 10;

    const int expand_btn_w = 16;
    const int expand_btn_pad_right = 10;
    const int expand_btn_gap = 10;

    const int left_pad = 14 + close_btn_pad_left + close_btn_w + close_btn_gap;
    const int right_pad = expand_btn_pad_right + expand_btn_w + expand_btn_gap;
    int origin_x = left_pad;
    int origin_y = ADE_TAB_CONTENT_Y_OFFSET + 1;

    // Text color: a darker brown-ish for contrast (RGB only here)
    const float ink_rgb[3] = { 0.25f, 0.18f, 0.05f };

    // Resize the yellow tab to fit the text (with clamps)
    int text_w = ade_title_measure_px(safe_title, px);
    int desired_w = left_pad + right_pad + text_w;
    const int min_w = 120;
    const int max_w = 420;
    if (desired_w < min_w) desired_w = min_w;
    if (desired_w > max_w) desired_w = max_w;

    toplevel->tab_width_px = desired_w;

    // Apply tab width
    if (toplevel->tab_rect != NULL) {
        wlr_scene_rect_set_size(toplevel->tab_rect, desired_w, ADE_TAB_HEIGHT); // tab height?
    }
    // If selected (gradient active), rebuild gradient to match new width
    if (toplevel->tab_grad_tree != NULL) {
        ade_tab_build_gradient(toplevel, desired_w);
    }
    
    
    // Keep tab edge detail lines aligned with the current tab size
    if (toplevel->tab_line_left) {
        wlr_scene_rect_set_size(toplevel->tab_line_left, 1, ADE_TAB_HEIGHT);
        wlr_scene_node_set_position(&toplevel->tab_line_left->node, 0, 0);
        wlr_scene_node_raise_to_top(&toplevel->tab_line_left->node);
    }
    if (toplevel->tab_line_right) {
        wlr_scene_rect_set_size(toplevel->tab_line_right, 1, ADE_TAB_HEIGHT);
        wlr_scene_node_set_position(&toplevel->tab_line_right->node, desired_w - 1, 0);
        wlr_scene_node_raise_to_top(&toplevel->tab_line_right->node);
    }
    if (toplevel->tab_line_top) {
        wlr_scene_rect_set_size(toplevel->tab_line_top, desired_w, 1);
        wlr_scene_node_set_position(&toplevel->tab_line_top->node, 0, 0);
        wlr_scene_node_raise_to_top(&toplevel->tab_line_top->node);
    }
     

    // Build/rebuild close button for this tab width
    ade_tab_build_close(toplevel, desired_w);
    // Build/rebuild expand button for this tab width
    ade_tab_build_expand(toplevel, desired_w);

    // Draw title using soft glyphs (pseudo-AA)
    const int max_chars = 24;
    for (int i = 0; safe_title[i] != '\0' && i < max_chars; i++) {
        char c = safe_title[i];
        int char_x = origin_x + i * ((5 + 1) * px);
        ade_tab_draw_glyph_soft(toplevel->tab_text_tree, c, char_x, origin_y, px, ink_rgb);
    }

    // Keep text and close button above the tab rectangle
    wlr_scene_node_raise_to_top(&toplevel->tab_text_tree->node);
    if (toplevel->close_tree != NULL) {
        wlr_scene_node_raise_to_top(&toplevel->close_tree->node);
    }
    if (toplevel->expand_tree != NULL) {
        wlr_scene_node_raise_to_top(&toplevel->expand_tree->node);
    }
}

// Set tab background color based on focus/selection state
static void ade_set_tab_selected(struct tinywl_toplevel *toplevel, bool selected) {
    if (toplevel == NULL || toplevel->tab_rect == NULL) return;

    static const float col_unselected[4] = { 0.85f, 0.85f, 0.85f, 1.0f };

    if (selected) {
        // Selected: gradient + transparent tab_rect
        const float transparent[4] = { 0, 0, 0, 0 };
        wlr_scene_rect_set_color(toplevel->tab_rect, transparent);

        int w = (toplevel->tab_width_px > 0) ? toplevel->tab_width_px : 160;
        ade_tab_build_gradient(toplevel, w);

        // Ensure content stays on top
        if (toplevel->tab_line_left)  wlr_scene_node_raise_to_top(&toplevel->tab_line_left->node);
        if (toplevel->tab_line_right) wlr_scene_node_raise_to_top(&toplevel->tab_line_right->node);
        if (toplevel->tab_line_top)   wlr_scene_node_raise_to_top(&toplevel->tab_line_top->node);
        if (toplevel->tab_text_tree)  wlr_scene_node_raise_to_top(&toplevel->tab_text_tree->node);
        if (toplevel->close_tree)     wlr_scene_node_raise_to_top(&toplevel->close_tree->node);
        if (toplevel->expand_tree)    wlr_scene_node_raise_to_top(&toplevel->expand_tree->node);
    } else {
        // Unselected: solid grey, no gradient
        ade_tab_clear_gradient(toplevel);
        wlr_scene_rect_set_color(toplevel->tab_rect, col_unselected);
    }
}


static void focus_toplevel(struct tinywl_toplevel *toplevel) {
    /* Note: this function only deals with keyboard focus. */
    if (toplevel == NULL) {
        return;
    }

    struct tinywl_server *server = toplevel->server;
    struct wlr_seat *seat = server->seat;
    struct wlr_surface *prev_surface = seat->keyboard_state.focused_surface;
    struct wlr_surface *surface = toplevel->xdg_toplevel->base->surface;

    /*
     * Important: Don't clear tab colors before we check for "already focused".
     * Otherwise the yellow tab can disappear when clicking inside the already
     * focused window.
     */
    if (prev_surface == surface) {
        /* Ensure the visual state stays correct even if focus didn't change. */
        struct tinywl_toplevel *it;
        wl_list_for_each(it, &server->toplevels, link) {
            ade_set_tab_selected(it, false);
        }
        ade_set_tab_selected(toplevel, true);
        return;
    }

    if (prev_surface) {
        /* Deactivate previously focused surface. */
        struct wlr_xdg_toplevel *prev_toplevel =
            wlr_xdg_toplevel_try_from_wlr_surface(prev_surface);
        if (prev_toplevel != NULL) {
            wlr_xdg_toplevel_set_activated(prev_toplevel, false);
        }
    }

    struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(seat);

    /* Move the toplevel to the front */
    wlr_scene_node_raise_to_top(&toplevel->scene_tree->node);
    wl_list_remove(&toplevel->link);
    wl_list_insert(&server->toplevels, &toplevel->link);

    /* Activate the new surface */
    wlr_xdg_toplevel_set_activated(toplevel->xdg_toplevel, true);

    /* Update tab colors: selected (focused) = yellow, others = light grey */
    struct tinywl_toplevel *it;
    wl_list_for_each(it, &server->toplevels, link) {
        ade_set_tab_selected(it, false);
    }
    ade_set_tab_selected(toplevel, true);

    /* Notify keyboard focus */
    if (keyboard != NULL) {
        wlr_seat_keyboard_notify_enter(seat, surface,
            keyboard->keycodes, keyboard->num_keycodes, &keyboard->modifiers);
    }
}


// -----------------------------------------------------------------------------
// Clean shutdown handling
// -----------------------------------------------------------------------------
static struct wl_display *ade_global_display = NULL;

static void ade_handle_signal(int sig) {
    wlr_log(WLR_ERROR, "ADE: received signal %d", sig);
    if (ade_global_display != NULL) {
        wl_display_terminate(ade_global_display);
    }
}


static void ade_backend_destroy_notify(struct wl_listener *listener, void *data) {
    (void)data;
    struct tinywl_server *server = wl_container_of(listener, server, backend_destroy);

    // IMPORTANT: remove our listener immediately so wlroots won't assert later
    wl_list_remove(&server->backend_destroy.link);

    server->backend_destroyed = true;
    server->backend = NULL;

    wlr_log(WLR_ERROR, "ADE: backend destroyed -> terminating display");
    if (server->wl_display) {
        wl_display_terminate(server->wl_display);
    }
}

static void keyboard_handle_modifiers(
        struct wl_listener *listener, void *data) {
    /* This event is raised when a modifier key, such as shift or alt, is
     * pressed. We simply communicate this to the client. */
    struct tinywl_keyboard *keyboard =
        wl_container_of(listener, keyboard, modifiers);
    /*
     * A seat can only have one keyboard, but this is a limitation of the
     * Wayland protocol - not wlroots. We assign all connected keyboards to the
     * same seat. You can swap out the underlying wlr_keyboard like this and
     * wlr_seat handles this transparently.
     */
    wlr_seat_set_keyboard(keyboard->server->seat, keyboard->wlr_keyboard);
    /* Send modifiers to the client. */
    wlr_seat_keyboard_notify_modifiers(keyboard->server->seat,
        &keyboard->wlr_keyboard->modifiers);
}

static bool handle_keybinding(struct tinywl_server *server, xkb_keysym_t sym) {
    /*
     * Here we handle compositor keybindings. This is when the compositor is
     * processing keys, rather than passing them on to the client for its own
     * processing.
     *
     * This function assumes Alt is held down.
     */
    switch (sym) {
    case XKB_KEY_Escape:
        wl_display_terminate(server->wl_display);
        break;
    case XKB_KEY_F1:
        /* Cycle to the next toplevel */
        if (wl_list_length(&server->toplevels) < 2) {
            break;
        }
        struct tinywl_toplevel *next_toplevel =
            wl_container_of(server->toplevels.prev, next_toplevel, link);
        focus_toplevel(next_toplevel);
        break;
    case XKB_KEY_h:
    case XKB_KEY_H:
        ade_show_help();
        break;
    default:
        return false;
    }
    return true;
}

static void keyboard_handle_key(
        struct wl_listener *listener, void *data) {
    /* This event is raised when a key is pressed or released. */
    struct tinywl_keyboard *keyboard =
        wl_container_of(listener, keyboard, key);
    struct tinywl_server *server = keyboard->server;
    struct wlr_keyboard_key_event *event = data;
    struct wlr_seat *seat = server->seat;

    /* Translate libinput keycode -> xkbcommon */
    uint32_t keycode = event->keycode + 8;
    /* Get a list of keysyms based on the keymap for this keyboard */
    const xkb_keysym_t *syms;
    int nsyms = xkb_state_key_get_syms(
            keyboard->wlr_keyboard->xkb_state, keycode, &syms);

    bool handled = false;
    uint32_t modifiers = wlr_keyboard_get_modifiers(keyboard->wlr_keyboard);
    if ((modifiers & WLR_MODIFIER_ALT) &&
            event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
        /* If alt is held down and this button was _pressed_, we attempt to
         * process it as a compositor keybinding. */
        for (int i = 0; i < nsyms; i++) {
            handled = handle_keybinding(server, syms[i]);
        }
    }

    // VM-friendly compositor shortcuts (no Ctrl/Alt needed)
    if (!handled && event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
        for (int i = 0; i < nsyms; i++) {
            switch (syms[i]) {
            case XKB_KEY_F12: // quit compositor
                wl_display_terminate(server->wl_display);
                handled = true;
                break;
            case XKB_KEY_F1: // terminal
                ade_spawn("foot");
                handled = true;
                break;
            case XKB_KEY_F2: { // launcher
                char *argv[] = { "fuzzel", NULL };
                ade_spawn_argv(argv);
                handled = true;
                break;
            }
            case XKB_KEY_F9: // help
                ade_show_help();
                handled = true;
                break;
            default:
                break;
            }
            if (handled) {
                break;
            }
        }
    }

    if (!handled) {
        /* Otherwise, we pass it along to the client. */
        wlr_seat_set_keyboard(seat, keyboard->wlr_keyboard);
        wlr_seat_keyboard_notify_key(seat, event->time_msec,
            event->keycode, event->state);
    }
}

static void keyboard_handle_destroy(struct wl_listener *listener, void *data) {
    /* This event is raised by the keyboard base wlr_input_device to signal
     * the destruction of the wlr_keyboard. It will no longer receive events
     * and should be destroyed.
     */
    struct tinywl_keyboard *keyboard =
        wl_container_of(listener, keyboard, destroy);
    wl_list_remove(&keyboard->modifiers.link);
    wl_list_remove(&keyboard->key.link);
    wl_list_remove(&keyboard->destroy.link);
    wl_list_remove(&keyboard->link);
    free(keyboard);
}

static void server_new_keyboard(struct tinywl_server *server,
        struct wlr_input_device *device) {
    struct wlr_keyboard *wlr_keyboard = wlr_keyboard_from_input_device(device);

    struct tinywl_keyboard *keyboard = calloc(1, sizeof(*keyboard));
    keyboard->server = server;
    keyboard->wlr_keyboard = wlr_keyboard;

    /* We need to prepare an XKB keymap and assign it to the keyboard. This
     * assumes the defaults (e.g. layout = "us"). */
    struct xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    struct xkb_keymap *keymap = xkb_keymap_new_from_names(context, NULL,
        XKB_KEYMAP_COMPILE_NO_FLAGS);

    wlr_keyboard_set_keymap(wlr_keyboard, keymap);
    xkb_keymap_unref(keymap);
    xkb_context_unref(context);
    wlr_keyboard_set_repeat_info(wlr_keyboard, 25, 600);

    /* Here we set up listeners for keyboard events. */
    keyboard->modifiers.notify = keyboard_handle_modifiers;
    wl_signal_add(&wlr_keyboard->events.modifiers, &keyboard->modifiers);
    keyboard->key.notify = keyboard_handle_key;
    wl_signal_add(&wlr_keyboard->events.key, &keyboard->key);
    keyboard->destroy.notify = keyboard_handle_destroy;
    wl_signal_add(&device->events.destroy, &keyboard->destroy);

    wlr_seat_set_keyboard(server->seat, keyboard->wlr_keyboard);

    /* And add the keyboard to our list of keyboards */
    wl_list_insert(&server->keyboards, &keyboard->link);
}

static void server_new_pointer(struct tinywl_server *server,
        struct wlr_input_device *device) {
    /* We don't do anything special with pointers. All of our pointer handling
     * is proxied through wlr_cursor. On another compositor, you might take this
     * opportunity to do libinput configuration on the device to set
     * acceleration, etc. */
    wlr_cursor_attach_input_device(server->cursor, device);
}

static void server_new_input(struct wl_listener *listener, void *data) {
    /* This event is raised by the backend when a new input device becomes
     * available. */
    struct tinywl_server *server =
        wl_container_of(listener, server, new_input);
    struct wlr_input_device *device = data;
    switch (device->type) {
    case WLR_INPUT_DEVICE_KEYBOARD:
        server_new_keyboard(server, device);
        break;
    case WLR_INPUT_DEVICE_POINTER:
        server_new_pointer(server, device);
        break;
    default:
        break;
    }
    /* We need to let the wlr_seat know what our capabilities are, which is
     * communiciated to the client. In TinyWL we always have a cursor, even if
     * there are no pointer devices, so we always include that capability. */
    uint32_t caps = WL_SEAT_CAPABILITY_POINTER;
    if (!wl_list_empty(&server->keyboards)) {
        caps |= WL_SEAT_CAPABILITY_KEYBOARD;
    }
    wlr_seat_set_capabilities(server->seat, caps);
}

static void seat_request_cursor(struct wl_listener *listener, void *data) {
    struct tinywl_server *server = wl_container_of(
            listener, server, request_cursor);
    /*
     * Some clients provide their own cursor surfaces. Under some backends/VMs
     * this can lead to inverted/offset cursors. For now, ignore client cursor
     * requests and always use the compositor's xcursor theme.
     */
    struct wlr_seat_pointer_request_set_cursor_event *event = data;
    struct wlr_seat_client *focused_client =
        server->seat->pointer_state.focused_client;

    if (focused_client == event->seat_client) {
        // Keep a stable compositor cursor under nested/VM backends
        wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "default");
    }
}

static void seat_pointer_focus_change(struct wl_listener *listener, void *data) {
    struct tinywl_server *server = wl_container_of(
            listener, server, pointer_focus_change);
    /* This event is raised when the pointer focus is changed, including when the
     * client is closed. We set the cursor image to its default if target surface
     * is NULL */
    struct wlr_seat_pointer_focus_change_event *event = data;
    if (event->new_surface == NULL) {
        wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "default");
    }
}

static void seat_request_set_selection(struct wl_listener *listener, void *data) {
    /* This event is raised by the seat when a client wants to set the selection,
     * usually when the user copies something. wlroots allows compositors to
     * ignore such requests if they so choose, but in tinywl we always honor
     */
    struct tinywl_server *server = wl_container_of(
            listener, server, request_set_selection);
    struct wlr_seat_request_set_selection_event *event = data;
    wlr_seat_set_selection(server->seat, event->source, event->serial);
}

static struct tinywl_toplevel *toplevel_from_scene_node(struct wlr_scene_node *node) {
    if (node == NULL) {
        return NULL;
    }
    // Walk up the scene tree until we hit the tinywl_toplevel root node (it has node.data set).
    struct wlr_scene_tree *tree = node->parent;
    while (tree != NULL && tree->node.data == NULL) {
        tree = tree->node.parent;
    }
    return tree ? (struct tinywl_toplevel *)tree->node.data : NULL;
}

static struct tinywl_toplevel *desktop_toplevel_at(
        struct tinywl_server *server, double lx, double ly,
        struct wlr_surface **surface, double *sx, double *sy) {
    /* This returns the topmost node in the scene at the given layout coords.
     * We only care about surface nodes as we are specifically looking for a
     * surface in the surface tree of a tinywl_toplevel. */
    struct wlr_scene_node *node = wlr_scene_node_at(
        &server->scene->tree.node, lx, ly, sx, sy);
    if (node == NULL || node->type != WLR_SCENE_NODE_BUFFER) {
        return NULL;
    }
    struct wlr_scene_buffer *scene_buffer = wlr_scene_buffer_from_node(node);
    struct wlr_scene_surface *scene_surface =
        wlr_scene_surface_try_from_buffer(scene_buffer);
    if (!scene_surface) {
        return NULL;
    }

    *surface = scene_surface->surface;
    /* Find the node corresponding to the tinywl_toplevel at the root of this
     * surface tree, it is the only one for which we set the data field. */
    struct wlr_scene_tree *tree = node->parent;
    while (tree != NULL && tree->node.data == NULL) {
        tree = tree->node.parent;
    }
    return tree->node.data;
}

static void begin_tab_drag(struct tinywl_toplevel *toplevel) {
    struct tinywl_server *server = toplevel->server;
    server->grabbed_toplevel = toplevel;
    server->cursor_mode = TINYWL_CURSOR_TABDRAG;

    // Grab relative to tab_tree position (tab_tree is in scene coords)
    server->grab_x = server->cursor->x - toplevel->tab_tree->node.x;
    server->grab_y = 0;
}

static void begin_interactive(struct tinywl_toplevel *toplevel,
        enum tinywl_cursor_mode mode, uint32_t edges);

static void reset_cursor_mode(struct tinywl_server *server) {
    /* Reset the cursor mode to passthrough. */
    server->cursor_mode = TINYWL_CURSOR_PASSTHROUGH;
    server->grabbed_toplevel = NULL;
    
    // Clear desktop icon drag state
    server->dragged_icon_node = NULL;
    server->icon_drag_candidate = false;
    server->icon_dragging = false;
    
    server->last_clicked_icon = NULL;
}

static void process_cursor_move(struct tinywl_server *server) {
    /* Move the grabbed toplevel to the new position. */
    struct tinywl_toplevel *toplevel = server->grabbed_toplevel;
    wlr_scene_node_set_position(&toplevel->scene_tree->node,
        server->cursor->x - server->grab_x,
        server->cursor->y - server->grab_y);
}

static void process_cursor_resize(struct tinywl_server *server) {
    /*
     * Resizing the grabbed toplevel can be a little bit complicated, because we
     * could be resizing from any corner or edge. This not only resizes the
     * toplevel on one or two axes, but can also move the toplevel if you resize
     * from the top or left edges (or top-left corner).
     *
     * Note that some shortcuts are taken here. In a more fleshed-out
     * compositor, you'd wait for the client to prepare a buffer at the new
     * size, then commit any movement that was prepared.
     */
    struct tinywl_toplevel *toplevel = server->grabbed_toplevel;
    double border_x = server->cursor->x - server->grab_x;
    double border_y = server->cursor->y - server->grab_y;
    int new_left = server->grab_geobox.x;
    int new_right = server->grab_geobox.x + server->grab_geobox.width;
    int new_top = server->grab_geobox.y;
    int new_bottom = server->grab_geobox.y + server->grab_geobox.height;

    if (server->resize_edges & WLR_EDGE_TOP) {
        new_top = border_y;
        if (new_top >= new_bottom) {
            new_top = new_bottom - 1;
        }
    } else if (server->resize_edges & WLR_EDGE_BOTTOM) {
        new_bottom = border_y;
        if (new_bottom <= new_top) {
            new_bottom = new_top + 1;
        }
    }
    if (server->resize_edges & WLR_EDGE_LEFT) {
        new_left = border_x;
        if (new_left >= new_right) {
            new_left = new_right - 1;
        }
    } else if (server->resize_edges & WLR_EDGE_RIGHT) {
        new_right = border_x;
        if (new_right <= new_left) {
            new_right = new_left + 1;
        }
    }

    struct wlr_box *geo_box = &toplevel->xdg_toplevel->base->geometry;
    wlr_scene_node_set_position(&toplevel->scene_tree->node,
        new_left - geo_box->x, new_top - geo_box->y);

    int new_width = new_right - new_left;
    int new_height = new_bottom - new_top;
    wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel, new_width, new_height);
}

static void process_cursor_motion(struct tinywl_server *server, uint32_t time) {
    // If the user pressed on an icon, begin dragging after a small motion threshold
    if (server->icon_drag_candidate && !server->icon_dragging && server->dragged_icon_node != NULL) {
        const double thresh = 5.0;
        double dx = server->cursor->x - server->icon_press_x;
        double dy = server->cursor->y - server->icon_press_y;
        if (dx * dx + dy * dy >= thresh * thresh) {
            server->icon_dragging = true;
            server->cursor_mode = TINYWL_CURSOR_ICONDRAG;
        }
    }
    
    /* If the mode is non-passthrough, delegate to those functions. */
    if (server->cursor_mode == TINYWL_CURSOR_MOVE) {
        process_cursor_move(server);
        return;
    } else if (server->cursor_mode == TINYWL_CURSOR_RESIZE) {
        process_cursor_resize(server);
        return;
    } else if (server->cursor_mode == TINYWL_CURSOR_TABDRAG) {
        struct tinywl_toplevel *toplevel = server->grabbed_toplevel;
        if (toplevel && toplevel->tab_tree && toplevel->xdg_toplevel) {
            int desired_x = (int)(server->cursor->x - server->grab_x);

            // Clamp within the window width (xdg geometry width)
            struct wlr_box geo = toplevel->xdg_toplevel->base->geometry;
            int win_w = geo.width;
            if (win_w <= 0) win_w = 800;

            int tab_w = (toplevel->tab_width_px > 0) ? toplevel->tab_width_px : 160;

            int min_x = -ADE_LEFT_RESIZE_GRIP_W;
            int max_x = win_w - tab_w + ADE_LEFT_RESIZE_GRIP_W;
            if (max_x < min_x) max_x = min_x;

            if (desired_x < min_x) desired_x = min_x;
            if (desired_x > max_x) desired_x = max_x;

            toplevel->tab_x_px = desired_x;
            wlr_scene_node_set_position(&toplevel->tab_tree->node, toplevel->tab_x_px, ADE_TAB_Y);
        }
        return;
    }
    else if (server->cursor_mode == TINYWL_CURSOR_ICONDRAG) {
        if (server->dragged_icon_node != NULL) {
            int nx = (int)(server->cursor->x - server->icon_grab_dx);
            int ny = (int)(server->cursor->y - server->icon_grab_dy);
            wlr_scene_node_set_position(server->dragged_icon_node, nx, ny);
        }
        return;
    }

    /* Otherwise, find the toplevel under the pointer and send the event along. */
    double sx, sy;
    struct wlr_seat *seat = server->seat;
    struct wlr_surface *surface = NULL;
    struct tinywl_toplevel *toplevel = desktop_toplevel_at(server,
            server->cursor->x, server->cursor->y, &surface, &sx, &sy);
    if (!toplevel) {
        /* If there's no toplevel under the cursor, set the cursor image to a
         * default. This is what makes the cursor image appear when you move it
         * around the screen, not over any toplevels. */
        wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "default");
    }
    if (surface) {
        /*
         * Send pointer enter and motion events.
         *
         * The enter event gives the surface "pointer focus", which is distinct
         * from keyboard focus. You get pointer focus by moving the pointer over
         * a window.
         *
         * Note that wlroots will avoid sending duplicate enter/motion events if
         * the surface has already has pointer focus or if the client is already
         * aware of the coordinates passed.
         */
        wlr_seat_pointer_notify_enter(seat, surface, sx, sy);
        wlr_seat_pointer_notify_motion(seat, time, sx, sy);
    } else {
        /* Clear pointer focus so future button events and such are not sent to
         * the last client to have the cursor over it. */
        wlr_seat_pointer_clear_focus(seat);
    }
}

static void server_cursor_motion(struct wl_listener *listener, void *data) {
    /* This event is forwarded by the cursor when a pointer emits a _relative_
     * pointer motion event (i.e. a delta) */
    struct tinywl_server *server =
        wl_container_of(listener, server, cursor_motion);
    struct wlr_pointer_motion_event *event = data;
    /* The cursor doesn't move unless we tell it to. The cursor automatically
     * handles constraining the motion to the output layout, as well as any
     * special configuration applied for the specific input device which
     * generated the event. You can pass NULL for the device if you want to move
     * the cursor around without any input. */
    wlr_cursor_move(server->cursor, &event->pointer->base,
            event->delta_x, event->delta_y);
    process_cursor_motion(server, event->time_msec);
}

static void server_cursor_motion_absolute(
        struct wl_listener *listener, void *data) {
    /* This event is forwarded by the cursor when a pointer emits an _absolute_
     * motion event, from 0..1 on each axis. This happens, for example, when
     * wlroots is running under a Wayland window rather than KMS+DRM, and you
     * move the mouse over the window. You could enter the window from any edge,
     * so we have to warp the mouse there. There is also some hardware which
     * emits these events. */
    struct tinywl_server *server =
        wl_container_of(listener, server, cursor_motion_absolute);
    struct wlr_pointer_motion_absolute_event *event = data;
    wlr_cursor_warp_absolute(server->cursor, &event->pointer->base, event->x,
        event->y);
    process_cursor_motion(server, event->time_msec);
}

// -----------------------------------------------------------------------------
// Desktop icons (config-driven)
// Config format (one per line):
//   x y icon_png_path | command to run
// Example:
//   40 40 icons/64x64/apps/terminal.png | foot
// Lines starting with # are comments.
// -----------------------------------------------------------------------------

static char *ade_trim(char *s) {
    while (*s && isspace((unsigned char)*s)) s++;
    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1])) end--;
    *end = '\0';
    return s;
}

// -----------------------------------------------------------------------------
// Tiny 5x7 bitmap font for desktop icon labels (rect-based, no font deps)
// -----------------------------------------------------------------------------

static const uint8_t *ade_glyph_5x7(char c) {
    // Each glyph is 7 rows, 5 bits used per row (MSB on the left in the low 5 bits)
    // Only a small subset is implemented; unknown chars map to '?'.
    static const uint8_t GLYPH_SPACE[7] = {0,0,0,0,0,0,0};
    static const uint8_t GLYPH_QMARK[7] = {0x0E,0x11,0x01,0x02,0x04,0x00,0x04};

    // Digits 0-9
    static const uint8_t GLYPH_0[7] = {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E};
    static const uint8_t GLYPH_1[7] = {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E};
    static const uint8_t GLYPH_2[7] = {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F};
    static const uint8_t GLYPH_3[7] = {0x1F,0x02,0x04,0x02,0x01,0x11,0x0E};
    static const uint8_t GLYPH_4[7] = {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02};
    static const uint8_t GLYPH_5[7] = {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E};
    static const uint8_t GLYPH_6[7] = {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E};
    static const uint8_t GLYPH_7[7] = {0x1F,0x01,0x02,0x04,0x08,0x08,0x08};
    static const uint8_t GLYPH_8[7] = {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E};
    static const uint8_t GLYPH_9[7] = {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C};

    // Uppercase A-Z (subset that covers most app names well)
    static const uint8_t GLYPH_A[7] = {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11};
    static const uint8_t GLYPH_B[7] = {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E};
    static const uint8_t GLYPH_C[7] = {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E};
    static const uint8_t GLYPH_D[7] = {0x1E,0x11,0x11,0x11,0x11,0x11,0x1E};
    static const uint8_t GLYPH_E[7] = {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F};
    static const uint8_t GLYPH_F[7] = {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10};
    static const uint8_t GLYPH_G[7] = {0x0E,0x11,0x10,0x17,0x11,0x11,0x0F};
    static const uint8_t GLYPH_H[7] = {0x11,0x11,0x11,0x1F,0x11,0x11,0x11};
    static const uint8_t GLYPH_I[7] = {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E};
    static const uint8_t GLYPH_J[7] = {0x07,0x02,0x02,0x02,0x02,0x12,0x0C};
    static const uint8_t GLYPH_K[7] = {0x11,0x12,0x14,0x18,0x14,0x12,0x11};
    static const uint8_t GLYPH_L[7] = {0x10,0x10,0x10,0x10,0x10,0x10,0x1F};
    static const uint8_t GLYPH_M[7] = {0x11,0x1B,0x15,0x15,0x11,0x11,0x11};
    static const uint8_t GLYPH_N[7] = {0x11,0x19,0x15,0x13,0x11,0x11,0x11};
    static const uint8_t GLYPH_O[7] = {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E};
    static const uint8_t GLYPH_P[7] = {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10};
    static const uint8_t GLYPH_Q[7] = {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D};
    static const uint8_t GLYPH_R[7] = {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11};
    static const uint8_t GLYPH_S[7] = {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E};
    static const uint8_t GLYPH_T[7] = {0x1F,0x04,0x04,0x04,0x04,0x04,0x04};
    static const uint8_t GLYPH_U[7] = {0x11,0x11,0x11,0x11,0x11,0x11,0x0E};
    static const uint8_t GLYPH_V[7] = {0x11,0x11,0x11,0x11,0x11,0x0A,0x04};
    static const uint8_t GLYPH_W[7] = {0x11,0x11,0x11,0x15,0x15,0x1B,0x11};
    static const uint8_t GLYPH_X[7] = {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11};
    static const uint8_t GLYPH_Y[7] = {0x11,0x11,0x0A,0x04,0x04,0x04,0x04};
    static const uint8_t GLYPH_Z[7] = {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F};

    static const uint8_t GLYPH_DOT[7]  = {0,0,0,0,0,0,0x04};
    static const uint8_t GLYPH_DASH[7] = {0,0,0,0x1F,0,0,0};
    static const uint8_t GLYPH_UND[7]  = {0,0,0,0,0,0,0x1F};

    if (c == ' ') return GLYPH_SPACE;
    if (c == '.') return GLYPH_DOT;
    if (c == '-') return GLYPH_DASH;
    if (c == '_') return GLYPH_UND;

    if (c >= 'a' && c <= 'z') c = (char)(c - 32); // map to uppercase glyphs

    switch (c) {
        case '0': return GLYPH_0;
        case '1': return GLYPH_1;
        case '2': return GLYPH_2;
        case '3': return GLYPH_3;
        case '4': return GLYPH_4;
        case '5': return GLYPH_5;
        case '6': return GLYPH_6;
        case '7': return GLYPH_7;
        case '8': return GLYPH_8;
        case '9': return GLYPH_9;
        case 'A': return GLYPH_A;
        case 'B': return GLYPH_B;
        case 'C': return GLYPH_C;
        case 'D': return GLYPH_D;
        case 'E': return GLYPH_E;
        case 'F': return GLYPH_F;
        case 'G': return GLYPH_G;
        case 'H': return GLYPH_H;
        case 'I': return GLYPH_I;
        case 'J': return GLYPH_J;
        case 'K': return GLYPH_K;
        case 'L': return GLYPH_L;
        case 'M': return GLYPH_M;
        case 'N': return GLYPH_N;
        case 'O': return GLYPH_O;
        case 'P': return GLYPH_P;
        case 'Q': return GLYPH_Q;
        case 'R': return GLYPH_R;
        case 'S': return GLYPH_S;
        case 'T': return GLYPH_T;
        case 'U': return GLYPH_U;
        case 'V': return GLYPH_V;
        case 'W': return GLYPH_W;
        case 'X': return GLYPH_X;
        case 'Y': return GLYPH_Y;
        case 'Z': return GLYPH_Z;
        default:  return GLYPH_QMARK;
    }
}

static int ade_text5x7_width_px(const char *text, int scale, int spacing) {
    if (!text) return 0;
    int len = (int)strlen(text);
    if (len <= 0) return 0;
    int glyph_w = 5 * scale;
    int step = glyph_w + spacing;
    return (len * step) - spacing;
}

static void ade_scene_draw_text5x7(struct wlr_scene_tree *parent, int x, int y,
                                  int scale, int spacing,
                                  const float color[4], const char *text) {
    if (!parent || !text) return;
    int pen_x = x;
    for (const char *p = text; *p; p++) {
        const uint8_t *g = ade_glyph_5x7(*p);
        for (int row = 0; row < 7; row++) {
            uint8_t bits = g[row] & 0x1F;
            for (int col = 0; col < 5; col++) {
                if (bits & (1u << (4 - col))) {
                    struct wlr_scene_rect *px = wlr_scene_rect_create(parent, scale, scale, color);
                    if (px) {
                        wlr_scene_node_set_position(&px->node, pen_x + col * scale, y + row * scale);
                        // IMPORTANT: do not set node.data here; clicks should resolve to the icon's tree data.
                    }
                }
            }
        }
        pen_x += (5 * scale) + spacing;
    }
}

// Pseudo anti-aliased text: draw a faint 2x2 "ink" behind each 1x1 pixel.
// This softens jaggies without adding any new deps.
static void ade_scene_draw_text5x7_aa(struct wlr_scene_tree *parent, int x, int y,
                                     int scale, int spacing,
                                     const float color[4], const char *text) {
    if (!parent || !text) return;

    // A soft halo behind the main pixel
    float halo[4] = { color[0], color[1], color[2], color[3] * 0.35f };

    int pen_x = x;
    for (const char *p = text; *p; p++) {
        const uint8_t *g = ade_glyph_5x7(*p);
        for (int row = 0; row < 7; row++) {
            uint8_t bits = g[row] & 0x1F;
            for (int col = 0; col < 5; col++) {
                if (bits & (1u << (4 - col))) {
                    int px_x = pen_x + col * scale;
                    int px_y = y + row * scale;

                    // Halo pass (slightly larger)
                    struct wlr_scene_rect *h = wlr_scene_rect_create(parent, scale + 1, scale + 1, halo);
                    if (h) {
                        wlr_scene_node_set_position(&h->node, px_x, px_y);
                    }

                    // Main pixel pass
                    struct wlr_scene_rect *m = wlr_scene_rect_create(parent, scale, scale, color);
                    if (m) {
                        wlr_scene_node_set_position(&m->node, px_x, px_y);
                    }
                }
            }
        }
        pen_x += (5 * scale) + spacing;
    }
}

static struct ade_desktop_icon *ade_desktop_icon_from_scene_node(struct wlr_scene_node *node) {
    struct wlr_scene_node *n = node;
    while (n != NULL) {
        if (n->data != NULL) {
            return (struct ade_desktop_icon *)n->data;
        }
        n = n->parent ? &n->parent->node : NULL;
    }
    return NULL;
}

static void ade_free_desktop_icon(struct ade_desktop_icon *ic) {
    if (!ic) return;
    free(ic->exec_cmd);
    free(ic);
}

static void ade_load_desktop_icons_from_conf(struct tinywl_server *server, const char *conf_path) {
    if (server == NULL || server->desktop_icons == NULL || conf_path == NULL) {
        return;
    }

    FILE *f = fopen(conf_path, "r");
    if (!f) {
        wlr_log(WLR_ERROR, "ADE: could not open desktop icons config: %s", conf_path);
        return;
    }

    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        char *p = ade_trim(line);
        if (*p == '\0' || *p == '#') continue;

        char *bar = strchr(p, '|');
        if (!bar) continue;
        *bar = '\0';

        char *left = ade_trim(p);
        char *right = ade_trim(bar + 1);
        if (*right == '\0') continue;

        int x = 0, y = 0;
        char png[512];
        char label_raw[256];
        png[0] = '\0';
        label_raw[0] = '\0';

        // Allow optional label after the png path:
        //   x y path/to/icon.png My Label Here
        // (label continues until the '|' split; we already split that out)
        int n = sscanf(left, "%d %d %511s %255[^\n]", &x, &y, png, label_raw);
        if (n < 3) continue;

        char *label = NULL;
        if (n >= 4) {
            label = ade_trim(label_raw);
            // strip surrounding quotes if present
            if (label[0] == '"') {
                label++;
                size_t L = strlen(label);
                if (L > 0 && label[L - 1] == '"') {
                    label[L - 1] = '\0';
                }
                label = ade_trim(label);
            }
        }
        if (label == NULL || *label == '\0') {
            label = "APP"; // default so we can prove labels render even if config label missing
        }

        struct ade_desktop_icon *ic = calloc(1, sizeof(*ic));
        if (!ic) continue;
        ic->exec_cmd = strdup(right);

        // Create a per-icon tree so icon + label move together and clicks on label still hit the icon.
        struct wlr_scene_tree *icon_tree = wlr_scene_tree_create(server->desktop_icons);
        if (!icon_tree) {
            ade_free_desktop_icon(ic);
            continue;
        }
        wlr_scene_node_set_position(&icon_tree->node, x, y);
        icon_tree->node.data = ic;        // hit-testing anchor
        ic->node = &icon_tree->node;      // drag this tree

        // Try PNG first (placed at 0,0 inside icon_tree)
        bool placed = false;
        int icon_box_w = 48;
        int icon_box_h = 48;

        struct wlr_buffer *icon_buf = ade_load_png_as_wlr_buffer(png);
        if (icon_buf != NULL) {
            // Use the actual buffer size for centering the label
            icon_box_w = (int)icon_buf->width;
            icon_box_h = (int)icon_buf->height;

            struct wlr_scene_buffer *sb = wlr_scene_buffer_create(icon_tree, icon_buf);
            wlr_buffer_drop(icon_buf);
            if (sb != NULL) {
                wlr_scene_node_set_position(&sb->node, 0, 0);
                placed = true;
            }
        }

        // Fallback: placeholder rect
        if (!placed) {
            const float col[4] = { 0.80f, 0.80f, 0.80f, 1.0f };
            struct wlr_scene_rect *r = wlr_scene_rect_create(icon_tree, icon_box_w, icon_box_h, col);
            if (r != NULL) {
                wlr_scene_node_set_position(&r->node, 0, 0);
                placed = true;
            }
        }

        // If we still couldn't place anything, clean up
        if (!placed) {
            wlr_scene_node_destroy(&icon_tree->node);
            ade_free_desktop_icon(ic);
            continue;
        }

        // Bitmap label under the icon (smaller, centered, slightly lower, pseudo-AA)
        const int label_gap = 8;                 // space below the icon
        const int label_y = icon_box_h + label_gap;
        const int scale = 1;                     // smaller font
        const int spacing = 1;

        // Limit label length to avoid huge blocks of pixels
        char label_buf[64];
        snprintf(label_buf, sizeof(label_buf), "%s", label);
        if (strlen(label_buf) > 14) {
            label_buf[14] = '\0';
        }

        int tw = ade_text5x7_width_px(label_buf, scale, spacing);
        int tx = (icon_box_w / 2) - (tw / 2);

        // allow slight negative for perfect centering (but clamp to keep on-icon)
        if (tx < -8) tx = -8;

        // BeOS-ish: white text with a subtle dark shadow behind it
        const float shadow[4]  = { 0.0f, 0.0f, 0.0f, 0.55f };
        const float textcol[4] = { 0.98f, 0.98f, 0.98f, 1.0f };

        // Shadow first
        ade_scene_draw_text5x7_aa(icon_tree, tx + 1, label_y + 1, scale, spacing, shadow, label_buf);
        // Main text
        ade_scene_draw_text5x7_aa(icon_tree, tx,     label_y,     scale, spacing, textcol, label_buf);

        // keep icons above background
        wlr_scene_node_raise_to_top(&icon_tree->node);
        continue;
    }

    fclose(f);
}

static void server_cursor_button(struct wl_listener *listener, void *data) {
    /* This event is forwarded by the cursor when a pointer emits a button
     * event. */
    struct tinywl_server *server =
        wl_container_of(listener, server, cursor_button);
    struct wlr_pointer_button_event *event = data;
    // If you released any buttons, we may exit interactive modes.
    if (event->state == WL_POINTER_BUTTON_STATE_RELEASED) {
        // Desktop icon: drop on release; if it was a click (not a drag), allow double-click launch.
        if (event->button == BTN_LEFT &&
            (server->cursor_mode == TINYWL_CURSOR_ICONDRAG || server->icon_drag_candidate)) {

            bool dragged = server->icon_dragging;

            if (!dragged) {
                const uint32_t dbl_ms = 400;

                if (server->last_icon_click_msec != 0 &&
                    (event->time_msec - server->last_icon_click_msec) <= dbl_ms) {
                    server->icon_click_count++;
                } else {
                    server->icon_click_count = 1;
                }

                server->last_icon_click_msec = event->time_msec;

                if (server->icon_click_count >= 2) {
                    server->icon_click_count = 0;
                    server->last_icon_click_msec = 0;
                    //ade_launch_terminal();
                    if (server->last_clicked_icon != NULL && server->last_clicked_icon->exec_cmd != NULL) {
                        ade_spawn(server->last_clicked_icon->exec_cmd);
                    }
                }
            }

            reset_cursor_mode(server);
            return; // Do not forward icon clicks/drags to clients
        }

        reset_cursor_mode(server);
        // Still notify release to the client which had pointer focus.
        wlr_seat_pointer_notify_button(server->seat,
                event->time_msec, event->button, event->state);
        return;
    }

    // Button pressed: first check if we clicked on a compositor decoration (the yellow tab).
    double sx = 0, sy = 0;
    struct wlr_scene_node *node = wlr_scene_node_at(
        &server->scene->tree.node, server->cursor->x, server->cursor->y, &sx, &sy);

    
    /*
    // Desktop icon handling (arm drag on press)
    if (event->button == BTN_LEFT &&
        event->state == WL_POINTER_BUTTON_STATE_PRESSED &&
        node != NULL) {

        bool hit_terminal_icon = false;

        if (server->terminal_icon_scene != NULL) {
            hit_terminal_icon = ade_node_is_or_ancestor(node, &server->terminal_icon_scene->node);
        }
        if (!hit_terminal_icon && server->terminal_icon_rect != NULL) {
            hit_terminal_icon = ade_node_is_or_ancestor(node, &server->terminal_icon_rect->node);
        }

        if (hit_terminal_icon) {
            // Arm icon drag; we only start dragging after a small motion threshold.
            server->icon_drag_candidate = true;
            server->icon_dragging = false;
            server->icon_press_x = server->cursor->x;
            server->icon_press_y = server->cursor->y;

            if (server->terminal_icon_scene != NULL) {
                server->dragged_icon_node = &server->terminal_icon_scene->node;
            } else if (server->terminal_icon_rect != NULL) {
                server->dragged_icon_node = &server->terminal_icon_rect->node;
            } else {
                server->dragged_icon_node = NULL;
            }

            if (server->dragged_icon_node != NULL) {
                server->icon_grab_dx = server->cursor->x - server->dragged_icon_node->x;
                server->icon_grab_dy = server->cursor->y - server->dragged_icon_node->y;
            } else {
                server->icon_grab_dx = 0;
                server->icon_grab_dy = 0;
            }

            // Don't forward icon presses to clients
            return;
        }
    }
     */
    
    
    // Desktop icon handling (arm drag on press)
    // IMPORTANT: Only treat nodes as desktop icons if they are within the desktop icon scene tree.
    // Otherwise we may misinterpret window scene nodes (which also use node.data) as icons.
    if (event->button == BTN_LEFT &&
        event->state == WL_POINTER_BUTTON_STATE_PRESSED &&
        node != NULL &&
        server->desktop_icons != NULL &&
        ade_node_is_or_ancestor(node, &server->desktop_icons->node)) {

        struct ade_desktop_icon *hit_icon = ade_desktop_icon_from_scene_node(node);
        if (hit_icon != NULL && hit_icon->node != NULL) {
            server->icon_drag_candidate = true;
            server->icon_dragging = false;
            server->icon_press_x = server->cursor->x;
            server->icon_press_y = server->cursor->y;

            server->dragged_icon_node = hit_icon->node;
            server->last_clicked_icon = hit_icon;

            server->icon_grab_dx = server->cursor->x - server->dragged_icon_node->x;
            server->icon_grab_dy = server->cursor->y - server->dragged_icon_node->y;

            return; // don't forward to clients
        }
    }
    
    
    struct tinywl_toplevel *clicked_toplevel = NULL;
    if (node != NULL) {
        clicked_toplevel = toplevel_from_scene_node(node);
    }
    
    // Left resize grip: click-drag to resize from the left edge
    if (clicked_toplevel != NULL && event->button == BTN_LEFT) {
        if (clicked_toplevel->left_resize_grip != NULL &&
            node == &clicked_toplevel->left_resize_grip->node) {
            begin_interactive(clicked_toplevel, TINYWL_CURSOR_RESIZE, WLR_EDGE_LEFT);
            return;
        }
    }
    
    
    // Top resize grip: click-drag to resize from the top edge
    if (clicked_toplevel != NULL && event->button == BTN_LEFT) {
        if (clicked_toplevel->top_resize_grip != NULL &&
            node == &clicked_toplevel->top_resize_grip->node) {
            begin_interactive(clicked_toplevel, TINYWL_CURSOR_RESIZE, WLR_EDGE_TOP);
            return;
        }
    }
    
    // Right resize grip: click-drag to resize from the right edge
    if (clicked_toplevel != NULL && event->button == BTN_LEFT) {
        if (clicked_toplevel->right_resize_grip != NULL &&
            node == &clicked_toplevel->right_resize_grip->node) {
            begin_interactive(clicked_toplevel, TINYWL_CURSOR_RESIZE, WLR_EDGE_RIGHT);
            return;
        }
    }

    // Bottom resize grip: click-drag to resize from the bottom edge
    if (clicked_toplevel != NULL && event->button == BTN_LEFT) {
        if (clicked_toplevel->bottom_resize_grip != NULL &&
            node == &clicked_toplevel->bottom_resize_grip->node) {
            begin_interactive(clicked_toplevel, TINYWL_CURSOR_RESIZE, WLR_EDGE_BOTTOM);
            return;
        }
    }
    
    // Bottom-left corner resize grip: click-drag to resize bottom + left
    if (clicked_toplevel != NULL && event->button == BTN_LEFT) {
        if (clicked_toplevel->corner_bl_resize_grip != NULL &&
            node == &clicked_toplevel->corner_bl_resize_grip->node) {
            begin_interactive(clicked_toplevel, TINYWL_CURSOR_RESIZE, WLR_EDGE_BOTTOM | WLR_EDGE_LEFT);
            return;
        }
    }
    
    // Bottom-right corner resize grip: click-drag to resize right + bottom
    if (clicked_toplevel != NULL && event->button == BTN_LEFT) {
        if (clicked_toplevel->corner_rl_resize_grip != NULL &&
            node == &clicked_toplevel->corner_rl_resize_grip->node) {
            begin_interactive(clicked_toplevel, TINYWL_CURSOR_RESIZE,
                WLR_EDGE_RIGHT | WLR_EDGE_BOTTOM);
            return;
        }
    }
    
    // Top-right corner resize grip: click-drag to resize top + right
    if (clicked_toplevel != NULL && event->button == BTN_LEFT) {
        if (clicked_toplevel->corner_ru_resize_grip != NULL &&
            node == &clicked_toplevel->corner_ru_resize_grip->node) {
            begin_interactive(clicked_toplevel, TINYWL_CURSOR_RESIZE, WLR_EDGE_TOP | WLR_EDGE_RIGHT);
            return;
        }
    }
    
    // Top-left corner resize grip: click-drag to resize top + left
    if (clicked_toplevel != NULL && event->button == BTN_LEFT) {
        if (clicked_toplevel->corner_lu_resize_grip != NULL &&
            node == &clicked_toplevel->corner_lu_resize_grip->node) {
            begin_interactive(clicked_toplevel, TINYWL_CURSOR_RESIZE, WLR_EDGE_TOP | WLR_EDGE_LEFT);
            return;
        }
    }
    

    if (clicked_toplevel != NULL && event->button == BTN_LEFT) {
        // Close button: click to request the client close.
        if (clicked_toplevel->close_bg != NULL &&
            node == &clicked_toplevel->close_bg->node) {
            wlr_xdg_toplevel_send_close(clicked_toplevel->xdg_toplevel);
            return; // Do not send this click to clients
        }

        // Treat ANY click on the tab (background, text, or decorations) as a tab click
        bool hit_tab = false;
        if (clicked_toplevel->tab_tree != NULL) {
            hit_tab = ade_node_is_or_ancestor(
                node, &clicked_toplevel->tab_tree->node);
        }

        if (hit_tab) {
            focus_toplevel(clicked_toplevel);

            bool shift_down = false;
            struct wlr_keyboard *kb = wlr_seat_get_keyboard(server->seat);
            if (kb != NULL) {
                uint32_t mods = wlr_keyboard_get_modifiers(kb);
                shift_down = (mods & WLR_MODIFIER_SHIFT) != 0;
            }

            if (shift_down) {
                begin_tab_drag(clicked_toplevel);
            } else {
                begin_interactive(clicked_toplevel, TINYWL_CURSOR_MOVE, 0);
            }
            return;
        }
    }

    // Otherwise, behave like tinywl: notify the client and focus the toplevel under the cursor.
    wlr_seat_pointer_notify_button(server->seat,
            event->time_msec, event->button, event->state);

    {
        double csx, csy;
        struct wlr_surface *surface = NULL;
        struct tinywl_toplevel *toplevel = desktop_toplevel_at(server,
                server->cursor->x, server->cursor->y, &surface, &csx, &csy);
        focus_toplevel(toplevel);
    }
}

static void server_cursor_axis(struct wl_listener *listener, void *data) {
    /* This event is forwarded by the cursor when a pointer emits an axis event,
     * for example when you move the scroll wheel. */
    struct tinywl_server *server =
        wl_container_of(listener, server, cursor_axis);
    struct wlr_pointer_axis_event *event = data;
    /* Notify the client with pointer focus of the axis event. */
    wlr_seat_pointer_notify_axis(server->seat,
            event->time_msec, event->orientation, event->delta,
            event->delta_discrete, event->source, event->relative_direction);
}

static void server_cursor_frame(struct wl_listener *listener, void *data) {
    /* This event is forwarded by the cursor when a pointer emits an frame
     * event. Frame events are sent after regular pointer events to group
     * multiple events together. For instance, two axis events may happen at the
     * same time, in which case a frame event won't be sent in between. */
    struct tinywl_server *server =
        wl_container_of(listener, server, cursor_frame);
    /* Notify the client with pointer focus of the frame event. */
    wlr_seat_pointer_notify_frame(server->seat);
}

static void output_frame(struct wl_listener *listener, void *data) {
    /* This function is called every time an output is ready to display a frame,
     * generally at the output's refresh rate (e.g. 60Hz). */
    struct tinywl_output *output = wl_container_of(listener, output, frame);
    struct wlr_scene_output *scene_output = output->scene_output;
    if (scene_output == NULL) {
        wlr_log(WLR_ERROR, "ADE: output_frame called but scene_output is NULL");
        return;
    }

    /* Render the scene if needed and commit the output */
    wlr_scene_output_commit(scene_output, NULL);

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    wlr_scene_output_send_frame_done(scene_output, &now);
}


// Forward declarations (defined later in the file)
static void ade_deskbar_init(struct tinywl_server *server);
static void ade_deskbar_update_layout(struct tinywl_server *server);

static void output_request_state(struct wl_listener *listener, void *data) {
    // Apply the backend-requested output state as-is. For nested/VM backends
    // (Wayland/X11), forcing a transform here can result in an inverted output
    // and mismatched pointer coordinates.
    struct tinywl_output *output = wl_container_of(listener, output, request_state);
    const struct wlr_output_event_request_state *event = data;

    struct wlr_output_state state;
    wlr_output_state_init(&state);
    wlr_output_state_copy(&state, event->state);

    // Commit the state requested by the backend. Forcing an output transform here can
    // invert the output and break pointer coordinates under nested/VM backends.
    wlr_output_commit_state(output->wlr_output, &state);

    wlr_output_state_finish(&state);

    // Output size/scale can change under Wayland/X11 backends; keep background and cursor in sync.
    ade_update_background(output->server);
    ade_reload_cursor_for_output(output->server, output->wlr_output);
    
    ade_deskbar_update_layout(output->server);
}

static bool ade_get_primary_output_size(struct tinywl_server *server, int *out_w, int *out_h) {
    if (out_w) *out_w = 0;
    if (out_h) *out_h = 0;

    struct tinywl_output *o = NULL;
    wl_list_for_each(o, &server->outputs, link) {
        int w = 0, h = 0;
        wlr_output_effective_resolution(o->wlr_output, &w, &h);
        if (w > 0 && h > 0) {
            if (out_w) *out_w = w;
            if (out_h) *out_h = h;
            return true;
        }
    }
    return false;
}


// -----------------------------------------------------------------------------
// Deskbar (Haiku-style top-right bar)
// Minimal first pass: background slab + per-app slots; grows with mapped windows.
// -----------------------------------------------------------------------------

static int ade_count_mapped_toplevels(struct tinywl_server *server) {
    if (server == NULL) return 0;
    int count = 0;
    struct tinywl_toplevel *it;
    wl_list_for_each(it, &server->toplevels, link) {
        // If it's in our list, it's mapped in our current design
        if (it != NULL && it->xdg_toplevel != NULL) {
            count++;
        }
    }
    return count;
}

static void ade_deskbar_rebuild_apps(struct tinywl_server *server) {
    if (server == NULL || server->deskbar_tree == NULL) return;

    // Recreate the apps subtree each time for simplicity
    if (server->deskbar_apps_tree != NULL) {
        wlr_scene_node_destroy(&server->deskbar_apps_tree->node);
        server->deskbar_apps_tree = NULL;
    }
    server->deskbar_apps_tree = wlr_scene_tree_create(server->deskbar_tree);

    const int pad = 3;
    const int slot_w = 18;
    const int slot_gap = 3;
    const int slot_h = server->deskbar_height - pad * 2;

    const float slot_bg[4]     = { 0.78f, 0.78f, 0.78f, 1.0f };
    const float slot_border[4] = { 0.62f, 0.62f, 0.62f, 1.0f };

    int x = pad;

    for (int i = 0; i < server->deskbar_app_count; i++) {
        struct wlr_scene_rect *r =
            wlr_scene_rect_create(server->deskbar_apps_tree, slot_w, slot_h, slot_bg);
        wlr_scene_node_set_position(&r->node, x, pad);

        // Subtle border: top/bottom/left/right
        struct wlr_scene_rect *bt = wlr_scene_rect_create(server->deskbar_apps_tree, slot_w, 1, slot_border);
        wlr_scene_node_set_position(&bt->node, x, pad);

        struct wlr_scene_rect *bb = wlr_scene_rect_create(server->deskbar_apps_tree, slot_w, 1, slot_border);
        wlr_scene_node_set_position(&bb->node, x, pad + slot_h - 1);

        struct wlr_scene_rect *bl = wlr_scene_rect_create(server->deskbar_apps_tree, 1, slot_h, slot_border);
        wlr_scene_node_set_position(&bl->node, x, pad);

        struct wlr_scene_rect *br = wlr_scene_rect_create(server->deskbar_apps_tree, 1, slot_h, slot_border);
        wlr_scene_node_set_position(&br->node, x + slot_w - 1, pad);

        x += slot_w + slot_gap;
    }

    wlr_scene_node_raise_to_top(&server->deskbar_apps_tree->node);
}

static void ade_deskbar_update_layout(struct tinywl_server *server) {
    if (server == NULL || server->deskbar_tree == NULL || server->deskbar_bg == NULL) return;

    int ow = 0, oh = 0;
    if (!ade_get_primary_output_size(server, &ow, &oh) || ow <= 0 || oh <= 0) {
        ow = 1920;
        oh = 1080;
    }

    server->deskbar_app_count = ade_count_mapped_toplevels(server);

    const int margin = 8;
    const int base_w = 90;   // reserved for future tray/clock
    const int slot_w = 18;
    const int slot_gap = 3;

    server->deskbar_height = 24;

    int apps_w = 0;
    if (server->deskbar_app_count > 0) {
        apps_w = (server->deskbar_app_count * slot_w) + ((server->deskbar_app_count - 1) * slot_gap);
    }

    server->deskbar_width = base_w + 6 + apps_w;

    // Clamp so it can't go off-screen
    if (server->deskbar_width > ow - margin * 2) {
        server->deskbar_width = ow - margin * 2;
    }

    int x = ow - margin - server->deskbar_width;
    int y = margin;
    if (x < margin) x = margin;

    wlr_scene_node_set_position(&server->deskbar_tree->node, x, y);

    const float bg_col[4] = { 0.86f, 0.86f, 0.86f, 0.92f };
    wlr_scene_rect_set_size(server->deskbar_bg, server->deskbar_width, server->deskbar_height);
    wlr_scene_rect_set_color(server->deskbar_bg, bg_col);

    ade_deskbar_rebuild_apps(server);

    // Keep it on top
    wlr_scene_node_raise_to_top(&server->deskbar_tree->node);
}

static void ade_deskbar_init(struct tinywl_server *server) {
    if (server == NULL || server->scene == NULL) return;
    if (server->deskbar_tree != NULL) return; // already created

    server->deskbar_height = 24;
    server->deskbar_width = 140;
    server->deskbar_app_count = 0;

    server->deskbar_tree = wlr_scene_tree_create(&server->scene->tree);

    const float bg_col[4] = { 0.86f, 0.86f, 0.86f, 0.92f };
    server->deskbar_bg = wlr_scene_rect_create(server->deskbar_tree,
        server->deskbar_width, server->deskbar_height, bg_col);
    wlr_scene_node_set_position(&server->deskbar_bg->node, 0, 0);

    server->deskbar_apps_tree = NULL;

    ade_deskbar_update_layout(server);
}


static void output_destroy(struct wl_listener *listener, void *data) {
    struct tinywl_output *output = wl_container_of(listener, output, destroy);

    wl_list_remove(&output->frame.link);
    wl_list_remove(&output->request_state.link);
    wl_list_remove(&output->destroy.link);
    wl_list_remove(&output->link);
    output->scene_output = NULL;
    free(output);
}

static void server_new_output(struct wl_listener *listener, void *data) {
    /* This event is raised by the backend when a new output (aka a display or
     * monitor) becomes available. */
    struct tinywl_server *server =
        wl_container_of(listener, server, new_output);
    struct wlr_output *wlr_output = data;

    /* Configures the output created by the backend to use our allocator
     * and our renderer. Must be done once, before commiting the output */
    wlr_output_init_render(wlr_output, server->allocator, server->renderer);

    /* The output may be disabled, switch it on. */
    struct wlr_output_state state;
    wlr_output_state_init(&state);
    wlr_output_state_set_enabled(&state, true);
    /* Some backends don't have modes. DRM+KMS does, and we need to set a mode
     * before we can use the output. The mode is a tuple of (width, height,
     * refresh rate), and each monitor supports only a specific set of modes. We
     * just pick the monitor's preferred mode, a more sophisticated compositor
     * would let the user configure it. */
    struct wlr_output_mode *mode = wlr_output_preferred_mode(wlr_output);
    if (mode != NULL) {
        wlr_output_state_set_mode(&state, mode);
    }

    /* Atomically applies the new output state. */
    wlr_output_commit_state(wlr_output, &state);
    wlr_output_state_finish(&state);

    /* Allocates and configures our state for this output */
    struct tinywl_output *output = calloc(1, sizeof(*output));
    output->wlr_output = wlr_output;
    output->server = server;

    /* Sets up a listener for the frame event. */
    output->frame.notify = output_frame;
    wl_signal_add(&wlr_output->events.frame, &output->frame);

    /* Sets up a listener for the state request event. */
    output->request_state.notify = output_request_state;
    wl_signal_add(&wlr_output->events.request_state, &output->request_state);

    /* Sets up a listener for the destroy event. */
    output->destroy.notify = output_destroy;
    wl_signal_add(&wlr_output->events.destroy, &output->destroy);

    wl_list_insert(&server->outputs, &output->link);

    /* Adds this to the output layout. The add_auto function arranges outputs
     * from left-to-right in the order they appear. A more sophisticated
     * compositor would let the user configure the arrangement of outputs in the
     * layout.
     *
     * The output layout utility automatically adds a wl_output global to the
     * display, which Wayland clients can see to find out information about the
     * output (such as DPI, scale factor, manufacturer, etc).
     */
    struct wlr_output_layout_output *l_output = wlr_output_layout_add_auto(server->output_layout,
        wlr_output);
    output->scene_output = wlr_scene_output_create(server->scene, wlr_output);
    if (output->scene_output == NULL) {
        wlr_log(WLR_ERROR, "ADE: failed to create scene_output for %s", wlr_output->name ? wlr_output->name : "(unnamed)");
    } else {
        wlr_scene_output_layout_add_output(server->scene_layout, l_output, output->scene_output);
    }

    // Resize background to match the largest output now that we have one.
    ade_update_background(server);
    // Load cursor theme for this output's scale.
    ade_reload_cursor_for_output(server, wlr_output);
    
    ade_deskbar_init(server);
    ade_deskbar_update_layout(server);

    int ow = 0, oh = 0;
    wlr_output_effective_resolution(wlr_output, &ow, &oh);
    wlr_log(WLR_INFO, "ADE: new output %s %dx%d", wlr_output->name ? wlr_output->name : "(unnamed)", ow, oh);
}

static void xdg_toplevel_map(struct wl_listener *listener, void *data) {
    /* Called when the surface is mapped, or ready to display on-screen. */
    struct tinywl_toplevel *toplevel = wl_container_of(listener, toplevel, map);

    wl_list_insert(&toplevel->server->toplevels, &toplevel->link);

    // Center the window on the primary output the first time it maps,
    // and cascade each new window slightly so they don't overlap perfectly.
    int ow = 0, oh = 0;
    if (ade_get_primary_output_size(toplevel->server, &ow, &oh)) {
        // wlroots 0.19 exposes the negotiated xdg-surface geometry directly.
        struct wlr_box geo = toplevel->xdg_toplevel->base->geometry;
        if (geo.width > 0 && geo.height > 0) {
            struct tinywl_server *server = toplevel->server;

            int base_x = (ow - geo.width) / 2 - geo.x;
            int base_y = (oh - geo.height) / 2 - geo.y;

            int step = (server->cascade_step > 0) ? server->cascade_step : 24;
            int idx = (server->cascade_index < 0) ? 0 : server->cascade_index;

            // Offset diagonally. Wrap every ~12 windows to avoid drifting too far.
            int wrap = 12;
            int off = (idx % wrap) * step;

            int x = base_x + off;
            int y = base_y + off;

            // Clamp to keep the window mostly on-screen
            int min_x = -geo.x;
            int min_y = -geo.y;
            int max_x = (ow - geo.width) - geo.x;
            int max_y = (oh - geo.height) - geo.y;
            if (x < min_x) x = min_x;
            if (y < min_y) y = min_y;
            if (x > max_x) x = max_x;
            if (y > max_y) y = max_y;

            wlr_scene_node_set_position(&toplevel->scene_tree->node, x, y);

            server->cascade_index = idx + 1;
        }
    }

    // Render the window title into the tab
    ade_tab_render_title(toplevel, toplevel->xdg_toplevel->title);

    // Default to unfocused appearance until focus_toplevel decides otherwise
    ade_set_tab_selected(toplevel, false);

    /*
    // Enforce server-side decorations if the surface is mapped and ready.
    if (toplevel->xdg_decoration && toplevel->xdg_toplevel && toplevel->xdg_toplevel->base && toplevel->xdg_toplevel->base->initialized) {
        wlr_xdg_toplevel_decoration_v1_set_mode(
            toplevel->xdg_decoration,
            WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE
        );
    }
     */
    // Enforce server-side decorations once the surface is initialized (safe here on map).
    if (toplevel->xdg_decoration) {
        if (toplevel->xdg_toplevel && toplevel->xdg_toplevel->base &&
            toplevel->xdg_toplevel->base->initialized) {
            wlr_xdg_toplevel_decoration_v1_set_mode(
                toplevel->xdg_decoration,
                WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE
            );
        } else {
            wlr_log(WLR_DEBUG, "ADE: toplevel mapped but decoration surface not initialized?");
        }
    }

    focus_toplevel(toplevel);
    
    ade_deskbar_update_layout(toplevel->server);
}

static void xdg_toplevel_unmap(struct wl_listener *listener, void *data) {
    /* Called when the surface is unmapped, and should no longer be shown. */
    struct tinywl_toplevel *toplevel = wl_container_of(listener, toplevel, unmap);

    /* Reset the cursor mode if the grabbed toplevel was unmapped. */
    if (toplevel == toplevel->server->grabbed_toplevel) {
        reset_cursor_mode(toplevel->server);
    }

    wl_list_remove(&toplevel->link);
    
    ade_deskbar_update_layout(toplevel->server);
}

static void xdg_toplevel_commit(struct wl_listener *listener, void *data) {
    /* Called when a new surface state is committed. */
    struct tinywl_toplevel *toplevel = wl_container_of(listener, toplevel, commit);

    // Update tab title on commits (clients can set title after mapping)
    if (toplevel->xdg_toplevel != NULL) {
        ade_tab_render_title(toplevel, toplevel->xdg_toplevel->title);
    }

    // --- Window border geometry sync ---
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
    //if (toplevel->border_left) {
    //    wlr_scene_rect_set_size(toplevel->border_left, bw, h);
    //    wlr_scene_node_set_position(&toplevel->border_left->node, -bw, 0);
    //}
    if (toplevel->border_right) {
        wlr_scene_rect_set_size(toplevel->border_right, bw, h);
        wlr_scene_node_set_position(&toplevel->border_right->node, w, 0);
    }
    
    
    if (toplevel->left_resize_grip) {
        wlr_scene_rect_set_size(toplevel->left_resize_grip, ADE_LEFT_RESIZE_GRIP_W, h );
        wlr_scene_node_set_position(&toplevel->left_resize_grip->node, -ADE_LEFT_RESIZE_GRIP_W, 0);
        wlr_scene_node_raise_to_top(&toplevel->left_resize_grip->node);
    }
    if (toplevel->left_resize_grip_redline) {
        wlr_scene_rect_set_size(toplevel->left_resize_grip_redline, 1, h);
        wlr_scene_node_set_position(&toplevel->left_resize_grip_redline->node, -ADE_LEFT_RESIZE_GRIP_W, 0);
        wlr_scene_node_raise_to_top(&toplevel->left_resize_grip_redline->node);
    }
    if (toplevel->left_resize_grip_redline2) {
        wlr_scene_rect_set_size(toplevel->left_resize_grip_redline2, 1, h);
        wlr_scene_node_set_position(&toplevel->left_resize_grip_redline2->node,
            -ADE_LEFT_RESIZE_GRIP_W + (ADE_LEFT_RESIZE_GRIP_W - 1), 0);
        wlr_scene_node_raise_to_top(&toplevel->left_resize_grip_redline2->node);
    }
    
    if (toplevel->top_resize_grip) {
        wlr_scene_rect_set_size(toplevel->top_resize_grip, w, ADE_TOP_RESIZE_GRIP_H);
        wlr_scene_node_set_position(&toplevel->top_resize_grip->node, 0, -ADE_TOP_RESIZE_GRIP_H);
    }
    if (toplevel->top_resize_grip_line_top) {
        wlr_scene_rect_set_size(toplevel->top_resize_grip_line_top, w, 1);
        wlr_scene_node_set_position(&toplevel->top_resize_grip_line_top->node, 0, -ADE_TOP_RESIZE_GRIP_H);
        wlr_scene_node_raise_to_top(&toplevel->top_resize_grip_line_top->node);
    }
    if (toplevel->top_resize_grip_line_bottom) {
        wlr_scene_rect_set_size(toplevel->top_resize_grip_line_bottom, w, 1);
        wlr_scene_node_set_position(&toplevel->top_resize_grip_line_bottom->node, 0, -1);
        wlr_scene_node_raise_to_top(&toplevel->top_resize_grip_line_bottom->node);
    }
    
    
    if (toplevel->corner_ru_resize_grip) {
        wlr_scene_rect_set_size(toplevel->corner_ru_resize_grip, ADE_LEFT_RESIZE_GRIP_W, ADE_TOP_RESIZE_GRIP_H);
        wlr_scene_node_set_position(&toplevel->corner_ru_resize_grip->node, w, -ADE_TOP_RESIZE_GRIP_H);
        wlr_scene_node_raise_to_top(&toplevel->corner_ru_resize_grip->node);
    }
    if (toplevel->corner_ru_resize_grip_line_top) {
        wlr_scene_rect_set_size(toplevel->corner_ru_resize_grip_line_top, ADE_LEFT_RESIZE_GRIP_W, 1);
        wlr_scene_node_set_position(&toplevel->corner_ru_resize_grip_line_top->node, w, -ADE_TOP_RESIZE_GRIP_H);
        wlr_scene_node_raise_to_top(&toplevel->corner_ru_resize_grip_line_top->node);
    }
    if (toplevel->corner_ru_resize_grip_line_right) {
        wlr_scene_rect_set_size(toplevel->corner_ru_resize_grip_line_right, 1, ADE_TOP_RESIZE_GRIP_H);
        wlr_scene_node_set_position(&toplevel->corner_ru_resize_grip_line_right->node,
            w + (ADE_LEFT_RESIZE_GRIP_W - 1), -ADE_TOP_RESIZE_GRIP_H);
        wlr_scene_node_raise_to_top(&toplevel->corner_ru_resize_grip_line_right->node);
    }
    
    
    
    if (toplevel->corner_lu_resize_grip) {
        wlr_scene_rect_set_size(toplevel->corner_lu_resize_grip, ADE_LEFT_RESIZE_GRIP_W, ADE_TOP_RESIZE_GRIP_H);
        wlr_scene_node_set_position(&toplevel->corner_lu_resize_grip->node,
            -ADE_LEFT_RESIZE_GRIP_W, -ADE_TOP_RESIZE_GRIP_H);
        wlr_scene_node_raise_to_top(&toplevel->corner_lu_resize_grip->node);
    }
    if (toplevel->corner_lu_resize_grip_line_top) {
        wlr_scene_rect_set_size(toplevel->corner_lu_resize_grip_line_top, ADE_LEFT_RESIZE_GRIP_W, 1);
        wlr_scene_node_set_position(&toplevel->corner_lu_resize_grip_line_top->node,
            -ADE_LEFT_RESIZE_GRIP_W, -ADE_TOP_RESIZE_GRIP_H);
        wlr_scene_node_raise_to_top(&toplevel->corner_lu_resize_grip_line_top->node);
    }
    if (toplevel->corner_lu_resize_grip_line_left) {
        wlr_scene_rect_set_size(toplevel->corner_lu_resize_grip_line_left, 1, ADE_TOP_RESIZE_GRIP_H);
        wlr_scene_node_set_position(&toplevel->corner_lu_resize_grip_line_left->node,
            -ADE_LEFT_RESIZE_GRIP_W, -ADE_TOP_RESIZE_GRIP_H);
        wlr_scene_node_raise_to_top(&toplevel->corner_lu_resize_grip_line_left->node);
    }
    
    

    // Keep the yellow tab above the top resize grip visuals
    if (toplevel->tab_tree) {
        wlr_scene_node_raise_to_top(&toplevel->tab_tree->node);
    }
    
    if (toplevel->right_resize_grip) {
        wlr_scene_rect_set_size(toplevel->right_resize_grip, ADE_LEFT_RESIZE_GRIP_W, h);
        wlr_scene_node_set_position(&toplevel->right_resize_grip->node, w, 0);
        wlr_scene_node_raise_to_top(&toplevel->right_resize_grip->node);
    }
    if (toplevel->right_resize_grip_redline) {
        wlr_scene_rect_set_size(toplevel->right_resize_grip_redline, 1, h);
        wlr_scene_node_set_position(&toplevel->right_resize_grip_redline->node, w, 0);
        wlr_scene_node_raise_to_top(&toplevel->right_resize_grip_redline->node);
    }
    if (toplevel->right_resize_grip_redline2) {
        wlr_scene_rect_set_size(toplevel->right_resize_grip_redline2, 1, h);
        wlr_scene_node_set_position(&toplevel->right_resize_grip_redline2->node,
            w + (ADE_LEFT_RESIZE_GRIP_W - 1), 0);
        wlr_scene_node_raise_to_top(&toplevel->right_resize_grip_redline2->node);
    }

    if (toplevel->bottom_resize_grip) {
        wlr_scene_rect_set_size(toplevel->bottom_resize_grip, w, ADE_TOP_RESIZE_GRIP_H);
        wlr_scene_node_set_position(&toplevel->bottom_resize_grip->node, 0, h);
    }
    if (toplevel->bottom_resize_grip_line_top) {
        wlr_scene_rect_set_size(toplevel->bottom_resize_grip_line_top, w, 1);
        wlr_scene_node_set_position(&toplevel->bottom_resize_grip_line_top->node, 0, h);
        wlr_scene_node_raise_to_top(&toplevel->bottom_resize_grip_line_top->node);
    }
    if (toplevel->bottom_resize_grip_line_bottom) {
        wlr_scene_rect_set_size(toplevel->bottom_resize_grip_line_bottom, w, 1);
        wlr_scene_node_set_position(&toplevel->bottom_resize_grip_line_bottom->node,
            0, h + (ADE_TOP_RESIZE_GRIP_H - 1));
        wlr_scene_node_raise_to_top(&toplevel->bottom_resize_grip_line_bottom->node);
    }
    
    if (toplevel->corner_rl_resize_grip) {
        wlr_scene_rect_set_size(toplevel->corner_rl_resize_grip,
            ADE_LEFT_RESIZE_GRIP_W, ADE_TOP_RESIZE_GRIP_H);
        wlr_scene_node_set_position(&toplevel->corner_rl_resize_grip->node, w, h);
        wlr_scene_node_raise_to_top(&toplevel->corner_rl_resize_grip->node);
    }
    
    if (toplevel->corner_bl_resize_grip) {
        wlr_scene_rect_set_size(toplevel->corner_bl_resize_grip, ADE_LEFT_RESIZE_GRIP_W, ADE_TOP_RESIZE_GRIP_H);
        wlr_scene_node_set_position(&toplevel->corner_bl_resize_grip->node, -ADE_LEFT_RESIZE_GRIP_W, h);
        wlr_scene_node_raise_to_top(&toplevel->corner_bl_resize_grip->node);
    }
    if (toplevel->corner_bl_resize_grip_line_bottom) {
        wlr_scene_rect_set_size(toplevel->corner_bl_resize_grip_line_bottom, ADE_LEFT_RESIZE_GRIP_W, 1);
        wlr_scene_node_set_position(&toplevel->corner_bl_resize_grip_line_bottom->node,
            -ADE_LEFT_RESIZE_GRIP_W, h + (ADE_TOP_RESIZE_GRIP_H - 1));
        wlr_scene_node_raise_to_top(&toplevel->corner_bl_resize_grip_line_bottom->node);
    }
    if (toplevel->corner_bl_resize_grip_line_left) {
        wlr_scene_rect_set_size(toplevel->corner_bl_resize_grip_line_left, 1, ADE_TOP_RESIZE_GRIP_H);
        wlr_scene_node_set_position(&toplevel->corner_bl_resize_grip_line_left->node,
            -ADE_LEFT_RESIZE_GRIP_W, h);
        wlr_scene_node_raise_to_top(&toplevel->corner_bl_resize_grip_line_left->node);
    }
    
    if (toplevel->corner_rl_resize_grip_line_bottom) {
        wlr_scene_rect_set_size(
            toplevel->corner_rl_resize_grip_line_bottom,
            ADE_LEFT_RESIZE_GRIP_W,
            1
        );
        wlr_scene_node_set_position(
            &toplevel->corner_rl_resize_grip_line_bottom->node,
            w,
            h + (ADE_TOP_RESIZE_GRIP_H - 1)
        );
        wlr_scene_node_raise_to_top(&toplevel->corner_rl_resize_grip_line_bottom->node);
    }
    
    if (toplevel->corner_rl_resize_grip_line_right) {
        wlr_scene_rect_set_size(
            toplevel->corner_rl_resize_grip_line_right,
            1,
            ADE_TOP_RESIZE_GRIP_H
        );
        wlr_scene_node_set_position(
            &toplevel->corner_rl_resize_grip_line_right->node,
            w + (ADE_LEFT_RESIZE_GRIP_W - 1),
            h
        );
        wlr_scene_node_raise_to_top(&toplevel->corner_rl_resize_grip_line_right->node);
    }
    
    
    

    if (toplevel->xdg_toplevel->base->initial_commit) {
        /* When an xdg_surface performs an initial commit, the compositor must
         * reply with a configure so the client can map the surface. tinywl
         * configures the xdg_toplevel with 0,0 size to let the client pick the
         * dimensions itself. */
        wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel, 0, 0);
    }
}

static void xdg_toplevel_destroy(struct wl_listener *listener, void *data) {
    /* Called when the xdg_toplevel is destroyed. */
    struct tinywl_toplevel *toplevel = wl_container_of(listener, toplevel, destroy);

    ade_tab_clear_text(toplevel);
    ade_tab_clear_close(toplevel);
    ade_tab_clear_expand(toplevel);
    ade_tab_clear_gradient(toplevel);
    
    
    if (toplevel->tab_line_left) {
        wlr_scene_node_destroy(&toplevel->tab_line_left->node);
        toplevel->tab_line_left = NULL;
    }
    if (toplevel->tab_line_right) {
        wlr_scene_node_destroy(&toplevel->tab_line_right->node);
        toplevel->tab_line_right = NULL;
    }
    if (toplevel->tab_line_top) {
        wlr_scene_node_destroy(&toplevel->tab_line_top->node);
        toplevel->tab_line_top = NULL;
    }
     

    // Destroy window border nodes
    if (toplevel->border_top) wlr_scene_node_destroy(&toplevel->border_top->node);
    if (toplevel->border_bottom) wlr_scene_node_destroy(&toplevel->border_bottom->node);
    //if (toplevel->border_left) wlr_scene_node_destroy(&toplevel->border_left->node);
    if (toplevel->border_right) wlr_scene_node_destroy(&toplevel->border_right->node);

    if (toplevel->left_resize_grip) wlr_scene_node_destroy(&toplevel->left_resize_grip->node);
    if (toplevel->left_resize_grip_redline) wlr_scene_node_destroy(&toplevel->left_resize_grip_redline->node);
    if (toplevel->left_resize_grip_redline2) wlr_scene_node_destroy(&toplevel->left_resize_grip_redline2->node);
    
    if (toplevel->top_resize_grip) wlr_scene_node_destroy(&toplevel->top_resize_grip->node);
    if (toplevel->top_resize_grip_line_top) wlr_scene_node_destroy(&toplevel->top_resize_grip_line_top->node);
    if (toplevel->top_resize_grip_line_bottom) wlr_scene_node_destroy(&toplevel->top_resize_grip_line_bottom->node);

    if (toplevel->right_resize_grip) wlr_scene_node_destroy(&toplevel->right_resize_grip->node);
    if (toplevel->right_resize_grip_redline) wlr_scene_node_destroy(&toplevel->right_resize_grip_redline->node);
    if (toplevel->right_resize_grip_redline2) wlr_scene_node_destroy(&toplevel->right_resize_grip_redline2->node);

    if (toplevel->bottom_resize_grip) wlr_scene_node_destroy(&toplevel->bottom_resize_grip->node);
    if (toplevel->bottom_resize_grip_line_top) wlr_scene_node_destroy(&toplevel->bottom_resize_grip_line_top->node);
    if (toplevel->bottom_resize_grip_line_bottom) wlr_scene_node_destroy(&toplevel->bottom_resize_grip_line_bottom->node);
    
    if (toplevel->corner_rl_resize_grip) wlr_scene_node_destroy(&toplevel->corner_rl_resize_grip->node);
    
    if (toplevel->corner_rl_resize_grip_line_bottom)
        wlr_scene_node_destroy(&toplevel->corner_rl_resize_grip_line_bottom->node);
    
    if (toplevel->corner_rl_resize_grip_line_right)
        wlr_scene_node_destroy(&toplevel->corner_rl_resize_grip_line_right->node);
    
    if (toplevel->corner_ru_resize_grip) wlr_scene_node_destroy(&toplevel->corner_ru_resize_grip->node);
    if (toplevel->corner_ru_resize_grip_line_top) wlr_scene_node_destroy(&toplevel->corner_ru_resize_grip_line_top->node);
    if (toplevel->corner_ru_resize_grip_line_right) wlr_scene_node_destroy(&toplevel->corner_ru_resize_grip_line_right->node);
    
    if (toplevel->corner_lu_resize_grip) wlr_scene_node_destroy(&toplevel->corner_lu_resize_grip->node);
    if (toplevel->corner_lu_resize_grip_line_top) wlr_scene_node_destroy(&toplevel->corner_lu_resize_grip_line_top->node);
    if (toplevel->corner_lu_resize_grip_line_left) wlr_scene_node_destroy(&toplevel->corner_lu_resize_grip_line_left->node);
    
    
    if (toplevel->corner_bl_resize_grip) wlr_scene_node_destroy(&toplevel->corner_bl_resize_grip->node);
    if (toplevel->corner_bl_resize_grip_line_bottom) wlr_scene_node_destroy(&toplevel->corner_bl_resize_grip_line_bottom->node);
    if (toplevel->corner_bl_resize_grip_line_left) wlr_scene_node_destroy(&toplevel->corner_bl_resize_grip_line_left->node);
    // ***
    
    wl_list_remove(&toplevel->map.link);
    
    wl_list_remove(&toplevel->unmap.link);
    wl_list_remove(&toplevel->commit.link);
    wl_list_remove(&toplevel->destroy.link);
    wl_list_remove(&toplevel->request_move.link);
    wl_list_remove(&toplevel->request_resize.link);
    wl_list_remove(&toplevel->request_maximize.link);
    wl_list_remove(&toplevel->request_fullscreen.link);
    
    if (toplevel->xdg_decoration) {
        wl_list_remove(&toplevel->xdg_decoration_request_mode.link);
        wl_list_remove(&toplevel->xdg_decoration_destroy.link);
        toplevel->xdg_decoration = NULL;
    }

    free(toplevel);
}

static void begin_interactive(struct tinywl_toplevel *toplevel,
        enum tinywl_cursor_mode mode, uint32_t edges) {
    /* This function sets up an interactive move or resize operation, where the
     * compositor stops propegating pointer events to clients and instead
     * consumes them itself, to move or resize windows. */
    struct tinywl_server *server = toplevel->server;

    server->grabbed_toplevel = toplevel;
    server->cursor_mode = mode;

    if (mode == TINYWL_CURSOR_MOVE) {
        server->grab_x = server->cursor->x - toplevel->scene_tree->node.x;
        server->grab_y = server->cursor->y - toplevel->scene_tree->node.y;
    } else {
        struct wlr_box *geo_box = &toplevel->xdg_toplevel->base->geometry;

        double border_x = (toplevel->scene_tree->node.x + geo_box->x) +
            ((edges & WLR_EDGE_RIGHT) ? geo_box->width : 0);
        double border_y = (toplevel->scene_tree->node.y + geo_box->y) +
            ((edges & WLR_EDGE_BOTTOM) ? geo_box->height : 0);
        server->grab_x = server->cursor->x - border_x;
        server->grab_y = server->cursor->y - border_y;

        server->grab_geobox = *geo_box;
        server->grab_geobox.x += toplevel->scene_tree->node.x;
        server->grab_geobox.y += toplevel->scene_tree->node.y;

        server->resize_edges = edges;
    }
}

static void xdg_toplevel_request_move(
        struct wl_listener *listener, void *data) {
    /* This event is raised when a client would like to begin an interactive
     * move, typically because the user clicked on their client-side
     * decorations. Note that a more sophisticated compositor should check the
     * provided serial against a list of button press serials sent to this
     * client, to prevent the client from requesting this whenever they want. */
    struct tinywl_toplevel *toplevel = wl_container_of(listener, toplevel, request_move);
    begin_interactive(toplevel, TINYWL_CURSOR_MOVE, 0);
}

static void xdg_toplevel_request_resize(
        struct wl_listener *listener, void *data) {
    /* This event is raised when a client would like to begin an interactive
     * resize, typically because the user clicked on their client-side
     * decorations. Note that a more sophisticated compositor should check the
     * provided serial against a list of button press serials sent to this
     * client, to prevent the client from requesting this whenever they want. */
    struct wlr_xdg_toplevel_resize_event *event = data;
    struct tinywl_toplevel *toplevel = wl_container_of(listener, toplevel, request_resize);
    begin_interactive(toplevel, TINYWL_CURSOR_RESIZE, event->edges);
}

static void xdg_toplevel_request_maximize(
        struct wl_listener *listener, void *data) {
    /* This event is raised when a client would like to maximize itself,
     * typically because the user clicked on the maximize button on client-side
     * decorations. tinywl doesn't support maximization, but to conform to
     * xdg-shell protocol we still must send a configure.
     * wlr_xdg_surface_schedule_configure() is used to send an empty reply.
     * However, if the request was sent before an initial commit, we don't do
     * anything and let the client finish the initial surface setup. */
    struct tinywl_toplevel *toplevel =
        wl_container_of(listener, toplevel, request_maximize);
    if (toplevel->xdg_toplevel->base->initialized) {
        wlr_xdg_surface_schedule_configure(toplevel->xdg_toplevel->base);
    }
}

static void xdg_toplevel_request_fullscreen(
        struct wl_listener *listener, void *data) {
    /* Just as with request_maximize, we must send a configure here. */
    struct tinywl_toplevel *toplevel =
        wl_container_of(listener, toplevel, request_fullscreen);
    if (toplevel->xdg_toplevel->base->initialized) {
        wlr_xdg_surface_schedule_configure(toplevel->xdg_toplevel->base);
    }
}

static void server_new_xdg_toplevel(struct wl_listener *listener, void *data) {
    /* This event is raised when a client creates a new toplevel (application window). */
    struct tinywl_server *server = wl_container_of(listener, server, new_xdg_toplevel);
    struct wlr_xdg_toplevel *xdg_toplevel = data;

    /* Allocate a tinywl_toplevel for this surface */
    struct tinywl_toplevel *toplevel = calloc(1, sizeof(*toplevel));
    toplevel->server = server;
    toplevel->xdg_toplevel = xdg_toplevel;
    toplevel->scene_tree =
        wlr_scene_xdg_surface_create(&toplevel->server->scene->tree, xdg_toplevel->base);
    toplevel->scene_tree->node.data = toplevel;
    xdg_toplevel->base->data = toplevel->scene_tree;

    
    /* Create decoration tree above the window */
    toplevel->decor_tree =
        wlr_scene_tree_create(toplevel->scene_tree);

    // Simple gray window border (BeOS-style)
    float border_col[4] = { 0.65f, 0.65f, 0.65f, 1.0f };
    const int bw = 1;
    // geometry will be corrected on map/commit
    struct wlr_box geo = xdg_toplevel->base->geometry;
    int w = geo.width  > 0 ? geo.width  : 800;
    int h = geo.height > 0 ? geo.height : 600;
    // Top
    toplevel->border_top = wlr_scene_rect_create(toplevel->decor_tree, w, bw, border_col);
    wlr_scene_node_set_position(&toplevel->border_top->node, 0, -bw);
    // Bottom
    toplevel->border_bottom = wlr_scene_rect_create(toplevel->decor_tree, w, bw, border_col);
    wlr_scene_node_set_position(&toplevel->border_bottom->node, 0, h);
    // Left
    //toplevel->border_left = wlr_scene_rect_create(toplevel->decor_tree, bw, h, border_col);
    //wlr_scene_node_set_position(&toplevel->border_left->node, -bw, 0);
    // Right
    toplevel->border_right = wlr_scene_rect_create(toplevel->decor_tree, bw, h, border_col);
    wlr_scene_node_set_position(&toplevel->border_right->node, w, 0);
    
    
    
    
    // Top resize grip region (6px tall) spanning the window width
    // Positioned above the content: y = -ADE_TOP_RESIZE_GRIP_H
    // tab_tree is raised above later, so tab clicks still work.
    {
        float top_grip_col[4] = { 0.92f, 0.92f, 0.92f, 0.18f };
        toplevel->top_resize_grip = wlr_scene_rect_create(
            toplevel->decor_tree, w, ADE_TOP_RESIZE_GRIP_H, top_grip_col);
        if (toplevel->top_resize_grip) {
            wlr_scene_node_set_position(&toplevel->top_resize_grip->node, 0, -ADE_TOP_RESIZE_GRIP_H);
        }
    }
    // Detail lines on top/bottom edges of the top resize grip
    {
        float line_col[4] = { 0.54f, 0.54f, 0.54f, 1.0f };

        // Top edge of grip
        toplevel->top_resize_grip_line_top = wlr_scene_rect_create(
            toplevel->decor_tree, w, 1, line_col);
        if (toplevel->top_resize_grip_line_top) {
            wlr_scene_node_set_position(&toplevel->top_resize_grip_line_top->node, 0, -ADE_TOP_RESIZE_GRIP_H);
            wlr_scene_node_raise_to_top(&toplevel->top_resize_grip_line_top->node);
        }

        // Bottom edge of grip (just under it)
        toplevel->top_resize_grip_line_bottom = wlr_scene_rect_create(
            toplevel->decor_tree, w, 1, line_col);
        if (toplevel->top_resize_grip_line_bottom) {
            wlr_scene_node_set_position(&toplevel->top_resize_grip_line_bottom->node, 0, -1);
            wlr_scene_node_raise_to_top(&toplevel->top_resize_grip_line_bottom->node);
        }
    }
    
    
    // Left resize grip region (6px) + subtle vertical lines
    // NOTE: This region matches the window content height (h)
    float grip_col[4] = { 0.94f, 0.94f, 0.94f, 1.0f };
    float line_col[4] = { 0.54f, 0.54f, 0.54f, 1.0f };

    // Grip background
    toplevel->left_resize_grip = wlr_scene_rect_create(
        toplevel->decor_tree, ADE_LEFT_RESIZE_GRIP_W, h, grip_col);
    wlr_scene_node_set_position(&toplevel->left_resize_grip->node, -ADE_LEFT_RESIZE_GRIP_W, 0);
    wlr_scene_node_raise_to_top(&toplevel->left_resize_grip->node);

    // Left edge line
    toplevel->left_resize_grip_redline = wlr_scene_rect_create(
        toplevel->decor_tree, 1, h, line_col);
    wlr_scene_node_set_position(&toplevel->left_resize_grip_redline->node, -ADE_LEFT_RESIZE_GRIP_W, 0);
    wlr_scene_node_raise_to_top(&toplevel->left_resize_grip_redline->node);

    // Right edge line (inside the 6px grip: x = -1 when width == 6)
    toplevel->left_resize_grip_redline2 = wlr_scene_rect_create(
        toplevel->decor_tree, 1, h, line_col);
    wlr_scene_node_set_position(&toplevel->left_resize_grip_redline2->node,
        -ADE_LEFT_RESIZE_GRIP_W + (ADE_LEFT_RESIZE_GRIP_W - 1), 0);
    wlr_scene_node_raise_to_top(&toplevel->left_resize_grip_redline2->node);
    
    // Right resize grip region (6px) + subtle vertical lines
    // NOTE: This region matches the window content height (h)
    {
        float grip_col_r[4] = { 0.94f, 0.94f, 0.94f, 1.0f };
        float line_col_r[4] = { 0.54f, 0.54f, 0.54f, 1.0f };

        // Grip background (outside the content on the right: x = w)
        toplevel->right_resize_grip = wlr_scene_rect_create(
            toplevel->decor_tree, ADE_LEFT_RESIZE_GRIP_W, h, grip_col_r);
        wlr_scene_node_set_position(&toplevel->right_resize_grip->node, w, 0);
        wlr_scene_node_raise_to_top(&toplevel->right_resize_grip->node);

        // Left edge line of the right grip (at x = w)
        toplevel->right_resize_grip_redline = wlr_scene_rect_create(
            toplevel->decor_tree, 1, h, line_col_r);
        wlr_scene_node_set_position(&toplevel->right_resize_grip_redline->node, w, 0);
        wlr_scene_node_raise_to_top(&toplevel->right_resize_grip_redline->node);

        // Right edge line of the right grip (at x = w + width - 1)
        toplevel->right_resize_grip_redline2 = wlr_scene_rect_create(
            toplevel->decor_tree, 1, h, line_col_r);
        wlr_scene_node_set_position(&toplevel->right_resize_grip_redline2->node,
            w + (ADE_LEFT_RESIZE_GRIP_W - 1), 0);
        wlr_scene_node_raise_to_top(&toplevel->right_resize_grip_redline2->node);
    }
    
    // Bottom resize grip region (6px tall) spanning the window width
    // Positioned below the content: y = h
    {
        float bot_grip_col[4] = { 0.92f, 0.92f, 0.92f, 0.18f };
        toplevel->bottom_resize_grip = wlr_scene_rect_create(
            toplevel->decor_tree, w, ADE_TOP_RESIZE_GRIP_H, bot_grip_col);
        if (toplevel->bottom_resize_grip) {
            wlr_scene_node_set_position(&toplevel->bottom_resize_grip->node, 0, h);
        }

        // Detail lines on top/bottom edges of the bottom resize grip
        float line_col_b[4] = { 0.54f, 0.54f, 0.54f, 1.0f };

        // Top edge of bottom grip (y = h)
        toplevel->bottom_resize_grip_line_top = wlr_scene_rect_create(
            toplevel->decor_tree, w, 1, line_col_b);
        if (toplevel->bottom_resize_grip_line_top) {
            wlr_scene_node_set_position(&toplevel->bottom_resize_grip_line_top->node, 0, h);
            wlr_scene_node_raise_to_top(&toplevel->bottom_resize_grip_line_top->node);
        }

        // Bottom edge of bottom grip (y = h + H - 1)
        toplevel->bottom_resize_grip_line_bottom = wlr_scene_rect_create(
            toplevel->decor_tree, w, 1, line_col_b);
        if (toplevel->bottom_resize_grip_line_bottom) {
            wlr_scene_node_set_position(&toplevel->bottom_resize_grip_line_bottom->node,
                0, h + (ADE_TOP_RESIZE_GRIP_H - 1));
            wlr_scene_node_raise_to_top(&toplevel->bottom_resize_grip_line_bottom->node);
        }
    }
    
    // Bottom-right corner resize grip (6x6) outside the window (at x=w, y=h)
    {
        float corner_col[4] = { 0.92f, 0.92f, 0.92f, 0.28f };
        toplevel->corner_rl_resize_grip = wlr_scene_rect_create(
            toplevel->decor_tree,
            ADE_LEFT_RESIZE_GRIP_W,
            ADE_TOP_RESIZE_GRIP_H,
            corner_col
        );
        if (toplevel->corner_rl_resize_grip) {
            wlr_scene_node_set_position(&toplevel->corner_rl_resize_grip->node, w, h);
            wlr_scene_node_raise_to_top(&toplevel->corner_rl_resize_grip->node);
        }
    }
    
    // Grey bottom detail line for corner resize grip
    {
        const float line_col_corner[4] = { 0.54f, 0.54f, 0.54f, 1.0f };
        toplevel->corner_rl_resize_grip_line_bottom = wlr_scene_rect_create(
            toplevel->decor_tree, ADE_LEFT_RESIZE_GRIP_W, 1, line_col_corner);
        if (toplevel->corner_rl_resize_grip_line_bottom) {
            wlr_scene_node_set_position(
                &toplevel->corner_rl_resize_grip_line_bottom->node,
                w,
                h + (ADE_TOP_RESIZE_GRIP_H - 1)
            );
            wlr_scene_node_raise_to_top(&toplevel->corner_rl_resize_grip_line_bottom->node);
        }
    }
    
    // Grey right detail line for corner resize grip
    {
        const float line_col_corner_r[4] = { 0.54f, 0.54f, 0.54f, 1.0f };
        toplevel->corner_rl_resize_grip_line_right = wlr_scene_rect_create(
            toplevel->decor_tree, 1, ADE_TOP_RESIZE_GRIP_H, line_col_corner_r);
        if (toplevel->corner_rl_resize_grip_line_right) {
            // Right edge of the 6x6 grip
            wlr_scene_node_set_position(
                &toplevel->corner_rl_resize_grip_line_right->node,
                w + (ADE_LEFT_RESIZE_GRIP_W - 1),
                h
            );
            wlr_scene_node_raise_to_top(&toplevel->corner_rl_resize_grip_line_right->node);
        }
    }
    
    // Top-right corner resize grip (6x6) outside the window (at x=w, y=-ADE_TOP_RESIZE_GRIP_H)
    {
        float corner_col[4] = { 0.92f, 0.92f, 0.92f, 0.28f };

        toplevel->corner_ru_resize_grip = wlr_scene_rect_create(
            toplevel->decor_tree, ADE_LEFT_RESIZE_GRIP_W, ADE_TOP_RESIZE_GRIP_H, corner_col);
        if (toplevel->corner_ru_resize_grip) {
            wlr_scene_node_set_position(&toplevel->corner_ru_resize_grip->node, w, -ADE_TOP_RESIZE_GRIP_H);
            wlr_scene_node_raise_to_top(&toplevel->corner_ru_resize_grip->node);
        }

        // Grey top detail line (horizontal)
        {
            const float line_col[4] = { 0.54f, 0.54f, 0.54f, 1.0f };
            toplevel->corner_ru_resize_grip_line_top = wlr_scene_rect_create(
                toplevel->decor_tree, ADE_LEFT_RESIZE_GRIP_W, 1, line_col);
            if (toplevel->corner_ru_resize_grip_line_top) {
                wlr_scene_node_set_position(&toplevel->corner_ru_resize_grip_line_top->node,
                    w, -ADE_TOP_RESIZE_GRIP_H);
                wlr_scene_node_raise_to_top(&toplevel->corner_ru_resize_grip_line_top->node);
            }
        }

        // Grey right detail line (vertical)
        {
            const float line_col[4] = { 0.54f, 0.54f, 0.54f, 1.0f };
            toplevel->corner_ru_resize_grip_line_right = wlr_scene_rect_create(
                toplevel->decor_tree, 1, ADE_TOP_RESIZE_GRIP_H, line_col);
            if (toplevel->corner_ru_resize_grip_line_right) {
                wlr_scene_node_set_position(&toplevel->corner_ru_resize_grip_line_right->node,
                    w + (ADE_LEFT_RESIZE_GRIP_W - 1), -ADE_TOP_RESIZE_GRIP_H);
                wlr_scene_node_raise_to_top(&toplevel->corner_ru_resize_grip_line_right->node);
            }
        }
    }
    
    
    // Top-left corner resize grip (6x6) outside the window (at x=-W, y=-H)
    {
        float corner_col[4] = { 0.92f, 0.92f, 0.92f, 0.28f };

        toplevel->corner_lu_resize_grip = wlr_scene_rect_create(
            toplevel->decor_tree, ADE_LEFT_RESIZE_GRIP_W, ADE_TOP_RESIZE_GRIP_H, corner_col);
        if (toplevel->corner_lu_resize_grip) {
            wlr_scene_node_set_position(
                &toplevel->corner_lu_resize_grip->node,
                -ADE_LEFT_RESIZE_GRIP_W,
                -ADE_TOP_RESIZE_GRIP_H
            );
            wlr_scene_node_raise_to_top(&toplevel->corner_lu_resize_grip->node);
        }

        // Grey top detail line
        {
            const float line_col[4] = { 0.54f, 0.54f, 0.54f, 1.0f };
            toplevel->corner_lu_resize_grip_line_top = wlr_scene_rect_create(
                toplevel->decor_tree, ADE_LEFT_RESIZE_GRIP_W, 1, line_col);
            if (toplevel->corner_lu_resize_grip_line_top) {
                wlr_scene_node_set_position(
                    &toplevel->corner_lu_resize_grip_line_top->node,
                    -ADE_LEFT_RESIZE_GRIP_W,
                    -ADE_TOP_RESIZE_GRIP_H
                );
                wlr_scene_node_raise_to_top(&toplevel->corner_lu_resize_grip_line_top->node);
            }
        }

        // Grey left detail line
        {
            const float line_col[4] = { 0.54f, 0.54f, 0.54f, 1.0f };
            toplevel->corner_lu_resize_grip_line_left = wlr_scene_rect_create(
                toplevel->decor_tree, 1, ADE_TOP_RESIZE_GRIP_H, line_col);
            if (toplevel->corner_lu_resize_grip_line_left) {
                wlr_scene_node_set_position(
                    &toplevel->corner_lu_resize_grip_line_left->node,
                    -ADE_LEFT_RESIZE_GRIP_W,
                    -ADE_TOP_RESIZE_GRIP_H
                );
                wlr_scene_node_raise_to_top(&toplevel->corner_lu_resize_grip_line_left->node);
            }
        }
    }
    
    
    // ***
    
    
    // Bottom-left corner resize grip (6x6) outside the window (at x=-W, y=h)
    {
        float corner_col[4] = { 0.92f, 0.92f, 0.92f, 0.28f };

        toplevel->corner_bl_resize_grip = wlr_scene_rect_create(
            toplevel->decor_tree, ADE_LEFT_RESIZE_GRIP_W, ADE_TOP_RESIZE_GRIP_H, corner_col);
        if (toplevel->corner_bl_resize_grip) {
            wlr_scene_node_set_position(
                &toplevel->corner_bl_resize_grip->node,
                -ADE_LEFT_RESIZE_GRIP_W,
                h
            );
            wlr_scene_node_raise_to_top(&toplevel->corner_bl_resize_grip->node);
        }

        // Grey bottom detail line
        {
            const float line_col[4] = { 0.54f, 0.54f, 0.54f, 1.0f };
            toplevel->corner_bl_resize_grip_line_bottom = wlr_scene_rect_create(
                toplevel->decor_tree, ADE_LEFT_RESIZE_GRIP_W, 1, line_col);
            if (toplevel->corner_bl_resize_grip_line_bottom) {
                wlr_scene_node_set_position(
                    &toplevel->corner_bl_resize_grip_line_bottom->node,
                    -ADE_LEFT_RESIZE_GRIP_W,
                    h + (ADE_TOP_RESIZE_GRIP_H - 1)
                );
                wlr_scene_node_raise_to_top(&toplevel->corner_bl_resize_grip_line_bottom->node);
            }
        }

        // Grey left detail line
        {
            const float line_col[4] = { 0.54f, 0.54f, 0.54f, 1.0f };
            toplevel->corner_bl_resize_grip_line_left = wlr_scene_rect_create(
                toplevel->decor_tree, 1, ADE_TOP_RESIZE_GRIP_H, line_col);
            if (toplevel->corner_bl_resize_grip_line_left) {
                wlr_scene_node_set_position(
                    &toplevel->corner_bl_resize_grip_line_left->node,
                    -ADE_LEFT_RESIZE_GRIP_W,
                    h
                );
                wlr_scene_node_raise_to_top(&toplevel->corner_bl_resize_grip_line_left->node);
            }
        }
    }
    
    
    /* Tab container (movable along the top edge) */
    toplevel->tab_tree = wlr_scene_tree_create(toplevel->decor_tree);
    // Allow tab to slide over the 6px left resize grip; minimum logical position is -6
    toplevel->tab_x_px = -ADE_LEFT_RESIZE_GRIP_W;
    toplevel->tab_width_px = 180; // default until title render computes
    wlr_scene_node_set_position(&toplevel->tab_tree->node, toplevel->tab_x_px, ADE_TAB_Y);
    wlr_scene_node_raise_to_top(&toplevel->tab_tree->node);
    
    /* BeOS-style yellow tab (rect is inside tab_tree so it moves with it) */
    // Start unfocused tabs as light grey; focus_toplevel() will turn the active one yellow
    //float yellow[4] = { 0.85f, 0.85f, 0.85f, 1.0f };
    float tab_hit[4] = { 0.0f, 0.0f, 0.0f, 0.0f }; // transparent

    toplevel->tab_rect = wlr_scene_rect_create(
        toplevel->tab_tree,
        160,   // tab width
        ADE_TAB_HEIGHT,    // tab height
                                               tab_hit
    );

    /* tab_rect at (0,0) inside tab_tree */
    wlr_scene_node_set_position(&toplevel->tab_rect->node, 0, 0);
    
    // Grey vertical detail lines on tab left/right edges
    {
        const float tab_line_col[4] = { 0.62f, 0.62f, 0.62f, 1.0f };
        int tab_w = 160; // matches the initial tab_rect width
        int tab_h = ADE_TAB_HEIGHT;

        toplevel->tab_line_left = wlr_scene_rect_create(toplevel->tab_tree, 1, tab_h, tab_line_col);
        if (toplevel->tab_line_left) {
            wlr_scene_node_set_position(&toplevel->tab_line_left->node, 0, 0);
            wlr_scene_node_raise_to_top(&toplevel->tab_line_left->node);
        }

        toplevel->tab_line_right = wlr_scene_rect_create(toplevel->tab_tree, 1, tab_h, tab_line_col);
        if (toplevel->tab_line_right) {
            wlr_scene_node_set_position(&toplevel->tab_line_right->node, tab_w - 1, 0);
            wlr_scene_node_raise_to_top(&toplevel->tab_line_right->node);
        }
        
        // Top edge line across the tab
        toplevel->tab_line_top = wlr_scene_rect_create(toplevel->tab_tree, tab_w, 1, tab_line_col);
        if (toplevel->tab_line_top) {
            wlr_scene_node_set_position(&toplevel->tab_line_top->node, 0, 0);
            wlr_scene_node_raise_to_top(&toplevel->tab_line_top->node);
        }
    }
    

    toplevel->tab_text_tree = NULL;
    toplevel->close_tree = NULL;
    toplevel->close_bg = NULL;
    toplevel->close_x_tree = NULL;

    ade_tab_render_title(toplevel, xdg_toplevel->title);

    wlr_scene_node_raise_to_top(&toplevel->tab_tree->node);

    /* Listen to the various events it can emit */
    toplevel->map.notify = xdg_toplevel_map;
    wl_signal_add(&xdg_toplevel->base->surface->events.map, &toplevel->map);
    toplevel->unmap.notify = xdg_toplevel_unmap;
    wl_signal_add(&xdg_toplevel->base->surface->events.unmap, &toplevel->unmap);
    toplevel->commit.notify = xdg_toplevel_commit;
    wl_signal_add(&xdg_toplevel->base->surface->events.commit, &toplevel->commit);

    toplevel->destroy.notify = xdg_toplevel_destroy;
    wl_signal_add(&xdg_toplevel->events.destroy, &toplevel->destroy);

    /* cotd */
    toplevel->request_move.notify = xdg_toplevel_request_move;
    wl_signal_add(&xdg_toplevel->events.request_move, &toplevel->request_move);
    toplevel->request_resize.notify = xdg_toplevel_request_resize;
    wl_signal_add(&xdg_toplevel->events.request_resize, &toplevel->request_resize);
    toplevel->request_maximize.notify = xdg_toplevel_request_maximize;
    wl_signal_add(&xdg_toplevel->events.request_maximize, &toplevel->request_maximize);
    toplevel->request_fullscreen.notify = xdg_toplevel_request_fullscreen;
    wl_signal_add(&xdg_toplevel->events.request_fullscreen, &toplevel->request_fullscreen);
    
    toplevel->xdg_decoration = NULL;
}

static void xdg_popup_commit(struct wl_listener *listener, void *data) {
    /* Called when a new surface state is committed. */
    struct tinywl_popup *popup = wl_container_of(listener, popup, commit);

    if (popup->xdg_popup->base->initial_commit) {
        /* When an xdg_surface performs an initial commit, the compositor must
         * reply with a configure so the client can map the surface.
         * tinywl sends an empty configure. A more sophisticated compositor
         * might change an xdg_popup's geometry to ensure it's not positioned
         * off-screen, for example. */
        wlr_xdg_surface_schedule_configure(popup->xdg_popup->base);
    }
}

static void xdg_popup_destroy(struct wl_listener *listener, void *data) {
    /* Called when the xdg_popup is destroyed. */
    struct tinywl_popup *popup = wl_container_of(listener, popup, destroy);

    wl_list_remove(&popup->commit.link);
    wl_list_remove(&popup->destroy.link);

    free(popup);
}

static void server_new_xdg_popup(struct wl_listener *listener, void *data) {
    /* This event is raised when a client creates a new popup. */
    struct wlr_xdg_popup *xdg_popup = data;

    struct tinywl_popup *popup = calloc(1, sizeof(*popup));
    popup->xdg_popup = xdg_popup;

    /* We must add xdg popups to the scene graph so they get rendered. The
     * wlroots scene graph provides a helper for this, but to use it we must
     * provide the proper parent scene node of the xdg popup. To enable this,
     * we always set the user data field of xdg_surfaces to the corresponding
     * scene node. */
    struct wlr_xdg_surface *parent = wlr_xdg_surface_try_from_wlr_surface(xdg_popup->parent);
    assert(parent != NULL);
    struct wlr_scene_tree *parent_tree = parent->data;
    xdg_popup->base->data = wlr_scene_xdg_surface_create(parent_tree, xdg_popup->base);

    popup->commit.notify = xdg_popup_commit;
    wl_signal_add(&xdg_popup->base->surface->events.commit, &popup->commit);

    popup->destroy.notify = xdg_popup_destroy;
    wl_signal_add(&xdg_popup->events.destroy, &popup->destroy);
}

// Helper to safely detach the backend destroy listener if still linked.
static void ade_detach_backend_destroy_listener(struct tinywl_server *server) {
    if (!server) return;
    // If the listener was added, its link pointers will be non-NULL.
    if (server->backend_destroy.link.next != NULL && server->backend_destroy.link.prev != NULL) {
        // Only remove if it looks linked (best-effort; wl_list_remove is safe if linked)
        wl_list_remove(&server->backend_destroy.link);
        wl_list_init(&server->backend_destroy.link);
    }
}

static void server_new_layer_surface(struct wl_listener *listener, void *data) {
    struct tinywl_server *server =
        wl_container_of(listener, server, new_layer_surface);
    struct wlr_layer_surface_v1 *layer_surface = data;

    // Safety check: layer_surface and its surface must not be NULL
    if (layer_surface == NULL || layer_surface->surface == NULL) {
        wlr_log(WLR_ERROR, "ADE: new layer surface is NULL");
        return;
    }

    /* Create a scene node for the layer surface (used by launchers/panels like fuzzel) */
    struct wlr_scene_layer_surface_v1 *scene_layer =
        wlr_scene_layer_surface_v1_create(&server->scene->tree, layer_surface);
    if (scene_layer == NULL) {
        wlr_log(WLR_ERROR, "ADE: failed to create scene layer surface");
        return;
    }

    /* wlroots doesn't auto-place layer surfaces: the compositor must configure them.
     * Without this, clients like fuzzel may appear "invisible" (0-sized/unpositioned). */
    struct wlr_output *out = layer_surface->output;
    if (out == NULL) {
        /* Fall back to the first known output */
        struct tinywl_output *o = NULL;
        wl_list_for_each(o, &server->outputs, link) {
            out = o->wlr_output;
            break;
        }
    }

    if (out != NULL) {
        int ow = 0, oh = 0;
        wlr_output_effective_resolution(out, &ow, &oh);
        if (ow > 0 && oh > 0) {
            struct wlr_box full_area = {
                .x = 0,
                .y = 0,
                .width = ow,
                .height = oh,
            };
            struct wlr_box usable_area = full_area;
            wlr_scene_layer_surface_v1_configure(scene_layer, &full_area, &usable_area);
        } else {
            wlr_log(WLR_ERROR, "ADE: output resolution invalid for layer surface (ow=%d oh=%d)", ow, oh);
        }
    }

    /* Ensure overlays are above regular app surfaces */
    wlr_scene_node_raise_to_top(&scene_layer->tree->node);

    /* If the layer surface wants keyboard input (fuzzel does), give it focus */
    if (layer_surface->surface != NULL && layer_surface->current.keyboard_interactive) {
        struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(server->seat);
        if (keyboard != NULL) {
            wlr_seat_keyboard_notify_enter(server->seat, layer_surface->surface,
                keyboard->keycodes, keyboard->num_keycodes, &keyboard->modifiers);
        }
    }
}

static void ade_update_background(struct tinywl_server *server) {
    if (!server || !server->bg_rect) return;

    int max_w = 0, max_h = 0;
    struct tinywl_output *o = NULL;
    wl_list_for_each(o, &server->outputs, link) {
        int w = 0, h = 0;
        wlr_output_effective_resolution(o->wlr_output, &w, &h);
        if (w > max_w) max_w = w;
        if (h > max_h) max_h = h;
    }

    if (max_w <= 0) max_w = 1920;
    if (max_h <= 0) max_h = 1080;

    wlr_scene_rect_set_size(server->bg_rect, max_w, max_h);
    wlr_scene_node_lower_to_bottom(&server->bg_rect->node);
}

static struct tinywl_toplevel *toplevel_from_xdg_toplevel(struct wlr_xdg_toplevel *xdg_toplevel) {
    if (!xdg_toplevel || !xdg_toplevel->base) return NULL;
    struct wlr_scene_tree *scene_tree = xdg_toplevel->base->data;
    if (!scene_tree) return NULL;
    return (struct tinywl_toplevel *)scene_tree->node.data;
}

static void xdg_decoration_request_mode(struct wl_listener *listener, void *data) {
    (void)data;
    struct tinywl_toplevel *toplevel =
        wl_container_of(listener, toplevel, xdg_decoration_request_mode);

    if (!toplevel || !toplevel->xdg_decoration) {
        return;
    }

    // wlroots aborts if this schedules a configure before the xdg_surface is initialized
    if (!toplevel->xdg_toplevel || !toplevel->xdg_toplevel->base ||
        !toplevel->xdg_toplevel->base->initialized) {
        wlr_log(WLR_DEBUG, "ADE: decoration request_mode before surface initialized; deferring");
        return;
    }

    wlr_xdg_toplevel_decoration_v1_set_mode(
        toplevel->xdg_decoration,
        WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE
    );
}

static void xdg_decoration_destroy(struct wl_listener *listener, void *data) {
    (void)data;
    struct tinywl_toplevel *toplevel =
        wl_container_of(listener, toplevel, xdg_decoration_destroy);

    if (!toplevel) return;

    wl_list_remove(&toplevel->xdg_decoration_request_mode.link);
    wl_list_remove(&toplevel->xdg_decoration_destroy.link);
    toplevel->xdg_decoration = NULL;
}

static void server_new_xdg_decoration(struct wl_listener *listener, void *data) {
    struct tinywl_server *server =
        wl_container_of(listener, server, new_xdg_decoration);
    (void)server;

    struct wlr_xdg_toplevel_decoration_v1 *decoration = data;
    if (!decoration || !decoration->toplevel) {
        return;
    }

    struct tinywl_toplevel *toplevel = toplevel_from_xdg_toplevel(decoration->toplevel);

    /*
     * IMPORTANT:
     * wlroots aborts if we try to schedule a configure before the xdg_surface
     * is initialized. `wlr_xdg_toplevel_decoration_v1_set_mode()` internally
     * schedules a configure, so only call it once the surface is initialized
     * (typically true by the time it maps).
     */
    if (!toplevel) {
        return;
    }

    // If an old decoration exists, detach listeners
    if (toplevel->xdg_decoration) {
        wl_list_remove(&toplevel->xdg_decoration_request_mode.link);
        wl_list_remove(&toplevel->xdg_decoration_destroy.link);
    }

    toplevel->xdg_decoration = decoration;

    toplevel->xdg_decoration_request_mode.notify = xdg_decoration_request_mode;
    wl_signal_add(&decoration->events.request_mode, &toplevel->xdg_decoration_request_mode);

    toplevel->xdg_decoration_destroy.notify = xdg_decoration_destroy;
    wl_signal_add(&decoration->events.destroy, &toplevel->xdg_decoration_destroy);

    /*
    if (toplevel->xdg_toplevel && toplevel->xdg_toplevel->base && toplevel->xdg_toplevel->base->initialized) {
        wlr_xdg_toplevel_decoration_v1_set_mode(
            toplevel->xdg_decoration,
            WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE
        );
    }
     */
}

int main(int argc, char *argv[]) {

    // Some VM/nested backends have broken hardware cursors (offset/inverted).
    // Force software cursors so the drawn cursor matches click coordinates.
    setenv("WLR_NO_HARDWARE_CURSORS", "1", 1);



    wlr_log_init(WLR_DEBUG, NULL);
    char *startup_cmd = NULL;

    int c;
    while ((c = getopt(argc, argv, "s:h")) != -1) {
        switch (c) {
        case 's':
            startup_cmd = optarg;
            break;
        default:
            printf("Usage: %s [-s startup command]\n", argv[0]);
            return 0;
        }
    }
    if (optind < argc) {
        printf("Usage: %s [-s startup command]\n", argv[0]);
        return 0;
    }

    struct tinywl_server server = {0};
    server.backend_destroyed = false;
    server.cascade_index = 0;
    server.cascade_step = 24; // pixels per new window
    
    server.last_icon_click_msec = 0;
    server.icon_click_count = 0;
    
    server.dragged_icon_node = NULL;
    server.icon_grab_dx = 0;
    server.icon_grab_dy = 0;
    server.icon_drag_candidate = false;
    server.icon_dragging = false;
    server.icon_press_x = 0;
    server.icon_press_y = 0;
    
    
    /* The Wayland display is managed by libwayland. It handles accepting
     * clients from the Unix socket, manging Wayland globals, and so on. */
    server.wl_display = wl_display_create();
    if (server.wl_display == NULL) {
        fprintf(stderr, "ADE: wl_display_create failed\n");
        return 1;
    }
    
    // Allow Ctrl-C / kill / hangup to exit the compositor cleanly.
    ade_global_display = server.wl_display;
    signal(SIGINT, ade_handle_signal);
    signal(SIGTERM, ade_handle_signal);
    signal(SIGHUP, ade_handle_signal);
    
    /* The backend is a wlroots feature which abstracts the underlying input and
     * output hardware. The autocreate option will choose the most suitable
     * backend based on the current environment, such as opening an X11 window
     * if an X11 server is running. */
    server.backend = wlr_backend_autocreate(wl_display_get_event_loop(server.wl_display), NULL);
    if (server.backend == NULL) {
        wlr_log(WLR_ERROR, "failed to create wlr_backend");
        wl_display_destroy(server.wl_display);
        return 1;
    }
    
    // Log if the backend shuts down under us (helps diagnose “starts then exits”)
    server.backend_destroy.notify = ade_backend_destroy_notify;
    wl_signal_add(&server.backend->events.destroy, &server.backend_destroy);
    

    /* Autocreates a renderer, either Pixman, GLES2 or Vulkan for us. The user
     * can also specify a renderer using the WLR_RENDERER env var.
     * The renderer is responsible for defining the various pixel formats it
     * supports for shared memory, this configures that for clients. */
    server.renderer = wlr_renderer_autocreate(server.backend);
    if (server.renderer == NULL) {
        wlr_log(WLR_ERROR, "failed to create wlr_renderer");
        if (!server.backend_destroyed) {
            ade_detach_backend_destroy_listener(&server);
            server.backend_destroyed = true;
        }
        if (server.backend != NULL) {
            wlr_backend_destroy(server.backend);
            server.backend = NULL;
        }
        wl_display_destroy(server.wl_display);
        return 1;
    }

    wlr_renderer_init_wl_display(server.renderer, server.wl_display);

    /* Autocreates an allocator for us.
     * The allocator is the bridge between the renderer and the backend. It
     * handles the buffer creation, allowing wlroots to render onto the
     * screen */
    server.allocator = wlr_allocator_autocreate(server.backend,
        server.renderer);
    if (server.allocator == NULL) {
        wlr_log(WLR_ERROR, "failed to create wlr_allocator");
        wlr_renderer_destroy(server.renderer);
        if (!server.backend_destroyed) {
            ade_detach_backend_destroy_listener(&server);
            server.backend_destroyed = true;
        }
        if (server.backend != NULL) {
            wlr_backend_destroy(server.backend);
            server.backend = NULL;
        }
        wl_display_destroy(server.wl_display);
        return 1;
    }

    /* This creates some hands-off wlroots interfaces. The compositor is
     * necessary for clients to allocate surfaces, the subcompositor allows to
     * assign the role of subsurfaces to surfaces and the data device manager
     * handles the clipboard. Each of these wlroots interfaces has room for you
     * to dig your fingers in and play with their behavior if you want. Note that
     * the clients cannot set the selection directly without compositor approval,
     * see the handling of the request_set_selection event below.*/
    wlr_compositor_create(server.wl_display, 5, server.renderer);
    wlr_subcompositor_create(server.wl_display);
    wlr_data_device_manager_create(server.wl_display);

    /* Creates an output layout, which a wlroots utility for working with an
     * arrangement of screens in a physical layout. */
    server.output_layout = wlr_output_layout_create(server.wl_display);
    if (server.output_layout == NULL) {
        wlr_log(WLR_ERROR, "ADE: failed to create output layout");
        wlr_allocator_destroy(server.allocator);
        wlr_renderer_destroy(server.renderer);
        if (!server.backend_destroyed) {
            ade_detach_backend_destroy_listener(&server);
            server.backend_destroyed = true;
        }
        if (server.backend != NULL) {
            wlr_backend_destroy(server.backend);
            server.backend = NULL;
        }
        wl_display_destroy(server.wl_display);
        return 1;
    }

    /* Configure a listener to be notified when new outputs are available on the
     * backend. */
    wl_list_init(&server.outputs);
    server.new_output.notify = server_new_output;
    wl_signal_add(&server.backend->events.new_output, &server.new_output);

    /* Create a scene graph. This is a wlroots abstraction that handles all
     * rendering and damage tracking. All the compositor author needs to do
     * is add things that should be rendered to the scene graph at the proper
     * positions and then call wlr_scene_output_commit() to render a frame if
     * necessary.
     */
    server.scene = wlr_scene_create();
    if (server.scene == NULL) {
        wlr_log(WLR_ERROR, "ADE: failed to create scene");
        wlr_output_layout_destroy(server.output_layout);
        wlr_allocator_destroy(server.allocator);
        wlr_renderer_destroy(server.renderer);
        if (!server.backend_destroyed) {
            ade_detach_backend_destroy_listener(&server);
            server.backend_destroyed = true;
        }
        if (server.backend != NULL) {
            wlr_backend_destroy(server.backend);
            server.backend = NULL;
        }
        wl_display_destroy(server.wl_display);
        return 1;
    }
    server.scene_layout = wlr_scene_attach_output_layout(server.scene, server.output_layout);
    if (server.scene_layout == NULL) {
        wlr_log(WLR_ERROR, "ADE: failed to attach scene to output layout");
        wlr_scene_node_destroy(&server.scene->tree.node);
        wlr_output_layout_destroy(server.output_layout);
        wlr_allocator_destroy(server.allocator);
        wlr_renderer_destroy(server.renderer);
        if (!server.backend_destroyed) {
            ade_detach_backend_destroy_listener(&server);
            server.backend_destroyed = true;
        }
        if (server.backend != NULL) {
            wlr_backend_destroy(server.backend);
            server.backend = NULL;
        }
        wl_display_destroy(server.wl_display);
        return 1;
    }

    // BeOS-like blue desktop background
    float beos_blue[4] = { 0.1608f, 0.3137f, 0.5255f, 1.0f };
    server.bg_rect = wlr_scene_rect_create(&server.scene->tree, 1920, 1080, beos_blue);
    if (server.bg_rect == NULL) {
        wlr_log(WLR_ERROR, "ADE: failed to create background rect");
        wlr_scene_node_destroy(&server.scene->tree.node);
        wlr_output_layout_destroy(server.output_layout);
        wlr_allocator_destroy(server.allocator);
        wlr_renderer_destroy(server.renderer);
        if (!server.backend_destroyed) {
            ade_detach_backend_destroy_listener(&server);
            server.backend_destroyed = true;
        }
        if (server.backend != NULL) {
            wlr_backend_destroy(server.backend);
            server.backend = NULL;
        }
        wl_display_destroy(server.wl_display);
        return 1;
    }
    wlr_scene_node_set_position(&server.bg_rect->node, 0, 0);
    wlr_scene_node_lower_to_bottom(&server.bg_rect->node);
    
    // Desktop icon layer
    server.desktop_icons = wlr_scene_tree_create(&server.scene->tree);
    
    // Desktop icons are loaded from a repo-local config so rsync can copy it with source.
    ade_load_desktop_icons_from_conf(&server, "desktop-icons.conf");

    // Icons are config-driven; remove hard-coded icons so double-click behavior
    // always uses the config's exec_cmd.
    server.terminal_icon_scene = NULL;
    server.terminal_icon_rect = NULL;

    wlr_scene_node_lower_to_bottom(&server.bg_rect->node);
    
    
    /* Set up xdg-shell version 3. The xdg-shell is a Wayland protocol which is
     * used for application windows. For more detail on shells, refer to
     * https://drewdevault.com/2018/07/29/Wayland-shells.html.
     */
    wl_list_init(&server.toplevels);
    server.xdg_shell = wlr_xdg_shell_create(server.wl_display, 3);
    if (server.xdg_shell == NULL) {
        wlr_log(WLR_ERROR, "ADE: failed to create xdg_shell");
        wlr_scene_node_destroy(&server.scene->tree.node);
        wlr_output_layout_destroy(server.output_layout);
        wlr_allocator_destroy(server.allocator);
        wlr_renderer_destroy(server.renderer);
        if (!server.backend_destroyed) {
            ade_detach_backend_destroy_listener(&server);
            server.backend_destroyed = true;
        }
        if (server.backend != NULL) {
            wlr_backend_destroy(server.backend);
            server.backend = NULL;
        }
        wl_display_destroy(server.wl_display);
        return 1;
    }
    server.new_xdg_toplevel.notify = server_new_xdg_toplevel;
    wl_signal_add(&server.xdg_shell->events.new_toplevel, &server.new_xdg_toplevel);
    server.new_xdg_popup.notify = server_new_xdg_popup;
    wl_signal_add(&server.xdg_shell->events.new_popup, &server.new_xdg_popup);
    
    
    // --- xdg-decoration (force server-side decorations / remove app titlebars) ---
    // Initialize the listener link so cleanup is safe even if the protocol isn't available.
    wl_list_init(&server.new_xdg_decoration.link);

    server.xdg_decoration_manager =
        wlr_xdg_decoration_manager_v1_create(server.wl_display);

    if (server.xdg_decoration_manager != NULL) {
        server.new_xdg_decoration.notify = server_new_xdg_decoration;
        wl_signal_add(&server.xdg_decoration_manager->events.new_toplevel_decoration,
                      &server.new_xdg_decoration);
    } else {
        wlr_log(WLR_ERROR,
            "ADE: xdg-decoration manager not available; clients may show their own titlebars");
    }
    

    /* Set up layer-shell. This is used by launchers/panels/overlays like fuzzel. */
    server.layer_shell = wlr_layer_shell_v1_create(server.wl_display, 4);
    if (server.layer_shell == NULL) {
        wlr_log(WLR_ERROR, "ADE: failed to create layer_shell");
        wlr_scene_node_destroy(&server.scene->tree.node);
        wlr_output_layout_destroy(server.output_layout);
        wlr_allocator_destroy(server.allocator);
        wlr_renderer_destroy(server.renderer);
        if (!server.backend_destroyed) {
            ade_detach_backend_destroy_listener(&server);
            server.backend_destroyed = true;
        }
        if (server.backend != NULL) {
            wlr_backend_destroy(server.backend);
            server.backend = NULL;
        }
        wl_display_destroy(server.wl_display);
        return 1;
    }
    server.new_layer_surface.notify = server_new_layer_surface;
    wl_signal_add(&server.layer_shell->events.new_surface, &server.new_layer_surface);

    /*
     * Creates a cursor, which is a wlroots utility for tracking the cursor
     * image shown on screen.
     */
    server.cursor = wlr_cursor_create();
    if (server.cursor == NULL) {
        wlr_log(WLR_ERROR, "ADE: failed to create cursor");
        wlr_scene_node_destroy(&server.scene->tree.node);
        wlr_output_layout_destroy(server.output_layout);
        wlr_allocator_destroy(server.allocator);
        wlr_renderer_destroy(server.renderer);

        // Prevent wlroots assertion: remove backend destroy listener before finishing backend
        if (!server.backend_destroyed) {
            ade_detach_backend_destroy_listener(&server);
            server.backend_destroyed = true;
        }
        if (server.backend != NULL) {
            wlr_backend_destroy(server.backend);
            server.backend = NULL;
        }

        wl_display_destroy(server.wl_display);
        return 1;
    }
    wlr_cursor_attach_output_layout(server.cursor, server.output_layout);

    /* Creates an xcursor manager, another wlroots utility which loads up
     * Xcursor themes to source cursor images from and makes sure that cursor
     * images are available at all scale factors on the screen (necessary for
     * HiDPI support). */
    server.cursor_mgr = wlr_xcursor_manager_create(NULL, 24);
    if (server.cursor_mgr == NULL) {
        wlr_log(WLR_ERROR, "ADE: failed to create xcursor manager");
        wlr_cursor_destroy(server.cursor);
        wlr_scene_node_destroy(&server.scene->tree.node);
        wlr_output_layout_destroy(server.output_layout);
        wlr_allocator_destroy(server.allocator);
        wlr_renderer_destroy(server.renderer);
        wlr_backend_destroy(server.backend);
        wl_display_destroy(server.wl_display);
        return 1;
    }
    // Start with a sane compositor cursor at scale=1; outputs will reload at their scale.
    ade_reload_cursor_for_output(&server, NULL);

    /*
     * wlr_cursor *only* displays an image on screen. It does not move around
     * when the pointer moves. However, we can attach input devices to it, and
     * it will generate aggregate events for all of them. In these events, we
     * can choose how we want to process them, forwarding them to clients and
     * moving the cursor around. More detail on this process is described in
     * https://drewdevault.com/2018/07/17/Input-handling-in-wlroots.html.
     *
     * And more comments are sprinkled throughout the notify functions above.
     */
    server.cursor_mode = TINYWL_CURSOR_PASSTHROUGH;
    server.cursor_motion.notify = server_cursor_motion;
    wl_signal_add(&server.cursor->events.motion, &server.cursor_motion);
    server.cursor_motion_absolute.notify = server_cursor_motion_absolute;
    wl_signal_add(&server.cursor->events.motion_absolute,
            &server.cursor_motion_absolute);
    server.cursor_button.notify = server_cursor_button;
    wl_signal_add(&server.cursor->events.button, &server.cursor_button);
    server.cursor_axis.notify = server_cursor_axis;
    wl_signal_add(&server.cursor->events.axis, &server.cursor_axis);
    server.cursor_frame.notify = server_cursor_frame;
    wl_signal_add(&server.cursor->events.frame, &server.cursor_frame);

    /*
     * Configures a seat, which is a single "seat" at which a user sits and
     * operates the computer. This conceptually includes up to one keyboard,
     * pointer, touch, and drawing tablet device. We also rig up a listener to
     * let us know when new input devices are available on the backend.
     */
    wl_list_init(&server.keyboards);
    server.new_input.notify = server_new_input;
    wl_signal_add(&server.backend->events.new_input, &server.new_input);
    server.seat = wlr_seat_create(server.wl_display, "seat0");
    if (server.seat == NULL) {
        wlr_log(WLR_ERROR, "ADE: failed to create seat");
        wlr_xcursor_manager_destroy(server.cursor_mgr);
        wlr_cursor_destroy(server.cursor);
        wlr_scene_node_destroy(&server.scene->tree.node);
        wlr_output_layout_destroy(server.output_layout);
        wlr_allocator_destroy(server.allocator);
        wlr_renderer_destroy(server.renderer);
        wlr_backend_destroy(server.backend);
        wl_display_destroy(server.wl_display);
        return 1;
    }
    server.request_cursor.notify = seat_request_cursor;
    wl_signal_add(&server.seat->events.request_set_cursor,
            &server.request_cursor);
    server.pointer_focus_change.notify = seat_pointer_focus_change;
    wl_signal_add(&server.seat->pointer_state.events.focus_change,
            &server.pointer_focus_change);
    server.request_set_selection.notify = seat_request_set_selection;
    wl_signal_add(&server.seat->events.request_set_selection,
            &server.request_set_selection);

    /* Add a Unix socket to the Wayland display. */
    const char *socket = wl_display_add_socket_auto(server.wl_display);
    if (!socket) {
        wlr_backend_destroy(server.backend);
        return 1;
    }

    /* Start the backend. This will enumerate outputs and inputs, become the DRM
     * master, etc */
    if (!wlr_backend_start(server.backend)) {
        wlr_backend_destroy(server.backend);
        wl_display_destroy(server.wl_display);
        return 1;
    }

    /* Set the WAYLAND_DISPLAY environment variable to our socket and run the
     * startup command if requested. */
    setenv("WAYLAND_DISPLAY", socket, true);

    /* Show help on startup so shortcuts are visible */
    ade_show_help();

    if (startup_cmd) {
        if (fork() == 0) {
            execl("/bin/sh", "/bin/sh", "-c", startup_cmd, (void *)NULL);
        }
    }
    /* Run the Wayland event loop. This does not return until you exit the
     * compositor. Starting the backend rigged up all of the necessary event
     * loop configuration to listen to libinput events, DRM events, generate
     * frame events at the refresh rate, and so on. */
    wlr_log(WLR_INFO, "Running Wayland compositor on WAYLAND_DISPLAY=%s",
            socket);
    wl_display_run(server.wl_display);
    wlr_log(WLR_ERROR, "ADE: wl_display_run returned (compositor exiting)");


    /* Once wl_display_run returns, we destroy all clients then shut down the
     * server. */
    wl_display_destroy_clients(server.wl_display);

    wl_list_remove(&server.new_xdg_toplevel.link);
    wl_list_remove(&server.new_xdg_popup.link);

    wl_list_remove(&server.new_layer_surface.link);

    wl_list_remove(&server.cursor_motion.link);
    wl_list_remove(&server.cursor_motion_absolute.link);
    wl_list_remove(&server.cursor_button.link);
    wl_list_remove(&server.cursor_axis.link);
    wl_list_remove(&server.cursor_frame.link);

    wl_list_remove(&server.new_input.link);
    wl_list_remove(&server.request_cursor.link);
    wl_list_remove(&server.pointer_focus_change.link);
    wl_list_remove(&server.request_set_selection.link);

    wl_list_remove(&server.new_output.link);

    // Only remove the decoration listener if it was actually registered
    if (server.xdg_decoration_manager != NULL &&
        server.new_xdg_decoration.link.next != &server.new_xdg_decoration.link &&
        server.new_xdg_decoration.link.prev != &server.new_xdg_decoration.link) {
        wl_list_remove(&server.new_xdg_decoration.link);
    }
    

    wlr_scene_node_destroy(&server.scene->tree.node);
    wlr_xcursor_manager_destroy(server.cursor_mgr);
    wlr_cursor_destroy(server.cursor);
    wlr_allocator_destroy(server.allocator);
    wlr_renderer_destroy(server.renderer);

    // Prevent wlroots assertion: remove backend destroy listener before finishing backend.
    // If the backend already died, ade_backend_destroy_notify() should have removed it and set backend=NULL.
    if (!server.backend_destroyed) {
        ade_detach_backend_destroy_listener(&server);
        server.backend_destroyed = true;
    }
    if (server.backend != NULL) {
        wlr_backend_destroy(server.backend);
        server.backend = NULL;
    }

    wl_display_destroy(server.wl_display);
    
    ade_global_display = NULL;
    
    
    server.last_clicked_icon = NULL;
    
    return 0;
}


