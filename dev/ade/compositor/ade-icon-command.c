#include <gtk/gtk.h>
#include <glib.h>

struct ade_icon_command_app {
    GtkWidget *window;
    GtkEntry *command_entry;
    GtkLabel *status_label;
    char *title;
    char *config_path;
};

static void ade_set_status(struct ade_icon_command_app *app, const char *text) {
    gtk_label_set_text(app->status_label, text != NULL ? text : "");
}

static gboolean ade_save_icon_command(const char *config_path,
        const char *title, const char *command, gchar **error_text) {
    if (config_path == NULL || title == NULL || command == NULL) {
        if (error_text != NULL) {
            *error_text = g_strdup("Missing icon command editor arguments.");
        }
        return FALSE;
    }

    GKeyFile *keyfile = g_key_file_new();
    GError *error = NULL;
    if (g_file_test(config_path, G_FILE_TEST_EXISTS)) {
        g_key_file_load_from_file(keyfile, config_path, G_KEY_FILE_NONE, NULL);
    }

    g_key_file_set_string(keyfile, title, "command", command);
    gchar *data = g_key_file_to_data(keyfile, NULL, &error);
    if (data == NULL) {
        if (error_text != NULL) {
            *error_text = g_strdup(error != NULL ? error->message :
                "Unable to serialize command override.");
        }
        g_clear_error(&error);
        g_key_file_unref(keyfile);
        return FALSE;
    }

    gboolean ok = g_file_set_contents(config_path, data, -1, &error);
    if (!ok && error_text != NULL) {
        *error_text = g_strdup(error != NULL ? error->message :
            "Unable to save icon command override.");
    }

    g_clear_error(&error);
    g_free(data);
    g_key_file_unref(keyfile);
    return ok;
}

static void ade_save_clicked(GtkButton *button, gpointer user_data) {
    (void)button;
    struct ade_icon_command_app *app = user_data;
    const char *command = gtk_entry_get_text(app->command_entry);

    gchar *error_text = NULL;
    if (ade_save_icon_command(app->config_path, app->title, command, &error_text)) {
        ade_set_status(app, "Saved. The desktop icon will use the new command immediately.");
    } else {
        ade_set_status(app, error_text != NULL ? error_text :
            "Unable to save the icon command.");
    }
    g_free(error_text);
}

int main(int argc, char **argv) {
    gtk_init(&argc, &argv);

    struct ade_icon_command_app app = {0};
    app.title = g_strdup((argc > 1 && argv[1] != NULL) ? argv[1] : "Desktop Application");
    char *initial_command = g_strdup((argc > 2 && argv[2] != NULL) ? argv[2] : "");
    app.config_path = g_strdup((argc > 3 && argv[3] != NULL && argv[3][0] != '\0') ?
        argv[3] : "desktop-icon-commands.ini");

    app.window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(app.window), "ADE Icon Command");
    gtk_window_set_default_size(GTK_WINDOW(app.window), 520, 170);
    gtk_window_set_decorated(GTK_WINDOW(app.window), FALSE);
    gtk_window_set_resizable(GTK_WINDOW(app.window), FALSE);
    g_signal_connect(app.window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    GtkWidget *outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(outer), 14);
    gtk_container_add(GTK_CONTAINER(app.window), outer);

    GtkWidget *title_label =
        gtk_label_new(NULL);
    char *title_markup = g_markup_printf_escaped("<b>%s</b>", app.title);
    gtk_label_set_markup(GTK_LABEL(title_label), title_markup);
    gtk_widget_set_halign(title_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(outer), title_label, FALSE, FALSE, 0);
    g_free(title_markup);

    GtkWidget *hint =
        gtk_label_new("Edit the command this desktop icon runs when double-clicked.");
    gtk_widget_set_halign(hint, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(outer), hint, FALSE, FALSE, 0);

    app.command_entry = GTK_ENTRY(gtk_entry_new());
    gtk_entry_set_text(app.command_entry, initial_command);
    gtk_box_pack_start(GTK_BOX(outer), GTK_WIDGET(app.command_entry), FALSE, FALSE, 0);

    GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_pack_start(GTK_BOX(outer), actions, FALSE, FALSE, 0);

    app.status_label = GTK_LABEL(gtk_label_new(" "));
    gtk_widget_set_halign(GTK_WIDGET(app.status_label), GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(actions), GTK_WIDGET(app.status_label), TRUE, TRUE, 0);

    GtkWidget *save_button = gtk_button_new_with_label("Save");
    gtk_box_pack_end(GTK_BOX(actions), save_button, FALSE, FALSE, 0);
    g_signal_connect(save_button, "clicked", G_CALLBACK(ade_save_clicked), &app);

    gtk_widget_show_all(app.window);
    gtk_main();

    g_free(app.title);
    g_free(initial_command);
    g_free(app.config_path);
    return 0;
}
