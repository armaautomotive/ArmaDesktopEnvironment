#include "desktop_icons.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include <wlr/types/wlr_scene.h>
#include <wlr/util/log.h>

// You already have these helpers in ade-compositor.c.
// Easiest is to move them into a small "util.h/.c" later,
// but for now declare them extern so we can call them.
struct wlr_buffer *ade_load_png_as_wlr_buffer(const char *path);
void ade_launch_terminal(void);                // (or replace with spawn shell)
void ade_spawn_shell(const char *sh_cmd);      // if you want arbitrary commands
bool ade_node_is_or_ancestor(struct wlr_scene_node *node, struct wlr_scene_node *target);

// Add these fields to tinywl_server (recommended):
//   struct wlr_scene_tree *desktop_icons;   // already present in your file
//   struct ade_desktop_icons desktop_icons_state;
//   char desktop_icons_conf_path[512];

static char g_last_path[512] = {0};

static void free_icon(struct ade_desktop_icon *ic) {
    if (!ic) return;
    free(ic->id);
    free(ic->title);
    free(ic->png_path);
    free(ic->exec_cmd);
    ic->id = ic->title = ic->png_path = ic->exec_cmd = NULL;
    ic->node = NULL;
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
// x y png_path | exec command...
// Example:
// 40 60 ./icons/terminal.png | foot
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

    // left: "x y png_path"
    if (sscanf(left, "%d %d %383s", &x, &y, png) != 3) {
        return false;
    }

    out->x = x;
    out->y = y;
    out->png_path = strdup(png);
    out->exec_cmd = strdup(right);

    // optional defaults
    out->title = NULL;
    out->id = NULL;
    out->node = NULL;

    return out->png_path && out->exec_cmd;
}

static void build_icon_scene(struct tinywl_server *server, struct ade_desktop_icon *ic) {
    if (!server || !server->desktop_icons || !ic) return;

    struct wlr_buffer *buf = ade_load_png_as_wlr_buffer(ic->png_path);
    if (buf) {
        struct wlr_scene_buffer *sb = wlr_scene_buffer_create(server->desktop_icons, buf);
        wlr_buffer_drop(buf); // scene holds a ref

        wlr_scene_node_set_position(&sb->node, ic->x, ic->y);
        ic->node = &sb->node;
        return;
    }

    // fallback: draw a simple rectangle if PNG failed
    const float col[4] = { 0.75f, 0.75f, 0.75f, 1.0f };
    struct wlr_scene_rect *r = wlr_scene_rect_create(server->desktop_icons, 48, 48, col);
    wlr_scene_node_set_position(&r->node, ic->x, ic->y);
    ic->node = &r->node;
}

bool ade_desktop_icons_init(struct tinywl_server *server) {
    (void)server;
    g_last_path[0] = '\0';
    return true;
}

void ade_desktop_icons_destroy(struct tinywl_server *server) {
    if (!server) return;
    // If you store icons in server->desktop_icons_state, clear it here.
    // clear_icons(&server->desktop_icons_state);
}

bool ade_desktop_icons_reload(struct tinywl_server *server, const char *conf_path) {
    if (!server || !conf_path || !server->desktop_icons) return false;

    FILE *f = fopen(conf_path, "r");
    if (!f) {
        wlr_log(WLR_ERROR, "ADE: failed to open desktop icons config: %s", conf_path);
        return false;
    }

    // Destroy the old icon scene tree entirely and recreate it (simple + reliable).
    // NOTE: This assumes server->desktop_icons is ONLY for desktop icons.
    wlr_scene_node_destroy(&server->desktop_icons->node);
    server->desktop_icons = wlr_scene_tree_create(&server->scene->tree);

    // If you want icons above background but below windows/deskbar, you can raise/lower later.
    // wlr_scene_node_raise_to_top(&server->desktop_icons->node);

    struct ade_desktop_icons set = {0};

    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        struct ade_desktop_icon ic = {0};
        if (!parse_line(line, &ic)) continue;

        set.items = realloc(set.items, sizeof(set.items[0]) * (set.count + 1));
        set.items[set.count++] = ic;
    }
    fclose(f);

    // Build scene nodes
    for (int i = 0; i < set.count; i++) {
        build_icon_scene(server, &set.items[i]);
    }

    snprintf(g_last_path, sizeof(g_last_path), "%s", conf_path);

    // If you store it on server, copy it there:
    // server->desktop_icons_state = set;
    // snprintf(server->desktop_icons_conf_path, sizeof(server->desktop_icons_conf_path), "%s", conf_path);

    // If NOT storing on server yet, free set here after build (but then hit-test won’t know exec_cmd).
    // For real use, store it on server.
    //
    // For now, I recommend storing it on server, so comment out:
    // clear_icons(&set);

    return true;
}

bool ade_desktop_icons_hit_test(struct tinywl_server *server,
                                struct wlr_scene_node *hit_node,
                                struct wlr_scene_node **out_icon_node) {
    if (out_icon_node) *out_icon_node = NULL;
    if (!server || !hit_node) return false;

    // If you keep your icon nodes under server->desktop_icons,
    // anything under that tree is an icon hit.
    if (!server->desktop_icons) return false;
    if (ade_node_is_or_ancestor(hit_node, &server->desktop_icons->node)) {
        // We want the “top-level icon node” if possible; for now return the hit node.
        if (out_icon_node) *out_icon_node = hit_node;
        return true;
    }
    return false;
}

void ade_desktop_icons_launch_for_node(struct tinywl_server *server,
                                       struct wlr_scene_node *icon_node) {
    if (!server || !icon_node) return;

    // If you store the icon list on server, lookup icon_node -> exec_cmd.
    // Example (pseudo):
    // for each icon in server->desktop_icons_state:
    //   if (icon.node == icon_node || ade_node_is_or_ancestor(icon_node, icon.node)) launch icon.exec_cmd

    // Temporary fallback:
    // ade_launch_terminal();

    // Better:
    // ade_spawn_shell(icon->exec_cmd);
    (void)server;
    (void)icon_node;
}

const char *ade_desktop_icons_last_path(struct tinywl_server *server) {
    (void)server;
    return g_last_path[0] ? g_last_path : NULL;
}

