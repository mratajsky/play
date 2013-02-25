/**
 * PLAY
 * play.c: Play itself
 * Copyright (C) 2011-2013 Michal Ratajsky <michal.ratajsky@gmail.com>
 */
#include "play-common.h"
#include "play-gstreamer.h"
#include "play-queue.h"
#include "play-queue-item.h"
#include "play-simple-queue.h"
#include "play-terminal.h"

// File size constants for play_format_size ()
#define PLAY_KB (1000)
#define PLAY_MB (1000LLU * PLAY_KB)
#define PLAY_GB (1000LLU * PLAY_MB)
#define PLAY_TB (1000LLU * PLAY_GB)
#define PLAY_PB (1000LLU * PLAY_TB)
#define PLAY_EB (1000LLU * PLAY_PB)

// Initialize the backend, queue, main loop and signal handlers
static gboolean play_init (int *argcp, char **argvp[]);

// Release the resources claimed during initialization
static void play_cleanup (void);

// Start playing the first item in the queue
static void play_start (void);

// Watch the position in the current track and schedule an information
// line redraw when the number of seconds has changed
static gboolean play_loop (void);

// Draw or redraw the information about the current track and position
static gboolean play_redraw (void);

// Set the next queue item to be played using the backend depending
// on the queue position and command line options
static gboolean play_set_next (gboolean stop_before_set);

// Set the previous queue item to be played using the backend depending
// on the queue position and command line options
static gboolean play_set_previous (gboolean stop_before_set);

// Terminal input handler
static void play_process_input (PlayTerminal *terminal, int key);

// Simplified controls when downloading playlists
static void play_process_input_download (PlayTerminal *terminal, int key);

// Seek to the next queue item and play it
static gboolean play_seek_next (void);

// Seek to the previous queue item and play it
static gboolean play_seek_previous (void);

// Schedule a function to run periodically waiting until playlists have
// been downloaded and allow simple keyboard controls
static void play_wait_for_queue (void);

// Function executed periodically that checks if playlists have been
// downloaded and the queue now contains media files
static gboolean play_watch_playlists (void);

// Playing of the current stream has finished
static void play_gst_end_of_stream (PlayGstreamer *backend);

// An error while playing the current track has occured
static void play_gst_error (PlayGstreamer *backend, GError *error);

// The metadata of the currenly played track has been updated
static void play_gst_metadata_updated (PlayGstreamer *backend,
                                       PlayMetadata meta,
                                       const gchar *value);

// Handle backend events that may be important for the displayed
// information
static void play_gst_redraw_event (PlayGstreamer *backend);

// An error has occured while reading a playlist
static void play_queue_playlist_error (PlayQueue *queue,
                                       const gchar *uri,
                                       const gchar *error);

// Update progress of playlist downloads
static void play_queue_playlist_progress (PlayQueue *queue);

// Handle a quitting signal
static void play_signal_quit (int signum);

// Handle a terminal window resizing signal
static void play_signal_winch (int signum);

// Format size for display
static gchar *play_format_size (guint64 size);

// Global variables
static GMainLoop       *loop;
static PlayQueue       *queue;
static PlaySimpleQueue *history;
static PlayTerminal    *terminal;
static PlayGstreamer   *backend;

// When set to TRUE the information line will be redrawn
static gboolean redraw = TRUE;

// Set to TRUE when playing is paused
static gboolean paused;

// Set to TRUE when the cursor is not at the beginning of a line and
// a newline is need to be print
static gboolean newline;

// Set to TRUE when a playlist error is displayed
static gboolean playlist_error_shown;

// Current terminal screen width
static guint width;

static guint64 download_current;
static guint64 download_total;

// Global command line options
static gboolean opt_quiet;
static gboolean opt_no_controls;
static gboolean opt_repeat;
static gboolean opt_shuffle;

// Print a newline when the cursor is not at the beginning of a line
#define PRINT_NEWLINE_IF_NEEDED() \
 if (newline) {         \
     putchar ('\n');    \
     newline = FALSE;   \
 }

// Initialize the backend, queue, main loop and signal handlers
static gboolean play_init (int *argcp, char **argvp[])
{
    GError *error = NULL;
    int i;

    // Initialize the backend
    play_gstreamer_global_initialize (argcp, argvp);

    // Prepare the backend
    backend = play_gstreamer_new (&error);
    if (!backend) {
        g_printerr ("Error: %s\n", error->message);
        g_error_free (error);
        return FALSE;
    }
    g_signal_connect (
        backend,
        "end-of-stream",
        G_CALLBACK (play_gst_end_of_stream),
        NULL);
    g_signal_connect (
        backend,
        "metadata-updated",
        G_CALLBACK (play_gst_metadata_updated),
        NULL);
    g_signal_connect (
        backend,
        "error",
        G_CALLBACK (play_gst_error),
        NULL);
    if (!opt_quiet) {
        g_signal_connect (
            backend,
            "duration-updated",
            G_CALLBACK (play_gst_redraw_event),
            NULL);
        g_signal_connect (
            backend,
            "state-playing",
            G_CALLBACK (play_gst_redraw_event),
            NULL);
        g_signal_connect (
            backend,
            "state-paused",
            G_CALLBACK (play_gst_redraw_event),
            NULL);
    }
    // Prepare the terminal input
    terminal = play_terminal_new ();

    // Prepare the queue
    queue = play_queue_new ();
    g_signal_connect (
        queue,
        "playlist-error",
        G_CALLBACK (play_queue_playlist_error),
        NULL);
    g_signal_connect (
        queue,
        "playlist-progress-updated",
        G_CALLBACK (play_queue_playlist_progress),
        NULL);

    // If shuffling along with repetition is enabled, use a separate
    // queue to hold the history of what has been played
    if (opt_shuffle && opt_repeat && !opt_no_controls)
        history = play_simple_queue_new ();

    // The command line arguments don't contain any options anymore
    for (i = 1; i < *argcp; i++)
        play_queue_add (queue, (*argvp)[i]);

    // Initialize custom signal handlers
    signal (SIGHUP,  &play_signal_quit);
    signal (SIGINT,  &play_signal_quit);
    signal (SIGQUIT, &play_signal_quit);
    signal (SIGTERM, &play_signal_quit);
    if (!opt_quiet)
        signal (SIGWINCH, &play_signal_winch);

    // Create the main loop
    loop = g_main_loop_new (NULL, FALSE);

    // Close the standard error output to stop errors from the backend
    // plugins from messing up the display
    close (STDERR_FILENO);
    return TRUE;
}

// Release the resources claimed during initialization
static void play_cleanup (void)
{
    gboolean mute;

    PRINT_NEWLINE_IF_NEEDED ();

    // Make sure to unmute the sound output when done playing
    if (play_gstreamer_get_mute (backend, &mute) && mute) {
        play_gstreamer_set_mute (backend, FALSE);
    }
    g_object_unref (backend);
    g_object_unref (terminal);
    g_object_unref (queue);
    g_main_loop_unref (loop);
}

// Start playing the first item in the queue
static void play_start (void)
{
    // Make sure the queue is sorted properly and pick the first item
    if (opt_shuffle) {
        play_queue_randomize (queue);
    } else {
        play_queue_sort_by_position (queue);
    }
    play_queue_position_set_first (queue);
    if (history)
        play_simple_queue_append (
            history,
            play_queue_get_current (queue),
            TRUE);

    // Play the first item in the queue, seeking to the next ones
    // will be done in the end-of-stream callback
    play_gstreamer_set_item (backend, play_queue_get_current (queue));

    if (!opt_quiet) {
        // Create timers to update the display
        g_timeout_add (
            50,
            (GSourceFunc) play_loop,
            NULL);
        g_timeout_add (
            50,
            (GSourceFunc) play_redraw,
            NULL);
    }
    if (!opt_no_controls) {
        g_signal_connect (
            terminal,
            "input-read",
            G_CALLBACK (play_process_input),
            NULL);
        // Change the input mode
        play_terminal_listen (terminal);
    }
    play_gstreamer_set_state_playing (backend);
}

// Watch the position in the current track and schedule an information
// line redraw when the number of seconds has changed
static gboolean play_loop (void)
{
    gint64 position;
    static gint seconds = 0;

    if (play_gstreamer_get_position (backend, &position)) {
        gint current = PLAY_GSTREAMER_TIME_SECONDS (position);
        if (current != seconds) {
            redraw = TRUE;
            seconds = current;
        }
    }
    return TRUE;
}

// Draw or redraw the information about the current track and position
static gboolean play_redraw (void)
{
    PlayQueueItem *item;
    const gchar   *title;
    GString       *line;
    gint64         position;
    gint64         duration;
    gint           length = 0;
    gint           i;

    if (!redraw) {
        // The line is redrawn only when something has changed
        return TRUE;
    }
    if (!play_gstreamer_get_position (backend, &position)) {
        // This function can be executed during switching to a different
        // track and the position will then be unknown
        return TRUE;
    }
    item = play_gstreamer_get_current (backend);
    line = g_string_new (NULL);

    // The displayed track title will be either title read from the
    // track by gstreamer, file name or URI
    title = play_queue_item_get_metadata (item, PLAY_METADATA_TITLE_FULL);
    if (!title)
        title = play_queue_item_get_name (item);

    // Clear the current line
    putchar ('\r');
    for (i = 0; i < width; i++)
        putchar (' ');
    putchar ('\r');

    // Time information of the current track
    // Duration is not present in live streams
    if (play_gstreamer_get_duration (backend, &duration)) {
        g_string_append_printf (
            line,
            "[ %02u:%02u:%02u / %02u:%02u:%02u ]",
            PLAY_GSTREAMER_TIME_HOURS (position),
            PLAY_GSTREAMER_TIME_MINUTES (position),
            PLAY_GSTREAMER_TIME_SECONDS (position),
            PLAY_GSTREAMER_TIME_HOURS (duration),
            PLAY_GSTREAMER_TIME_MINUTES (duration),
            PLAY_GSTREAMER_TIME_SECONDS (duration));
    } else {
        g_string_append_printf (
            line,
            "[ %02u:%02u:%02u ]",
            PLAY_GSTREAMER_TIME_HOURS (position),
            PLAY_GSTREAMER_TIME_MINUTES (position),
            PLAY_GSTREAMER_TIME_SECONDS (position));
    }
    // The length variable includes the number of characters to follow
    // excluding the track title
    if (paused)
        length = 10;
    else
        length = 1;
    if (width > line->len) {
        length = width - (line->len + length);
        if (length > 2) {
            if (title) {
                g_string_append_c (line, ' ');
                if (g_utf8_strlen (title, -1) < length) {
                    // The terminal is wide enough to contain the whole
                    // information line
                    g_string_append (line, title);
                } else {
                    // The terminal is not wide enough, the title is shortened
                    // and the string .. is appened
                    g_string_append_len (line, title, length - 3);
                    g_string_append (line, "..");
                }
            }
            if (paused)
                g_string_append (line, " [PAUSED]");
        }
        if (!g_get_charset (NULL)) {
            // The terminal uses a non-utf8 charset and all of our
            // strings are in utf8
            gchar *converted = g_locale_from_utf8 (
                line->str,
                -1,
                NULL, NULL, NULL);
            if (converted) {
                g_print ("%s", converted);
                g_free (converted);
            } else {
                g_print ("%s", line->str);
            }
        } else {
            g_print ("%s", line->str);
        }
        g_string_free (line, TRUE);
    }
    redraw  = FALSE;
    newline = TRUE;
    return TRUE;
}

// Set the next queue item to be played using the backend depending
// on the queue position and command line options
static gboolean play_set_next (gboolean stop_before_set)
{
    PlayQueueItem *item;

    if (history && !play_simple_queue_position_is_last (history)) {
        // Playing accordingly to shuffle queue history
        // Advance the queue and use the next item in the history
        play_simple_queue_position_set_next (history);

        item = play_simple_queue_get_current (history);
    } else {
        // Try to advance the queue position by one item
        // If it fails we have reached the end of the queue
        if (!play_queue_position_set_next (queue)) {
            if (!opt_repeat) {
                // Report a failure to advance the queue position as
                // repetition is disabled and we are at the end
                return FALSE;
            }
            if (opt_shuffle) {
                // If shuffling is enabled the queue order is
                // randomized each time we reach the end
                play_queue_randomize (queue);
            }
            // If repetition is enabled the queue position is
            // moved back to the start
            play_queue_position_set_first (queue);
        }
        item = play_queue_get_current (queue);
        if (history)
            play_simple_queue_append (history, item, TRUE);
    }
    if (stop_before_set)
        play_gstreamer_set_state_stopped (backend);

    play_gstreamer_set_item (backend, item);
    return TRUE;
}

// Set the previous queue item to be played using the backend depending
// on the queue position and command line options
static gboolean play_set_previous (gboolean stop_before_set)
{
    PlayQueueItem *item;

    if (history) {
        if (play_simple_queue_position_is_first (history)) {
            // Already at the start of the history
            return FALSE;
        }
        // Playing accordingly to shuffle queue history
        // Advance the queue and use the next item in the history
        play_simple_queue_position_set_previous (history);

        item = play_simple_queue_get_current (history);
    } else {
        if (!play_queue_position_set_previous (queue)) {
            if (!opt_repeat) {
                // Report a failure to advance the queue position as
                // repetition is disabled and we are at the start
                return FALSE;
            }
            // If repetition is enabled the queue position is
            // moved to the end
            play_queue_position_set_last (queue);
        }
        item = play_queue_get_current (queue);
    }
    if (stop_before_set)
        play_gstreamer_set_state_stopped (backend);

    play_gstreamer_set_item (backend, item);
    return TRUE;
}

// Terminal input handlers
static void play_process_input (PlayTerminal *terminal, gint key)
{
    switch (key) {
        case 'p':
        case 'P':
        case ' ':
            // Pause/unpause
            if (paused) {
                play_gstreamer_set_state_playing (backend);
                paused = FALSE;
            } else {
                play_gstreamer_set_state_paused (backend);
                paused = TRUE;
            }
            break;
        case PLAY_TERMINAL_KEY_UP:
            // Seek 10 seconds forward
            play_gstreamer_set_position_seconds (backend, 60);
            break;
        case PLAY_TERMINAL_KEY_DOWN:
            // Seek 10 seconds backward
            play_gstreamer_set_position_seconds (backend, -60);
            break;
        case '+':
            // Volume up
            play_gstreamer_set_volume_relative (backend, 0.05);
            break;
        case '-':
            // Volume down
            play_gstreamer_set_volume_relative (backend, -0.05);
            break;
        case PLAY_TERMINAL_KEY_LEFT:
            // Seek 10 seconds backward
            play_gstreamer_set_position_seconds (backend, -10);
            break;
        case PLAY_TERMINAL_KEY_RIGHT:
            // Seek 10 seconds forward
            play_gstreamer_set_position_seconds (backend, 10);
            break;
        case PLAY_TERMINAL_KEY_PAGE_UP:
            // Seek to the next track
            play_seek_next ();
            break;
        case PLAY_TERMINAL_KEY_PAGE_DOWN:
            // Seek to the previous track
            play_seek_previous ();
            break;
        case 'm':
        case 'M':
            // Mute/unmute
            play_gstreamer_toggle_mute (backend);
            break;
        case PLAY_TERMINAL_KEY_CTRL_C:
        case PLAY_TERMINAL_KEY_ESC:
        case 'q':
        case 'Q':
            // Quit
            g_main_loop_quit (loop);
            break;
    }
}

// Simplified controls when downloading playlists
static void play_process_input_download (PlayTerminal *terminal, gint key)
{
    switch (key) {
        case PLAY_TERMINAL_KEY_CTRL_C:
        case PLAY_TERMINAL_KEY_ESC:
        case 'q':
        case 'Q':
            // Quit
            g_main_loop_quit (loop);
            break;
    }
}

// Seek to the next queue item and play it
static gboolean play_seek_next (void)
{
    if (play_set_next (TRUE)) {
        PRINT_NEWLINE_IF_NEEDED ();
        play_gstreamer_set_state_playing (backend);
        return TRUE;
    }
    return FALSE;
}

// Seek to the previous queue item and play it
static gboolean play_seek_previous (void)
{
    if (play_set_previous (TRUE)) {
        PRINT_NEWLINE_IF_NEEDED ();
        play_gstreamer_set_state_playing (backend);
        return TRUE;
    }
    return FALSE;
}

// Schedule a function to run periodically waiting until playlists have
// been downloaded and allow simple keyboard controls
static void play_wait_for_queue (void)
{
    if (!opt_no_controls) {
        g_signal_connect (
            terminal,
            "input-read",
            G_CALLBACK (play_process_input_download),
            NULL);
        // Change the input mode
        play_terminal_listen (terminal);
    }
    g_timeout_add (
        100,
        (GSourceFunc) play_watch_playlists,
        NULL);
}

// Function executed periodically that checks if playlists have been
// downloaded and the queue now contains media files
static gboolean play_watch_playlists (void)
{
    if (play_queue_get_count_pending (queue)) {
        static gboolean print_message = FALSE;
        static gboolean print_progress = FALSE;
        // Timestamp of the first execution of this function
        static time_t download_start = 0;

        if (print_message) {
            gint i;

            putchar ('\r');
            for (i = 0; i < width; i++)
                putchar (' ');
            putchar ('\r');
            g_print ("Downloading playlists...");
            if (print_progress) {
                gchar *current = play_format_size (download_current);
                gchar *total   = play_format_size (download_total);

                g_print (" %s / %s", current, total);
                g_free (current);
                g_free (total);
            } else {
                // Start printing the progress if the download is
                // taking more than 1 second
                if ((time (NULL) - download_start) > 1)
                    print_progress = TRUE;
            }
            newline = TRUE;
        } else {
            if (!download_start)
                download_start = time (NULL);

            // Start printing a progress message when the download is
            // taking at least 1 second
            if (!opt_quiet && (time (NULL) - download_start))
                print_message = TRUE;
        }
        return TRUE;
    }
    if (!opt_no_controls) {
        // Custom terminal input handler was used during download
        g_signal_handlers_disconnect_by_func (
            terminal,
            G_CALLBACK (play_process_input_download),
            NULL);
    }
    // Start playing if there is something in the queue
    // The downloads might have failed and then the queue would be empty
    if (play_queue_get_count (queue)) {
        play_start ();
    } else {
        if (!playlist_error_shown)
            g_print ("No playable tracks have been found.\n");

        g_main_loop_quit (loop);
    }
    // Return FALSE so the function is not executed anymore
    return FALSE;
}

// Playing of the current stream has finished
static void play_gst_end_of_stream (PlayGstreamer *backend)
{
    if (play_set_next (FALSE)) {
        PRINT_NEWLINE_IF_NEEDED ();
        play_gstreamer_set_state_playing (backend);
    } else
        g_main_loop_quit (loop);
}

// An error while playing the current track has occured
static void play_gst_error (PlayGstreamer *gstreamer, GError *error)
{
    // Here it would more appropriate to use the g_printerr () function and
    // print the error on the standard error output
    // However the error stream is closed to stop gstreamer from
    // from flooding the terminal
    PRINT_NEWLINE_IF_NEEDED ();
    g_print ("Error reading %s: %s\n",
        play_queue_item_get_name (play_gstreamer_get_current (backend)),
        error->message);

    // Try to play the next track
    // If there are no next tracks in the queue the program is ended
    if (play_set_next (FALSE))
        play_gstreamer_set_state_playing (backend);
    else
        g_main_loop_quit (loop);
}

// The metadata of the currenly played track has been updated
static void play_gst_metadata_updated (PlayGstreamer *backend,
                                       PlayMetadata meta,
                                       const gchar *value)
{
    // Schedule a redraw if an information which is displayed has
    // been updated
    switch (meta) {
        case PLAY_METADATA_ARTIST:
        case PLAY_METADATA_TITLE:
            redraw = TRUE;
            break;  
        default:
            break;
    }
}

// Handle backend events that may be important for the displayed
// information
static void play_gst_redraw_event (PlayGstreamer *backend)
{
    // Make sure the information line is redrawn the next time the
    // display update function is executed
    redraw = TRUE;
}

// An error has occured while reading a playlist
static void play_queue_playlist_error (PlayQueue *queue,
                                       const gchar *uri,
                                       const gchar *error)
{
    // Here it would more appropriate to use the g_printerr () function and
    // print the error on the standard error output
    // However the error stream is closed to stop gstreamer from
    // flooding the terminal
    PRINT_NEWLINE_IF_NEEDED ();
    g_print ("Error reading %s: %s\n", uri, error);

    playlist_error_shown = TRUE;
}

// Update progress of playlist downloads
static void play_queue_playlist_progress (PlayQueue *queue)
{
    download_current = play_queue_get_download_current (queue);
    download_total   = play_queue_get_download_total (queue);
}

// Handle a quitting signal
static void play_signal_quit (int signum)
{
    g_main_loop_quit (loop);
}

// Handle a terminal window resizing signal
static void play_signal_winch (int signum)
{
    // Tell the terminal handler to read the current width
    // of the terminal window
    play_terminal_update_width (terminal);
    width = play_terminal_get_width (terminal);
}

// Format size for display
static gchar *play_format_size (guint64 size)
{
    if (size < PLAY_KB)
        return g_strdup_printf ("%u B", (guint) size);
    if (size < PLAY_MB)
        return g_strdup_printf ("%.1f kB", (gdouble)size / (gdouble)PLAY_KB);
    if (size < PLAY_GB)
        return g_strdup_printf ("%.1f MB", (gdouble)size / (gdouble)PLAY_MB);
    if (size < PLAY_TB)
        return g_strdup_printf ("%.1f GB", (gdouble)size / (gdouble)PLAY_GB);
    if (size < PLAY_PB)
        return g_strdup_printf ("%.1f TB", (gdouble)size / (gdouble)PLAY_TB);
    if (size < PLAY_EB)
        return g_strdup_printf ("%.1f PB", (gdouble)size / (gdouble)PLAY_PB);
    else
        return g_strdup_printf ("%.1f EB", (gdouble)size / (gdouble)PLAY_EB);
}

int main(int argc, char *argv[])
{
    GError          *err = NULL;
    GOptionContext  *context;
    static gboolean  opt_version = FALSE;

    // Command line options
    static const GOptionEntry opts[] = {
        { "quiet",   'q', 0, G_OPTION_ARG_NONE, &opt_quiet,
          "Display no output except for errors",
          NULL },
        { "no-controls", 'n', 0, G_OPTION_ARG_NONE, &opt_no_controls,
          "Disable keyboard controls",
          NULL },
        { "repeat",  'r', 0, G_OPTION_ARG_NONE, &opt_repeat,
          "Repeat playback continuously",
          NULL },
        { "shuffle", 's', 0, G_OPTION_ARG_NONE, &opt_shuffle,
          "Play the tracks in a random order",
          NULL },
        { "version", 'v', 0, G_OPTION_ARG_NONE, &opt_version,
          "Show the program version and quit",
          NULL },
        { NULL }
    };
    context = g_option_context_new ("- command-line audio player");

    // Add the command line options to the glib context
    g_option_context_add_main_entries (context, opts, NULL);
    // Add the backend command line options
    g_option_context_add_group (context, gst_init_get_option_group());

    // Parse the command line options and quit the program on error
    if (!g_option_context_parse (context, &argc, &argv, &err)) {
        g_printerr ("%s\n", err->message);
        g_option_context_free (context);
        g_error_free (err);
        return 0;
    }
    g_option_context_free (context);

    // Show program version
    if (opt_version) {
        g_print ("play version %s\n", VERSION);
        return 0;
    }
    if (argc < 2) {
        // Nothing given on the command line - print the program usage
        gchar *program = g_path_get_basename (argv[0]);

        g_print ("Usage: %s [OPTION...] FILE... URI...\n", program);
        g_print ("See %s --help for more help.\n", program);
        g_free (program);
        return 0;
    }

    if (!play_init (&argc, &argv))
        return 1;

    // If the queue has no playable items an error must have been displayed
    // as the number of arguments was verified
    if (play_queue_get_count (queue) ||
        play_queue_get_count_pending (queue)) {
        if (!opt_quiet) {
            // Read the initial terminal width
            play_terminal_update_width (terminal);
            width = play_terminal_get_width (terminal);
        }
        if (play_queue_get_count_pending (queue)) {
            play_wait_for_queue ();
        } else {
            play_start ();
        }
        g_main_loop_run (loop);
    }
    play_cleanup ();
    return 0;
}
