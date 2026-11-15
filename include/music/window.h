
#ifndef MUSIC_WINDOW_H
#define MUSIC_WINDOW_H

#include <adwaita.h>

GtkWindow *music_window_new(GtkApplication *app);
void music_window_open_files(GtkWindow *window, GFile **files, gint n_files);

#endif
