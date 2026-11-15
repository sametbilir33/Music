#ifndef MUSIC_PLAYLIST_H
#define MUSIC_PLAYLIST_H

#include <glib.h>

G_BEGIN_DECLS

typedef enum {
    MUSIC_PLAYLIST_MODE_DIRECTORY,
    MUSIC_PLAYLIST_MODE_SINGLE,
    MUSIC_PLAYLIST_MODE_MULTIPLE
} MusicPlaylistMode;

typedef enum {
    MUSIC_PLAYLIST_SORT_CUSTOM,
    MUSIC_PLAYLIST_SORT_NAME_ASC,
    MUSIC_PLAYLIST_SORT_NAME_DESC,
    MUSIC_PLAYLIST_SORT_DATE_NEWEST,
    MUSIC_PLAYLIST_SORT_DATE_OLDEST
} MusicPlaylistSort;

typedef struct _MusicPlaylist MusicPlaylist;

MusicPlaylist *music_playlist_new(void);
void music_playlist_free(MusicPlaylist *playlist);

gboolean music_playlist_load_directory(MusicPlaylist *playlist, const gchar *path, GError **error);
gboolean music_playlist_open_single(MusicPlaylist *playlist, const gchar *path, GError **error);
void music_playlist_return_to_directory(MusicPlaylist *playlist);
gboolean music_playlist_open_multiple(MusicPlaylist *playlist, const gchar * const *paths, gsize count, GError **error);

const gchar *music_playlist_get_current(MusicPlaylist *playlist);
const gchar *music_playlist_get_track(MusicPlaylist *playlist, guint index);
const gchar *music_playlist_get_directory(MusicPlaylist *playlist);

gboolean music_playlist_next(MusicPlaylist *playlist);
gboolean music_playlist_previous(MusicPlaylist *playlist);
gboolean music_playlist_set_index(MusicPlaylist *playlist, guint index);
void music_playlist_restore(MusicPlaylist *playlist, const gchar * const *tracks, gsize count);

gboolean music_playlist_remove(MusicPlaylist *playlist, guint index);
void music_playlist_clear(MusicPlaylist *playlist);
void music_playlist_sort(MusicPlaylist *playlist, MusicPlaylistSort sort);
gboolean music_playlist_move(MusicPlaylist *playlist, guint from, guint to);
MusicPlaylistSort music_playlist_get_sort(MusicPlaylist *playlist);

guint music_playlist_get_count(MusicPlaylist *playlist);
guint music_playlist_get_index(MusicPlaylist *playlist);
MusicPlaylistMode music_playlist_get_mode(MusicPlaylist *playlist);

G_END_DECLS

#endif