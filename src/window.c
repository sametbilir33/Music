#include "music/window.h"
#include "music/player.h"
#include "music/visualizer.h"
#include "music/playlist.h"
#include "music/config.h"
#include "music/mpris.h"

typedef struct {
    GtkWindow *window;
    GtkListBox *list_box;
    GtkSearchEntry *search_entry;
    AdwOverlaySplitView *split_view;
    GtkLabel *current_label;
    GtkButton *play_button;
    MusicPlayer *player;
    MusicPlaylist *playlist;
    MusicMpris *mpris;
    MusicConfig config;
    GtkScale *progress_scale;
    GtkScale *volume_scale;
    GtkLabel *time_label;
    guint update_timer_id;
    GtkButton *shuffle_button;
    GtkButton *repeat_button;
    gboolean shuffle_enabled;
    guint repeat_mode;
    GArray *shuffle_order;
    guint shuffle_position;
} MusicWindowState;

static gchar *music_window_format_time(gint64 nanoseconds);
static gboolean music_window_update_progress(gpointer user_data);
static gboolean music_window_on_seek(GtkRange *range, GtkScrollType scroll, gdouble value, gpointer user_data);
static void music_window_on_volume_changed(GtkRange *range, gpointer user_data);
static GtkWidget *music_window_create_menu(void);
static GtkWidget *music_window_create_playlist_menu(void);
static void music_window_refresh_playlist(MusicWindowState *state);
static void music_window_update_current_label(MusicWindowState *state);
static void music_window_save_playlist(MusicWindowState *state);
static gboolean music_window_filter_row(GtkListBoxRow *row, gpointer user_data);
static void music_window_on_search_changed(GtkSearchEntry *entry, gpointer user_data);
static void music_window_select_current_track(MusicWindowState *state);
static void music_window_shuffle_reset(MusicWindowState *state);
static void music_window_play_current(MusicWindowState *state);
static void music_window_mpris_set_shuffle(gboolean enabled, gpointer user_data);
static void music_window_mpris_set_repeat(guint mode, gpointer user_data);

static gboolean music_window_filter_row(GtkListBoxRow *row, gpointer user_data) {
    MusicWindowState *state = user_data;
    const gchar *query = gtk_editable_get_text(GTK_EDITABLE(state->search_entry));
    if (query == NULL || *query == '\0') return TRUE;
    guint index = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(row), "track-index")) - 1;
    const gchar *uri = music_playlist_get_track(state->playlist, index);
    if (uri == NULL) return FALSE;
    g_autofree gchar *path = g_filename_from_uri(uri, NULL, NULL);
    g_autofree gchar *basename = path != NULL ? g_path_get_basename(path) : g_strdup(uri);
    g_autofree gchar *folded_name = g_utf8_casefold(basename, -1);
    g_autofree gchar *folded_query = g_utf8_casefold(query, -1);
    return g_strstr_len(folded_name, -1, folded_query) != NULL;
}

static void music_window_on_search_changed(GtkSearchEntry *entry, gpointer user_data) {
    (void) entry;
    MusicWindowState *state = user_data;
    gtk_list_box_invalidate_filter(state->list_box);
}

static void music_window_apply_sort(MusicWindowState *state, MusicPlaylistSort sort)
{
    if (music_playlist_get_mode(state->playlist) != MUSIC_PLAYLIST_MODE_DIRECTORY) return;
    music_playlist_sort(state->playlist, sort);
    state->config.sort = sort;
    music_config_save(&state->config, NULL);
    music_window_refresh_playlist(state);
    music_window_shuffle_reset(state);
}

static void music_window_on_sort_action(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    (void) parameter;
    MusicWindowState *state = user_data;
    const gchar *name = g_action_get_name(G_ACTION(action));

    if (g_str_equal(name, "sort-asc")) music_window_apply_sort(state, MUSIC_PLAYLIST_SORT_NAME_ASC);
    else if (g_str_equal(name, "sort-desc")) music_window_apply_sort(state, MUSIC_PLAYLIST_SORT_NAME_DESC);
    else if (g_str_equal(name, "sort-newest")) music_window_apply_sort(state, MUSIC_PLAYLIST_SORT_DATE_NEWEST);
    else if (g_str_equal(name, "sort-oldest")) music_window_apply_sort(state, MUSIC_PLAYLIST_SORT_DATE_OLDEST);
}

static void music_window_on_remove_selected(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    (void) action;
    (void) parameter;

    MusicWindowState *state = user_data;
    if (music_playlist_get_mode(state->playlist) != MUSIC_PLAYLIST_MODE_DIRECTORY) return;

    GtkListBoxRow *row = gtk_list_box_get_selected_row(state->list_box);
    if (row == NULL) return;

    guint index = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(row), "track-index")) - 1;
    const gchar *removed_track = music_playlist_get_track(state->playlist, index);
    const gchar *playing_track = music_player_get_current_uri(state->player);

    gboolean removed_current_track = index == music_playlist_get_index(state->playlist) && g_strcmp0(removed_track, playing_track) == 0;
    gboolean removed_last_track = g_strcmp0(removed_track, state->config.last_track) == 0;

    if (!music_playlist_remove(state->playlist, index)) return;

    if (removed_last_track) g_clear_pointer(&state->config.last_track, g_free);

    music_window_save_playlist(state);
    music_window_refresh_playlist(state);
    music_window_shuffle_reset(state);

    if (removed_current_track) {
        if (music_playlist_get_count(state->playlist) > 0) {
            music_window_play_current(state);
        } else {
            music_player_stop(state->player);
            music_player_clear_current_uri(state->player);
            gtk_list_box_unselect_all(state->list_box);
            gtk_label_set_text(state->current_label, "Şu anda çalan parça yok");
            music_mpris_update(state->mpris);
        }
    } else {
        music_window_select_current_track(state);
        music_window_update_current_label(state);
        music_mpris_update(state->mpris);
    }
}

static void music_window_on_clear_playlist(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    (void) action;
    (void) parameter;

    MusicWindowState *state = user_data;
    if (music_playlist_get_mode(state->playlist) != MUSIC_PLAYLIST_MODE_DIRECTORY) return;

    music_playlist_clear(state->playlist);
    music_player_stop(state->player);
    music_player_clear_current_uri(state->player);

    g_clear_pointer(&state->config.last_track, g_free);
    music_window_save_playlist(state);
    music_window_refresh_playlist(state);
    gtk_list_box_unselect_all(state->list_box);
    music_window_update_current_label(state);
    music_mpris_update(state->mpris);
    music_window_shuffle_reset(state);
}

static void music_window_on_settings_sort_changed(GObject *object, GParamSpec *pspec, gpointer user_data)
{
    (void) pspec;

    MusicWindowState *state = user_data;
    guint selected = adw_combo_row_get_selected(ADW_COMBO_ROW(object));

    if (selected <= 3) music_window_apply_sort(state, (MusicPlaylistSort)(selected + 1));
}


static void music_window_show_settings(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    (void) action;
    (void) parameter;

    MusicWindowState *state = user_data;
    AdwPreferencesDialog *preferences = ADW_PREFERENCES_DIALOG(adw_preferences_dialog_new());

    adw_dialog_set_title(ADW_DIALOG(preferences), "Ayarlar");

    AdwPreferencesPage *page = ADW_PREFERENCES_PAGE(adw_preferences_page_new());
    AdwPreferencesGroup *group = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(group, "Çalma Listesi");

    GtkStringList *sort_model = gtk_string_list_new((const gchar * const[]) { "A-Z", "Z-A", "En yeni önce", "En eski önce", NULL });
    AdwComboRow *sort_row = ADW_COMBO_ROW(adw_combo_row_new());

    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(sort_row), "Sıralama");
    adw_combo_row_set_model(sort_row, G_LIST_MODEL(sort_model));
    adw_combo_row_set_selected(sort_row, CLAMP(state->config.sort - 1, 0, 3));

    g_signal_connect(sort_row, "notify::selected", G_CALLBACK(music_window_on_settings_sort_changed), state);

    adw_preferences_group_add(group, GTK_WIDGET(sort_row));
    adw_preferences_page_add(page, group);
    adw_preferences_dialog_add(preferences, page);

    adw_dialog_present(ADW_DIALOG(preferences), GTK_WIDGET(state->window));

    g_object_unref(sort_model);
}

static void music_window_show_error(MusicWindowState *state, const gchar *message)
{
    GtkAlertDialog *dialog = gtk_alert_dialog_new("%s", message);
    gtk_alert_dialog_show(dialog, state->window);
    g_object_unref(dialog);
}

static void music_window_update_current_label(MusicWindowState *state)
{
    const gchar *uri = music_player_get_current_uri(state->player);
    if (uri == NULL) uri = music_playlist_get_current(state->playlist);

    if (uri == NULL) {
        gtk_label_set_text(state->current_label, "Şu anda çalan parça yok");
        return;
    }

    g_autofree gchar *path = g_filename_from_uri(uri, NULL, NULL);
    g_autofree gchar *basename = path != NULL ? g_path_get_basename(path) : g_strdup(uri);
    gtk_label_set_text(state->current_label, basename);
}

static void music_window_select_current_track(MusicWindowState *state) {
    if (music_playlist_get_mode(state->playlist) == MUSIC_PLAYLIST_MODE_SINGLE) return;
    guint index = music_playlist_get_index(state->playlist);
    for (GtkWidget *child = gtk_widget_get_first_child(GTK_WIDGET(state->list_box)); child != NULL; child = gtk_widget_get_next_sibling(child)) {
        if (GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(child), "track-index")) == index + 1) {
            gtk_list_box_select_row(state->list_box, GTK_LIST_BOX_ROW(child));
            break;
        }
    }
}

static void music_window_update_playback_modes(MusicWindowState *state)
{
    gtk_button_set_icon_name(state->shuffle_button, "media-playlist-shuffle-symbolic");
    gtk_widget_set_opacity(GTK_WIDGET(state->shuffle_button), state->shuffle_enabled ? 1.0 : 0.5);
    gtk_widget_set_tooltip_text(GTK_WIDGET(state->shuffle_button), state->shuffle_enabled ? "Rastgele Çalma: Açık" : "Rastgele Çalma: Kapalı");

    const gchar *repeat_icon = state->repeat_mode == 2 ? "media-playlist-repeat-song-symbolic" : "media-playlist-repeat-symbolic";
    const gchar *repeat_tooltip = state->repeat_mode == 0 ? "Tekrar: Kapalı" : state->repeat_mode == 1 ? "Tekrar: Tüm Liste" : "Tekrar: Tek Parça";

    gtk_button_set_icon_name(state->repeat_button, repeat_icon);
    gtk_widget_set_opacity(GTK_WIDGET(state->repeat_button), state->repeat_mode == 0 ? 0.5 : 1.0);
    gtk_widget_set_tooltip_text(GTK_WIDGET(state->repeat_button), repeat_tooltip);
}

static void music_window_shuffle_reset(MusicWindowState *state)
{
    guint count = music_playlist_get_count(state->playlist);
    guint current = music_playlist_get_index(state->playlist);

    if (state->shuffle_order == NULL) state->shuffle_order = g_array_new(FALSE, FALSE, sizeof(guint));
    g_array_set_size(state->shuffle_order, 0);

    if (count == 0) {
        state->shuffle_position = 0;
        return;
    }

    g_array_append_val(state->shuffle_order, current);

    GArray *remaining = g_array_new(FALSE, FALSE, sizeof(guint));

    for (guint i = 0; i < count; i++) {
        if (i != current) g_array_append_val(remaining, i);
    }

    for (guint i = remaining->len; i > 1; i--) {
        guint j = g_random_int_range(0, i);
        guint temp = g_array_index(remaining, guint, i - 1);
        g_array_index(remaining, guint, i - 1) = g_array_index(remaining, guint, j);
        g_array_index(remaining, guint, j) = temp;
    }

    g_array_append_vals(state->shuffle_order, remaining->data, remaining->len);
    g_array_unref(remaining);
    state->shuffle_position = 0;
}

static void music_window_shuffle_sync(MusicWindowState *state)
{
    guint count = music_playlist_get_count(state->playlist);
    guint current = music_playlist_get_index(state->playlist);

    if (state->shuffle_order == NULL || state->shuffle_order->len != count || state->shuffle_position >= state->shuffle_order->len || g_array_index(state->shuffle_order, guint, state->shuffle_position) != current) {
        music_window_shuffle_reset(state);
    }
}

static gboolean music_window_shuffle_next(MusicWindowState *state, gboolean repeat_all)
{
    music_window_shuffle_sync(state);

    guint count = music_playlist_get_count(state->playlist);
    if (count == 0) return FALSE;
    if (state->shuffle_position + 1 < state->shuffle_order->len) state->shuffle_position++;
    else {
        if (!repeat_all) return FALSE;
        if (count == 1) return TRUE;
        music_window_shuffle_reset(state);
        state->shuffle_position = 1;
    }

    return music_playlist_set_index(state->playlist, g_array_index(state->shuffle_order, guint, state->shuffle_position));
}

static gboolean music_window_select_random_track(MusicWindowState *state)
{
    if (music_playlist_get_mode(state->playlist) == MUSIC_PLAYLIST_MODE_SINGLE) return FALSE;

    guint count = music_playlist_get_count(state->playlist);
    if (count < 2) return FALSE;

    guint current = music_playlist_get_index(state->playlist);
    guint next;

    do {
        next = g_random_int_range(0, count);
    } while (next == current);

    return music_playlist_set_index(state->playlist, next);
}

static void music_window_on_shuffle_clicked(GtkButton *button, gpointer user_data)
{
    (void) button;
    MusicWindowState *state = user_data;
    music_window_mpris_set_shuffle(!state->shuffle_enabled, state);
    music_mpris_update(state->mpris);
}

static void music_window_on_repeat_clicked(GtkButton *button, gpointer user_data)
{
    (void) button;
    MusicWindowState *state = user_data;
    music_window_mpris_set_repeat((state->repeat_mode + 1) % 3, state);
    music_mpris_update(state->mpris);
}

static void music_window_play_current(MusicWindowState *state)
{
    const gchar *uri = music_playlist_get_current(state->playlist);

    if (uri == NULL) {
        return;
    }

    g_autoptr(GError) error = NULL;

    if (!music_player_open(state->player, uri, &error)) {
        music_window_show_error(state, error->message);
        return;
    }

    if (music_playlist_get_mode(state->playlist) == MUSIC_PLAYLIST_MODE_DIRECTORY) {
        g_free(state->config.last_track);
        state->config.last_track = g_strdup(uri);
        music_config_save(&state->config, NULL);
    }

    music_player_play(state->player);
    music_window_select_current_track(state);
    music_window_update_current_label(state);
    music_mpris_update(state->mpris);
}

static void music_window_save_playlist(MusicWindowState *state)
{
    if (music_playlist_get_mode(state->playlist) != MUSIC_PLAYLIST_MODE_DIRECTORY) return;

    g_clear_pointer(&state->config.custom_order, g_strfreev);
    state->config.custom_order_length = music_playlist_get_count(state->playlist);
    state->config.custom_order = g_new0(gchar *, state->config.custom_order_length + 1);

    for (gsize i = 0; i < state->config.custom_order_length; i++) state->config.custom_order[i] = g_strdup(music_playlist_get_track(state->playlist, i));

    state->config.playlist_saved = TRUE;
    state->config.sort = music_playlist_get_sort(state->playlist);
    music_config_save(&state->config, NULL);
}

static void music_window_refresh_playlist(MusicWindowState *state)
{
    GtkWidget *child;

    while ((child = gtk_widget_get_first_child(GTK_WIDGET(state->list_box))) != NULL) {
        gtk_list_box_remove(state->list_box, child);
    }

    guint count = music_playlist_get_count(state->playlist);

    for (guint i = 0; i < count; i++) {
        const gchar *uri = music_playlist_get_track(state->playlist, i);
        g_autofree gchar *path = g_filename_from_uri(uri, NULL, NULL);
        g_autofree gchar *basename = path != NULL ? g_path_get_basename(path) : g_strdup(uri);

        GtkWidget *row = gtk_list_box_row_new();
        GtkWidget *label = gtk_label_new(basename);

        gtk_label_set_xalign(GTK_LABEL(label), 0.0);
        gtk_widget_set_margin_start(label, 12);
        gtk_widget_set_margin_end(label, 12);
        gtk_widget_set_margin_top(label, 10);
        gtk_widget_set_margin_bottom(label, 10);

        gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), label);
        g_object_set_data(G_OBJECT(row), "track-index", GUINT_TO_POINTER(i + 1));

        gtk_list_box_append(state->list_box, row);
    }
}

static void music_window_on_row_activated(GtkListBox *list_box, GtkListBoxRow *row, gpointer user_data)
{
    (void) list_box;

    MusicWindowState *state = user_data;
    guint index = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(row), "track-index")) - 1;

    if (music_playlist_get_mode(state->playlist) == MUSIC_PLAYLIST_MODE_SINGLE) {
        music_playlist_return_to_directory(state->playlist);
    }

    if (music_playlist_set_index(state->playlist, index)) {
        music_window_play_current(state);
    }
}

static void music_window_on_play_pause(GtkButton *button, gpointer user_data)
{
    (void) button;

    MusicWindowState *state = user_data;
    MusicPlayerState player_state = music_player_get_state(state->player);

    if (player_state == MUSIC_PLAYER_STATE_PLAYING) {
        music_player_pause(state->player);
        return;
    }

    if (player_state == MUSIC_PLAYER_STATE_PAUSED) {
        music_player_play(state->player);
        return;
    }

    music_window_play_current(state);
}

static void music_window_on_previous(GtkButton *button, gpointer user_data)
{
    (void) button;

    MusicWindowState *state = user_data;
    MusicPlaylistMode mode = music_playlist_get_mode(state->playlist);

    if (mode == MUSIC_PLAYLIST_MODE_SINGLE) return;

    if (state->shuffle_enabled) {
        music_window_shuffle_sync(state);
        if (state->shuffle_position == 0) return;
        state->shuffle_position--;
        if (music_playlist_set_index(state->playlist, g_array_index(state->shuffle_order, guint, state->shuffle_position))) music_window_play_current(state);
    } else if (music_playlist_previous(state->playlist)) {
        music_window_play_current(state);
    }
}

static void music_window_on_next(GtkButton *button, gpointer user_data)
{
    (void) button;

    MusicWindowState *state = user_data;
    if (music_playlist_get_mode(state->playlist) == MUSIC_PLAYLIST_MODE_SINGLE) return;

    gboolean changed = state->shuffle_enabled ? music_window_shuffle_next(state, state->repeat_mode == 1) : music_playlist_next(state->playlist);

    if (!state->shuffle_enabled && !changed && state->repeat_mode == 1 && music_playlist_get_count(state->playlist) > 0) changed = music_playlist_set_index(state->playlist, 0);

    if (changed) music_window_play_current(state);
}

static void music_window_mpris_next(gpointer user_data)
{
    music_window_on_next(NULL, user_data);
}

static void music_window_mpris_previous(gpointer user_data)
{
    music_window_on_previous(NULL, user_data);
}

static gboolean music_window_mpris_get_shuffle(gpointer user_data)
{
    MusicWindowState *state = user_data;
    return state->shuffle_enabled;
}

static void music_window_mpris_set_shuffle(gboolean enabled, gpointer user_data)
{
    MusicWindowState *state = user_data;
    state->shuffle_enabled = enabled;
    state->config.shuffle = enabled;
    if (enabled) music_window_shuffle_reset(state);
    music_config_save(&state->config, NULL);
    music_window_update_playback_modes(state);
}

static guint music_window_mpris_get_repeat(gpointer user_data)
{
    MusicWindowState *state = user_data;
    return state->repeat_mode;
}

static void music_window_mpris_set_repeat(guint mode, gpointer user_data)
{
    MusicWindowState *state = user_data;
    if (mode > 2) return;
    state->repeat_mode = mode;
    state->config.repeat = mode != 0;
    music_config_save(&state->config, NULL);
    music_window_update_playback_modes(state);
}

static void music_window_on_player_state_changed(MusicPlayer *player, gint player_state, gpointer user_data)
{
    (void) player;

    MusicWindowState *state = user_data;
    gboolean playing = player_state == MUSIC_PLAYER_STATE_PLAYING;

    gtk_button_set_icon_name(state->play_button, playing ? "media-playback-pause-symbolic" : "media-playback-start-symbolic");
    gtk_widget_set_tooltip_text(GTK_WIDGET(state->play_button), playing ? "Duraklat" : "Oynat");
    music_mpris_update(state->mpris);
}

static void music_window_on_eos(MusicPlayer *player, gpointer user_data)
{
    (void) player;

    MusicWindowState *state = user_data;
    MusicPlaylistMode mode = music_playlist_get_mode(state->playlist);

    if (state->repeat_mode == 2) {
        music_window_play_current(state);
        return;
    }

    if (mode == MUSIC_PLAYLIST_MODE_SINGLE) return;

    guint count = music_playlist_get_count(state->playlist);
    if (count == 0) return;

    gboolean changed = state->shuffle_enabled ? music_window_shuffle_next(state, state->repeat_mode == 1) : music_playlist_next(state->playlist);

    if (!state->shuffle_enabled && !changed && state->repeat_mode == 1) changed = music_playlist_set_index(state->playlist, 0);

    if (changed) music_window_play_current(state);
    else {
        gtk_button_set_icon_name(state->play_button, "media-playback-start-symbolic");
        gtk_widget_set_tooltip_text(GTK_WIDGET(state->play_button), "Oynat");
    }

    music_mpris_update(state->mpris);
}

static void music_window_on_player_error(MusicPlayer *player, const gchar *message, gpointer user_data)
{
    (void) player;

    MusicWindowState *state = user_data;
    music_window_show_error(state, message);
}

static void music_window_on_folder_selected(GObject *source, GAsyncResult *result, gpointer user_data)
{
    g_autoptr(GtkWindow) callback_window = GTK_WINDOW(user_data);
    MusicWindowState *state = g_object_get_data(G_OBJECT(callback_window), "music-state");
    if (state == NULL || !gtk_widget_get_visible(GTK_WIDGET(callback_window))) return;
    g_autoptr(GError) error = NULL;
    g_autoptr(GFile) folder = gtk_file_dialog_select_folder_finish(GTK_FILE_DIALOG(source), result, &error);

    if (folder == NULL) {
        if (error != NULL && !g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
            music_window_show_error(state, error->message);
        }
        return;
    }

    g_autofree gchar *path = g_file_get_path(folder);

    if (!music_playlist_load_directory(state->playlist, path, &error)) {
        music_window_show_error(state, error->message);
        return;
    }

    music_player_stop(state->player);
    music_player_clear_current_uri(state->player);
    gtk_list_box_unselect_all(state->list_box);

    g_free(state->config.directory);
    state->config.directory = g_strdup(path);
    g_clear_pointer(&state->config.last_track, g_free);

    music_window_save_playlist(state);
    music_window_refresh_playlist(state);
    music_window_shuffle_reset(state);
    music_window_update_current_label(state);
    music_mpris_update(state->mpris);
    music_config_save(&state->config, NULL);
}

static void music_window_on_file_selected(GObject *source, GAsyncResult *result, gpointer user_data)
{
    g_autoptr(GtkWindow) callback_window = GTK_WINDOW(user_data);
    MusicWindowState *state = g_object_get_data(G_OBJECT(callback_window), "music-state");
    if (state == NULL || !gtk_widget_get_visible(GTK_WIDGET(callback_window))) return;
    g_autoptr(GError) error = NULL;
    g_autoptr(GListModel) files = gtk_file_dialog_open_multiple_finish(GTK_FILE_DIALOG(source), result, &error);

    if (files == NULL) {
        if (error != NULL && !g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
            music_window_show_error(state, error->message);
        return;
    }

    g_autoptr(GPtrArray) paths = g_ptr_array_new_with_free_func(g_free);
    guint count = g_list_model_get_n_items(files);

    for (guint i = 0; i < count; i++) {
        g_autoptr(GFile) file = g_list_model_get_item(files, i);
        g_autofree gchar *path = g_file_get_path(file);

        if (path == NULL) {
            music_window_show_error(state, "Yerel müzik dosyaları seçilmelidir.");
            return;
        }

        g_ptr_array_add(paths, g_strdup(path));
    }

    if (!music_playlist_open_multiple(state->playlist, (const gchar * const *)paths->pdata, paths->len, &error)) {
        music_window_show_error(state, error->message);
        return;
    }

    music_window_refresh_playlist(state);
    music_window_shuffle_reset(state);
    music_window_play_current(state);
}

static void music_window_on_open_folder(GtkButton *button, gpointer user_data)
{
    (void) button;

    MusicWindowState *state = user_data;
    GtkFileDialog *dialog = gtk_file_dialog_new();

    gtk_file_dialog_set_title(dialog, "Müzik dizini seç");
    gtk_file_dialog_select_folder(dialog, state->window, NULL, music_window_on_folder_selected, g_object_ref(state->window));

    g_object_unref(dialog);
}

static void music_window_on_open_file(GtkButton *button, gpointer user_data)
{
    (void) button;

    MusicWindowState *state = user_data;
    GtkFileDialog *dialog = gtk_file_dialog_new();

    gtk_file_dialog_set_title(dialog, "Müzik dosyası aç");
    gtk_file_dialog_open_multiple(dialog, state->window, NULL, music_window_on_file_selected, g_object_ref(state->window));

    g_object_unref(dialog);
}

static void music_window_state_free(gpointer data)
{
    MusicWindowState *state = data;

    if (state->update_timer_id != 0) g_source_remove(state->update_timer_id);

    music_config_save(&state->config, NULL);
    music_config_clear(&state->config);
    g_clear_object(&state->mpris);
    g_clear_object(&state->player);
    music_playlist_free(state->playlist);
    g_free(state);
}

static void music_window_on_toggle_sidebar(GtkButton *button, gpointer user_data)
{
    MusicWindowState *state = user_data;
    gboolean show = !adw_overlay_split_view_get_show_sidebar(state->split_view);

    adw_overlay_split_view_set_show_sidebar(state->split_view, show);
    gtk_button_set_icon_name(button, show ? "sidebar-show-symbolic" : "sidebar-show-right-symbolic");
    gtk_widget_set_tooltip_text(GTK_WIDGET(button), show ? "Çalma Listesini Gizle" : "Çalma Listesini Göster");
}

GtkWindow *music_window_new(GtkApplication *app)
{
    MusicWindowState *state = g_new0(MusicWindowState, 1);

    state->player = music_player_new();
    state->playlist = music_playlist_new();
    state->mpris = music_mpris_new(state->player, state->playlist);

    const MusicMprisCallbacks mpris_callbacks = {
    .next = music_window_mpris_next,
    .previous = music_window_mpris_previous,
    .get_shuffle = music_window_mpris_get_shuffle,
    .set_shuffle = music_window_mpris_set_shuffle,
    .get_repeat = music_window_mpris_get_repeat,
    .set_repeat = music_window_mpris_set_repeat
    };

    music_mpris_set_callbacks(state->mpris, &mpris_callbacks, state);
    
    music_config_init(&state->config);
    music_config_load(&state->config);

    state->shuffle_enabled = state->config.shuffle;
    state->repeat_mode = state->config.repeat ? 1 : 0;

    AdwApplicationWindow *window = ADW_APPLICATION_WINDOW(adw_application_window_new(app));
    state->window = GTK_WINDOW(window);

    gtk_window_set_title(GTK_WINDOW(window), "Music");
    gtk_window_set_default_size(GTK_WINDOW(window), 1000, 680);
    g_object_set_data_full(G_OBJECT(window), "music-state", state, music_window_state_free);

    const GActionEntry window_actions[] = {
        { .name = "preferences", .activate = music_window_show_settings },
        { .name = "sort-asc", .activate = music_window_on_sort_action },
        { .name = "sort-desc", .activate = music_window_on_sort_action },
        { .name = "sort-newest", .activate = music_window_on_sort_action },
        { .name = "sort-oldest", .activate = music_window_on_sort_action },
        { .name = "remove-selected", .activate = music_window_on_remove_selected },
        { .name = "clear-playlist", .activate = music_window_on_clear_playlist }
    };

    GSimpleActionGroup *action_group = g_simple_action_group_new();
    g_action_map_add_action_entries(G_ACTION_MAP(action_group), window_actions, G_N_ELEMENTS(window_actions), state);
    gtk_widget_insert_action_group(GTK_WIDGET(window), "win", G_ACTION_GROUP(action_group));
    g_object_unref(action_group);

    g_signal_connect(state->player, "state-changed", G_CALLBACK(music_window_on_player_state_changed), state);
    g_signal_connect(state->player, "eos", G_CALLBACK(music_window_on_eos), state);
    g_signal_connect(state->player, "error", G_CALLBACK(music_window_on_player_error), state);

    AdwHeaderBar *header_bar = ADW_HEADER_BAR(adw_header_bar_new());
    AdwWindowTitle *window_title = ADW_WINDOW_TITLE(adw_window_title_new("Music", "Müzik kitaplığın"));
    adw_header_bar_set_title_widget(header_bar, GTK_WIDGET(window_title));

    GtkWidget *open_folder_button = gtk_button_new_from_icon_name("view-list-symbolic");
    GtkWidget *open_file_button = gtk_button_new_from_icon_name("audio-x-generic-symbolic");

    GtkWidget *sidebar_toggle_button = gtk_button_new_from_icon_name("sidebar-show-symbolic");
    gtk_widget_set_tooltip_text(sidebar_toggle_button, "Çalma Listesini Gizle");
    adw_header_bar_pack_start(header_bar, sidebar_toggle_button);
    g_signal_connect(sidebar_toggle_button, "clicked", G_CALLBACK(music_window_on_toggle_sidebar), state);

    gtk_widget_set_tooltip_text(open_folder_button, "Çalma Listesi Aç");
    gtk_widget_set_tooltip_text(open_file_button, "Tekli Dosya Aç");

    gtk_accessible_update_property(GTK_ACCESSIBLE(open_folder_button), GTK_ACCESSIBLE_PROPERTY_LABEL, "Çalma Listesi Aç", -1);
    gtk_accessible_update_property(GTK_ACCESSIBLE(open_file_button), GTK_ACCESSIBLE_PROPERTY_LABEL, "Tekli Dosya Aç", -1);

    adw_header_bar_pack_start(header_bar, open_folder_button);
    adw_header_bar_pack_start(header_bar, open_file_button);
    adw_header_bar_pack_end(header_bar, music_window_create_menu());

    g_signal_connect(open_folder_button, "clicked", G_CALLBACK(music_window_on_open_folder), state);
    g_signal_connect(open_file_button, "clicked", G_CALLBACK(music_window_on_open_file), state);

    AdwToolbarView *toolbar_view = ADW_TOOLBAR_VIEW(adw_toolbar_view_new());
    adw_toolbar_view_add_top_bar(toolbar_view, GTK_WIDGET(header_bar));

    /* Sol çalma listesi paneli */
    GtkWidget *sidebar_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(sidebar_box, "sidebar");
    gtk_widget_set_size_request(sidebar_box, 220, -1);

    GtkWidget *sidebar_header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_margin_start(sidebar_header, 18);
    gtk_widget_set_margin_end(sidebar_header, 12);
    gtk_widget_set_margin_top(sidebar_header, 12);
    gtk_widget_set_margin_bottom(sidebar_header, 6);

    GtkWidget *sidebar_title = gtk_label_new("Çalma Listesi");
    gtk_widget_add_css_class(sidebar_title, "heading");
    gtk_widget_set_hexpand(sidebar_title, TRUE);
    gtk_widget_set_halign(sidebar_title, GTK_ALIGN_START);

    gtk_box_append(GTK_BOX(sidebar_header), sidebar_title);
    gtk_box_append(GTK_BOX(sidebar_header), music_window_create_playlist_menu());
    gtk_box_append(GTK_BOX(sidebar_box), sidebar_header);

    state->search_entry = GTK_SEARCH_ENTRY(gtk_search_entry_new());
    gtk_search_entry_set_placeholder_text(state->search_entry, "Parçalarda ara...");
    gtk_widget_set_margin_start(GTK_WIDGET(state->search_entry), 12);
    gtk_widget_set_margin_end(GTK_WIDGET(state->search_entry), 12);
    gtk_widget_set_margin_top(GTK_WIDGET(state->search_entry), 6);
    gtk_widget_set_margin_bottom(GTK_WIDGET(state->search_entry), 8);
    gtk_box_append(GTK_BOX(sidebar_box), GTK_WIDGET(state->search_entry));

    state->list_box = GTK_LIST_BOX(gtk_list_box_new());

    gtk_list_box_set_filter_func(state->list_box, music_window_filter_row, state, NULL);
    g_signal_connect(state->search_entry, "search-changed", G_CALLBACK(music_window_on_search_changed), state);
    gtk_list_box_set_selection_mode(state->list_box, GTK_SELECTION_SINGLE);
    gtk_list_box_set_placeholder(state->list_box, gtk_label_new("Henüz müzik eklenmedi."));

    GtkWidget *playlist_scrolled = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(playlist_scrolled, TRUE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(playlist_scrolled), GTK_WIDGET(state->list_box));
    gtk_box_append(GTK_BOX(sidebar_box), playlist_scrolled);

    g_signal_connect(state->list_box, "row-activated", G_CALLBACK(music_window_on_row_activated), state);

    /* Sağ ana oynatıcı alanı */
    GtkWidget *main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 18);
    gtk_widget_set_margin_start(main_box, 24);
    gtk_widget_set_margin_end(main_box, 24);
    gtk_widget_set_margin_top(main_box, 24);
    gtk_widget_set_margin_bottom(main_box, 24);
    gtk_widget_set_valign(main_box, GTK_ALIGN_CENTER);

    GtkWidget *cover_image = gtk_image_new_from_icon_name("audio-x-generic-symbolic");
    gtk_image_set_pixel_size(GTK_IMAGE(cover_image), 128);
    gtk_widget_set_halign(cover_image, GTK_ALIGN_CENTER);
    gtk_widget_add_css_class(cover_image, "dim-label");
    gtk_box_append(GTK_BOX(main_box), cover_image);

    state->current_label = GTK_LABEL(gtk_label_new("Şu anda çalan parça yok"));
    gtk_label_set_ellipsize(state->current_label, PANGO_ELLIPSIZE_END);
    gtk_label_set_xalign(state->current_label, 0.5);
    gtk_widget_add_css_class(GTK_WIDGET(state->current_label), "title-2");
    gtk_box_append(GTK_BOX(main_box), GTK_WIDGET(state->current_label));

    state->progress_scale = GTK_SCALE(gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 1, 1));
    gtk_widget_set_hexpand(GTK_WIDGET(state->progress_scale), TRUE);
    gtk_scale_set_draw_value(state->progress_scale, FALSE);
    gtk_box_append(GTK_BOX(main_box), GTK_WIDGET(state->progress_scale));

    state->time_label = GTK_LABEL(gtk_label_new("00:00 / 00:00"));
    gtk_widget_set_halign(GTK_WIDGET(state->time_label), GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(main_box), GTK_WIDGET(state->time_label));

    GtkWidget *visualizer = music_visualizer_new(state->player);
    gtk_widget_set_margin_top(visualizer, 8);
    gtk_widget_set_margin_bottom(visualizer, 8);
    gtk_box_append(GTK_BOX(main_box), visualizer);

    /* Oynatma kontrolleri */
    GtkWidget *player_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_set_halign(player_box, GTK_ALIGN_CENTER);

    GtkWidget *previous_button = gtk_button_new_from_icon_name("media-skip-backward-symbolic");
    state->play_button = GTK_BUTTON(gtk_button_new_from_icon_name("media-playback-start-symbolic"));
    GtkWidget *next_button = gtk_button_new_from_icon_name("media-skip-forward-symbolic");

    state->shuffle_button = GTK_BUTTON(gtk_button_new_from_icon_name("media-playlist-shuffle-symbolic"));
    state->repeat_button = GTK_BUTTON(gtk_button_new_from_icon_name("media-playlist-repeat-symbolic"));

    gtk_widget_set_tooltip_text(previous_button, "Önceki Parça");
    gtk_widget_set_tooltip_text(GTK_WIDGET(state->play_button), "Oynat");
    gtk_widget_set_tooltip_text(next_button, "Sonraki Parça");

    gtk_box_append(GTK_BOX(player_box), previous_button);
    gtk_box_append(GTK_BOX(player_box), GTK_WIDGET(state->shuffle_button));
    gtk_box_append(GTK_BOX(player_box), GTK_WIDGET(state->play_button));
    gtk_box_append(GTK_BOX(player_box), GTK_WIDGET(state->repeat_button));
    gtk_box_append(GTK_BOX(player_box), next_button);

    state->volume_scale = GTK_SCALE(gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 1, 0.01));
    gtk_widget_set_size_request(GTK_WIDGET(state->volume_scale), 120, -1);
    gtk_range_set_value(GTK_RANGE(state->volume_scale), CLAMP(state->config.volume, 0.0, 1.0));
    gtk_scale_set_draw_value(state->volume_scale, FALSE);
    music_player_set_volume(state->player, CLAMP(state->config.volume, 0.0, 1.0));
    gtk_box_append(GTK_BOX(player_box), GTK_WIDGET(state->volume_scale));

    gtk_box_append(GTK_BOX(main_box), player_box);
    music_window_update_playback_modes(state);

    g_signal_connect(state->progress_scale, "change-value", G_CALLBACK(music_window_on_seek), state);
    g_signal_connect(state->volume_scale, "value-changed", G_CALLBACK(music_window_on_volume_changed), state);
    g_signal_connect(previous_button, "clicked", G_CALLBACK(music_window_on_previous), state);
    g_signal_connect(state->play_button, "clicked", G_CALLBACK(music_window_on_play_pause), state);
    g_signal_connect(next_button, "clicked", G_CALLBACK(music_window_on_next), state);

    g_signal_connect(state->shuffle_button, "clicked", G_CALLBACK(music_window_on_shuffle_clicked), state);
    g_signal_connect(state->repeat_button, "clicked", G_CALLBACK(music_window_on_repeat_clicked), state);

    state->split_view = ADW_OVERLAY_SPLIT_VIEW(adw_overlay_split_view_new());
    adw_overlay_split_view_set_sidebar(state->split_view, sidebar_box);
    adw_overlay_split_view_set_content(state->split_view, main_box);
    adw_overlay_split_view_set_sidebar_width_fraction(state->split_view, 0.28);
    adw_overlay_split_view_set_min_sidebar_width(state->split_view, 220);
    adw_overlay_split_view_set_max_sidebar_width(state->split_view, 340);
    adw_overlay_split_view_set_show_sidebar(state->split_view, TRUE);

    adw_toolbar_view_set_content(toolbar_view, GTK_WIDGET(state->split_view));
    adw_application_window_set_content(window, GTK_WIDGET(toolbar_view));

    /* Kaydedilmiş müzik dizinini ve çalma listesini yükle */
    if (state->config.directory != NULL && g_file_test(state->config.directory, G_FILE_TEST_IS_DIR)) {
        g_autoptr(GError) load_error = NULL;

        if (music_playlist_load_directory(state->playlist, state->config.directory, &load_error)) {
            if (state->config.playlist_saved) {
                music_playlist_restore(state->playlist, (const gchar * const *)state->config.custom_order, state->config.custom_order_length);
            } else {
                music_window_save_playlist(state);
            }

            if (state->config.sort != MUSIC_PLAYLIST_SORT_CUSTOM) {
                music_playlist_sort(state->playlist, state->config.sort);
            }

            music_window_refresh_playlist(state);

            for (guint i = 0; i < music_playlist_get_count(state->playlist); i++) {
                const gchar *track = music_playlist_get_track(state->playlist, i);
                if (g_strcmp0(track, state->config.last_track) == 0) {
                    music_playlist_set_index(state->playlist, i);
                    break;
                }
            }

            music_window_update_current_label(state);
        } else {
            g_warning("Kaydedilmiş müzik dizini yüklenemedi: %s", load_error->message);
        }
    }

    g_autoptr(GError) mpris_error = NULL;
    if (!music_mpris_start(state->mpris, &mpris_error)) {
        g_warning("MPRIS başlatılamadı: %s", mpris_error != NULL ? mpris_error->message : "Bilinmeyen hata");
    } else {
        music_mpris_update(state->mpris);
    }

    state->update_timer_id = g_timeout_add(250, music_window_update_progress, state);

    return GTK_WINDOW(window);
}

static gchar *music_window_format_time(gint64 nanoseconds)
{
    gint64 seconds = MAX(0, nanoseconds / GST_SECOND);
    return seconds >= 3600 ? g_strdup_printf("%02" G_GINT64_FORMAT ":%02" G_GINT64_FORMAT ":%02" G_GINT64_FORMAT, seconds / 3600, (seconds / 60) % 60, seconds % 60) : g_strdup_printf("%02" G_GINT64_FORMAT ":%02" G_GINT64_FORMAT, seconds / 60, seconds % 60);
}


static gboolean music_window_update_progress(gpointer user_data)
{
    MusicWindowState *state = user_data;
    gint64 position = music_player_get_position(state->player);
    gint64 duration = music_player_get_duration(state->player);

    if (duration <= 0) {
        gtk_range_set_range(GTK_RANGE(state->progress_scale), 0, 1);
        gtk_label_set_text(state->time_label, "00:00 / 00:00");
        return G_SOURCE_CONTINUE;
    }

    gtk_range_set_range(GTK_RANGE(state->progress_scale), 0, (gdouble) duration / GST_SECOND);
    gtk_range_set_value(GTK_RANGE(state->progress_scale), (gdouble) position / GST_SECOND);

    g_autofree gchar *current = music_window_format_time(position);
    g_autofree gchar *total = music_window_format_time(duration);
    g_autofree gchar *text = g_strdup_printf("%s / %s", current, total);

    gtk_label_set_text(state->time_label, text);

    return G_SOURCE_CONTINUE;
}

static gboolean music_window_on_seek(GtkRange *range, GtkScrollType scroll, gdouble value, gpointer user_data)
{
    (void) range;
    (void) scroll;

    MusicWindowState *state = user_data;
    music_player_seek(state->player, (gint64) (value * GST_SECOND));

    music_mpris_update(state->mpris);

    return FALSE;
}

static void music_window_on_volume_changed(GtkRange *range, gpointer user_data)
{
    MusicWindowState *state = user_data;
    gdouble volume = gtk_range_get_value(range);

    music_player_set_volume(state->player, volume);
    state->config.volume = volume;
    music_config_save(&state->config, NULL);
    music_mpris_update(state->mpris);
}

static GtkWidget *music_window_create_menu(void)
{
    GMenu *menu = g_menu_new();
    g_menu_append(menu, "Ayarlar", "win.preferences");
    g_menu_append(menu, "Klavye Kısayolları", "app.shortcuts");
    g_menu_append(menu, "Music Hakkında", "app.about");

    GtkWidget *button = gtk_menu_button_new();
    gtk_menu_button_set_icon_name(GTK_MENU_BUTTON(button), "open-menu-symbolic");
    gtk_menu_button_set_menu_model(GTK_MENU_BUTTON(button), G_MENU_MODEL(menu));
    gtk_widget_set_tooltip_text(button, "Ana Menü");

    g_object_unref(menu);
    return button;
}

static GtkWidget *music_window_create_playlist_menu(void)
{
    GMenu *menu = g_menu_new();
    GMenu *sort_menu = g_menu_new();

    g_menu_append(sort_menu, "A-Z", "win.sort-asc");
    g_menu_append(sort_menu, "Z-A", "win.sort-desc");
    g_menu_append(sort_menu, "En yeni önce", "win.sort-newest");
    g_menu_append(sort_menu, "En eski önce", "win.sort-oldest");

    g_menu_append_submenu(menu, "Sırala", G_MENU_MODEL(sort_menu));
    g_menu_append(menu, "Seçili Parçayı Kaldır", "win.remove-selected");
    g_menu_append(menu, "Çalma Listesini Temizle", "win.clear-playlist");

    GtkWidget *button = gtk_menu_button_new();
    gtk_menu_button_set_icon_name(GTK_MENU_BUTTON(button), "view-more-symbolic");
    gtk_menu_button_set_menu_model(GTK_MENU_BUTTON(button), G_MENU_MODEL(menu));
    gtk_widget_set_tooltip_text(button, "Çalma Listesi İşlemleri");

    g_object_unref(sort_menu);
    g_object_unref(menu);
    return button;
}

void music_window_open_files(GtkWindow *window, GFile **files, gint n_files)
{
    g_return_if_fail(GTK_IS_WINDOW(window));
    g_return_if_fail(files != NULL);
    g_return_if_fail(n_files > 0);

    MusicWindowState *state = g_object_get_data(G_OBJECT(window), "music-state");
    if (state == NULL) return;

    g_autoptr(GError) error = NULL;
    g_autoptr(GPtrArray) paths = g_ptr_array_new_with_free_func(g_free);

    for (gint i = 0; i < n_files; i++) {
        gchar *path = g_file_get_path(files[i]);
        if (path == NULL) {
            music_window_show_error(state, "Yalnızca yerel dosyalar açılabilir.");
            return;
        }
        g_ptr_array_add(paths, path);
    }

    if (paths->len == 1 && g_file_test(g_ptr_array_index(paths, 0), G_FILE_TEST_IS_DIR)) {
        const gchar *path = g_ptr_array_index(paths, 0);

        if (!music_playlist_load_directory(state->playlist, path, &error)) {
            music_window_show_error(state, error->message);
            return;
        }

        music_player_stop(state->player);
        music_player_clear_current_uri(state->player);
        gtk_list_box_unselect_all(state->list_box);

        g_free(state->config.directory);
        state->config.directory = g_strdup(path);
        g_clear_pointer(&state->config.last_track, g_free);

        music_window_save_playlist(state);
        music_window_refresh_playlist(state);
        music_window_shuffle_reset(state);
        music_window_update_current_label(state);
        music_mpris_update(state->mpris);
        music_config_save(&state->config, NULL);
        return;
    }

    if (paths->len == 1) {
        const gchar *path = g_ptr_array_index(paths, 0);
        if (!music_playlist_open_single(state->playlist, path, &error)) {
            music_window_show_error(state, error->message);
            return;
        }
    } else {
        if (!music_playlist_open_multiple(state->playlist, (const gchar * const *)paths->pdata, paths->len, &error)) {
            music_window_show_error(state, error->message);
            return;
        }
        music_window_refresh_playlist(state);
    }

    music_window_shuffle_reset(state);
    music_window_play_current(state);
}