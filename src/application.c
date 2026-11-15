
#include "music/application.h"
#include "music/window.h"


static void music_application_show_about(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    (void) action;
    (void) parameter;

    GtkApplication *app = GTK_APPLICATION(user_data);
    GtkWindow *parent = gtk_application_get_active_window(app);
    if (parent == NULL) return;

    AdwAboutDialog *dialog = ADW_ABOUT_DIALOG(adw_about_dialog_new());
    adw_about_dialog_set_application_name(dialog, "Music");
    g_autofree gchar *icon_path = g_build_filename(g_get_current_dir(), "data", "icons", NULL);
    gtk_icon_theme_add_search_path(gtk_icon_theme_get_for_display(gdk_display_get_default()), icon_path);
    adw_about_dialog_set_application_icon(dialog, "io.github.sametbilir33.Music");
    adw_about_dialog_set_version(dialog, "0.1.0");
    adw_about_dialog_set_developer_name(dialog, "Samet Bilir");
    adw_about_dialog_set_comments(dialog, "A native Linux music player built with GTK4, Libadwaita, and GStreamer.");

    adw_about_dialog_set_license_type(dialog, GTK_LICENSE_GPL_3_0);
    adw_about_dialog_set_website(dialog, "https://github.com/sametbilir33/Music");
    adw_about_dialog_set_issue_url(dialog, "https://github.com/sametbilir33/Music/issues");

    adw_dialog_present(ADW_DIALOG(dialog), GTK_WIDGET(parent));
}

static void music_application_show_shortcuts(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    (void) action;
    (void) parameter;

    GtkApplication *app = GTK_APPLICATION(user_data);
    GtkWindow *parent = gtk_application_get_active_window(app);
    if (parent == NULL) return;

    AdwShortcutsDialog *dialog = ADW_SHORTCUTS_DIALOG(adw_shortcuts_dialog_new());
    AdwShortcutsSection *section = adw_shortcuts_section_new("Genel");

    AdwShortcutsItem *menu_item = adw_shortcuts_item_new("Ana Menüyü Aç", "F10");
    adw_shortcuts_section_add(section, menu_item);
    g_object_unref(menu_item);

    AdwShortcutsItem *shortcuts_item = adw_shortcuts_item_new("Klavye Kısayollarını Aç", "<Control>question");
    adw_shortcuts_section_add(section, shortcuts_item);
    g_object_unref(shortcuts_item);

    adw_shortcuts_dialog_add(dialog, section);
    g_object_unref(section);

    adw_dialog_set_title(ADW_DIALOG(dialog), "Klavye Kısayolları");
    adw_dialog_present(ADW_DIALOG(dialog), GTK_WIDGET(parent));
}

void music_application_activate(GtkApplication *app, gpointer user_data)
{
    (void) user_data;

    if (g_action_map_lookup_action(G_ACTION_MAP(app), "about") == NULL) {
        const GActionEntry entries[] = {
            { .name = "about", .activate = music_application_show_about },
            { .name = "shortcuts", .activate = music_application_show_shortcuts }
        };

        g_action_map_add_action_entries(G_ACTION_MAP(app), entries, G_N_ELEMENTS(entries), app);

        const gchar *shortcuts_accels[] = { "<Control>question", NULL };
        const gchar *about_accels[] = { "<Control>comma", NULL };
        gtk_application_set_accels_for_action(app, "app.shortcuts", shortcuts_accels);
        gtk_application_set_accels_for_action(app, "app.about", about_accels);
    }

    GtkWindow *window = music_window_new(app);
    gtk_window_present(window);
}
