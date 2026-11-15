#ifndef MUSIC_APPLICATION_H
#define MUSIC_APPLICATION_H

#include <adwaita.h>

void music_application_activate(GtkApplication *app, gpointer user_data);
void music_application_open(GApplication *app, GFile **files, gint n_files, const gchar *hint, gpointer user_data);

#endif