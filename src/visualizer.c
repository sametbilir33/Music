#include "music/visualizer.h"

#include <math.h>

#define VISUALIZER_HEIGHT 84
#define VISUALIZER_FPS 30

typedef struct {
    MusicPlayer *player;
    GtkWidget *area;
    guint timer_id;
    gdouble bars[MUSIC_SPECTRUM_BANDS];
} MusicVisualizer;

static gboolean music_visualizer_draw_tick(gpointer user_data)
{
    MusicVisualizer *visualizer = user_data;
    const gdouble *spectrum = music_player_get_spectrum(visualizer->player);

    if (spectrum != NULL) {
        for (guint i = 0; i < MUSIC_SPECTRUM_BANDS; i++) {
            visualizer->bars[i] += (spectrum[i] - visualizer->bars[i]) * 0.35;
        }
    }

    gtk_widget_queue_draw(visualizer->area);
    return G_SOURCE_CONTINUE;
}

static void music_visualizer_draw(GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer user_data)
{
    (void) area;

    MusicVisualizer *visualizer = user_data;
    const guint half_bands = MUSIC_SPECTRUM_BANDS / 2;
    const gdouble spacing = 4.0;
    const gdouble bar_width = ((gdouble) width - spacing * (MUSIC_SPECTRUM_BANDS - 1)) / MUSIC_SPECTRUM_BANDS;

    GdkRGBA color;
    gtk_widget_get_color(visualizer->area, &color);

    for (guint i = 0; i < MUSIC_SPECTRUM_BANDS; i++) {
        guint distance = MIN(i, MUSIC_SPECTRUM_BANDS - 1 - i);
        guint source_index = half_bands - 1 - distance;
        gdouble value = visualizer->bars[source_index];
        gdouble bar_height = MAX(3.0, value * height);
        gdouble x = i * (bar_width + spacing);
        gdouble y = height - bar_height;

        cairo_set_source_rgba(cr, color.red, color.green, color.blue, 0.45 + value * 0.55);
        cairo_rectangle(cr, x, y, bar_width, bar_height);
        cairo_fill(cr);
    }
}

static void music_visualizer_free(gpointer data)
{
    MusicVisualizer *visualizer = data;

    if (visualizer->timer_id != 0) g_source_remove(visualizer->timer_id);

    g_object_unref(visualizer->player);
    g_free(visualizer);
}

GtkWidget *music_visualizer_new(MusicPlayer *player)
{
    g_return_val_if_fail(MUSIC_IS_PLAYER(player), NULL);

    MusicVisualizer *visualizer = g_new0(MusicVisualizer, 1);
    visualizer->player = g_object_ref(player);
    visualizer->area = gtk_drawing_area_new();

    gtk_widget_set_size_request(visualizer->area, -1, VISUALIZER_HEIGHT);
    gtk_widget_set_hexpand(visualizer->area, TRUE);

    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(visualizer->area), music_visualizer_draw, visualizer, NULL);

    visualizer->timer_id = g_timeout_add(1000 / VISUALIZER_FPS, music_visualizer_draw_tick, visualizer);

    g_object_set_data_full(G_OBJECT(visualizer->area), "music-visualizer", visualizer, music_visualizer_free);

    return visualizer->area;
}
