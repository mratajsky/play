/**
 * PLAY
 * play-queue.c: A sorted queue of media files
 * Copyright (C) 2011-2013 Michal Ratajsky <michal.ratajsky@gmail.com>
 */
#include "play-common.h"
#include "play-playlist.h"
#include "play-queue.h"
#include "play-queue-item.h"

G_DEFINE_TYPE (PlayQueue, play_queue, G_TYPE_OBJECT);

typedef struct {
    guint current_bytes;
    guint total_bytes;
} PlayQueueDownload;

// Add a GFile to the queue
static gboolean queue_add_gfile (PlayQueue *queue, GFile *file);

// Add a PlayQueueItem to the queue at the given position
// If the given position is 0 the item is placed at the end of the queue
static gboolean queue_add_item (PlayQueue *queue,
                                PlayQueueItem *item,
                                guint position);

// Read a playlist and add the content to the queue
static gboolean queue_add_playlist (PlayQueue *queue, GFile *file);

// Free memory allocated for a temporary data structure
static void queue_download_free (PlayQueueDownload *download);

// Download progress of a playlist has been updated
static void queue_playlist_download_progress (PlayPlaylist *playlist,
                                              guint id,
                                              guint current_bytes,
                                              guint total_bytes,
                                              gpointer custom,
                                              PlayQueue *queue);

// Handle a playlist reading/parsing error by forwarding it
// to the queue owner
static void queue_playlist_error (PlayPlaylist *playlist,
                                  guint id,
                                  const gchar *error,
                                  gpointer custom,
                                  PlayQueue *queue);

// A playlist has been completely processed and no more callbacks will follow
static void queue_playlist_finished (PlayPlaylist *playlist,
                                     guint id,
                                     gpointer custom,
                                     PlayQueue *queue);

// A file or URI has been found in a playlist
static void queue_playlist_item (PlayPlaylist *playlist,
                                 guint id,
                                 PlayQueueItem *item,
                                 gpointer custom,
                                 PlayQueue *queue);

// Helper sorting function used for position sorting
static gint queue_sort_position (gconstpointer a,
                                 gconstpointer b,
                                 gpointer data);

// Helper sorting function used for random sorting
static gint queue_sort_random (gconstpointer a,
                               gconstpointer b,
                               gpointer data);

// Helper sorting function used for reverse sorting
static gint queue_sort_reverse (gconstpointer a,
                                gconstpointer b,
                                gpointer data);

// Recalculate the total number of already downloaded bytes
static void queue_update_download_current (PlayQueue *queue);

// Recalculate the total number of already downloaded bytes - helper function
static void queue_update_download_current_func (gpointer key,
                                                PlayQueueDownload *download,
                                                PlayQueue *queue);

// Signals
enum {
    ITEM_ADDED,
    PLAYLIST_ERROR,
    PLAYLIST_FINISHED,
    PLAYLIST_PROGRESS_UPDATED,
    LAST_SIGNAL
};
static guint signals[LAST_SIGNAL];

// GObject/finalize
static void play_queue_finalize (GObject *object)
{
    PlayQueue *queue = PLAY_QUEUE (object);

    // Clean up
    g_sequence_free (queue->sequence);
    g_object_unref (queue->playlist);
    g_hash_table_destroy (queue->download);

    // Chain up to the parent class
    G_OBJECT_CLASS (play_queue_parent_class)->finalize (object);
}

// GObject/class init
static void play_queue_class_init (PlayQueueClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

    gobject_class->finalize = play_queue_finalize;

    // Signals
    signals[ITEM_ADDED] =
        g_signal_new ("item-added",
                      G_TYPE_FROM_CLASS (gobject_class),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (PlayQueueClass, item_added),
                      NULL,
                      NULL,
                      g_cclosure_marshal_VOID__OBJECT,
                      G_TYPE_NONE,
                      1,
                      PLAY_TYPE_QUEUE_ITEM);
    signals[PLAYLIST_ERROR] =
        g_signal_new ("playlist-error",
                      G_TYPE_FROM_CLASS (gobject_class),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (PlayQueueClass, playlist_error),
                      NULL,
                      NULL,
                      play_marshal_VOID__STRING_STRING,
                      G_TYPE_NONE,
                      2,
                      G_TYPE_STRING,
                      G_TYPE_STRING);
    signals[PLAYLIST_FINISHED] =
        g_signal_new ("playlist-finished",
                      G_TYPE_FROM_CLASS (gobject_class),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (PlayQueueClass, playlist_finished),
                      NULL,
                      NULL,
                      g_cclosure_marshal_VOID__STRING,
                      G_TYPE_NONE,
                      1,
                      G_TYPE_STRING);
    signals[PLAYLIST_PROGRESS_UPDATED] =
        g_signal_new ("playlist-progress-updated",
                      G_TYPE_FROM_CLASS (gobject_class),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (PlayQueueClass, playlist_progress_updated),
                      NULL,
                      NULL,
                      g_cclosure_marshal_VOID__VOID,
                      G_TYPE_NONE,
                      0);
}

// GObject/init
static void play_queue_init (PlayQueue *queue)
{
    queue->sequence = g_sequence_new ((GDestroyNotify) g_object_unref);
    queue->position = 1;
    queue->playlist = play_playlist_new ();
    queue->download = g_hash_table_new_full (
        g_direct_hash,
        g_direct_equal,
        NULL,
        (GDestroyNotify) queue_download_free);
    g_signal_connect (
        queue->playlist,
        "download-progress",
        G_CALLBACK (queue_playlist_download_progress),
        queue);
    g_signal_connect (
        queue->playlist,
        "error",
        G_CALLBACK (queue_playlist_error),
        queue);
    g_signal_connect (
        queue->playlist,
        "finished",
        G_CALLBACK (queue_playlist_finished),
        queue);
    g_signal_connect (
        queue->playlist,
        "queue-item",
        G_CALLBACK (queue_playlist_item),
        queue);
}

// Create a new queue object
PlayQueue *play_queue_new (void)
{
    return PLAY_QUEUE (g_object_new (PLAY_TYPE_QUEUE, NULL));
}

// Add a file, directory or URI to the queue
// Returns TRUE on success
gboolean play_queue_add (PlayQueue *queue, const gchar *file_or_uri)
{
    GFile    *file;
    gchar    *name;
    gboolean  result;

    g_return_val_if_fail (PLAY_IS_QUEUE (queue), FALSE);
    g_return_val_if_fail (file_or_uri, FALSE);

    // Create a GFile to retrieve the file name which might be hidden
    // in a URI
    file = g_file_new_for_commandline_arg (file_or_uri);
    name = g_file_get_basename (file);
    if (name) {
        if (play_playlist_file_is_playlist (name)) {
            // Read the list of media from a playlist
            result = queue_add_playlist (queue, file);
        } else {
            // Add the item to queue directly
            result = queue_add_gfile (queue, file);
        }
        g_free (name);
    } else {
        result = FALSE;
    }
    g_object_unref (file);
    return result;
}

// Return the count of items in the queue
guint play_queue_get_count (PlayQueue *queue)
{
    g_return_val_if_fail (PLAY_IS_QUEUE (queue), 0);

    return g_sequence_get_length (queue->sequence);
}

// Return the number of bytes already dowloaded from remote playlists
guint64 play_queue_get_download_current (PlayQueue *queue)
{
    return queue->download_current;
}

// Return the total number of bytes of remote playlists
guint64 play_queue_get_download_total (PlayQueue *queue)
{
    return queue->download_total;
}

// Return the number of items still waiting to be added to the queue
// These are playlists being downloaded
guint play_queue_get_count_pending (PlayQueue *queue)
{
    g_return_val_if_fail (PLAY_IS_QUEUE (queue), 0);

    return queue->pending;
}

// Return the queue item at the current position or NULL if the queue is empty
PlayQueueItem *play_queue_get_current (PlayQueue *queue)
{
    g_return_val_if_fail (PLAY_IS_QUEUE (queue), NULL);

    // Empty queue
    if (!queue->iterator)
        return NULL;

    return g_sequence_get (queue->iterator);
}

// Return the queue item at a random position or NULL if the queue is empty
PlayQueueItem *play_queue_get_random (PlayQueue *queue)
{
    GSequenceIter *iter;

    g_return_val_if_fail (PLAY_IS_QUEUE (queue), NULL);

    // Empty queue
    if (!queue->iterator)
        return NULL;

    iter = g_sequence_get_iter_at_pos (
        queue->sequence,
        g_random_int_range (0, play_queue_get_count (queue)));
    if (!iter)
        return NULL;

    return g_sequence_get (iter);
}

// Set the current queue position to the first item of the queue
// Returns TRUE on success
gboolean play_queue_position_set_first (PlayQueue *queue)
{
    g_return_val_if_fail (PLAY_IS_QUEUE (queue), FALSE);

    queue->iterator = g_sequence_get_begin_iter (queue->sequence);
    return TRUE;
}

// Set the current queue position to the last item of the queue
// Returns TRUE on success
gboolean play_queue_position_set_last (PlayQueue *queue)
{
    g_return_val_if_fail (PLAY_IS_QUEUE (queue), FALSE);

    queue->iterator = g_sequence_iter_prev (
        g_sequence_get_end_iter (queue->sequence));

    return TRUE;
}

// Move the queue position one item forward in the queue
// Returns TRUE on success
// Returns FALSE if the queue is empty or already at the last item
gboolean play_queue_position_set_next (PlayQueue *queue)
{
    GSequenceIter *iter;

    g_return_val_if_fail (PLAY_IS_QUEUE (queue), FALSE);

    // Empty queue
    if (!queue->iterator)
        return FALSE;

    iter = g_sequence_iter_next (queue->iterator);
    // Do not let the iterator go beyond the last element in the sequence
    if (iter == g_sequence_get_end_iter (queue->sequence))
        return FALSE;

    queue->iterator = iter;
    return TRUE;
}

// Move the queue position one item backward in the queue
// Returns TRUE on success
// Returns FALSE if the queue is empty or already at the first item
gboolean play_queue_position_set_previous (PlayQueue *queue)
{
    GSequenceIter *iter;

    g_return_val_if_fail (PLAY_IS_QUEUE (queue), FALSE);

    // Empty queue
    if (!queue->iterator)
        return FALSE;

    iter = g_sequence_iter_prev (queue->iterator);
    // If the previous iterator is the same as the current one, we are
    // already at the first item
    if (iter == queue->iterator)
        return FALSE;

    queue->iterator = iter;
    return TRUE;
}

// Return TRUE if the current position is at the first item of the queue
gboolean play_queue_position_is_first (PlayQueue *queue)
{
    g_return_val_if_fail (PLAY_IS_QUEUE (queue), FALSE);

    // Empty queue
    if (!queue->iterator)
        return FALSE;

    return g_sequence_iter_is_begin (queue->iterator);
}

// Return TRUE if the current position is at the last item of the queue
gboolean play_queue_position_is_last (PlayQueue *queue)
{
    g_return_val_if_fail (PLAY_IS_QUEUE (queue), FALSE);

    // Empty queue
    if (!queue->iterator)
        return FALSE;

    // The GSequence iterator can point behind the last element,
    // we do not let this happen and instead check if it points one step
    // before (on the last element)
    return queue->iterator == g_sequence_iter_prev (
        g_sequence_get_end_iter (queue->sequence));
}

// Randomize the order of items in the queue
// The current position is kept at the item it was before the call
// of this function, use play_queue_position_set_first() to set it
// to the first item of the queue
gboolean play_queue_randomize (PlayQueue *queue)
{
    g_return_val_if_fail (PLAY_IS_QUEUE (queue), FALSE);

    g_sequence_sort (queue->sequence, queue_sort_random, NULL);
    return TRUE;
}

// Reverse the order of items in the queue
// The current position is kept at the item it was before the call
// of this function, use play_queue_position_set_first() to set it
// to the first item of the queue
gboolean play_queue_reverse (PlayQueue *queue)
{
    g_return_val_if_fail (PLAY_IS_QUEUE (queue), FALSE);

    g_sequence_sort (queue->sequence, queue_sort_reverse, NULL);
    return TRUE;
}

// Remove all items in the queue
gboolean play_queue_remove_all (PlayQueue *queue)
{
    g_return_val_if_fail (PLAY_IS_QUEUE (queue), FALSE);

    // Empty queue
    if (!queue->iterator)
        return FALSE;

    g_sequence_remove_range (
        g_sequence_get_begin_iter (queue->sequence),
        g_sequence_get_end_iter (queue->sequence));

    queue->iterator = NULL;
    return TRUE;
}

// Remove the queue item at the current position
// The current position is then adjusted to the nearest item after or
// before the removed item (before when removing the last item)
gboolean play_queue_remove_current (PlayQueue *queue)
{
    GSequenceIter *iter;

    g_return_val_if_fail (PLAY_IS_QUEUE (queue), FALSE);

    // Empty queue
    if (!queue->iterator)
        return FALSE;

    // Find the nearest valid position to keep the current position on
    // a valid element
    // If the removed element is the last one, set the iterator to NULL
    iter = g_sequence_iter_next (queue->iterator);
    if (g_sequence_iter_is_end (iter)) {
        if (g_sequence_iter_is_begin (queue->iterator))
            iter = NULL;
        else
            iter = g_sequence_iter_prev (queue->iterator);
    }
    g_sequence_remove (queue->iterator);

    // Fix the current position
    queue->iterator = iter;
    return TRUE;
}

// Remove the queue item at the end of the queue
// If the removed item is the last one the position is adjusted to
// the nearest valid item
gboolean play_queue_remove_last (PlayQueue *queue)
{
    GSequenceIter *iter;

    g_return_val_if_fail (PLAY_IS_QUEUE (queue), FALSE);

    // Empty queue
    if (!queue->iterator)
        return FALSE;

    // Find the last element
    iter = g_sequence_iter_prev (g_sequence_get_end_iter (queue->sequence));
    if (iter == queue->iterator) {
        // The current position is at the iterator to be deleted and it
        // will have to be adjusted
        return play_queue_remove_current (queue);
    }
    g_sequence_remove (iter);
    return TRUE;
}

// Sort the queue items by the position they were added in
// This function should be called after all items are added to the queue
gboolean play_queue_sort_by_position (PlayQueue *queue)
{
    g_return_val_if_fail (PLAY_IS_QUEUE (queue), FALSE);

    g_sequence_sort (queue->sequence, queue_sort_position, NULL);
    return TRUE;
}

// Add a GFile to the queue
static gboolean queue_add_gfile (PlayQueue *queue, GFile *file)
{
    PlayQueueItem *item;
    gboolean ret;

    item = play_queue_item_new ();

    if (G_UNLIKELY (!play_queue_item_set_gfile (item, file)))
        return FALSE;

    ret = queue_add_item (queue, item, 0);
    g_object_unref (item);
    return ret;
}

// Add a PlayQueueItem to the queue at the given position
// If the given position is 0 the item is placed at the end of the queue
static gboolean queue_add_item (PlayQueue *queue,
                                PlayQueueItem *item,
                                guint position)
{
    // Set the position as the item's custom data
    g_object_set_data (
        G_OBJECT (item),
        "queue-position",
        GUINT_TO_POINTER (position ? position : queue->position++));
    // Add to the queue
    g_sequence_append (queue->sequence, g_object_ref (item));
    if (!queue->iterator)
        queue->iterator = g_sequence_get_begin_iter (queue->sequence);

    g_signal_emit (
        queue,
        signals[ITEM_ADDED],
        0,
        item);
    return TRUE;
}

// Read a playlist and add the content to the queue
static gboolean queue_add_playlist (PlayQueue *queue, GFile *file)
{
    if (!play_playlist_parse_gfile (
            queue->playlist,
            file,
            GUINT_TO_POINTER (queue->position))) {
        return FALSE;
    }
    queue->pending++;
    queue->position++;
    return TRUE;
}

// Free memory allocated for a temporary data structure
static void queue_download_free (PlayQueueDownload *download)
{
    g_slice_free (PlayQueueDownload, download);
}

// Download progress of a playlist has been updated
static void queue_playlist_download_progress (PlayPlaylist *playlist,
                                              guint id,
                                              guint current_bytes,
                                              guint total_bytes,
                                              gpointer custom,
                                              PlayQueue *queue)
{
    PlayQueueDownload *download;

    download = g_hash_table_lookup (queue->download, GUINT_TO_POINTER (id));
    if (!download) {
        download = g_slice_new (PlayQueueDownload);
        g_hash_table_insert (
            queue->download,
            GUINT_TO_POINTER (id),
            download);

        queue->download_total += total_bytes;
    }
    // Update and recalculate the download progress
    download->current_bytes = current_bytes;
    download->total_bytes = total_bytes;
    queue_update_download_current (queue);
    g_signal_emit (
        queue,
        signals[PLAYLIST_PROGRESS_UPDATED],
        0);
}

// A playlist has been completely processed and no more callbacks will follow
static void queue_playlist_finished (PlayPlaylist *playlist,
                                     guint id,
                                     gpointer custom,
                                     PlayQueue *queue)
{
    PlayQueueDownload *download;

    download = g_hash_table_lookup (
        queue->download,
        GUINT_TO_POINTER (id));
    // On very small and fast downloads the progress may not be emitted
    // at all and then the hash entry would not exist
    if (download) {
        // Update and recalculate the download progress
        download->current_bytes = download->total_bytes;
        queue_update_download_current (queue);
    }
    queue->pending--;

    if (g_signal_has_handler_pending (
            queue,
            signals[PLAYLIST_FINISHED],
            0,
            FALSE)) {
        GFile *file = play_playlist_get_gfile (playlist, id);
        gchar *uri  = g_file_get_uri (file);

        g_signal_emit (
            queue,
            signals[PLAYLIST_FINISHED],
            0,
            uri);
        g_free (uri);
    }
    g_signal_emit (
        queue,
        signals[PLAYLIST_PROGRESS_UPDATED],
        0);
}

// A file or URI has been found in a playlist
static void queue_playlist_item (PlayPlaylist *playlist,
                                 guint id,
                                 PlayQueueItem *item,
                                 gpointer custom,
                                 PlayQueue *queue)
{
    // Add an item to the queue with the assigned position
    queue_add_item (queue, item, (gint) GPOINTER_TO_UINT (custom));
}

// Handle a playlist reading/parsing error by forwarding it
// to the queue owner
static void queue_playlist_error (PlayPlaylist *playlist,
                                  guint id,
                                  const gchar *error,
                                  gpointer custom,
                                  PlayQueue *queue)
{
    PlayQueueDownload *download;

    download = g_hash_table_lookup (
        queue->download,
        GUINT_TO_POINTER (id));
    if (download) {
        // Update the download progress
        queue->download_total -= download->total_bytes;
    }
    queue->pending--;
    g_hash_table_remove (queue->download, GUINT_TO_POINTER (id));

    if (g_signal_has_handler_pending (
            queue,
            signals[PLAYLIST_ERROR],
            0,
            FALSE)) {
        GFile *file = play_playlist_get_gfile (playlist, id);
        gchar *uri  = g_file_get_uri (file);

        g_signal_emit (
            queue,
            signals[PLAYLIST_ERROR],
            0,
            uri,
            error);
        g_free (uri);
    }
    g_signal_emit (
        queue,
        signals[PLAYLIST_PROGRESS_UPDATED],
        0);
}

// Helper sorting function used for position sorting
static gint queue_sort_position (gconstpointer a,
                                 gconstpointer b,
                                 gpointer data)
{
    return
        g_object_get_data (G_OBJECT (a), "queue-position") -
        g_object_get_data (G_OBJECT (b), "queue-position");
}

// Helper sorting function used for random sorting
static gint queue_sort_random (gconstpointer a,
                               gconstpointer b,
                               gpointer data)
{
    return g_random_boolean () ? 1 : -1;
}

// Helper sorting function used for reverse sorting
static gint queue_sort_reverse (gconstpointer a,
                                gconstpointer b,
                                gpointer data)
{
    return 1;
}

// Recalculate the total number of already downloaded bytes
static void queue_update_download_current (PlayQueue *queue)
{
    queue->download_current = 0;

    g_hash_table_foreach (
        queue->download,
        (GHFunc) queue_update_download_current_func,
        queue);
}

// Recalculate the total number of already downloaded bytes - helper function
static void queue_update_download_current_func (gpointer key,
                                                PlayQueueDownload *download,
                                                PlayQueue *queue)
{
    queue->download_current += download->current_bytes;
}
