#include "music/playlist.h"

#include <string.h>
#include <glib/gstdio.h>
#include <sys/stat.h>

struct _MusicPlaylist {
    GPtrArray *tracks;
    gchar *directory;
    gchar *single_track;
    guint current_index;
    MusicPlaylistMode mode;
    MusicPlaylistSort sort;
    GPtrArray *multiple_tracks;
    guint multiple_index;
};

static gboolean music_playlist_is_audio(const gchar *filename)
{
    static const gchar *extensions[] = { ".mp3", ".flac", ".ogg", ".oga", ".opus", ".wav", ".m4a", ".aac", ".aiff", ".aif", ".ape", ".wma", NULL };
    const gchar *extension = strrchr(filename, '.');
    if (extension == NULL) return FALSE;
    for (guint i = 0; extensions[i] != NULL; i++) if (g_ascii_strcasecmp(extension, extensions[i]) == 0) return TRUE;
    return FALSE;
}

static gint music_playlist_compare(gconstpointer a, gconstpointer b, gpointer user_data)
{
    const gchar *track_a = *(gchar * const *)a;
    const gchar *track_b = *(gchar * const *)b;
    MusicPlaylistSort sort = GPOINTER_TO_INT(user_data);
    g_autofree gchar *path_a = g_filename_from_uri(track_a, NULL, NULL);
    g_autofree gchar *path_b = g_filename_from_uri(track_b, NULL, NULL);

    if (sort == MUSIC_PLAYLIST_SORT_DATE_NEWEST || sort == MUSIC_PLAYLIST_SORT_DATE_OLDEST) {
        GStatBuf stat_a = { 0 }, stat_b = { 0 };
        if (path_a != NULL) g_stat(path_a, &stat_a);
        if (path_b != NULL) g_stat(path_b, &stat_b);
        gint result = (stat_a.st_mtime > stat_b.st_mtime) - (stat_a.st_mtime < stat_b.st_mtime);
        return sort == MUSIC_PLAYLIST_SORT_DATE_NEWEST ? -result : result;
    }

    g_autofree gchar *name_a = path_a != NULL ? g_path_get_basename(path_a) : g_strdup(track_a);
    g_autofree gchar *name_b = path_b != NULL ? g_path_get_basename(path_b) : g_strdup(track_b);
    g_autofree gchar *key_a = g_utf8_collate_key_for_filename(name_a, -1);
    g_autofree gchar *key_b = g_utf8_collate_key_for_filename(name_b, -1);
    gint result = g_strcmp0(key_a, key_b);
    return sort == MUSIC_PLAYLIST_SORT_NAME_DESC ? -result : result;
}

MusicPlaylist *music_playlist_new(void)
{
    MusicPlaylist *playlist = g_new0(MusicPlaylist, 1);
    playlist->tracks = g_ptr_array_new_with_free_func(g_free);
    playlist->multiple_tracks = g_ptr_array_new_with_free_func(g_free);
    playlist->mode = MUSIC_PLAYLIST_MODE_DIRECTORY;
    playlist->sort = MUSIC_PLAYLIST_SORT_NAME_ASC;
    return playlist;
}

void music_playlist_free(MusicPlaylist *playlist)
{
    if (playlist == NULL) return;
    g_ptr_array_unref(playlist->tracks);
    g_free(playlist->directory);
    g_free(playlist->single_track);
    g_ptr_array_unref(playlist->multiple_tracks);
    g_free(playlist);
}

gboolean music_playlist_load_directory(MusicPlaylist *playlist, const gchar *path, GError **error)
{
    g_return_val_if_fail(playlist != NULL, FALSE);
    g_return_val_if_fail(path != NULL, FALSE);

    if (!g_file_test(path, G_FILE_TEST_IS_DIR)) {
        g_set_error_literal(error, G_FILE_ERROR, G_FILE_ERROR_NOTDIR, "Seçilen yol bir dizin değil.");
        return FALSE;
    }

    GDir *dir = g_dir_open(path, 0, error);
    if (dir == NULL) return FALSE;

    GPtrArray *new_tracks = g_ptr_array_new_with_free_func(g_free);
    const gchar *name;

    while ((name = g_dir_read_name(dir)) != NULL) {
        if (!music_playlist_is_audio(name)) continue;
        g_autofree gchar *filepath = g_build_filename(path, name, NULL);
        GStatBuf file_stat;
        if (!g_stat(filepath, &file_stat) && S_ISREG(file_stat.st_mode) && file_stat.st_size > 0) {
            gchar *uri = g_filename_to_uri(filepath, NULL, NULL);
            if (uri != NULL) g_ptr_array_add(new_tracks, uri);
        }
    }

    g_dir_close(dir);
    g_ptr_array_sort_with_data(new_tracks, music_playlist_compare, GINT_TO_POINTER(MUSIC_PLAYLIST_SORT_NAME_ASC));
    g_ptr_array_unref(playlist->tracks);
    playlist->tracks = new_tracks;
    g_ptr_array_set_size(playlist->multiple_tracks, 0);
    playlist->multiple_index = 0;
    g_free(playlist->directory);
    playlist->directory = g_canonicalize_filename(path, NULL);
    playlist->current_index = 0;
    playlist->mode = MUSIC_PLAYLIST_MODE_DIRECTORY;
    playlist->sort = MUSIC_PLAYLIST_SORT_NAME_ASC;
    g_clear_pointer(&playlist->single_track, g_free);
    return TRUE;
}

gboolean music_playlist_open_single(MusicPlaylist *playlist, const gchar *path, GError **error)
{
    g_return_val_if_fail(playlist != NULL, FALSE);
    g_return_val_if_fail(path != NULL, FALSE);
    if (!g_file_test(path, G_FILE_TEST_IS_REGULAR)) {
        g_set_error_literal(error, G_FILE_ERROR, G_FILE_ERROR_NOENT, "Müzik dosyası bulunamadı.");
        return FALSE;
    }
    gchar *uri = g_filename_to_uri(path, NULL, error);
    if (uri == NULL) return FALSE;
    g_free(playlist->single_track);
    playlist->single_track = uri;
    playlist->mode = MUSIC_PLAYLIST_MODE_SINGLE;
    g_ptr_array_set_size(playlist->multiple_tracks, 0);
    playlist->multiple_index = 0;
    return TRUE;
}

gboolean music_playlist_open_multiple(MusicPlaylist *playlist, const gchar * const *paths, gsize count, GError **error)
{
    g_return_val_if_fail(playlist != NULL, FALSE);
    g_return_val_if_fail(paths != NULL, FALSE);

    GPtrArray *new_tracks = g_ptr_array_new_with_free_func(g_free);

    for (gsize i = 0; i < count; i++) {
        if (paths[i] == NULL || !g_file_test(paths[i], G_FILE_TEST_IS_REGULAR)) {
            g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_NOENT, "Dosya bulunamadı: %s", paths[i] != NULL ? paths[i] : "(null)");
            g_ptr_array_unref(new_tracks);
            return FALSE;
        }

        GStatBuf file_stat;
        if (g_stat(paths[i], &file_stat) != 0 || file_stat.st_size <= 0 || !music_playlist_is_audio(paths[i])) {
            g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_INVAL, "Geçersiz veya boş müzik dosyası: %s", paths[i]);
            g_ptr_array_unref(new_tracks);
            return FALSE;
        }

        gchar *uri = g_filename_to_uri(paths[i], NULL, error);
        if (uri == NULL) {
            g_ptr_array_unref(new_tracks);
            return FALSE;
        }

        g_ptr_array_add(new_tracks, uri);
    }

    if (new_tracks->len == 0) {
        g_set_error_literal(error, G_FILE_ERROR, G_FILE_ERROR_INVAL, "Açılacak müzik dosyası bulunamadı.");
        g_ptr_array_unref(new_tracks);
        return FALSE;
    }

    g_ptr_array_set_size(playlist->multiple_tracks, 0);
    g_ptr_array_unref(playlist->multiple_tracks);
    playlist->multiple_tracks = new_tracks;
    playlist->multiple_index = 0;
    playlist->mode = MUSIC_PLAYLIST_MODE_MULTIPLE;
    g_clear_pointer(&playlist->single_track, g_free);
    return TRUE;
}

void music_playlist_return_to_directory(MusicPlaylist *playlist)
{
    g_return_if_fail(playlist != NULL);
    playlist->mode = MUSIC_PLAYLIST_MODE_DIRECTORY;
    g_clear_pointer(&playlist->single_track, g_free);
    g_ptr_array_set_size(playlist->multiple_tracks, 0);
    playlist->multiple_index = 0;
}

const gchar *music_playlist_get_current(MusicPlaylist *playlist)
{
    g_return_val_if_fail(playlist != NULL, NULL);
    if (playlist->mode == MUSIC_PLAYLIST_MODE_SINGLE) return playlist->single_track;
    if (playlist->mode == MUSIC_PLAYLIST_MODE_MULTIPLE) return playlist->multiple_index < playlist->multiple_tracks->len ? g_ptr_array_index(playlist->multiple_tracks, playlist->multiple_index) : NULL;
    return playlist->current_index < playlist->tracks->len ? g_ptr_array_index(playlist->tracks, playlist->current_index) : NULL;
}

const gchar *music_playlist_get_track(MusicPlaylist *playlist, guint index)
{
    g_return_val_if_fail(playlist != NULL, NULL);
    if (playlist->mode == MUSIC_PLAYLIST_MODE_SINGLE) return index == 0 ? playlist->single_track : NULL;
    if (playlist->mode == MUSIC_PLAYLIST_MODE_MULTIPLE) return index < playlist->multiple_tracks->len ? g_ptr_array_index(playlist->multiple_tracks, index) : NULL;
    return index < playlist->tracks->len ? g_ptr_array_index(playlist->tracks, index) : NULL;
}

const gchar *music_playlist_get_directory(MusicPlaylist *playlist)
{
    g_return_val_if_fail(playlist != NULL, NULL);
    return playlist->directory;
}

gboolean music_playlist_next(MusicPlaylist *playlist)
{
    g_return_val_if_fail(playlist != NULL, FALSE);
    if (playlist->mode == MUSIC_PLAYLIST_MODE_MULTIPLE) {
        if (playlist->multiple_index + 1 >= playlist->multiple_tracks->len) return FALSE;
        playlist->multiple_index++;
        return TRUE;
    }
    if (playlist->mode != MUSIC_PLAYLIST_MODE_DIRECTORY || playlist->current_index + 1 >= playlist->tracks->len) return FALSE;
    playlist->current_index++;
    return TRUE;
}

gboolean music_playlist_previous(MusicPlaylist *playlist)
{
    g_return_val_if_fail(playlist != NULL, FALSE);
    if (playlist->mode == MUSIC_PLAYLIST_MODE_MULTIPLE) {
        if (playlist->multiple_index == 0) return FALSE;
        playlist->multiple_index--;
        return TRUE;
    }
    if (playlist->mode != MUSIC_PLAYLIST_MODE_DIRECTORY || playlist->current_index == 0) return FALSE;
    playlist->current_index--;
    return TRUE;
}

gboolean music_playlist_set_index(MusicPlaylist *playlist, guint index)
{
    g_return_val_if_fail(playlist != NULL, FALSE);
    if (playlist->mode == MUSIC_PLAYLIST_MODE_SINGLE) return index == 0 && playlist->single_track != NULL;
    if (playlist->mode == MUSIC_PLAYLIST_MODE_MULTIPLE) {
        if (index >= playlist->multiple_tracks->len) return FALSE;
        playlist->multiple_index = index;
        return TRUE;
    }
    if (index >= playlist->tracks->len) return FALSE;
    playlist->current_index = index;
    return TRUE;
}

gboolean music_playlist_remove(MusicPlaylist *playlist, guint index)
{
    g_return_val_if_fail(playlist != NULL, FALSE);
    if (playlist->mode != MUSIC_PLAYLIST_MODE_DIRECTORY || index >= playlist->tracks->len) return FALSE;
    g_ptr_array_remove_index(playlist->tracks, index);
    if (playlist->tracks->len == 0) playlist->current_index = 0;
    else if (index < playlist->current_index) playlist->current_index--;
    else if (playlist->current_index >= playlist->tracks->len) playlist->current_index = playlist->tracks->len - 1;
    return TRUE;
}

void music_playlist_clear(MusicPlaylist *playlist)
{
    g_return_if_fail(playlist != NULL);
    if (playlist->mode != MUSIC_PLAYLIST_MODE_DIRECTORY) return;
    g_ptr_array_set_size(playlist->tracks, 0);
    playlist->current_index = 0;
    playlist->sort = MUSIC_PLAYLIST_SORT_CUSTOM;
}

void music_playlist_sort(MusicPlaylist *playlist, MusicPlaylistSort sort)
{
    g_return_if_fail(playlist != NULL);
    if (playlist->mode != MUSIC_PLAYLIST_MODE_DIRECTORY || sort == MUSIC_PLAYLIST_SORT_CUSTOM) return;
    g_autofree gchar *current = g_strdup(music_playlist_get_current(playlist));
    g_ptr_array_sort_with_data(playlist->tracks, music_playlist_compare, GINT_TO_POINTER(sort));
    playlist->sort = sort;
    if (current != NULL) for (guint i = 0; i < playlist->tracks->len; i++) if (g_strcmp0(current, g_ptr_array_index(playlist->tracks, i)) == 0) { playlist->current_index = i; break; }
}

gboolean music_playlist_move(MusicPlaylist *playlist, guint from, guint to)
{
    g_return_val_if_fail(playlist != NULL, FALSE);
    if (playlist->mode != MUSIC_PLAYLIST_MODE_DIRECTORY || from >= playlist->tracks->len || to >= playlist->tracks->len) return FALSE;
    if (from == to) return TRUE;
    gpointer track = g_ptr_array_steal_index(playlist->tracks, from);
    g_ptr_array_insert(playlist->tracks, to, track);
    if (playlist->current_index == from) playlist->current_index = to;
    else if (from < playlist->current_index && to >= playlist->current_index) playlist->current_index--;
    else if (from > playlist->current_index && to <= playlist->current_index) playlist->current_index++;
    playlist->sort = MUSIC_PLAYLIST_SORT_CUSTOM;
    return TRUE;
}

MusicPlaylistSort music_playlist_get_sort(MusicPlaylist *playlist)
{
    g_return_val_if_fail(playlist != NULL, MUSIC_PLAYLIST_SORT_NAME_ASC);
    return playlist->sort;
}

guint music_playlist_get_count(MusicPlaylist *playlist)
{
    g_return_val_if_fail(playlist != NULL, 0);
    if (playlist->mode == MUSIC_PLAYLIST_MODE_SINGLE) return playlist->single_track != NULL ? 1 : 0;
    return playlist->mode == MUSIC_PLAYLIST_MODE_MULTIPLE ? playlist->multiple_tracks->len : playlist->tracks->len;
}

guint music_playlist_get_index(MusicPlaylist *playlist)
{
    g_return_val_if_fail(playlist != NULL, 0);
    if (playlist->mode == MUSIC_PLAYLIST_MODE_SINGLE) return 0;
    return playlist->mode == MUSIC_PLAYLIST_MODE_MULTIPLE ? playlist->multiple_index : playlist->current_index;
}

MusicPlaylistMode music_playlist_get_mode(MusicPlaylist *playlist)
{
    g_return_val_if_fail(playlist != NULL, MUSIC_PLAYLIST_MODE_DIRECTORY);
    return playlist->mode;
}

void music_playlist_restore(MusicPlaylist *playlist, const gchar * const *tracks, gsize count)
{
    g_return_if_fail(playlist != NULL);

    GPtrArray *restored = g_ptr_array_new_with_free_func(g_free);
    GHashTable *seen = g_hash_table_new(g_str_hash, g_str_equal);

    for (gsize i = 0; i < count; i++) {
        if (tracks[i] == NULL || g_hash_table_contains(seen, tracks[i])) continue;

        g_autofree gchar *path = g_filename_from_uri(tracks[i], NULL, NULL);
        if (path == NULL || !music_playlist_is_audio(path)) continue;

        GStatBuf file_stat;
        if (g_stat(path, &file_stat) != 0 || !S_ISREG(file_stat.st_mode) || file_stat.st_size <= 0) continue;

        gchar *uri = g_strdup(tracks[i]);
        g_ptr_array_add(restored, uri);
        g_hash_table_add(seen, uri);
    }

    for (guint i = 0; i < playlist->tracks->len; i++) {
        const gchar *uri = g_ptr_array_index(playlist->tracks, i);
        if (g_hash_table_contains(seen, uri)) continue;

        g_ptr_array_add(restored, g_strdup(uri));
        g_hash_table_add(seen, g_ptr_array_index(restored, restored->len - 1));
    }

    g_hash_table_unref(seen);
    g_ptr_array_unref(playlist->tracks);
    playlist->tracks = restored;
    playlist->current_index = 0;
    playlist->mode = MUSIC_PLAYLIST_MODE_DIRECTORY;
    playlist->sort = MUSIC_PLAYLIST_SORT_CUSTOM;
    g_clear_pointer(&playlist->single_track, g_free);
}