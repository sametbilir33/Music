#ifndef MUSIC_PLAYER_H
#define MUSIC_PLAYER_H
#define MUSIC_SPECTRUM_BANDS 32

#include <gst/gst.h>
#include <glib-object.h>

G_BEGIN_DECLS

#define MUSIC_TYPE_PLAYER (music_player_get_type())

G_DECLARE_FINAL_TYPE(MusicPlayer, music_player, MUSIC, PLAYER, GObject)

typedef enum {
    MUSIC_PLAYER_STATE_STOPPED,
    MUSIC_PLAYER_STATE_PLAYING,
    MUSIC_PLAYER_STATE_PAUSED
} MusicPlayerState;

MusicPlayer *music_player_new(void);

gboolean music_player_open(MusicPlayer *player, const gchar *path, GError **error);
void music_player_play(MusicPlayer *player);
void music_player_pause(MusicPlayer *player);
void music_player_stop(MusicPlayer *player);

void music_player_set_volume(MusicPlayer *player, gdouble volume);
gdouble music_player_get_volume(MusicPlayer *player);

gboolean music_player_seek(MusicPlayer *player, gint64 position);
gint64 music_player_get_position(MusicPlayer *player);
gint64 music_player_get_duration(MusicPlayer *player);
void music_player_clear_current_uri(MusicPlayer *player);

MusicPlayerState music_player_get_state(MusicPlayer *player);
const gchar *music_player_get_current_uri(MusicPlayer *player);

const gdouble *music_player_get_spectrum(MusicPlayer *player);

G_END_DECLS

#endif