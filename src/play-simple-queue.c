/**
 * PLAY
 * play-simple-queue.c: A sorted queue of media files
 * Copyright (C) 2011-2013 Michal Ratajsky <michal.ratajsky@gmail.com>
 */
#include "play-common.h"
#include "play-simple-queue.h"
#include "play-queue-item.h"

G_DEFINE_TYPE (PlaySimpleQueue, play_simple_queue, G_TYPE_OBJECT);

// GObject/finalize
static void play_simple_queue_finalize (GObject *object)
{
    // Clean up
    g_sequence_free (PLAY_SIMPLE_QUEUE (object)->sequence);

    // Chain up to the parent class
    G_OBJECT_CLASS (play_simple_queue_parent_class)->finalize (object);
}

// GObject/class init
static void play_simple_queue_class_init (PlaySimpleQueueClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

    gobject_class->finalize = play_simple_queue_finalize;
}

// GObject/init
static void play_simple_queue_init (PlaySimpleQueue *queue)
{
    queue->sequence = g_sequence_new ((GDestroyNotify) g_object_unref);
}

// Create a new queue object
PlaySimpleQueue *play_simple_queue_new (void)
{
    return PLAY_SIMPLE_QUEUE (g_object_new (PLAY_TYPE_SIMPLE_QUEUE, NULL));
}

// Add a queue item to the end of the queue
// Returns TRUE on success
gboolean play_simple_queue_append (PlaySimpleQueue *queue,
                                   PlayQueueItem *item,
                                   gboolean set_as_current)
{
    g_return_val_if_fail (PLAY_IS_SIMPLE_QUEUE (queue), FALSE);
    g_return_val_if_fail (PLAY_IS_QUEUE_ITEM (item), FALSE);

    // Add to the queue
    g_sequence_append (queue->sequence, g_object_ref (item));
    if (set_as_current)
        play_simple_queue_position_set_last (queue);
    else if (!queue->iterator)
        queue->iterator = g_sequence_get_begin_iter (queue->sequence);

    return TRUE;
}

// Add a queue item to the beginning of the queue
// Returns TRUE on success
gboolean play_simple_queue_prepend (PlaySimpleQueue *queue,
                                    PlayQueueItem *item,
                                    gboolean set_as_current)
{
    g_return_val_if_fail (PLAY_IS_SIMPLE_QUEUE (queue), FALSE);
    g_return_val_if_fail (PLAY_IS_QUEUE_ITEM (item), FALSE);

    // Add to the queue
    g_sequence_prepend (queue->sequence, g_object_ref (item));
    if (set_as_current || !queue->iterator)
        queue->iterator = g_sequence_get_begin_iter (queue->sequence);

    return TRUE;
}

// Return the count of items in the queue
guint play_simple_queue_get_count (PlaySimpleQueue *queue)
{
    g_return_val_if_fail (PLAY_IS_SIMPLE_QUEUE (queue), 0);

    return g_sequence_get_length (queue->sequence);
}

// Return the queue item at the current position or NULL if the queue is empty
PlayQueueItem *play_simple_queue_get_current (PlaySimpleQueue *queue)
{
    g_return_val_if_fail (PLAY_IS_SIMPLE_QUEUE (queue), NULL);

    // Empty queue
    if (!queue->iterator)
        return NULL;

    return g_sequence_get (queue->iterator);
}

// Set the current queue position to the first item of the queue
// Returns TRUE on success
gboolean play_simple_queue_position_set_first (PlaySimpleQueue *queue)
{
    g_return_val_if_fail (PLAY_IS_SIMPLE_QUEUE (queue), FALSE);

    queue->iterator = g_sequence_get_begin_iter (queue->sequence);
    return TRUE;
}

// Set the current queue position to the last item of the queue
// Returns TRUE on success
gboolean play_simple_queue_position_set_last (PlaySimpleQueue *queue)
{
    g_return_val_if_fail (PLAY_IS_SIMPLE_QUEUE (queue), FALSE);

    queue->iterator = g_sequence_iter_prev (
        g_sequence_get_end_iter (queue->sequence));

    return TRUE;
}

// Move the queue position one item forward in the queue
// Returns TRUE on success
// Returns FALSE if the queue is empty or already at the last item
gboolean play_simple_queue_position_set_next (PlaySimpleQueue *queue)
{
    GSequenceIter *iter;

    g_return_val_if_fail (PLAY_IS_SIMPLE_QUEUE (queue), FALSE);

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
gboolean play_simple_queue_position_set_previous (PlaySimpleQueue *queue)
{
    GSequenceIter *iter;

    g_return_val_if_fail (PLAY_IS_SIMPLE_QUEUE (queue), FALSE);

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
gboolean play_simple_queue_position_is_first (PlaySimpleQueue *queue)
{
    g_return_val_if_fail (PLAY_IS_SIMPLE_QUEUE (queue), FALSE);

    // Empty queue
    if (!queue->iterator)
        return FALSE;

    return g_sequence_iter_is_begin (queue->iterator);
}

// Return TRUE if the current position is at the last item of the queue
gboolean play_simple_queue_position_is_last (PlaySimpleQueue *queue)
{
    g_return_val_if_fail (PLAY_IS_SIMPLE_QUEUE (queue), FALSE);

    // Empty queue
    if (!queue->iterator)
        return FALSE;

    // The GSequence iterator can point behind the last element,
    // we do not let this happen and instead check if it points one step
    // before (on the last element)
    return queue->iterator == g_sequence_iter_prev (
        g_sequence_get_end_iter (queue->sequence));
}

// Remove all items in the queue
gboolean play_simple_queue_remove_all (PlaySimpleQueue *queue)
{
    g_return_val_if_fail (PLAY_IS_SIMPLE_QUEUE (queue), FALSE);

    // Empty queue
    if (!queue->iterator)
        return FALSE;

    g_sequence_remove_range (
        g_sequence_get_begin_iter (queue->sequence),
        g_sequence_get_end_iter (queue->sequence));

    queue->iterator = NULL;
    return TRUE;
}
