#pragma once
#include <stdbool.h>

struct tinywl_server;
struct tinywl_toplevel;

/*
 * Decorations backend interface.
 * The compositor MUST NOT know how decorations are drawn.
 */
struct ade_decorations_ops {
    // lifecycle
    bool (*init)(struct tinywl_server *server);
    void (*finish)(struct tinywl_server *server);

    // per-toplevel
    void (*toplevel_create)(struct tinywl_toplevel *toplevel);
    void (*toplevel_commit)(struct tinywl_toplevel *toplevel);
    void (*toplevel_destroy)(struct tinywl_toplevel *toplevel);

    // state updates
    void (*toplevel_set_title)(
        struct tinywl_toplevel *toplevel,
        const char *title);

    void (*toplevel_set_focused)(
        struct tinywl_toplevel *toplevel,
        bool focused);
};

/* Active decorations backend (never NULL) */
const struct ade_decorations_ops *ade_decorations_get_ops(void);
