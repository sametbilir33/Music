
#ifndef MUSIC_MPRIS_H
#define MUSIC_MPRIS_H

#include <glib-object.h>
#include "music/player.h"
#include "music/playlist.h"

G_BEGIN_DECLS

#define MUSIC_TYPE_MPRIS (music_mpris_get_type())

G_DECLARE_FINAL_TYPE(MusicMpris, music_mpris, MUSIC, MPRIS, GObject)

typedef struct {
    void (*next)(gpointer user_data);
    void (*previous)(gpointer user_data);
    gboolean (*get_shuffle)(gpointer user_data);
    void (*set_shuffle)(gboolean enabled, gpointer user_data);
    guint (*get_repeat)(gpointer user_data);
    void (*set_repeat)(guint mode, gpointer user_data);
} MusicMprisCallbacks;

MusicMpris *music_mpris_new(MusicPlayer *player, MusicPlaylist *playlist);
gboolean music_mpris_start(MusicMpris *mpris, GError **error);
void music_mpris_stop(MusicMpris *mpris);
void music_mpris_update(MusicMpris *mpris);
void music_mpris_set_callbacks(MusicMpris *mpris, const MusicMprisCallbacks *callbacks, gpointer user_data);

G_END_DECLS

#endif
