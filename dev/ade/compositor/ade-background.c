#include <gtk/gtk.h>
#include <glib.h>
#include <stdio.h>

struct ade_background_app {
    GtkWidget *window;
    GtkColorChooser *chooser;
    GtkLabel *status_label;
    char *config_path;
};

static gboolean ade_load_saved_color(const char *path, GdkRGBA *out_color) {
    if (path == NULL || out_color == NULL) {
        return FALSE;
    }

    gchar *contents = NULL;
    gsize len = 0;
    if (!g_file_get_contents(path, &contents, &len, NULL) || contents == NULL) {
        return FALSE;
    }

    g_strstrip(contents);
    gboolean ok = FALSE;
    if (contents[0] != '\0') {
        ok = gdk_rgba_parse(out_color, contents);
    }

    g_free(contents);
    return ok;
}

static gboolean ade_save_color(const char *path, const GdkRGBA *color, gchar **error_text) {
    if (path == NULL || color == NULL) {
        if (error_text != NULL) {
            *error_text = g_strdup("No config path was provided.");
        }
        return FALSE;
    }

    int r = (int)(color->red * 255.0 + 0.5);
    int g = (int)(color->green * 255.0 + 0.5);
    int b = (int)(color->blue * 255.0 + 0.5);
    gchar *text = g_strdup_printf("#%02X%02X%02X\n", r, g, b);

    GError *error = NULL;
    gboolean ok = g_file_set_contents(path, text, -1, &error);
    if (!ok && error_text != NULL) {
        *error_text = g_strdup(error != NULL ? error->message :
            "Unable to save desktop background color.");
    }

    g_clear_error(&error);
    g_free(text);
    return ok;
}

static void ade_set_status(struct ade_background_app *app, const char *text) {
    gtk_label_set_text(app->status_label, text != NULL ? text : "");
}

static void ade_apply_clicked(GtkButton *button, gpointer user_data) {
    (void)button;
    struct ade_background_app *app = user_data;
    GdkRGBA color = {0};
    gtk_color_chooser_get_rgba(app->chooser, &color);

    gchar *error_text = NULL;
    if (ade_save_color(app->config_path, &color, &error_text)) {
        ade_set_status(app, "Saved. Restart ADE to apply the new background.");
    } else {
        ade_set_status(app, error_text != NULL ? error_text :
            "Unable to save the desktop background color.");
    }
    g_free(error_text);
}

int main(int argc, char **argv) {
    gtk_init(&argc, &argv);

    struct ade_background_app app = {0};
    app.config_path = g_strdup((argc > 1 && argv[1] != NULL && argv[1][0] != '\0') ?
        argv[1] : "desktop-background.conf");

    app.window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(app.window), "ADE Desktop Background");
    gtk_window_set_default_size(GTK_WINDOW(app.window), 360, 180);
    gtk_window_set_decorated(GTK_WINDOW(app.window), FALSE);
    gtk_window_set_resizable(GTK_WINDOW(app.window), FALSE);
    g_signal_connect(app.window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    GtkWidget *outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(outer), 14);
    gtk_container_add(GTK_CONTAINER(app.window), outer);

    GtkWidget *title = gtk_label_new("Choose a desktop background color");
    gtk_widget_set_halign(title, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(outer), title, FALSE, FALSE, 0);

    GtkWidget *chooser = gtk_color_chooser_widget_new();
    app.chooser = GTK_COLOR_CHOOSER(chooser);
    gtk_box_pack_start(GTK_BOX(outer), chooser, TRUE, TRUE, 0);

    GdkRGBA initial = { 0.1608, 0.3137, 0.5255, 1.0 };
    ade_load_saved_color(app.config_path, &initial);
    gtk_color_chooser_set_rgba(app.chooser, &initial);

    GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_pack_start(GTK_BOX(outer), actions, FALSE, FALSE, 0);

    app.status_label = GTK_LABEL(gtk_label_new("Saved colors apply on next ADE start."));
    gtk_widget_set_halign(GTK_WIDGET(app.status_label), GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(actions), GTK_WIDGET(app.status_label), TRUE, TRUE, 0);

    GtkWidget *apply = gtk_button_new_with_label("Apply");
    gtk_box_pack_end(GTK_BOX(actions), apply, FALSE, FALSE, 0);
    g_signal_connect(apply, "clicked", G_CALLBACK(ade_apply_clicked), &app);

    gtk_widget_show_all(app.window);
    gtk_main();

    g_free(app.config_path);
    return 0;
}
