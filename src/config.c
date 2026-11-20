#include "music/config.h"
#include <errno.h>

#define CONFIG_DIRECTORY "Music"
#define CONFIG_FILENAME "config.ini"

static gchar *music_config_get_path(void)
{
    return g_build_filename(g_get_user_config_dir(), CONFIG_DIRECTORY, CONFIG_FILENAME, NULL);
}

void music_config_init(MusicConfig *config)
{
    g_return_if_fail(config != NULL);
    config->directory = NULL;
    config->last_track = NULL;
    config->volume = 0.75;
    config->shuffle = FALSE;
    config->repeat_mode = 0;
    config->sort = MUSIC_PLAYLIST_SORT_NAME_ASC;
    config->custom_order = NULL;
    config->custom_order_length = 0;
    config->playlist_saved = FALSE;
}

void music_config_load(MusicConfig *config)
{
    g_return_if_fail(config != NULL);

    g_autoptr(GKeyFile) key_file = g_key_file_new();
    g_autofree gchar *path = music_config_get_path();

    if (!g_key_file_load_from_file(key_file, path, G_KEY_FILE_NONE, NULL)) return;

    if (g_key_file_has_key(key_file, "DirectoryMode", "directory", NULL)) {
        g_free(config->directory);
        config->directory = g_key_file_get_string(key_file, "DirectoryMode", "directory", NULL);
    }

    if (g_key_file_has_key(key_file, "DirectoryMode", "last-track", NULL)) {
        g_free(config->last_track);
        config->last_track = g_key_file_get_string(key_file, "DirectoryMode", "last-track", NULL);
    }

    if (g_key_file_has_key(key_file, "Player", "volume", NULL)) config->volume = g_key_file_get_double(key_file, "Player", "volume", NULL);
    if (g_key_file_has_key(key_file, "Player", "shuffle", NULL)) config->shuffle = g_key_file_get_boolean(key_file, "Player", "shuffle", NULL);
    if (g_key_file_has_key(key_file, "Player", "repeat-mode", NULL)) config->repeat_mode = CLAMP(g_key_file_get_integer(key_file, "Player", "repeat-mode", NULL), 0, 2);
    else if (g_key_file_has_key(key_file, "Player", "repeat", NULL)) config->repeat_mode = g_key_file_get_boolean(key_file, "Player", "repeat", NULL) ? 1 : 0;
    if (g_key_file_has_key(key_file, "Playlist", "sort", NULL)) config->sort = CLAMP(g_key_file_get_integer(key_file, "Playlist", "sort", NULL), MUSIC_PLAYLIST_SORT_CUSTOM, MUSIC_PLAYLIST_SORT_DATE_OLDEST);
    if (g_key_file_has_key(key_file, "Playlist", "saved", NULL)) config->playlist_saved = g_key_file_get_boolean(key_file, "Playlist", "saved", NULL);

    if (g_key_file_has_key(key_file, "Playlist", "custom-order", NULL)) {
        g_clear_pointer(&config->custom_order, g_strfreev);
        config->custom_order = g_key_file_get_string_list(key_file, "Playlist", "custom-order", &config->custom_order_length, NULL);
    }

    config->volume = CLAMP(config->volume, 0.0, 1.0);
}

gboolean music_config_save(const MusicConfig *config, GError **error)
{
    g_return_val_if_fail(config != NULL, FALSE);

    g_autoptr(GKeyFile) key_file = g_key_file_new();
    g_autofree gchar *path = music_config_get_path();
    g_autofree gchar *directory = g_path_get_dirname(path);

    if (g_mkdir_with_parents(directory, 0755) != 0) {
        g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno), "Ayar dizini oluşturulamadı: %s", g_strerror(errno));
        return FALSE;
    }

    if (config->directory != NULL) g_key_file_set_string(key_file, "DirectoryMode", "directory", config->directory);
    if (config->last_track != NULL) g_key_file_set_string(key_file, "DirectoryMode", "last-track", config->last_track);

    g_key_file_set_double(key_file, "Player", "volume", CLAMP(config->volume, 0.0, 1.0));
    g_key_file_set_boolean(key_file, "Player", "shuffle", config->shuffle);
    g_key_file_set_integer(key_file, "Player", "repeat-mode", CLAMP(config->repeat_mode, 0, 2));
    g_key_file_set_integer(key_file, "Playlist", "sort", config->sort);
    g_key_file_set_boolean(key_file, "Playlist", "saved", config->playlist_saved);
    
    if (config->custom_order != NULL && config->custom_order_length > 0) g_key_file_set_string_list(key_file, "Playlist", "custom-order", (const gchar * const *)config->custom_order, config->custom_order_length);

    return g_key_file_save_to_file(key_file, path, error);
}

void music_config_clear(MusicConfig *config)
{
    g_return_if_fail(config != NULL);
    g_clear_pointer(&config->directory, g_free);
    g_clear_pointer(&config->last_track, g_free);
    g_clear_pointer(&config->custom_order, g_strfreev);
    config->custom_order_length = 0;
    config->volume = 0.75;
    config->shuffle = FALSE;
    config->repeat_mode = 0;
    config->sort = MUSIC_PLAYLIST_SORT_NAME_ASC;
}