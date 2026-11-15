
#ifndef MUSIC_MPRIS_H
#define MUSIC_MPRIS_H

#include <glib-object.h>
#include "music/player.h"
#include "music/playlist.h"

G_BEGIN_DECLS

#define MUSIC_TYPE_MPRIS (music_mpris_get_type())

G_DECLARE_FINAL_TYPE(MusicMpris, music_mpris, MUSIC, MPRIS, GObject)

MusicMpris *music_mpris_new(MusicPlayer *player, MusicPlaylist *playlist);
gboolean music_mpris_start(MusicMpris *mpris, GError **error);
void music_mpris_stop(MusicMpris *mpris);
void music_mpris_update(MusicMpris *mpris);

G_END_DECLS

#endif
