/**
 * PLAY
 * play-downloader.c: Download remote files
 * Copyright (C) 2011-2013 Michal Ratajsky <michal.ratajsky@gmail.com>
 */
#include "play-common.h"
#include "play-downloader.h"

G_DEFINE_TYPE (PlayDownloader, play_downloader, G_TYPE_OBJECT);

typedef struct {
    guint           id;
    PlayDownloader *downloader;
    GFile          *source;
    GFile          *destination;
    GCancellable   *cancellable;
    gpointer        custom;
} PlayDownloaderData;

// Internal function to initiate a file download and return an ID of
// the download
static guint downloader_download (PlayDownloader *downloader,
                                  GFile *source,
                                  GFile *destination,
                                  gpointer custom);

// Download progress callback
static void downloader_progress (goffset current_bytes,
                                 goffset total_bytes,
                                 PlayDownloaderData *data);

// Download finish callback
static void downloader_finished (GObject *source,
                                 GAsyncResult *result,
                                 PlayDownloaderData *data);

// Internal function to cancel a download when destroying the downloader
static void downloader_cancel (gpointer key, PlayDownloaderData *data);

// Free memory allocated for a temporary data structure
static void downloader_free_data (PlayDownloaderData *data);

// Signals
enum {
    PROGRESS,
    FINISHED,
    FAILED,
    LAST_SIGNAL
};
static guint signals[LAST_SIGNAL];

// GObject/finalize
static void play_downloader_finalize (GObject *object)
{
    PlayDownloader *downloader = PLAY_DOWNLOADER (object);

    // Clean up
    g_hash_table_foreach (
      downloader->data,
      (GHFunc) downloader_cancel,
      NULL);
    g_hash_table_destroy (downloader->data);

    // Chain up to the parent class
    G_OBJECT_CLASS (play_downloader_parent_class)->finalize (object);
}

// GObject/class init
static void play_downloader_class_init (PlayDownloaderClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

    gobject_class->finalize = play_downloader_finalize;

    // Signals
    signals[PROGRESS] =
        g_signal_new ("progress",
                      G_TYPE_FROM_CLASS (gobject_class),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (PlayDownloaderClass, progress),
                      NULL,
                      NULL,
                      play_marshal_VOID__UINT_UINT64_UINT64_POINTER,
                      G_TYPE_NONE,
                      4,
                      G_TYPE_UINT,
                      G_TYPE_UINT64,
                      G_TYPE_UINT64,
                      G_TYPE_POINTER);
    signals[FINISHED] =
        g_signal_new ("finished",
                      G_TYPE_FROM_CLASS (gobject_class),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (PlayDownloaderClass, finished),
                      NULL,
                      NULL,
                      play_marshal_VOID__UINT_OBJECT_POINTER,
                      G_TYPE_NONE,
                      3,
                      G_TYPE_UINT,
                      G_TYPE_OBJECT,
                      G_TYPE_POINTER);
    signals[FAILED] =
        g_signal_new ("failed",
                      G_TYPE_FROM_CLASS (gobject_class),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (PlayDownloaderClass, failed),
                      NULL,
                      NULL,
                      play_marshal_VOID__UINT_STRING_POINTER,
                      G_TYPE_NONE,
                      3,
                      G_TYPE_UINT,
                      G_TYPE_STRING,
                      G_TYPE_POINTER);
}

// GObject/init
static void play_downloader_init (PlayDownloader *downloader)
{
    downloader->data = g_hash_table_new_full (
        g_direct_hash,
        g_direct_equal,
        NULL,
        (GDestroyNotify) downloader_free_data);

    // Initial ID
    downloader->id_next = 1;
}

// Create a new downloader object
PlayDownloader *play_downloader_new (void)
{
    return PLAY_DOWNLOADER (g_object_new (PLAY_TYPE_DOWNLOADER, NULL));
}

// Download a file to the specified path, which must include the file name
// Returns a download ID or 0 on error
guint play_downloader_download (PlayDownloader *downloader,
                                const gchar *uri,
                                const gchar *destination_file,
                                gpointer custom)
{
    GFile *source;
    GFile *destination;
    guint  id;

    g_return_val_if_fail (PLAY_IS_DOWNLOADER (downloader), 0);
    g_return_val_if_fail (uri, 0);
    g_return_val_if_fail (destination_file, 0);

    source = g_file_new_for_uri (uri);
    destination = g_file_new_for_commandline_arg (destination_file);

    id = downloader_download (downloader, source, destination, custom);
    if (!id) {
        g_object_unref (source);
        g_object_unref (destination);
    }
    return id;
}

// Download a file to a temporary location specified as a template
// used by g_file_open_tmp()
// The real destination file name is later returned by the finished signal
// Returns a download ID or 0 on error
guint play_downloader_download_temp (PlayDownloader *downloader,
                                     const gchar *uri,
                                     const gchar *template,
                                     gpointer custom)
{
    GFile  *source;
    GFile  *destination;
    gchar  *temp;
    gint    fd;
    guint   id;

    g_return_val_if_fail (PLAY_IS_DOWNLOADER (downloader), 0);
    g_return_val_if_fail (uri, 0);
    g_return_val_if_fail (template, 0);

    // Open a temporary file
    fd = g_file_open_tmp (template, &temp, NULL);
    if (fd < 0)
        return 0;

    // Close the file descriptor and use the file name for a GFile
    close (fd);
    source = g_file_new_for_uri (uri);
    destination = g_file_new_for_path (temp);

    id = downloader_download (downloader, source, destination, custom);
    if (!id) {
        g_object_unref (source);
        g_object_unref (destination);
    }
    return id;
}

// Cancel an ongoing download specified by the download ID
// Returns TRUE on success
gboolean play_downloader_cancel (PlayDownloader *downloader, guint id)
{
    PlayDownloaderData *data;

    g_return_val_if_fail (PLAY_IS_DOWNLOADER (downloader), FALSE);
    g_return_val_if_fail (id, FALSE); // starts at 1

    data = g_hash_table_lookup (
        downloader->data,
        GUINT_TO_POINTER (id));
    if (!data)
        return FALSE;

    g_cancellable_cancel (data->cancellable);
    g_file_delete (data->destination, NULL, NULL);
    return TRUE;
}

// Internal function to initiate a file download and return an ID of
// the download
static guint downloader_download (PlayDownloader *downloader,
                                  GFile *source,
                                  GFile *destination,
                                  gpointer custom)
{
    PlayDownloaderData *data;

    // Temporary structure passed along in callbacks, keeps the original
    // source and destination file references
    data = g_slice_new (PlayDownloaderData);
    data->id = downloader->id_next++;
    data->source = source;
    data->destination = destination;
    data->cancellable = g_cancellable_new ();
    data->downloader = downloader;
    data->custom = custom;

    // Store the temporary data
    g_hash_table_insert (
        downloader->data,
        GUINT_TO_POINTER (data->id),
        data);

    // Initiate the download
    g_file_copy_async (
        data->source,
        data->destination,
        G_FILE_COPY_OVERWRITE,
        G_PRIORITY_DEFAULT,
        data->cancellable,
        (GFileProgressCallback) downloader_progress,
        data,
        (GAsyncReadyCallback) downloader_finished,
        data);

    return data->id;
}

// Download progress callback
static void downloader_progress (goffset current_bytes,
                                 goffset total_bytes,
                                 PlayDownloaderData *data)
{
    if (!g_cancellable_is_cancelled (data->cancellable))
        g_signal_emit (
            data->downloader,
            signals[PROGRESS],
            0,
            data->id,
            (guint64) current_bytes,
            (guint64) total_bytes,
            data->custom);
}

// Download finish callback
static void downloader_finished (GObject *source,
                                 GAsyncResult *result,
                                 PlayDownloaderData *data)
{
    if (!g_cancellable_is_cancelled (data->cancellable)) {
        GError   *error = NULL;
        gboolean  ret;

        // Read the result of the download
        ret = g_file_copy_finish (G_FILE (source), result, &error);
        if (!ret) {
            g_signal_emit (
                data->downloader,
                signals[FAILED],
                0,
                data->id,
                error->message,
                data->custom);
            g_error_free (error);
            g_file_delete (data->destination, NULL, NULL);
        } else {
            g_signal_emit (
                data->downloader,
                signals[FINISHED],
                0,
                data->id,
                data->destination,
                data->custom);
        }
    }
    // Delete data of the current download
    g_hash_table_remove (data->downloader->data, GUINT_TO_POINTER (data->id));
}

// Internal function to cancel a download when destroying the downloader
static void downloader_cancel (gpointer key, PlayDownloaderData *data)
{
    g_cancellable_cancel (data->cancellable);
    g_file_delete (data->destination, NULL, NULL);
}

// Free memory allocated for a temporary data structure
static void downloader_free_data (PlayDownloaderData *data)
{
    if (data->destination)
        g_object_unref (data->destination);
    if (data->source)
        g_object_unref (data->source);

    g_object_unref (data->cancellable);
    g_slice_free (PlayDownloaderData, data);
}
