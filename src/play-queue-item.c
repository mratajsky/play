/**
 * PLAY
 * play-queue-item.c: A media item in a queue
 * Copyright (C) 2011-2013 Michal Ratajsky <michal.ratajsky@gmail.com>
 */
#include "play-common.h"
#include "play-queue-item.h"

G_DEFINE_TYPE (PlayQueueItem, play_queue_item, G_TYPE_OBJECT);

GType play_metadata_get_type (void)
{
    static GType etype = 0;

    if (etype == 0) {
        static const GEnumValue values[] = {
            { PLAY_METADATA_UNKNOWN, "Unknown", "unknown" },
            { PLAY_METADATA_ARTIST, "Artist", "artist" },
            { PLAY_METADATA_TITLE, "Title", "title" },
            { PLAY_METADATA_TITLE_FULL, "Full title", "full-title" },
            { -1, NULL, NULL }
        };
        etype = g_enum_register_static ("PlayMetadataEnum", values);
    }
    return etype;
}

// GObject/finalize
static void play_queue_item_finalize (GObject *object)
{
    PlayQueueItem *item = PLAY_QUEUE_ITEM (object);
    
    // Clean up
    if (item->file)
        g_object_unref (item->file);
    if (item->uri)
        g_free (item->uri);
    if (item->name)
        g_free (item->name);

    g_hash_table_destroy (item->meta);

    // Chain up to the parent class
    G_OBJECT_CLASS (play_queue_item_parent_class)->finalize (object);
}

// GObject/class init
static void play_queue_item_class_init (PlayQueueItemClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

    gobject_class->finalize = play_queue_item_finalize;
}

// GObject/init
static void play_queue_item_init (PlayQueueItem *item)
{
    item->meta = g_hash_table_new_full (
        g_direct_hash,
        g_direct_equal,
        NULL,
        g_free);
}

// Create a new empty queue item object
PlayQueueItem *play_queue_item_new (void)
{
    return PLAY_QUEUE_ITEM (g_object_new (PLAY_TYPE_QUEUE_ITEM, NULL));
}

// Set the queue item to the given file path or URI
gboolean play_queue_item_set_file_or_uri (PlayQueueItem *item, const gchar *file_or_uri)
{
    GFile *file;
    gboolean ret;

    g_return_val_if_fail (PLAY_IS_QUEUE_ITEM (item), FALSE);
    g_return_val_if_fail (file_or_uri, FALSE);

    file = g_file_new_for_commandline_arg (file_or_uri);
    ret  = play_queue_item_set_gfile (item, file);

    g_object_unref (file);
    return ret;
}

// Set the queue item to the given GFile
gboolean play_queue_item_set_gfile (PlayQueueItem *item, GFile *file)
{
    g_return_val_if_fail (PLAY_IS_QUEUE_ITEM (item), FALSE);
    g_return_val_if_fail (G_IS_FILE (file), FALSE);

    if (item->file)
        g_object_unref (item->file);
    if (item->uri)
        g_free (item->uri);
    if (item->name)
        g_free (item->name);

    item->file = g_object_ref (file);
    item->uri  = g_file_get_uri (item->file);
    item->name = g_file_get_basename (item->file);
    if (!strcmp (item->name, "/")) {
        // If the GFile does not contain a file name component, the
        // function returns the string "/"
        // Treat it is as a special case here and use the URI instead
        g_free (item->name);
        item->name = g_strdup (item->uri);
    }
    return TRUE;
}

// Return TRUE if there is a file or location associated with this
// queue item, otherwise return FALSE
gboolean play_queue_item_is_valid (PlayQueueItem *item)
{
    g_return_val_if_fail (PLAY_IS_QUEUE_ITEM (item), FALSE);

    return item->file ? TRUE : FALSE;
}

// Return URI of the queue item
// The returned value is owned by the PlayQueueItem
const gchar *play_queue_item_get_uri (PlayQueueItem *item)
{
    g_return_val_if_fail (PLAY_IS_QUEUE_ITEM (item), NULL);

    return item->uri;
}

// Retrieve GFile of the queue item
// Use g_object_ref() to keep the reference
GFile *play_queue_item_get_gfile (PlayQueueItem *item)
{
    g_return_val_if_fail (PLAY_IS_QUEUE_ITEM (item), NULL);

    return item->file;
}

// Return a name to be displayed in the user interface
// The name is either a file name if known or a URI or NULL
// The returned value is owned by the PlayQueueItem
const gchar *play_queue_item_get_name (PlayQueueItem *item)
{
    g_return_val_if_fail (PLAY_IS_QUEUE_ITEM (item), NULL);

    if (item->name)
        return item->name;

    return item->uri;
}

// Retrieve a metadata value for the given metadata type
// The key should be one of the PLAY_METADATA_*
// The returned value is owned by the PlayQueueItem
const gchar *play_queue_item_get_metadata (PlayQueueItem *item, PlayMetadata type)
{
    g_return_val_if_fail (PLAY_IS_QUEUE_ITEM (item), NULL);

    return g_hash_table_lookup (item->meta, GUINT_TO_POINTER (type));
}

// Set a metadata value for the given metadata type
// Returns TRUE on success
gboolean play_queue_item_set_metadata (PlayQueueItem *item,
                                       PlayMetadata type,
                                       const gchar *value)
{
    g_return_val_if_fail (PLAY_IS_QUEUE_ITEM (item), FALSE);

    // The hash table automatically frees the values, so rather than
    // overwriting a previous value with NULL, unset it by removing it
    // from the table
    if (value)
        g_hash_table_insert (
            item->meta,
            GUINT_TO_POINTER (type),
            (gpointer) g_strdup (value));
    else
        g_hash_table_remove (
            item->meta,
            GUINT_TO_POINTER (type));

    if (type == PLAY_METADATA_ARTIST) {
        // Remember the time of the last artist update
        item->time_meta_artist = time (NULL);
    }
    if (type == PLAY_METADATA_TITLE &&
        time (NULL) - item->time_meta_artist > 1) {
        // If the artist information was received more than 1 second ago,
        // it is forgotten
        // Some online radios send out information about the current song and
        // when interrupted by an advertisment only the title information
        // is sent
        g_hash_table_remove (
            item->meta,
            GUINT_TO_POINTER (PLAY_METADATA_ARTIST));
    }
    // Create a custom field that contains the full title which consists of
    // both the artist and track name
    if (type == PLAY_METADATA_ARTIST || type == PLAY_METADATA_TITLE) {
        const gchar *artist;
        const gchar *title;
        gchar *title_full = NULL;

        // The current artist and title
        if (type == PLAY_METADATA_ARTIST)
            artist = value;
        else
            artist = play_queue_item_get_metadata (item, PLAY_METADATA_ARTIST);
        if (type == PLAY_METADATA_TITLE)
            title  = value;
        else
            title  = play_queue_item_get_metadata (item, PLAY_METADATA_TITLE);

        // Create the full track title
        if (artist && title) {
            title_full = g_strdup_printf ("%s - %s", artist, title);
        } else if (artist) {
            title_full = g_strdup (artist);
        } else if (title) {
            title_full = g_strdup (title);
        }
        if (title_full)
            g_hash_table_insert (
                item->meta,
                GUINT_TO_POINTER (PLAY_METADATA_TITLE_FULL),
                (gpointer) title_full);
        else
            g_hash_table_remove (
                item->meta,
                GUINT_TO_POINTER (PLAY_METADATA_TITLE_FULL));
    }
    return TRUE;
}

// Unset all the saved metadata
void play_queue_item_clear_metadata (PlayQueueItem *item)
{
    g_return_if_fail (PLAY_IS_QUEUE_ITEM (item));

    g_hash_table_remove_all (item->meta);
}
