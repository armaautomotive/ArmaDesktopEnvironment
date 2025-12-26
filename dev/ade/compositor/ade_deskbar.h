#pragma once

#include <stdbool.h>
#include <stdint.h>

/*
 * ADE Deskbar (Haiku-like top-right bar)
 *
 * This module owns the deskbar scene subtree and its state.
 * The compositor should:
 *  - call ade_deskbar_init() after creating server.scene
 *  - call ade_deskbar_handle_pointer_button() before normal window click logic
 *  - call ade_deskbar_on_toplevel_mapped/unmapped() when toplevels appear/disappear
 */

/* Forward declarations to avoid pulling large wlroots headers into every TU */
struct tinywl_server;
struct tinywl_toplevel;
struct wlr_output;

struct ade_deskbar;

/* Create and attach the deskbar to the compositor scene. Returns true on success. */
bool ade_deskbar_init(struct tinywl_server *server);

/* Destroy any deskbar state/resources. Safe to call even if init failed. */
void ade_deskbar_finish(struct tinywl_server *server);

/* Recompute deskbar size/position. Call on output resize or app list change. */
void ade_deskbar_update_layout(struct tinywl_server *server, struct wlr_output *output);

/*
 * Pointer handling
 *
 * Returns true if the deskbar consumed the click (so the compositor should NOT
 * pass it through to window focus/move/resize logic).
 */
bool ade_deskbar_handle_pointer_button(struct tinywl_server *server,
                                       uint32_t button,
                                       bool pressed);

/* Notify deskbar that a window/app (toplevel) has appeared/disappeared. */
void ade_deskbar_on_toplevel_mapped(struct tinywl_server *server, struct tinywl_toplevel *toplevel);
void ade_deskbar_on_toplevel_unmapped(struct tinywl_server *server, struct tinywl_toplevel *toplevel);

/* Optional: update focus indicator (if you want the deskbar to show focus). */
void ade_deskbar_on_focus_changed(struct tinywl_server *server, struct tinywl_toplevel *focused);
