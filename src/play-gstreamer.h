/**
 * PLAY
 * play-gstreamer.h: GStreamer library backend
 * Copyright (C) 2011-2013 Michal Ratajsky <michal.ratajsky@gmail.com>
 */
#ifndef _PLAY_GSTREAMER_H_
#define _PLAY_GSTREAMER_H_

#include "play-common.h"
#include "play-queue-item.h"

G_BEGIN_DECLS

typedef enum {
    PLAY_GSTREAMER_STOPPED = 0,
    PLAY_GSTREAMER_PLAYING,
    PLAY_GSTREAMER_PAUSED
} PlayGstreamerState;

typedef enum {
    PLAY_GSTREAMER_ERROR_PIPELINE_FAILED,
    PLAY_GSTREAMER_ERROR_PLAYBIN_FAILED,
    PLAY_GSTREAMER_ERROR_AUDIO_SINK_FAILED,
    PLAY_GSTREAMER_ERROR_BUS_FAILED
} PlayGstreamerError;

#define PLAY_TYPE_GSTREAMER                     \
    (play_gstreamer_get_type())
#define PLAY_GSTREAMER(o)                       \
    (G_TYPE_CHECK_INSTANCE_CAST((o), PLAY_TYPE_GSTREAMER, PlayGstreamer))
#define PLAY_GSTREAMER_CLASS(k)                 \
    (G_TYPE_CHECK_CLASS_CAST((k), PLAY_TYPE_GSTREAMER, PlayGstreamerClass))
#define PLAY_IS_GSTREAMER(o)                    \
    (G_TYPE_CHECK_INSTANCE_TYPE((o), PLAY_TYPE_GSTREAMER))
#define PLAY_IS_GSTREAMER_CLASS(k)              \
    (G_TYPE_CHECK_CLASS_TYPE((k), PLAY_TYPE_GSTREAMER))
#define PLAY_GSTREAMER_GET_CLASS(o)             \
    (G_TYPE_INSTANCE_GET_CLASS((o), PLAY_TYPE_GSTREAMER, PlayGstreamerClass))

#define PLAY_GSTREAMER_ERROR (play_gstreamer_get_error_quark ())

// Macros to convert a value from a number of nanoseconds
#define PLAY_GSTREAMER_TIME_HOURS(t)                                \
    GST_CLOCK_TIME_IS_VALID (t) ?                                   \
    (guint) (((GstClockTime)(t))  / (GST_SECOND  * 60   * 60)) : 0
#define PLAY_GSTREAMER_TIME_MINUTES(t)                              \
    GST_CLOCK_TIME_IS_VALID (t) ?                                   \
    (guint) ((((GstClockTime)(t)) / (GST_SECOND  * 60)) % 60)  : 0
#define PLAY_GSTREAMER_TIME_SECONDS(t)                              \
    GST_CLOCK_TIME_IS_VALID (t) ?                                   \
    (guint) ((((GstClockTime)(t)) /  GST_SECOND) % 60)         : 0

typedef struct {
    GObject        parent_instance;
    PlayQueueItem *current;
    GstElement    *pipe;
    GstElement    *playbin;
} PlayGstreamer;

typedef struct {
    GObjectClass   parent_class;

    // Signals
    // Buffer fill has been changed
    void (*buffering) (PlayGstreamer *gstreamer,
                       guint progress,
                       gpointer user_data);

    // Duration has become known or been changed
    void (*duration_updated) (PlayGstreamer *gstreamer,
                              gpointer user_data);

    // The playing stream has ended
    void (*end_of_stream) (PlayGstreamer *gstreamer,
                           gpointer user_data);

    // An error has occured and playing has stopped
    void (*error) (PlayGstreamer *gstreamer,
                   GError *error,
                   gpointer user_data);

    // A metadata information has become known or been updated
    void (*metadata_updated) (PlayGstreamer *gstreamer,
                              PlayMetadata type,
                              const gchar *value,
                              gpointer user_data);

    // The state has been changed to PLAYING
    void (*state_playing) (PlayGstreamer *gstreamer,
                           gpointer user_data);

    // The state has been changed to PAUSED
    void (*state_paused) (PlayGstreamer *gstreamer,
                          gpointer user_data);

    // The state has been changed to STOPPED
    void (*state_stopped) (PlayGstreamer *gstreamer,
                           gpointer user_data);
} PlayGstreamerClass;

extern GType  play_gstreamer_get_type (void);
extern GQuark play_gstreamer_get_error_quark (void);

// Global initialization function
// Should be called soon after the program start
extern void play_gstreamer_global_initialize (int *argcp, char ***argvp);

// Create a new gstreamer object
extern PlayGstreamer *play_gstreamer_new (GError **error);

// Set a new queue item to be played
// The playback should be stopped before using this function and then started
// again to play the new track
// Returns TRUE on success
extern gboolean play_gstreamer_set_item (PlayGstreamer *gstreamer,
                                         PlayQueueItem *item);

// Retrieve the current state of the gstreamer backend and store it in
// the passed PlayGstreamerState
// Returns TRUE if the state was successfully retrieved
extern gboolean play_gstreamer_get_state (PlayGstreamer *gstreamer,
                                          PlayGstreamerState *state);

// Set the current gstreamer backend status to PLAYING
// Use this function to start playing after an item was set using the
// play_gstreamer_set_item() function
// Returns TRUE if the state was successfully set or if it's going to be
// set asynchronously
extern gboolean play_gstreamer_set_state_playing (PlayGstreamer *gstreamer);

// Set the current gstreamer backend status to PAUSED
// Returns TRUE if the state was successfully set or if it's going to be
// set asynchronously
extern gboolean play_gstreamer_set_state_paused (PlayGstreamer *gstreamer);

// Set the current gstreamer backend status to STOPPED (stop playing)
// Returns TRUE if the state was successfully set or if it's going to be
// set asynchronously
extern gboolean play_gstreamer_set_state_stopped (PlayGstreamer *gstreamer);

// Retrieve the current PlayQueueItem set by play_gstreamer_set_item()
// Use g_object_ref() to keep the reference
extern PlayQueueItem *play_gstreamer_get_current (PlayGstreamer *gstreamer);

// Retrieve the current volume and store it in the passed gdouble
// Returns TRUE if the volume was successfully retrieved
extern gboolean play_gstreamer_get_volume (PlayGstreamer *gstreamer,
                                           gdouble *volume);

// Set a new volume
// The value should be between 0.0 and 1.0
// Returns TRUE on success
extern gboolean play_gstreamer_set_volume (PlayGstreamer *gstreamer,
                                           gdouble volume);

// Set a new volume
// The value is the margin between the old and new volume
// Returns TRUE on success
extern gboolean play_gstreamer_set_volume_relative (PlayGstreamer *gstreamer,
                                                    gdouble offset);

// Retrieve the current mute status
// The value is stored in the passed gboolean
// Returns TRUE if the status was successfully retrieved
extern gboolean play_gstreamer_get_mute (PlayGstreamer *gstreamer,
                                         gboolean *mute);

// Mute or unmute depending on the passed value
// Returns TRUE on success
extern gboolean play_gstreamer_set_mute (PlayGstreamer *gstreamer,
                                         gboolean mute);

// Toggle mute
// Returns TRUE on success
extern gboolean play_gstreamer_toggle_mute (PlayGstreamer *gstreamer);

// Retrieve the duration of the stream currently playing in nanoseconds
// Use the PLAY_GSTREAMER_TIME_* macros the convert the value
// Returns TRUE if the duration was successfully retrieved
extern gboolean play_gstreamer_get_duration (PlayGstreamer *gstreamer,
                                             gint64 *duration);

// Retrieve the position of the stream currently playing in nanoseconds
// Use the PLAY_GSTREAMER_TIME_* macros the convert the value
// Returns TRUE if the duration was successfully retrieved
extern gboolean play_gstreamer_get_position (PlayGstreamer *gstreamer,
                                             gint64 *position);

// Set the position in the current stream
// The position is the number of nanoseconds, relative to the current position
// Returns TRUE on success
extern gboolean play_gstreamer_set_position (PlayGstreamer *gstreamer,
                                             gint64 offset);

// Set the position in the current stream
// The position is the number of seconds, relative to the current position
// Returns TRUE on success
extern gboolean play_gstreamer_set_position_seconds (PlayGstreamer *gstreamer,
                                                     gint offset);

G_END_DECLS

#endif // _PLAY_GSTREAMER_H_
