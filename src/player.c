
#include "music/player.h"

struct _MusicPlayer {
    GObject parent_instance;
    GstElement *pipeline;
    GstBus *bus;
    gchar *current_uri;
    MusicPlayerState state;
    guint bus_watch_id;
};

G_DEFINE_TYPE(MusicPlayer, music_player, G_TYPE_OBJECT)

enum {
    SIGNAL_EOS,
    SIGNAL_ERROR,
    SIGNAL_STATE_CHANGED,
    N_SIGNALS
};

static guint signals[N_SIGNALS];

static gboolean music_player_bus_callback(GstBus *bus, GstMessage *message, gpointer user_data)
{
    (void) bus;

    MusicPlayer *player = MUSIC_PLAYER(user_data);

    switch (GST_MESSAGE_TYPE(message)) {
        case GST_MESSAGE_EOS:
            music_player_stop(player);
            g_signal_emit(player, signals[SIGNAL_EOS], 0);
            break;

        case GST_MESSAGE_ERROR: {
            GError *error = NULL;
            gchar *debug = NULL;

            gst_message_parse_error(message, &error, &debug);
            g_signal_emit(player, signals[SIGNAL_ERROR], 0, error->message);
            g_clear_error(&error);
            g_free(debug);
            music_player_stop(player);
            break;
        }

        default:
            break;
    }

    return G_SOURCE_CONTINUE;
}

static void music_player_set_state(MusicPlayer *player, MusicPlayerState state)
{
    if (player->state == state) {
        return;
    }

    player->state = state;
    g_signal_emit(player, signals[SIGNAL_STATE_CHANGED], 0, state);
}


static void music_player_dispose(GObject *object)
{
    MusicPlayer *player = MUSIC_PLAYER(object);

    if (player->bus_watch_id != 0) {
        g_source_remove(player->bus_watch_id);
        player->bus_watch_id = 0;
    }

    if (player->pipeline != NULL) {
        gst_element_set_state(player->pipeline, GST_STATE_NULL);
        g_clear_object(&player->pipeline);
    }

    g_clear_object(&player->bus);

    G_OBJECT_CLASS(music_player_parent_class)->dispose(object);
}

static void music_player_finalize(GObject *object)
{
    MusicPlayer *player = MUSIC_PLAYER(object);

    g_clear_pointer(&player->current_uri, g_free);

    G_OBJECT_CLASS(music_player_parent_class)->finalize(object);
}

static void music_player_class_init(MusicPlayerClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS(klass);

    object_class->dispose = music_player_dispose;
    object_class->finalize = music_player_finalize;

    signals[SIGNAL_EOS] = g_signal_new("eos", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 0);

    signals[SIGNAL_ERROR] = g_signal_new("error", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_STRING);

    signals[SIGNAL_STATE_CHANGED] = g_signal_new("state-changed", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_INT);
}

static void music_player_init(MusicPlayer *player)
{
    player->pipeline = gst_element_factory_make("playbin", "music-player");
    player->state = MUSIC_PLAYER_STATE_STOPPED;

    if (player->pipeline == NULL) {
        g_critical("GStreamer playbin elementi oluşturulamadı.");
        return;
    }

    player->bus = gst_element_get_bus(player->pipeline);
    player->bus_watch_id = gst_bus_add_watch(player->bus, music_player_bus_callback, player);
}

MusicPlayer *music_player_new(void)
{
    return g_object_new(MUSIC_TYPE_PLAYER, NULL);
}


gboolean music_player_open(MusicPlayer *player, const gchar *path, GError **error)
{
    g_return_val_if_fail(MUSIC_IS_PLAYER(player), FALSE);
    g_return_val_if_fail(path != NULL, FALSE);

    if (player->pipeline == NULL) {
        g_set_error_literal(error, GST_CORE_ERROR, GST_CORE_ERROR_MISSING_PLUGIN, "GStreamer oynatma motoru oluşturulamadı.");
        return FALSE;
    }

    g_autofree gchar *scheme = g_uri_parse_scheme(path);
    gchar *uri = scheme != NULL ? g_strdup(path) : gst_filename_to_uri(path, error);

    if (uri == NULL) {
        return FALSE;
    }

    gst_element_set_state(player->pipeline, GST_STATE_NULL);
    g_object_set(player->pipeline, "uri", uri, NULL);

    g_free(player->current_uri);
    player->current_uri = uri;

    music_player_set_state(player, MUSIC_PLAYER_STATE_STOPPED);

    return TRUE;
}

void music_player_play(MusicPlayer *player)
{
    g_return_if_fail(MUSIC_IS_PLAYER(player));

    if (player->pipeline == NULL) {
        return;
    }

    GstStateChangeReturn result = gst_element_set_state(player->pipeline, GST_STATE_PLAYING);

    if (result != GST_STATE_CHANGE_FAILURE) {
        music_player_set_state(player, MUSIC_PLAYER_STATE_PLAYING);
    }
}

void music_player_pause(MusicPlayer *player)
{
    g_return_if_fail(MUSIC_IS_PLAYER(player));

    if (player->pipeline == NULL) {
        return;
    }

    GstStateChangeReturn result = gst_element_set_state(player->pipeline, GST_STATE_PAUSED);

    if (result != GST_STATE_CHANGE_FAILURE) {
        music_player_set_state(player, MUSIC_PLAYER_STATE_PAUSED);
    }
}

void music_player_stop(MusicPlayer *player)
{
    g_return_if_fail(MUSIC_IS_PLAYER(player));

    if (player->pipeline == NULL) {
        return;
    }

    gst_element_set_state(player->pipeline, GST_STATE_READY);
    gst_element_set_state(player->pipeline, GST_STATE_NULL);

    music_player_set_state(player, MUSIC_PLAYER_STATE_STOPPED);
}

void music_player_set_volume(MusicPlayer *player, gdouble volume)
{
    g_return_if_fail(MUSIC_IS_PLAYER(player));

    if (player->pipeline == NULL) {
        return;
    }

    g_object_set(player->pipeline, "volume", CLAMP(volume, 0.0, 1.0), NULL);
}

gdouble music_player_get_volume(MusicPlayer *player)
{
    g_return_val_if_fail(MUSIC_IS_PLAYER(player), 0.75);

    if (player->pipeline == NULL) {
        return 0.75;
    }

    gdouble volume = 0.75;
    g_object_get(player->pipeline, "volume", &volume, NULL);

    return volume;
}

gboolean music_player_seek(MusicPlayer *player, gint64 position)
{
    g_return_val_if_fail(MUSIC_IS_PLAYER(player), FALSE);

    if (player->pipeline == NULL) {
        return FALSE;
    }

    return gst_element_seek_simple(player->pipeline, GST_FORMAT_TIME, GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT, position);
}

gint64 music_player_get_position(MusicPlayer *player)
{
    g_return_val_if_fail(MUSIC_IS_PLAYER(player), -1);

    gint64 position = 0;

    if (player->pipeline == NULL || !gst_element_query_position(player->pipeline, GST_FORMAT_TIME, &position)) {
        return -1;
    }

    return position;
}

gint64 music_player_get_duration(MusicPlayer *player)
{
    g_return_val_if_fail(MUSIC_IS_PLAYER(player), -1);

    gint64 duration = 0;

    if (player->pipeline == NULL || !gst_element_query_duration(player->pipeline, GST_FORMAT_TIME, &duration)) {
        return -1;
    }

    return duration;
}

MusicPlayerState music_player_get_state(MusicPlayer *player)
{
    g_return_val_if_fail(MUSIC_IS_PLAYER(player), MUSIC_PLAYER_STATE_STOPPED);

    return player->state;
}

const gchar *music_player_get_current_uri(MusicPlayer *player)
{
    g_return_val_if_fail(MUSIC_IS_PLAYER(player), NULL);

    return player->current_uri;
}
