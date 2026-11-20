
#include "music/mpris.h"

#include <gio/gio.h>
#include <string.h>

#define MPRIS_BUS_NAME "org.mpris.MediaPlayer2.io.github.sametbilir33.Music"
#define MPRIS_OBJECT_PATH "/org/mpris/MediaPlayer2"

struct _MusicMpris {
    GObject parent_instance;
    MusicPlayer *player;
    MusicPlaylist *playlist;
    GDBusConnection *connection;
    GDBusNodeInfo *introspection;
    guint registration_id;
    gboolean name_owned;
    MusicMprisCallbacks callbacks;
    gpointer callback_data;
};

G_DEFINE_TYPE(MusicMpris, music_mpris, G_TYPE_OBJECT)

static const gchar introspection_xml[] =
    "<node>"
    " <interface name='org.mpris.MediaPlayer2'>"
    "  <method name='Raise'/>"
    "  <method name='Quit'/>"
    "  <property name='CanQuit' type='b' access='read'/>"
    "  <property name='CanRaise' type='b' access='read'/>"
    "  <property name='HasTrackList' type='b' access='read'/>"
    "  <property name='Identity' type='s' access='read'/>"
    "  <property name='DesktopEntry' type='s' access='read'/>"
    "  <property name='SupportedUriSchemes' type='as' access='read'/>"
    "  <property name='SupportedMimeTypes' type='as' access='read'/>"
    " </interface>"
    " <interface name='org.mpris.MediaPlayer2.Player'>"
    "  <method name='Next'/>"
    "  <method name='Previous'/>"
    "  <method name='Pause'/>"
    "  <method name='PlayPause'/>"
    "  <method name='Stop'/>"
    "  <method name='Play'/>"
    "  <method name='Seek'>"
    "   <arg name='Offset' type='x' direction='in'/>"
    "  </method>"
    "  <method name='SetPosition'>"
    "   <arg name='TrackId' type='o' direction='in'/>"
    "   <arg name='Position' type='x' direction='in'/>"
    "  </method>"
    "  <method name='OpenUri'>"
    "   <arg name='Uri' type='s' direction='in'/>"
    "  </method>"
    "  <property name='PlaybackStatus' type='s' access='read'/>"
    "  <property name='LoopStatus' type='s' access='readwrite'/>"
    "  <property name='Rate' type='d' access='readwrite'/>"
    "  <property name='Shuffle' type='b' access='readwrite'/>"
    "  <property name='Metadata' type='a{sv}' access='read'/>"
    "  <property name='Volume' type='d' access='readwrite'/>"
    "  <property name='Position' type='x' access='read'/>"
    "  <property name='MinimumRate' type='d' access='read'/>"
    "  <property name='MaximumRate' type='d' access='read'/>"
    "  <property name='CanGoNext' type='b' access='read'/>"
    "  <property name='CanGoPrevious' type='b' access='read'/>"
    "  <property name='CanPlay' type='b' access='read'/>"
    "  <property name='CanPause' type='b' access='read'/>"
    "  <property name='CanSeek' type='b' access='read'/>"
    "  <property name='CanControl' type='b' access='read'/>"
    "  <signal name='Seeked'>"
    "   <arg name='Position' type='x'/>"
    "  </signal>"
    " </interface>"
    "</node>";

void music_mpris_emit_seeked(MusicMpris *mpris, gint64 position);

static gchar *music_mpris_get_track_title(MusicMpris *mpris)
{
    const gchar *uri = music_player_get_current_uri(mpris->player);
    if (uri == NULL) uri = music_playlist_get_current(mpris->playlist);
    if (uri == NULL) return g_strdup("Unknown");

    g_autofree gchar *path = g_filename_from_uri(uri, NULL, NULL);
    if (path == NULL) return g_path_get_basename(uri);
    return g_path_get_basename(path);
}

static GVariant *music_mpris_get_metadata(MusicMpris *mpris)
{
    GVariantBuilder builder;
    g_variant_builder_init(&builder, G_VARIANT_TYPE("a{sv}"));

    const gchar *uri = music_player_get_current_uri(mpris->player);
    if (uri == NULL) uri = music_playlist_get_current(mpris->playlist);

    if (uri != NULL) {
        g_autofree gchar *title = music_mpris_get_track_title(mpris);
        g_autofree gchar *track_id = g_strdup_printf("/org/mpris/MediaPlayer2/Track/%u", g_str_hash(uri));
        gint64 duration = music_player_get_duration(mpris->player) / 1000;

        g_variant_builder_add(&builder, "{sv}", "mpris:trackid", g_variant_new_object_path(track_id));
        g_variant_builder_add(&builder, "{sv}", "xesam:title", g_variant_new_string(title));
        g_variant_builder_add(&builder, "{sv}", "xesam:url", g_variant_new_string(uri));
        if (duration > 0) g_variant_builder_add(&builder, "{sv}", "mpris:length", g_variant_new_int64(duration));
    } else {
        g_variant_builder_add(&builder, "{sv}", "mpris:trackid", g_variant_new_object_path("/org/mpris/MediaPlayer2/TrackList/NoTrack"));
    }

    return g_variant_builder_end(&builder);
}

static const gchar *music_mpris_get_playback_status(MusicMpris *mpris)
{
    switch (music_player_get_state(mpris->player)) {
        case MUSIC_PLAYER_STATE_PLAYING: return "Playing";
        case MUSIC_PLAYER_STATE_PAUSED: return "Paused";
        default: return "Stopped";
    }
}

static gboolean music_mpris_seek_to(MusicMpris *mpris, gint64 position)
{
    gint64 duration = music_player_get_duration(mpris->player);
    if (duration <= 0 || position < 0 || position > duration) return FALSE;
    if (!music_player_seek(mpris->player, position)) return FALSE;
    music_mpris_emit_seeked(mpris, position);
    return TRUE;
}

void music_mpris_emit_seeked(MusicMpris *mpris, gint64 position)
{
    g_return_if_fail(MUSIC_IS_MPRIS(mpris));
    if (mpris->connection == NULL || position < 0) return;
    g_dbus_connection_emit_signal(mpris->connection, NULL, MPRIS_OBJECT_PATH, "org.mpris.MediaPlayer2.Player", "Seeked", g_variant_new("(x)", position / 1000), NULL);
}

static GVariant *music_mpris_get_property(GDBusConnection *connection, const gchar *sender, const gchar *object_path, const gchar *interface_name, const gchar *property_name, GError **error, gpointer user_data)
{
    (void) connection;
    (void) sender;
    (void) object_path;
    (void) error;

    MusicMpris *mpris = MUSIC_MPRIS(user_data);

    if (g_str_equal(interface_name, "org.mpris.MediaPlayer2")) {
        if (g_str_equal(property_name, "CanQuit")) return g_variant_new_boolean(FALSE);
        if (g_str_equal(property_name, "CanRaise")) return g_variant_new_boolean(FALSE);
        if (g_str_equal(property_name, "HasTrackList")) return g_variant_new_boolean(FALSE);
        if (g_str_equal(property_name, "Identity")) return g_variant_new_string("Music");
        if (g_str_equal(property_name, "DesktopEntry")) return g_variant_new_string("io.github.sametbilir33.Music");
        if (g_str_equal(property_name, "SupportedUriSchemes")) return g_variant_new_strv((const gchar *[]) { "file", NULL }, -1);
        if (g_str_equal(property_name, "SupportedMimeTypes")) return g_variant_new_strv((const gchar *[]) { "audio/mpeg", "audio/flac", "audio/ogg", "audio/wav", "audio/mp4", NULL }, -1);
    }

    if (g_str_equal(interface_name, "org.mpris.MediaPlayer2.Player")) {
        if (g_str_equal(property_name, "PlaybackStatus")) return g_variant_new_string(music_mpris_get_playback_status(mpris));
        if (g_str_equal(property_name, "LoopStatus")) {
            guint mode = mpris->callbacks.get_repeat != NULL ? mpris->callbacks.get_repeat(mpris->callback_data) : 0;
            return g_variant_new_string(mode == 1 ? "Playlist" : mode == 2 ? "Track" : "None");
        }
        if (g_str_equal(property_name, "Rate")) return g_variant_new_double(1.0);
        if (g_str_equal(property_name, "Shuffle")) return g_variant_new_boolean(mpris->callbacks.get_shuffle != NULL && mpris->callbacks.get_shuffle(mpris->callback_data));
        if (g_str_equal(property_name, "Metadata")) return music_mpris_get_metadata(mpris);
        if (g_str_equal(property_name, "Volume")) return g_variant_new_double(music_player_get_volume(mpris->player));
        if (g_str_equal(property_name, "Position")) return g_variant_new_int64(music_player_get_position(mpris->player) / 1000);
        if (g_str_equal(property_name, "MinimumRate")) return g_variant_new_double(1.0);
        if (g_str_equal(property_name, "MaximumRate")) return g_variant_new_double(1.0);
        if (g_str_equal(property_name, "CanGoNext")) return g_variant_new_boolean(music_playlist_get_mode(mpris->playlist) != MUSIC_PLAYLIST_MODE_SINGLE && music_playlist_get_count(mpris->playlist) > 1);
        if (g_str_equal(property_name, "CanGoPrevious")) return g_variant_new_boolean(music_playlist_get_mode(mpris->playlist) != MUSIC_PLAYLIST_MODE_SINGLE && music_playlist_get_count(mpris->playlist) > 1);
        if (g_str_equal(property_name, "CanPlay")) return g_variant_new_boolean(music_player_get_current_uri(mpris->player) != NULL || music_playlist_get_current(mpris->playlist) != NULL);
        if (g_str_equal(property_name, "CanPause")) return g_variant_new_boolean(music_player_get_current_uri(mpris->player) != NULL);
        if (g_str_equal(property_name, "CanSeek")) return g_variant_new_boolean(music_player_get_duration(mpris->player) > 0);
        if (g_str_equal(property_name, "CanControl")) return g_variant_new_boolean(TRUE);
    }

    return NULL;
}

static gboolean music_mpris_set_property(GDBusConnection *connection, const gchar *sender, const gchar *object_path, const gchar *interface_name, const gchar *property_name, GVariant *value, GError **error, gpointer user_data)
{
    (void) connection;
    (void) sender;
    (void) object_path;

    MusicMpris *mpris = MUSIC_MPRIS(user_data);

    if (!g_str_equal(interface_name, "org.mpris.MediaPlayer2.Player")) return FALSE;

    if (g_str_equal(property_name, "Volume")) {
        music_player_set_volume(mpris->player, g_variant_get_double(value));
        music_mpris_update(mpris);
        return TRUE;
    }

    if (g_str_equal(property_name, "Shuffle")) {
    if (mpris->callbacks.set_shuffle == NULL) return FALSE;
    mpris->callbacks.set_shuffle(g_variant_get_boolean(value), mpris->callback_data);
    music_mpris_update(mpris);
    return TRUE;
    }
    if (g_str_equal(property_name, "LoopStatus")) {
    if (mpris->callbacks.set_repeat == NULL) return FALSE;
    const gchar *mode = g_variant_get_string(value, NULL);
    if (g_str_equal(mode, "None")) mpris->callbacks.set_repeat(0, mpris->callback_data);
    else if (g_str_equal(mode, "Playlist")) mpris->callbacks.set_repeat(1, mpris->callback_data);
    else if (g_str_equal(mode, "Track")) mpris->callbacks.set_repeat(2, mpris->callback_data);
    else return FALSE;
    music_mpris_update(mpris);
    return TRUE;
    }
    if (g_str_equal(property_name, "Rate")) {
        if (g_variant_get_double(value) == 1.0) return TRUE;
        g_set_error_literal(error, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS, "Only playback rate 1.0 is supported.");
        return FALSE;
    }   

    return FALSE;
}

static void music_mpris_next(MusicMpris *mpris)
{
    if (mpris->callbacks.next != NULL) mpris->callbacks.next(mpris->callback_data);
    else if (music_playlist_next(mpris->playlist)) {
        const gchar *uri = music_playlist_get_current(mpris->playlist);
        if (uri != NULL && music_player_open(mpris->player, uri, NULL)) music_player_play(mpris->player);
    }
    music_mpris_update(mpris);
}

static void music_mpris_previous(MusicMpris *mpris)
{
    if (mpris->callbacks.previous != NULL) mpris->callbacks.previous(mpris->callback_data);
    else if (music_playlist_previous(mpris->playlist)) {
        const gchar *uri = music_playlist_get_current(mpris->playlist);
        if (uri != NULL && music_player_open(mpris->player, uri, NULL)) music_player_play(mpris->player);
    }
    music_mpris_update(mpris);
}

static void music_mpris_method_call(GDBusConnection *connection, const gchar *sender, const gchar *object_path, const gchar *interface_name, const gchar *method_name, GVariant *parameters, GDBusMethodInvocation *invocation, gpointer user_data)
{
    (void) connection;
    (void) sender;
    (void) object_path;

    MusicMpris *mpris = MUSIC_MPRIS(user_data);

    if (g_str_equal(interface_name, "org.mpris.MediaPlayer2")) {
        if (g_str_equal(method_name, "Raise") || g_str_equal(method_name, "Quit")) {
            g_dbus_method_invocation_return_value(invocation, NULL);
            return;
        }
    }

    if (g_str_equal(interface_name, "org.mpris.MediaPlayer2.Player")) {
        if (g_str_equal(method_name, "Next")) music_mpris_next(mpris);
        else if (g_str_equal(method_name, "Previous")) music_mpris_previous(mpris);
        else if (g_str_equal(method_name, "Pause")) music_player_pause(mpris->player);
        else if (g_str_equal(method_name, "PlayPause")) {
            if (music_player_get_state(mpris->player) == MUSIC_PLAYER_STATE_PLAYING) music_player_pause(mpris->player);
            else music_player_play(mpris->player);
        }
        else if (g_str_equal(method_name, "Stop")) music_player_stop(mpris->player);
        else if (g_str_equal(method_name, "Play")) music_player_play(mpris->player);
        else if (g_str_equal(method_name, "Seek")) {
            gint64 offset;
            g_variant_get(parameters, "(x)", &offset);

            gint64 current = music_player_get_position(mpris->player);
            gint64 duration = music_player_get_duration(mpris->player);

            if (current >= 0 && duration > 0 && current <= duration && offset <= G_MAXINT64 / 1000 && offset >= G_MININT64 / 1000) {
                gint64 delta = offset * 1000;

                if (delta > 0 && delta > duration - current) music_mpris_next(mpris);
                else if (delta < 0 && delta < -current) music_mpris_seek_to(mpris, 0);
                else music_mpris_seek_to(mpris, current + delta);
            }
        }
        else if (g_str_equal(method_name, "SetPosition")) {
            const gchar *track_id;
            gint64 position;
            g_variant_get(parameters, "(&ox)", &track_id, &position);

            g_autofree gchar *expected = NULL;
            const gchar *uri = music_player_get_current_uri(mpris->player);
            if (uri == NULL) uri = music_playlist_get_current(mpris->playlist);

            if (uri != NULL) expected = g_strdup_printf("/org/mpris/MediaPlayer2/Track/%u", g_str_hash(uri));

            if (expected != NULL && g_str_equal(track_id, expected) && position >= 0 && position <= G_MAXINT64 / 1000) {
                music_mpris_seek_to(mpris, position * 1000);
            }
        }
        else if (g_str_equal(method_name, "OpenUri")) {
            const gchar *uri;
            g_variant_get(parameters, "(&s)", &uri);

            g_autofree gchar *path = g_filename_from_uri(uri, NULL, NULL);

            if (path != NULL && music_playlist_open_single(mpris->playlist, path, NULL) && music_player_open(mpris->player, uri, NULL)) {
                music_player_play(mpris->player);
            }
        }
        else {
            g_dbus_method_invocation_return_error(invocation, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_METHOD, "Unknown MPRIS method");
            return;
        }

        music_mpris_update(mpris);
        g_dbus_method_invocation_return_value(invocation, NULL);
        return;
    }

    g_dbus_method_invocation_return_error(invocation, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_METHOD, "Unknown MPRIS method");
}

static const GDBusInterfaceVTable interface_vtable = {
    .method_call = music_mpris_method_call,
    .get_property = music_mpris_get_property,
    .set_property = music_mpris_set_property
};

static void music_mpris_dispose(GObject *object)
{
    MusicMpris *mpris = MUSIC_MPRIS(object);
    music_mpris_stop(mpris);
    g_clear_object(&mpris->player);
    g_clear_object(&mpris->connection);
    G_OBJECT_CLASS(music_mpris_parent_class)->dispose(object);
}

static void music_mpris_finalize(GObject *object)
{
    MusicMpris *mpris = MUSIC_MPRIS(object);
    g_clear_pointer(&mpris->introspection, g_dbus_node_info_unref);
    G_OBJECT_CLASS(music_mpris_parent_class)->finalize(object);
}

static void music_mpris_class_init(MusicMprisClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS(klass);
    object_class->dispose = music_mpris_dispose;
    object_class->finalize = music_mpris_finalize;
}

static void music_mpris_init(MusicMpris *mpris)
{
    (void) mpris;
}

MusicMpris *music_mpris_new(MusicPlayer *player, MusicPlaylist *playlist)
{
    g_return_val_if_fail(MUSIC_IS_PLAYER(player), NULL);
    g_return_val_if_fail(playlist != NULL, NULL);

    MusicMpris *mpris = g_object_new(MUSIC_TYPE_MPRIS, NULL);
    mpris->player = g_object_ref(player);
    mpris->playlist = playlist;
    return mpris;
}

void music_mpris_set_callbacks(MusicMpris *mpris, const MusicMprisCallbacks *callbacks, gpointer user_data)
{
    g_return_if_fail(MUSIC_IS_MPRIS(mpris));
    memset(&mpris->callbacks, 0, sizeof(mpris->callbacks));
    if (callbacks != NULL) mpris->callbacks = *callbacks;
    mpris->callback_data = user_data;
    music_mpris_update(mpris);
}

gboolean music_mpris_start(MusicMpris *mpris, GError **error)
{
    g_return_val_if_fail(MUSIC_IS_MPRIS(mpris), FALSE);
    if (mpris->connection != NULL) return TRUE;

    mpris->connection = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, error);
    if (mpris->connection == NULL) return FALSE;

    mpris->introspection = g_dbus_node_info_new_for_xml(introspection_xml, error);
    if (mpris->introspection == NULL) {
        g_clear_object(&mpris->connection);
        return FALSE;
    }

    GVariant *reply = g_dbus_connection_call_sync(mpris->connection, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "RequestName", g_variant_new("(su)", MPRIS_BUS_NAME, 0u), G_VARIANT_TYPE("(u)"), G_DBUS_CALL_FLAGS_NONE, -1, NULL, error);
    if (reply == NULL) {
        g_clear_pointer(&mpris->introspection, g_dbus_node_info_unref);
        g_clear_object(&mpris->connection);
        return FALSE;
    }

    guint name_result;
    g_variant_get(reply, "(u)", &name_result);
    g_variant_unref(reply);

    if (name_result != 1 && name_result != 4) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_EXISTS, "The MPRIS bus name is already owned");
        g_clear_pointer(&mpris->introspection, g_dbus_node_info_unref);
        g_clear_object(&mpris->connection);
        return FALSE;
    }

    mpris->name_owned = TRUE;
    mpris->registration_id = g_dbus_connection_register_object(mpris->connection, MPRIS_OBJECT_PATH, mpris->introspection->interfaces[0], &interface_vtable, mpris, NULL, error);
    if (mpris->registration_id == 0) {
        music_mpris_stop(mpris);
        return FALSE;
    }

    guint player_registration = g_dbus_connection_register_object(mpris->connection, MPRIS_OBJECT_PATH, mpris->introspection->interfaces[1], &interface_vtable, mpris, NULL, error);
    if (player_registration == 0) {
        music_mpris_stop(mpris);
        return FALSE;
    }

    g_object_set_data(G_OBJECT(mpris), "player-registration-id", GUINT_TO_POINTER(player_registration));
    music_mpris_update(mpris);
    return TRUE;
}

void music_mpris_stop(MusicMpris *mpris)
{
    g_return_if_fail(MUSIC_IS_MPRIS(mpris));

    if (mpris->connection != NULL) {
        guint player_registration = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(mpris), "player-registration-id"));
        if (player_registration != 0) g_dbus_connection_unregister_object(mpris->connection, player_registration);
        if (mpris->registration_id != 0) g_dbus_connection_unregister_object(mpris->connection, mpris->registration_id);
        mpris->registration_id = 0;
        g_object_set_data(G_OBJECT(mpris), "player-registration-id", NULL);

        if (mpris->name_owned) {
            g_dbus_connection_call(mpris->connection, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "ReleaseName", g_variant_new("(s)", MPRIS_BUS_NAME), NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL, NULL);
            mpris->name_owned = FALSE;
        }
    }

    g_clear_pointer(&mpris->introspection, g_dbus_node_info_unref);
    g_clear_object(&mpris->connection);
}

void music_mpris_update(MusicMpris *mpris)
{
    g_return_if_fail(MUSIC_IS_MPRIS(mpris));
    if (mpris->connection == NULL || mpris->registration_id == 0) return;

    GVariantBuilder changed;
    GVariantBuilder invalidated;
    g_variant_builder_init(&changed, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_init(&invalidated, G_VARIANT_TYPE("as"));

    g_variant_builder_add(&changed, "{sv}", "PlaybackStatus", g_variant_new_string(music_mpris_get_playback_status(mpris)));
    g_variant_builder_add(&changed, "{sv}", "Metadata", music_mpris_get_metadata(mpris));
    g_variant_builder_add(&changed, "{sv}", "Volume", g_variant_new_double(music_player_get_volume(mpris->player)));
    guint repeat_mode = mpris->callbacks.get_repeat != NULL ? mpris->callbacks.get_repeat(mpris->callback_data) : 0;
    g_variant_builder_add(&changed, "{sv}", "Shuffle", g_variant_new_boolean(mpris->callbacks.get_shuffle != NULL && mpris->callbacks.get_shuffle(mpris->callback_data)));
    g_variant_builder_add(&changed, "{sv}", "LoopStatus", g_variant_new_string(repeat_mode == 1 ? "Playlist" : repeat_mode == 2 ? "Track" : "None"));
    g_variant_builder_add(&changed, "{sv}", "CanGoNext", g_variant_new_boolean(music_playlist_get_mode(mpris->playlist) != MUSIC_PLAYLIST_MODE_SINGLE && music_playlist_get_count(mpris->playlist) > 1));
    g_variant_builder_add(&changed, "{sv}", "CanGoPrevious", g_variant_new_boolean(music_playlist_get_mode(mpris->playlist) != MUSIC_PLAYLIST_MODE_SINGLE && music_playlist_get_count(mpris->playlist) > 1));
    g_variant_builder_add(&changed, "{sv}", "CanPlay", g_variant_new_boolean(music_player_get_current_uri(mpris->player) != NULL || music_playlist_get_current(mpris->playlist) != NULL));
    g_variant_builder_add(&changed, "{sv}", "CanPause", g_variant_new_boolean(music_player_get_current_uri(mpris->player) != NULL));
    g_variant_builder_add(&changed, "{sv}", "CanSeek", g_variant_new_boolean(music_player_get_duration(mpris->player) > 0));
    
    g_dbus_connection_emit_signal(mpris->connection, NULL, MPRIS_OBJECT_PATH, "org.freedesktop.DBus.Properties", "PropertiesChanged", g_variant_new("(s@a{sv}@as)", "org.mpris.MediaPlayer2.Player", g_variant_builder_end(&changed), g_variant_builder_end(&invalidated)), NULL);
}
