/**
 * PLAY
 * play-gstreamer.c: GStreamer library backend
 * Copyright (C) 2011-2013 Michal Ratajsky <michal.ratajsky@gmail.com>
 */
#include "play-common.h"
#include "play-gstreamer.h"
#include "play-queue-item.h"

#ifdef GSTREAMER_1_0 // TODO
  #define PLAY_GST_PLAYBIN "playbin"
#else
  #define PLAY_GST_PLAYBIN "playbin2"
#endif

// This enum is not present in any header file:
// http://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-base-plugins/html/gst-plugins-base-plugins-playbin2.html#GstPlayBin2--flags
typedef enum {
    GST_PLAY_FLAG_VIDEO         = (1 << 0),
    GST_PLAY_FLAG_AUDIO         = (1 << 1),
    GST_PLAY_FLAG_TEXT          = (1 << 2),
    GST_PLAY_FLAG_VIS           = (1 << 3),
    GST_PLAY_FLAG_SOFT_VOLUME   = (1 << 4),
    GST_PLAY_FLAG_NATIVE_AUDIO  = (1 << 5),
    GST_PLAY_FLAG_NATIVE_VIDEO  = (1 << 6),
    GST_PLAY_FLAG_DOWNLOAD      = (1 << 7),
    GST_PLAY_FLAG_BUFFERING     = (1 << 8),
    GST_PLAY_FLAG_DEINTERLACE   = (1 << 9)
} GstPlayFlags;

G_DEFINE_TYPE (PlayGstreamer, play_gstreamer, G_TYPE_OBJECT);

// Initialize a newly created gstreamer object
static gboolean gstreamer_gst_initialize (PlayGstreamer *gstreamer,
                                          GError **error);

// Process a metadata tag
static void gstreamer_gst_bus_tag (const GstTagList *list,
                                   const gchar *tag,
                                   PlayGstreamer *gstreamer);

// GStreamer bus message handler
static gboolean gstreamer_gst_bus_message (GstBus *bus,
                                           GstMessage *message,
                                           PlayGstreamer *gstreamer);

// Signals
enum {
    BUFFERING,
    DURATION_UPDATED,
    END_OF_STREAM,
    ERROR,
    METADATA_UPDATED,
    STATE_PLAYING,
    STATE_PAUSED,
    STATE_STOPPED,
    LAST_SIGNAL
};
static guint signals[LAST_SIGNAL];

GQuark play_gstreamer_get_error_quark (void)
{
    static GQuark quark;

    if (quark == 0)
        quark = g_quark_from_static_string ("play-gstreamer-error-quark");

    return quark;
}

// Global initialization function
// Should be called soon after the program start
void play_gstreamer_global_initialize (int *argcp, char ***argvp)
{
    static gboolean initialized = FALSE;

    g_return_if_fail (!initialized);

    gst_init (argcp, argvp);
    initialized = TRUE;
}

// GObject/finalize
static void play_gstreamer_finalize (GObject *object)
{
    PlayGstreamer *gstreamer = PLAY_GSTREAMER (object);

    // Clean up
    if (gstreamer->pipe) {
        // Make sure the playbin is in the NULL state, otherwise the unref
        // calls would cause warnings
        play_gstreamer_set_state_stopped (gstreamer);
        if (gstreamer->pipe)
            gst_object_unref (GST_OBJECT (gstreamer->pipe));
    }
    if (gstreamer->current)
        g_object_unref (gstreamer->current);

    // Chain up to the parent class
    G_OBJECT_CLASS (play_gstreamer_parent_class)->finalize (object);
}

// GObject/class init
static void play_gstreamer_class_init (PlayGstreamerClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

    gobject_class->finalize = play_gstreamer_finalize;

    // Signals
    signals[BUFFERING] =
        g_signal_new ("buffering",
                      G_TYPE_FROM_CLASS (gobject_class),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (PlayGstreamerClass, buffering),
                      NULL,
                      NULL,
                      g_cclosure_marshal_VOID__UINT,
                      G_TYPE_NONE,
                      1,
                      G_TYPE_UINT);
    signals[DURATION_UPDATED] =
        g_signal_new ("duration-updated",
                      G_TYPE_FROM_CLASS (gobject_class),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (PlayGstreamerClass, duration_updated),
                      NULL,
                      NULL,
                      g_cclosure_marshal_VOID__VOID,
                      G_TYPE_NONE,
                      0);
    signals[END_OF_STREAM] =
        g_signal_new ("end-of-stream",
                      G_TYPE_FROM_CLASS (gobject_class),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (PlayGstreamerClass, end_of_stream),
                      NULL,
                      NULL,
                      g_cclosure_marshal_VOID__VOID,
                      G_TYPE_NONE,
                      0);
    signals[ERROR] =
        g_signal_new ("error",
                      G_TYPE_FROM_CLASS (gobject_class),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (PlayGstreamerClass, error),
                      NULL,
                      NULL,
                      g_cclosure_marshal_VOID__POINTER,
                      G_TYPE_NONE,
                      1,
                      G_TYPE_POINTER);
    signals[METADATA_UPDATED] =
        g_signal_new ("metadata-updated",
                      G_TYPE_FROM_CLASS (gobject_class),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (PlayGstreamerClass, metadata_updated),
                      NULL,
                      NULL,
                      play_marshal_VOID__ENUM_STRING,
                      G_TYPE_NONE,
                      2,
                      PLAY_TYPE_METADATA,
                      G_TYPE_STRING);
    signals[STATE_PLAYING] =
        g_signal_new ("state-playing",
                      G_TYPE_FROM_CLASS (gobject_class),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (PlayGstreamerClass, state_playing),
                      NULL,
                      NULL,
                      g_cclosure_marshal_VOID__VOID,
                      G_TYPE_NONE,
                      0);
    signals[STATE_PAUSED] =
        g_signal_new ("state-paused",
                      G_TYPE_FROM_CLASS (gobject_class),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (PlayGstreamerClass, state_paused),
                      NULL,
                      NULL,
                      g_cclosure_marshal_VOID__VOID,
                      G_TYPE_NONE,
                      0);
    signals[STATE_STOPPED] =
        g_signal_new ("state-stopped",
                      G_TYPE_FROM_CLASS (gobject_class),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (PlayGstreamerClass, state_stopped),
                      NULL,
                      NULL,
                      g_cclosure_marshal_VOID__VOID,
                      G_TYPE_NONE,
                      0);
}

// GObject/init
static void play_gstreamer_init (PlayGstreamer *gstreamer)
{
}

// Create a new gstreamer object
PlayGstreamer *play_gstreamer_new (GError **error)
{
    PlayGstreamer *gstreamer;

    gstreamer = PLAY_GSTREAMER (g_object_new (PLAY_TYPE_GSTREAMER, NULL));
    if (!gstreamer_gst_initialize (gstreamer, error)) {
        g_object_unref (gstreamer);
        return NULL;
    }
    return gstreamer;
}

// Set a new queue item to be played
// The playback should be stopped before using this function and then started
// again to play the new track
// Returns TRUE on success
gboolean play_gstreamer_set_item (PlayGstreamer *gstreamer, PlayQueueItem *item)
{
    const gchar *uri;

    g_return_val_if_fail (PLAY_IS_GSTREAMER (gstreamer), FALSE);
    g_return_val_if_fail (PLAY_IS_QUEUE_ITEM (item), FALSE);

    // Retrieve the URI and set it in the playbin "uri" property
    uri = play_queue_item_get_uri (item);
    if (G_UNLIKELY (!uri))
        return FALSE;

    g_object_set (G_OBJECT (gstreamer->playbin), "uri", uri, NULL);

    // Remember the current queue item
    if (gstreamer->current)
        g_object_unref (gstreamer->current);

    gstreamer->current = g_object_ref (item);
    return TRUE;
}

// Retrieve the current state of the gstreamer backend and store it in
// the passed PlayGstreamerState
// Returns TRUE if the state was successfully retrieved
gboolean play_gstreamer_get_state (PlayGstreamer *gstreamer,
                                   PlayGstreamerState *state)
{
    g_return_val_if_fail (PLAY_IS_GSTREAMER (gstreamer), FALSE);
    g_return_val_if_fail (state, FALSE);

    switch (GST_STATE (gstreamer->pipe)) {
        case GST_STATE_PAUSED:
            *state = PLAY_GSTREAMER_PAUSED;
            break;
        case GST_STATE_PLAYING:
            *state = PLAY_GSTREAMER_PLAYING;
            break;
        default:
            *state = PLAY_GSTREAMER_STOPPED;
            break;
    }
    return TRUE;
}

// Set the current gstreamer backend status to PLAYING
// Use this function to start playing after an item was set using the
// play_gstreamer_set_item() function
// Returns TRUE if the state was successfully set or if it's going to be
// set asynchronously
gboolean play_gstreamer_set_state_playing (PlayGstreamer *gstreamer)
{
    GstStateChangeReturn ret;

    g_return_val_if_fail (PLAY_IS_GSTREAMER (gstreamer), FALSE);

    ret = gst_element_set_state (gstreamer->pipe, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE)
        return FALSE;

    return TRUE;
}

// Set the current gstreamer backend status to PAUSED
// Returns TRUE if the state was successfully set or if it's going to be
// set asynchronously
gboolean play_gstreamer_set_state_paused (PlayGstreamer *gstreamer)
{
    GstStateChangeReturn ret;

    g_return_val_if_fail (PLAY_IS_GSTREAMER (gstreamer), FALSE);

    ret = gst_element_set_state (gstreamer->pipe, GST_STATE_PAUSED);
    if (ret == GST_STATE_CHANGE_FAILURE)
        return FALSE;

    return TRUE;
}

// Set the current gstreamer backend status to STOPPED (stop playing)
// Returns TRUE if the state was successfully set or if it's going to be
// set asynchronously
gboolean play_gstreamer_set_state_stopped (PlayGstreamer *gstreamer)
{
    GstStateChangeReturn ret;

    g_return_val_if_fail (PLAY_IS_GSTREAMER (gstreamer), FALSE);

    ret = gst_element_set_state (gstreamer->pipe, GST_STATE_NULL);
    if (ret == GST_STATE_CHANGE_FAILURE)
        return FALSE;

    return TRUE;
}

// Retrieve the current PlayQueueItem set by play_gstreamer_set_item()
// Use g_object_ref() to keep the reference
PlayQueueItem *play_gstreamer_get_current (PlayGstreamer *gstreamer)
{
    g_return_val_if_fail (PLAY_IS_GSTREAMER (gstreamer), NULL);

    return gstreamer->current;
}

// Retrieve the current volume and store it in the passed gdouble
// Returns TRUE if the volume was successfully retrieved
gboolean play_gstreamer_get_volume (PlayGstreamer *gstreamer, gdouble *volume)
{
    g_return_val_if_fail (PLAY_IS_GSTREAMER (gstreamer), FALSE);
    g_return_val_if_fail (volume, FALSE);

    g_object_get (G_OBJECT (gstreamer->playbin), "volume", volume, NULL);
    return TRUE;
}

// Set a new volume
// The value should be between 0.0 and 1.0
// Returns TRUE on success
gboolean play_gstreamer_set_volume (PlayGstreamer *gstreamer, gdouble volume)
{
    g_return_val_if_fail (PLAY_IS_GSTREAMER (gstreamer), FALSE);

    g_object_set (G_OBJECT (gstreamer->playbin), "volume",
        CLAMP (volume, 0.0, 1.0),
        NULL);
    return TRUE;
}

// Set a new volume
// The value is the margin between the old and new volume
// Returns TRUE on success
gboolean play_gstreamer_set_volume_relative (PlayGstreamer *gstreamer,
                                             gdouble offset)
{
    gdouble volume;

    g_return_val_if_fail (PLAY_IS_GSTREAMER (gstreamer), FALSE);

    if (!play_gstreamer_get_volume (gstreamer, &volume))
        return FALSE;

    return play_gstreamer_set_volume (gstreamer, volume + offset);
}

// Retrieve the current mute status
// The value is stored in the passed gboolean
// Returns TRUE if the status was successfully retrieved
gboolean play_gstreamer_get_mute (PlayGstreamer *gstreamer, gboolean *mute)
{
    g_return_val_if_fail (PLAY_IS_GSTREAMER (gstreamer), FALSE);
    g_return_val_if_fail (mute, FALSE);

    g_object_get (G_OBJECT (gstreamer->playbin), "mute", mute, NULL);
    return TRUE;
}

// Mute or unmute depending on the passed value
// Returns TRUE on success
gboolean play_gstreamer_set_mute (PlayGstreamer *gstreamer, gboolean mute)
{
    g_return_val_if_fail (PLAY_IS_GSTREAMER (gstreamer), FALSE);

    g_object_set (G_OBJECT (gstreamer->playbin), "mute", mute, NULL);
    return TRUE;
}

// Toggle mute
// Returns TRUE on success
gboolean play_gstreamer_toggle_mute (PlayGstreamer *gstreamer)
{
    gboolean mute;

    g_return_val_if_fail (PLAY_IS_GSTREAMER (gstreamer), FALSE);

    if (!play_gstreamer_get_mute (gstreamer, &mute))
        return FALSE;

    return play_gstreamer_set_mute (gstreamer, !mute);
}

// Retrieve the duration of the stream currently playing in nanoseconds
// Use the PLAY_GSTREAMER_TIME_* macros the convert the value
// Returns TRUE if the duration was successfully retrieved
gboolean play_gstreamer_get_duration (PlayGstreamer *gstreamer, gint64 *duration)
{
    GstFormat format = GST_FORMAT_TIME;
    gint64    d;

    g_return_val_if_fail (PLAY_IS_GSTREAMER (gstreamer), FALSE);

    if (!gst_element_query_duration (gstreamer->pipe, &format, &d))
        return FALSE;
    if (d >= 0) {
        *duration = d;
        return TRUE;
    }
    return FALSE;
}

// Retrieve the position of the stream currently playing in nanoseconds
// Use the PLAY_GSTREAMER_TIME_* macros the convert the value
// Returns TRUE if the duration was successfully retrieved
gboolean play_gstreamer_get_position (PlayGstreamer *gstreamer, gint64 *position)
{
    GstFormat format = GST_FORMAT_TIME;
    gint64    p;

    g_return_val_if_fail (PLAY_IS_GSTREAMER (gstreamer), FALSE);

    if (!gst_element_query_position (gstreamer->pipe, &format, &p))
        return FALSE;
    if (p >= 0) {
        *position = p;
        return TRUE;
    }
    return FALSE;
}

// Set the position in the current stream
// The position is the number of nanoseconds, relative to the current position
// Returns TRUE on success
gboolean play_gstreamer_set_position (PlayGstreamer *gstreamer, gint64 offset)
{
    GstFormat format = GST_FORMAT_TIME;
    gint64    p_current;
    gint64    p_new;
    gint64    d;

    g_return_val_if_fail (PLAY_IS_GSTREAMER (gstreamer), FALSE);

    // Read the current position
    if (!gst_element_query_position (gstreamer->pipe, &format, &p_current))
        return FALSE;

    // Seek to the new position
    p_new = p_current + offset;
    if (p_new < 0)
        p_new = 0;

    // Seeking over the length of the current stream normally just causes
    // the stream to end but seeking on a paused stream would cause wrong
    // information to be displayed
    if (gst_element_query_duration (gstreamer->pipe, &format, &d))
        p_new = CLAMP (p_new, 0, d);

    return gst_element_seek_simple (
        gstreamer->playbin,
        GST_FORMAT_TIME,
        GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT,
        p_new);
}

// Set the position in the current stream
// The position is the number of seconds, relative to the current position
// Returns TRUE on success
gboolean play_gstreamer_set_position_seconds (PlayGstreamer *gstreamer,
                                              gint offset)
{
    return play_gstreamer_set_position (
        gstreamer,
        offset * GST_SECOND);
}

// Initialize a newly created gstreamer object
static gboolean gstreamer_gst_initialize (PlayGstreamer *gstreamer, GError **error)
{
    GstElement *sink;
    GstBus     *bus;

    // Gstreamer pipeline
    gstreamer->pipe = gst_pipeline_new ("pipeline");
    if (!gstreamer->pipe) {
        g_set_error (
            error,
            PLAY_GSTREAMER_ERROR,
            PLAY_GSTREAMER_ERROR_PIPELINE_FAILED,
            "Could not create a GStreamer pipeline");
        return FALSE;
    }
    // Gstreamer playbin
    // Might fail if the plugin is not present
    gstreamer->playbin = gst_element_factory_make (
        PLAY_GST_PLAYBIN,
        NULL);
    if (!gstreamer->playbin) {
        g_set_error (
            error,
            PLAY_GSTREAMER_ERROR,
            PLAY_GSTREAMER_ERROR_PLAYBIN_FAILED,
            "The playbin plugin is missing (install the GStreamer \"base\" plugin set)");
        gst_object_unref (GST_OBJECT (gstreamer->pipe));
        gstreamer->pipe = NULL;
        return FALSE;
    }
    // Audio sink
    sink = gst_element_factory_make ("autoaudiosink", NULL);
    if (!sink) {
        g_set_error (
            error,
            PLAY_GSTREAMER_ERROR,
            PLAY_GSTREAMER_ERROR_AUDIO_SINK_FAILED,
            "Audio sink plugin is missing (install the GStreamer \"good\" plugin set)");
        gst_object_unref (GST_OBJECT (gstreamer->playbin));
        gst_object_unref (GST_OBJECT (gstreamer->pipe));
        gstreamer->playbin = NULL;
        gstreamer->pipe = NULL;
        return FALSE;
    }
    g_object_set (G_OBJECT (gstreamer->playbin), "audio-sink", sink, NULL);
    g_object_set (G_OBJECT (gstreamer->playbin), "flags",
        GST_PLAY_FLAG_AUDIO |
        GST_PLAY_FLAG_SOFT_VOLUME, NULL);

    // Connect playbin with the pipeline
    gst_bin_add (GST_BIN (gstreamer->pipe), gstreamer->playbin);

    // Setup a handler for GST bus messages
    bus = gst_pipeline_get_bus (GST_PIPELINE (gstreamer->pipe));
    if (!bus) {
        g_set_error (
            error,
            PLAY_GSTREAMER_ERROR,
            PLAY_GSTREAMER_ERROR_BUS_FAILED,
            "Could not initialize the GStreamer pipeline bus");
        gst_object_unref (GST_OBJECT (gstreamer->pipe));
        gstreamer->pipe = NULL;
        return FALSE;
    }
    gst_bus_add_watch (
        bus,
        (GstBusFunc) gstreamer_gst_bus_message,
        gstreamer);
    gst_object_unref (bus);

    play_gstreamer_set_state_stopped (gstreamer);
    return TRUE;
}

// Process a metadata tag
static void gstreamer_gst_bus_tag (const GstTagList *list,
                                   const gchar *tag,
                                   PlayGstreamer *gstreamer)
{
    PlayMetadata type;
    gchar *value;

    // Make sure there is a track being played and the tag has
    // an associated value
    if (G_UNLIKELY (!gstreamer->current))
        return;
    if (G_UNLIKELY (!gst_tag_list_get_tag_size (list, tag)))
        return;

    if (!strcmp (tag, GST_TAG_ARTIST)) {
        type = PLAY_METADATA_ARTIST;
    } else if (!strcmp (tag, GST_TAG_TITLE)) {
        type = PLAY_METADATA_TITLE;
    } else {
        // Tag not used - ignore
        return;
    }
    // Retrive the metadata value into a string
    gst_tag_list_get_string (list, tag, &value);

    // Update the metadata in the current queue item
    play_queue_item_set_metadata (gstreamer->current, type, value);
    g_signal_emit (
        gstreamer,
        signals[METADATA_UPDATED],
        0,
        type,
        value);
    g_free (value);
}

// GStreamer bus message handler
static gboolean gstreamer_gst_bus_message (GstBus *bus,
                                           GstMessage *message,
                                           PlayGstreamer *gstreamer)
{
    switch (GST_MESSAGE_TYPE (message)) {
        case GST_MESSAGE_EOS:
            // End of the stream
            if (G_LIKELY (gstreamer->current)) {
                g_object_unref (gstreamer->current);
                gstreamer->current = NULL;
            }
            play_gstreamer_set_state_stopped (gstreamer);

            g_signal_emit (
                gstreamer,
                signals[END_OF_STREAM],
                0);
            break;
        case GST_MESSAGE_ERROR: {
            // Error message
            GError *error = NULL;

            // Stop playing and broadcast the error
            play_gstreamer_set_state_stopped (gstreamer);
            gst_message_parse_error (message, &error, NULL);

            g_signal_emit (
                gstreamer,
                signals[ERROR],
                0,
                error);

            g_error_free (error);
            break;
        }
        case GST_MESSAGE_BUFFERING: {
            // Buffering progress
            gint progress;

            gst_message_parse_buffering (message, &progress);
            g_signal_emit (
                gstreamer,
                signals[BUFFERING],
                0,
                (guint) progress);
            break;
        }
        case GST_MESSAGE_TAG: {
            // A new metadata information has become available
            GstTagList *tags;

            gst_message_parse_tag (message, &tags);

            gst_tag_list_foreach (
                tags,
                (GstTagForeachFunc) gstreamer_gst_bus_tag,
                gstreamer);
            gst_tag_list_free (tags);
            break;
        }
        case GST_MESSAGE_STATE_CHANGED: {
            GstState old;
            GstState new;
            GstState pending;
            gst_message_parse_state_changed (message, &old, &new, &pending);
            switch (new) {
                case GST_STATE_PLAYING:
                    g_signal_emit (
                        gstreamer,
                        signals[STATE_PLAYING],
                        0);
                    break;
                case GST_STATE_PAUSED:
                    g_signal_emit (
                        gstreamer,
                        signals[STATE_PAUSED],
                        0);
                    break;
                case GST_STATE_NULL:
                    g_signal_emit (
                        gstreamer,
                        signals[STATE_STOPPED],
                        0);
                    break;
                default:
                    break;
            }
        }
        case GST_MESSAGE_DURATION:
            g_signal_emit (
                gstreamer,
                signals[DURATION_UPDATED],
                0);
            break;
        default:
            break;
    }
    return TRUE;
}
