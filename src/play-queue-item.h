/**
 * PLAY
 * play-queue-item.c: A media item in a queue
 * Copyright (C) 2011-2013 Michal Ratajsky <michal.ratajsky@gmail.com>
 */
#ifndef _PLAY_QUEUE_ITEM_H_
#define _PLAY_QUEUE_ITEM_H_

#include "play-common.h"

G_BEGIN_DECLS

typedef enum {
    PLAY_METADATA_UNKNOWN,
    PLAY_METADATA_ARTIST,
    PLAY_METADATA_TITLE,
    PLAY_METADATA_TITLE_FULL
} PlayMetadata;

#define PLAY_TYPE_METADATA                       \
    (play_metadata_get_type())
#define PLAY_TYPE_QUEUE_ITEM                     \
    (play_queue_item_get_type())
#define PLAY_QUEUE_ITEM(o)                       \
    (G_TYPE_CHECK_INSTANCE_CAST((o), PLAY_TYPE_QUEUE_ITEM, PlayQueueItem))
#define PLAY_QUEUE_ITEM_CLASS(k)                 \
    (G_TYPE_CHECK_CLASS_CAST((k), PLAY_TYPE_QUEUE_ITEM, PlayQueueItemClass))
#define PLAY_IS_QUEUE_ITEM(o)                    \
    (G_TYPE_CHECK_INSTANCE_TYPE((o), PLAY_TYPE_QUEUE_ITEM))
#define PLAY_IS_QUEUE_ITEM_CLASS(k)              \
    (G_TYPE_CHECK_CLASS_TYPE((k), PLAY_TYPE_QUEUE_ITEM))
#define PLAY_QUEUE_ITEM_GET_CLASS(o)             \
    (G_TYPE_INSTANCE_GET_CLASS((o), PLAY_TYPE_QUEUE_ITEM, PlayQueueItemClass))

typedef struct {
    GObject       parent_instance;
    GFile        *file;
    GHashTable   *meta;
    gchar        *uri;
    gchar        *name;
    time_t        time_meta_artist;
} PlayQueueItem;

typedef struct {
    GObjectClass  parent_class;
} PlayQueueItemClass;

extern GType play_metadata_get_type (void);
extern GType play_queue_item_get_type (void);

// Create a new empty queue item object
extern PlayQueueItem *play_queue_item_new (void);

// Set the queue item to the given file path or URI
extern gboolean play_queue_item_set_file_or_uri (PlayQueueItem *item,
                                                 const gchar *file_or_uri);

// Set the queue item to the given GFile
extern gboolean play_queue_item_set_gfile (PlayQueueItem *item, GFile *file);

// Return TRUE if there is a file or location associated with this
// queue item, otherwise return FALSE
extern gboolean play_queue_item_is_valid (PlayQueueItem *item);

// Return URI of the queue item
// The returned value is owned by the PlayQueueItem
extern const gchar *play_queue_item_get_uri (PlayQueueItem *item);

// Retrieve GFile of the queue item
// Use g_object_ref() to keep the reference
extern GFile *play_queue_item_get_gfile (PlayQueueItem *item);

// Return a name to be displayed in the user interface
// The name is either a file name if known or a URI or NULL
// The returned value is owned by the PlayQueueItem
extern const gchar *play_queue_item_get_name (PlayQueueItem *item);

// Retrieve a metadata value for the given metadata type
// The key should be one of the PLAY_METADATA_*
// The returned value is owned by the PlayQueueItem
extern const gchar *play_queue_item_get_metadata (PlayQueueItem *item,
                                                  PlayMetadata type);

// Set a metadata value for the given metadata type
// Returns TRUE on success
extern gboolean play_queue_item_set_metadata (PlayQueueItem *item,
                                              PlayMetadata type,
                                              const gchar *value);

// Unset all the saved metadata
extern void play_queue_item_clear_metadata (PlayQueueItem *item);

G_END_DECLS

#endif // _PLAY_QUEUE_ITEM_H_
