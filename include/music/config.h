#ifndef MUSIC_CONFIG_H
#define MUSIC_CONFIG_H

#include <glib.h>
#include "music/playlist.h"

typedef struct {
    gchar *directory;
    gchar *last_track;
    gdouble volume;
    gboolean shuffle;
    gboolean repeat;
    MusicPlaylistSort sort;
    gchar **custom_order;
    gsize custom_order_length;
    gboolean playlist_saved;
} MusicConfig;

void music_config_init(MusicConfig *config);
void music_config_load(MusicConfig *config);
gboolean music_config_save(const MusicConfig *config, GError **error);
void music_config_clear(MusicConfig *config);

#endif