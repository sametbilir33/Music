
#include <adwaita.h>
#include <gst/gst.h>
#include "music/application.h"

int main(int argc, char *argv[])
{
    gst_init(&argc, &argv);
    g_autoptr(AdwApplication) app = adw_application_new("io.github.sametbilir33.Music", G_APPLICATION_HANDLES_OPEN);
    g_signal_connect(app, "activate", G_CALLBACK(music_application_activate), NULL);
    g_signal_connect(app, "open", G_CALLBACK(music_application_open), NULL);
    return g_application_run(G_APPLICATION(app), argc, argv);
}