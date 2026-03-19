#include <gtk/gtk.h>
#include <glib.h>
#include <stdlib.h>
#include <string.h>

struct ade_mode_entry {
    char *label;
    char *cli_value;
    gboolean current;
};

struct ade_output_entry {
    char *name;
    GPtrArray *modes;
    guint current_mode;
};

struct ade_resolution_app {
    GtkWidget *window;
    GtkComboBoxText *output_combo;
    GtkComboBoxText *mode_combo;
    GtkLabel *status_label;
    GtkButton *apply_button;
    GPtrArray *outputs;
};

static void ade_mode_entry_free(struct ade_mode_entry *mode) {
    if (mode == NULL) {
        return;
    }
    g_free(mode->label);
    g_free(mode->cli_value);
    g_free(mode);
}

static void ade_output_entry_free(struct ade_output_entry *output) {
    if (output == NULL) {
        return;
    }
    g_free(output->name);
    if (output->modes != NULL) {
        g_ptr_array_free(output->modes, TRUE);
    }
    g_free(output);
}

static GPtrArray *ade_outputs_new(void) {
    return g_ptr_array_new_with_free_func((GDestroyNotify)ade_output_entry_free);
}

static char *ade_trim_copy(const char *text) {
    if (text == NULL) {
        return g_strdup("");
    }
    char *copy = g_strdup(text);
    g_strstrip(copy);
    return copy;
}

static gboolean ade_parse_mode_line(const char *line,
        struct ade_mode_entry **out_mode) {
    if (line == NULL || out_mode == NULL) {
        return FALSE;
    }

    char resolution[64] = {0};
    char refresh[64] = {0};
    if (sscanf(line, "%63s px, %63s Hz", resolution, refresh) < 2) {
        return FALSE;
    }

    char *clean_refresh = ade_trim_copy(refresh);
    char *comma = strchr(clean_refresh, ',');
    if (comma != NULL) {
        *comma = '\0';
    }

    struct ade_mode_entry *mode = g_new0(struct ade_mode_entry, 1);
    mode->current = (strstr(line, "current") != NULL);
    mode->cli_value = g_strdup_printf("%s@%sHz", resolution, clean_refresh);
    mode->label = g_strdup_printf("%s @ %s Hz%s",
        resolution,
        clean_refresh,
        mode->current ? "  current" : "");

    g_free(clean_refresh);
    *out_mode = mode;
    return TRUE;
}

static gboolean ade_load_outputs(GPtrArray *outputs, char **error_text) {
    gchar *stdout_text = NULL;
    gchar *stderr_text = NULL;
    gint status = 0;
    GError *error = NULL;

    if (!g_spawn_command_line_sync("wlr-randr",
            &stdout_text, &stderr_text, &status, &error)) {
        if (error_text != NULL) {
            *error_text = g_strdup(error != NULL ? error->message :
                "Failed to start wlr-randr.");
        }
        g_clear_error(&error);
        g_free(stdout_text);
        g_free(stderr_text);
        return FALSE;
    }

    if (!g_spawn_check_wait_status(status, &error)) {
        if (error_text != NULL) {
            const char *stderr_msg = (stderr_text != NULL && stderr_text[0] != '\0') ?
                stderr_text : "wlr-randr returned an error.";
            *error_text = g_strdup(stderr_msg);
        }
        g_clear_error(&error);
        g_free(stdout_text);
        g_free(stderr_text);
        return FALSE;
    }

    gchar **lines = g_strsplit(stdout_text, "\n", -1);
    struct ade_output_entry *current_output = NULL;
    gboolean in_modes = FALSE;

    for (guint i = 0; lines[i] != NULL; i++) {
        const char *raw = lines[i];
        if (raw == NULL || raw[0] == '\0') {
            continue;
        }

        gboolean indented = g_ascii_isspace(raw[0]) != 0;
        char *trimmed = ade_trim_copy(raw);
        if (trimmed[0] == '\0') {
            g_free(trimmed);
            continue;
        }

        if (!indented) {
            char output_name[128] = {0};
            if (sscanf(trimmed, "%127s", output_name) == 1) {
                current_output = g_new0(struct ade_output_entry, 1);
                current_output->name = g_strdup(output_name);
                current_output->modes =
                    g_ptr_array_new_with_free_func((GDestroyNotify)ade_mode_entry_free);
                current_output->current_mode = 0;
                g_ptr_array_add(outputs, current_output);
                in_modes = FALSE;
            }
            g_free(trimmed);
            continue;
        }

        if (g_strcmp0(trimmed, "Modes:") == 0) {
            in_modes = TRUE;
            g_free(trimmed);
            continue;
        }

        if (!in_modes || current_output == NULL || !g_ascii_isdigit(trimmed[0])) {
            g_free(trimmed);
            continue;
        }

        struct ade_mode_entry *mode = NULL;
        if (ade_parse_mode_line(trimmed, &mode)) {
            if (mode->current) {
                current_output->current_mode = current_output->modes->len;
            }
            g_ptr_array_add(current_output->modes, mode);
        }

        g_free(trimmed);
    }

    g_strfreev(lines);
    g_free(stdout_text);
    g_free(stderr_text);

    if (outputs->len == 0) {
        if (error_text != NULL) {
            *error_text = g_strdup("No outputs or modes were reported by wlr-randr.");
        }
        return FALSE;
    }

    return TRUE;
}

static void ade_set_status(struct ade_resolution_app *app, const char *text) {
    gtk_label_set_text(app->status_label, text != NULL ? text : "");
}

static void ade_populate_modes(struct ade_resolution_app *app, guint output_index) {
    gtk_combo_box_text_remove_all(app->mode_combo);

    if (app->outputs == NULL || output_index >= app->outputs->len) {
        gtk_widget_set_sensitive(GTK_WIDGET(app->mode_combo), FALSE);
        gtk_widget_set_sensitive(GTK_WIDGET(app->apply_button), FALSE);
        return;
    }

    struct ade_output_entry *output = g_ptr_array_index(app->outputs, output_index);
    for (guint i = 0; i < output->modes->len; i++) {
        struct ade_mode_entry *mode = g_ptr_array_index(output->modes, i);
        gtk_combo_box_text_append_text(app->mode_combo, mode->label);
    }

    gtk_widget_set_sensitive(GTK_WIDGET(app->mode_combo), output->modes->len > 0);
    gtk_widget_set_sensitive(GTK_WIDGET(app->apply_button), output->modes->len > 0);
    if (output->modes->len > 0) {
        gtk_combo_box_set_active(GTK_COMBO_BOX(app->mode_combo), (gint)output->current_mode);
    }
}

static void ade_reload_outputs(struct ade_resolution_app *app) {
    if (app->outputs != NULL) {
        g_ptr_array_free(app->outputs, TRUE);
        app->outputs = NULL;
    }

    gtk_combo_box_text_remove_all(app->output_combo);
    gtk_combo_box_text_remove_all(app->mode_combo);

    app->outputs = ade_outputs_new();
    char *error_text = NULL;
    if (!ade_load_outputs(app->outputs, &error_text)) {
        ade_set_status(app, error_text != NULL ? error_text :
            "Unable to load display modes.");
        gtk_widget_set_sensitive(GTK_WIDGET(app->output_combo), FALSE);
        gtk_widget_set_sensitive(GTK_WIDGET(app->mode_combo), FALSE);
        gtk_widget_set_sensitive(GTK_WIDGET(app->apply_button), FALSE);
        g_free(error_text);
        return;
    }

    gtk_widget_set_sensitive(GTK_WIDGET(app->output_combo), TRUE);
    for (guint i = 0; i < app->outputs->len; i++) {
        struct ade_output_entry *output = g_ptr_array_index(app->outputs, i);
        gtk_combo_box_text_append_text(app->output_combo, output->name);
    }

    gtk_combo_box_set_active(GTK_COMBO_BOX(app->output_combo), 0);
    ade_populate_modes(app, 0);
    ade_set_status(app, "Choose a display mode and click Apply.");
}

static void ade_on_output_changed(GtkComboBox *combo, gpointer user_data) {
    struct ade_resolution_app *app = user_data;
    gint active = gtk_combo_box_get_active(combo);
    if (active < 0) {
        return;
    }
    ade_populate_modes(app, (guint)active);
}

static void ade_apply_resolution(struct ade_resolution_app *app) {
    gint output_index = gtk_combo_box_get_active(GTK_COMBO_BOX(app->output_combo));
    gint mode_index = gtk_combo_box_get_active(GTK_COMBO_BOX(app->mode_combo));
    if (output_index < 0 || mode_index < 0 || app->outputs == NULL) {
        ade_set_status(app, "No display mode selected.");
        return;
    }

    struct ade_output_entry *output = g_ptr_array_index(app->outputs, (guint)output_index);
    if ((guint)mode_index >= output->modes->len) {
        ade_set_status(app, "Selected mode is no longer available.");
        return;
    }

    struct ade_mode_entry *mode = g_ptr_array_index(output->modes, (guint)mode_index);
    gchar *quoted_output = g_shell_quote(output->name);
    gchar *quoted_mode = g_shell_quote(mode->cli_value);
    gchar *command = g_strdup_printf("wlr-randr --output %s --mode %s",
        quoted_output, quoted_mode);

    gchar *stdout_text = NULL;
    gchar *stderr_text = NULL;
    gint status = 0;
    GError *error = NULL;

    if (!g_spawn_command_line_sync(command, &stdout_text, &stderr_text, &status, &error)) {
        ade_set_status(app, error != NULL ? error->message : "Failed to run wlr-randr.");
        g_clear_error(&error);
    } else if (!g_spawn_check_wait_status(status, &error)) {
        ade_set_status(app,
            (stderr_text != NULL && stderr_text[0] != '\0') ?
            stderr_text : (error != NULL ? error->message : "Resolution change failed."));
        g_clear_error(&error);
    } else {
        ade_set_status(app, "Display mode applied.");
        ade_reload_outputs(app);
    }

    g_free(stdout_text);
    g_free(stderr_text);
    g_free(command);
    g_free(quoted_output);
    g_free(quoted_mode);
}

static void ade_on_apply_clicked(GtkButton *button, gpointer user_data) {
    (void)button;
    ade_apply_resolution(user_data);
}

static void ade_on_refresh_clicked(GtkButton *button, gpointer user_data) {
    (void)button;
    ade_reload_outputs(user_data);
}

static void ade_on_window_destroy(GtkWidget *widget, gpointer user_data) {
    (void)widget;
    struct ade_resolution_app *app = user_data;
    if (app->outputs != NULL) {
        g_ptr_array_free(app->outputs, TRUE);
    }
    g_free(app);
    gtk_main_quit();
}

int main(int argc, char **argv) {
    gtk_init(&argc, &argv);

    struct ade_resolution_app *app = g_new0(struct ade_resolution_app, 1);

    app->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(app->window), "Screen");
    gtk_window_set_default_size(GTK_WINDOW(app->window), 360, 170);
    gtk_window_set_decorated(GTK_WINDOW(app->window), FALSE);
    gtk_window_set_resizable(GTK_WINDOW(app->window), FALSE);
    gtk_container_set_border_width(GTK_CONTAINER(app->window), 12);

    GtkWidget *outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_add(GTK_CONTAINER(app->window), outer);

    GtkWidget *frame = gtk_frame_new("Resolution");
    gtk_box_pack_start(GTK_BOX(outer), frame, TRUE, TRUE, 0);

    GtkWidget *grid = gtk_grid_new();
    gtk_container_set_border_width(GTK_CONTAINER(grid), 10);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 10);
    gtk_container_add(GTK_CONTAINER(frame), grid);

    GtkWidget *output_label = gtk_label_new("Display:");
    gtk_widget_set_halign(output_label, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(grid), output_label, 0, 0, 1, 1);

    app->output_combo = GTK_COMBO_BOX_TEXT(gtk_combo_box_text_new());
    gtk_widget_set_hexpand(GTK_WIDGET(app->output_combo), TRUE);
    gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(app->output_combo), 1, 0, 1, 1);

    GtkWidget *mode_label = gtk_label_new("Mode:");
    gtk_widget_set_halign(mode_label, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(grid), mode_label, 0, 1, 1, 1);

    app->mode_combo = GTK_COMBO_BOX_TEXT(gtk_combo_box_text_new());
    gtk_widget_set_hexpand(GTK_WIDGET(app->mode_combo), TRUE);
    gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(app->mode_combo), 1, 1, 1, 1);

    app->status_label = GTK_LABEL(gtk_label_new(""));
    gtk_widget_set_halign(GTK_WIDGET(app->status_label), GTK_ALIGN_START);
    gtk_label_set_xalign(app->status_label, 0.0f);
    gtk_box_pack_start(GTK_BOX(outer), GTK_WIDGET(app->status_label), FALSE, FALSE, 0);

    GtkWidget *buttons = gtk_button_box_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_button_box_set_layout(GTK_BUTTON_BOX(buttons), GTK_BUTTONBOX_END);
    gtk_box_set_spacing(GTK_BOX(buttons), 8);
    gtk_box_pack_end(GTK_BOX(outer), buttons, FALSE, FALSE, 0);

    GtkWidget *refresh_button = gtk_button_new_with_label("Refresh");
    gtk_container_add(GTK_CONTAINER(buttons), refresh_button);

    app->apply_button = GTK_BUTTON(gtk_button_new_with_label("Apply"));
    gtk_container_add(GTK_CONTAINER(buttons), GTK_WIDGET(app->apply_button));

    g_signal_connect(app->window, "destroy", G_CALLBACK(ade_on_window_destroy), app);
    g_signal_connect(app->output_combo, "changed", G_CALLBACK(ade_on_output_changed), app);
    g_signal_connect(refresh_button, "clicked", G_CALLBACK(ade_on_refresh_clicked), app);
    g_signal_connect(app->apply_button, "clicked", G_CALLBACK(ade_on_apply_clicked), app);

    ade_reload_outputs(app);

    gtk_widget_show_all(app->window);
    gtk_main();
    return 0;
}
