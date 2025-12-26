#pragma once

#include <stdbool.h>
#include <stdint.h>

struct tinywl_server;     // forward decl (defined in ade-compositor.c)
struct wlr_scene_node;    // from wlroots

// One desktop icon loaded from config
struct ade_desktop_icon {
    char *id;        // optional stable identifier
    char *title;     // label text (shown under icon when present)
    char *png_path;  // path to icon image
    char *exec_cmd;  // command to run on double-click
    int x;
    int y;

    // Scene nodes created for this icon
    struct wlr_scene_node *node;       // main icon node (buffer/rect)
    struct wlr_scene_node *label_node; // text label node (may be NULL)
    struct wlr_scene_node *select_node; // blue selection lozenge node (may be NULL)

    // Cached label size (used for selection sizing)
    int label_w;
    int label_h;

    // Selection state
    bool selected;
};

struct ade_desktop_icons {
    struct ade_desktop_icon *items;
    int count;
};

bool ade_desktop_icons_init(struct tinywl_server *server);
void ade_desktop_icons_destroy(struct tinywl_server *server);

// Reload icons from config file (destroys old, creates new)
bool ade_desktop_icons_reload(struct tinywl_server *server, const char *conf_path);

// Returns true if the press hit an icon; fills out_icon_node if you want drag support.
bool ade_desktop_icons_hit_test(struct tinywl_server *server,
                                struct wlr_scene_node *hit_node,
                                struct wlr_scene_node **out_icon_node);

// Call when a double-click is detected on an icon node
void ade_desktop_icons_launch_for_node(struct tinywl_server *server,
                                       struct wlr_scene_node *icon_node);

// Optional: expose where the config was loaded from (for logging/UI)
const char *ade_desktop_icons_last_path(struct tinywl_server *server);

void ade_desktop_icons_select_for_node(struct tinywl_server *server,
                                       struct wlr_scene_node *icon_node);
