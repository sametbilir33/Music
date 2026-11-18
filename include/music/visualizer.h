#ifndef MUSIC_VISUALIZER_H
#define MUSIC_VISUALIZER_H

#include <gtk/gtk.h>
#include "music/player.h"

G_BEGIN_DECLS

GtkWidget *music_visualizer_new(MusicPlayer *player);

G_END_DECLS

#endif
