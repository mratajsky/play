/**
 * PLAY
 * play-simple-queue.h: A sorted queue of media files
 * Copyright (C) 2011-2013 Michal Ratajsky <michal.ratajsky@gmail.com>
 */
#ifndef _PLAY_SIMPLE_QUEUE_H_
#define _PLAY_SIMPLE_QUEUE_H_

#include "play-common.h"
#include "play-queue-item.h"

G_BEGIN_DECLS

#define PLAY_TYPE_SIMPLE_QUEUE                     \
    (play_simple_queue_get_type())
#define PLAY_SIMPLE_QUEUE(o)                       \
    (G_TYPE_CHECK_INSTANCE_CAST((o), PLAY_TYPE_SIMPLE_QUEUE, PlaySimpleQueue))
#define PLAY_SIMPLE_QUEUE_CLASS(k)                 \
    (G_TYPE_CHECK_CLASS_CAST((k), PLAY_TYPE_SIMPLE_QUEUE, PlaySimpleQueueClass))
#define PLAY_IS_SIMPLE_QUEUE(o)                    \
    (G_TYPE_CHECK_INSTANCE_TYPE((o), PLAY_TYPE_SIMPLE_QUEUE))
#define PLAY_IS_SIMPLE_QUEUE_CLASS(k)              \
    (G_TYPE_CHECK_CLASS_TYPE((k), PLAY_TYPE_SIMPLE_QUEUE))
#define PLAY_SIMPLE_QUEUE_GET_CLASS(o)             \
    (G_TYPE_INSTANCE_GET_CLASS((o), PLAY_TYPE_SIMPLE_QUEUE, PlaySimpleQueueClass))

typedef struct {
    GObject        parent_instance;
    GSequence     *sequence;
    GSequenceIter *iterator;
} PlaySimpleQueue;

typedef struct {
    GObjectClass   parent_class;
} PlaySimpleQueueClass;

extern GType play_simple_queue_get_type (void);

// Create a new queue object
extern PlaySimpleQueue *play_simple_queue_new (void);

// Add a queue item to the end of the queue
// Returns TRUE on success
extern gboolean play_simple_queue_append (PlaySimpleQueue *queue,
                                          PlayQueueItem *item,
                                          gboolean set_as_current);

// Add a queue item to the beginning of the queue
// Returns TRUE on success
extern gboolean play_simple_queue_prepend (PlaySimpleQueue *queue,
                                           PlayQueueItem *item,
                                           gboolean set_as_current);

// Return the count of items in the queue
extern guint play_simple_queue_get_count (PlaySimpleQueue *queue);

// Return the queue item at the current position
extern PlayQueueItem *play_simple_queue_get_current (PlaySimpleQueue *queue);

// Set the current queue position to the first item of the queue
// Returns TRUE on success
extern gboolean play_simple_queue_position_set_first (PlaySimpleQueue *queue);

// Set the current queue position to the last item of the queue
// Returns TRUE on success
extern gboolean play_simple_queue_position_set_last (PlaySimpleQueue *queue);

// Move the queue position one item forward in the queue
// Returns TRUE on success
// Returns FALSE if the queue is empty or already at the last item
extern gboolean play_simple_queue_position_set_next (PlaySimpleQueue *queue);

// Move the queue position one item backward in the queue
// Returns TRUE on success
// Returns FALSE if the queue is empty or already at the first item
extern gboolean play_simple_queue_position_set_previous (PlaySimpleQueue *queue);

// Return TRUE if the current position is at the first item of the queue
extern gboolean play_simple_queue_position_is_first (PlaySimpleQueue *queue);

// Return TRUE if the current position is at the last item of the queue
extern gboolean play_simple_queue_position_is_last (PlaySimpleQueue *queue);

// Remove all items in the queue
extern gboolean play_simple_queue_remove_all (PlaySimpleQueue *queue);

G_END_DECLS

#endif // _PLAY_SIMPLE_QUEUE_H_
