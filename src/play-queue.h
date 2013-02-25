/**
 * PLAY
 * play-queue.h: A sorted queue of media files
 * Copyright (C) 2011-2013 Michal Ratajsky <michal.ratajsky@gmail.com>
 */
#ifndef _PLAY_QUEUE_H_
#define _PLAY_QUEUE_H_

#include "play-common.h"
#include "play-playlist.h"
#include "play-queue-item.h"

G_BEGIN_DECLS

#define PLAY_TYPE_QUEUE                     \
    (play_queue_get_type())
#define PLAY_QUEUE(o)                       \
    (G_TYPE_CHECK_INSTANCE_CAST((o), PLAY_TYPE_QUEUE, PlayQueue))
#define PLAY_QUEUE_CLASS(k)                 \
    (G_TYPE_CHECK_CLASS_CAST((k), PLAY_TYPE_QUEUE, PlayQueueClass))
#define PLAY_IS_QUEUE(o)                    \
    (G_TYPE_CHECK_INSTANCE_TYPE((o), PLAY_TYPE_QUEUE))
#define PLAY_IS_QUEUE_CLASS(k)              \
    (G_TYPE_CHECK_CLASS_TYPE((k), PLAY_TYPE_QUEUE))
#define PLAY_QUEUE_GET_CLASS(o)             \
    (G_TYPE_INSTANCE_GET_CLASS((o), PLAY_TYPE_QUEUE, PlayQueueClass))

typedef struct {
    GObject        parent_instance;
    PlayPlaylist  *playlist;
    guint          pending;
    guint          position;
    GSequence     *sequence;
    GSequenceIter *iterator;
    GHashTable    *download;
    guint64        download_current;
    guint64        download_total;
} PlayQueue;

typedef struct {
    GObjectClass   parent_class;

    // Signals
    // An item has been added to the queue
    void (*item_added) (PlayQueue *queue,
                        PlayQueueItem *item,
                        gpointer user_data);

    // An error occured while reading a playlist at the given URI
    void (*playlist_error) (PlayQueue *queue,
                            const gchar *uri,
                            const gchar *error,
                            gpointer user_data);

    // Playlist download has finished
    void (*playlist_finished) (PlayQueue *queue,
                               const gchar *uri,
                               gpointer user_data);

    // Playlist download progress has been updated
    // Use play_queue_get_download_current() to read the new value
    void (*playlist_progress_updated) (PlayQueue *queue,
                                       gpointer user_data);
} PlayQueueClass;

extern GType play_queue_get_type (void);

// Create a new queue object
extern PlayQueue *play_queue_new (void);

// Add a file, directory or URI to the queue
// Returns TRUE on success
extern gboolean play_queue_add (PlayQueue *queue, const gchar *file_or_uri);

// Return the count of items in the queue
extern guint play_queue_get_count (PlayQueue *queue);

// Return the number of items still waiting to be added to the queue
// These are playlists being downloaded
extern guint play_queue_get_count_pending (PlayQueue *queue);

// Return the number of bytes already dowloaded from remote playlists
extern guint64 play_queue_get_download_current (PlayQueue *queue);

// Return the total number of bytes of remote playlists
extern guint64 play_queue_get_download_total (PlayQueue *queue);

// Return the queue item at the current position
extern PlayQueueItem *play_queue_get_current (PlayQueue *queue);

// Return the queue item at a random position
extern PlayQueueItem *play_queue_get_random (PlayQueue *queue);

// Set the current queue position to the first item of the queue
// Returns TRUE on success
extern gboolean play_queue_position_set_first (PlayQueue *queue);

// Set the current queue position to the last item of the queue
// Returns TRUE on success
extern gboolean play_queue_position_set_last (PlayQueue *queue);

// Move the queue position one item forward in the queue
// Returns TRUE on success
// Returns FALSE if the queue is empty or already at the last item
extern gboolean play_queue_position_set_next (PlayQueue *queue);

// Move the queue position one item backward in the queue
// Returns TRUE on success
// Returns FALSE if the queue is empty or already at the first item
extern gboolean play_queue_position_set_previous (PlayQueue *queue);

// Return TRUE if the current position is at the first item of the queue
extern gboolean play_queue_position_is_first (PlayQueue *queue);

// Return TRUE if the current position is at the last item of the queue
extern gboolean play_queue_position_is_last (PlayQueue *queue);

// Randomize the order of items in the queue
// The current position is kept at the item it was before the call
// of this function, use play_queue_position_set_first() to set it
// to the first item of the queue
extern gboolean play_queue_randomize (PlayQueue *queue);

// Reverse the order of items in the queue
// The current position is kept at the item it was before the call
// of this function, use play_queue_position_set_first() to set it
// to the first item of the queue
extern gboolean play_queue_reverse (PlayQueue *queue);

// Remove all items in the queue
extern gboolean play_queue_remove_all (PlayQueue *queue);

// Remove the queue item at the current position
// The current position then adjusted to point at the nearest element
// after or before the removed element (before when removing the last one)
extern gboolean play_queue_remove_current (PlayQueue *queue);

// Remove the queue item at the end of the queue
extern gboolean play_queue_remove_last (PlayQueue *queue);

// Sort the queue items by the position they were added in
// This function should be called after all items are added to the queue
extern gboolean play_queue_sort_by_position (PlayQueue *queue);

G_END_DECLS

#endif // _PLAY_QUEUE_H_
