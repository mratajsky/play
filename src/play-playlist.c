/**
 * PLAY
 * play-playlist.c: Read playlists
 * Copyright (C) 2011-2013 Michal Ratajsky <michal.ratajsky@gmail.com>
 */
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/uri.h>
#include "play-common.h"
#include "play-downloader.h"
#include "play-playlist.h"
#include "play-queue-item.h"

G_DEFINE_TYPE (PlayPlaylist, play_playlist, G_TYPE_OBJECT);

typedef struct {
    guint            id;
    PlayPlaylistType type;
    GFile           *file;
    PlayQueueItem   *item;
    PlayPlaylist    *playlist;
    gpointer         custom;
} PlayPlaylistData;

// Return the playlist type based on the file suffix
static PlayPlaylistType playlist_get_type (const gchar *file_or_uri);

// Initiate a playlist download
static void playlist_download (PlayPlaylistData *data, const gchar *template);

// Playlist download progress indicator
static void playlist_download_progress (PlayDownloader *downloader,
                                        guint id,
                                        guint64 current_bytes,
                                        guint64 total_bytes,
                                        gpointer custom,
                                        PlayPlaylist *playlist);

// Playlist download completion indicator
static void playlist_download_finished (PlayDownloader *downloader,
                                        guint id,
                                        GFile *destination,
                                        gpointer custom,
                                        PlayPlaylist *playlist);

// Playlist download failure indicator
static void playlist_download_failed (PlayDownloader *downloader,
                                      guint id,
                                      const gchar *error,
                                      gpointer custom,
                                      PlayPlaylist *playlist);

// Parse a playlist in the ASX file format
// If the playlist is not a local file, it is downloaded first
static gboolean playlist_parse_asx (PlayPlaylistData *data);

// Parse a playlist in the M3U file format
// If the playlist is not a local file, it is read and parsed directly
// from the network as this format allows line-by-line parsing
static gboolean playlist_parse_m3u (PlayPlaylistData *data);

// Parse a playlist in the PLS file format
// If the playlist is not a local file, it is downloaded first
static gboolean playlist_parse_pls (PlayPlaylistData *data);

// Parse a playlist in the XSPF file format
// If the playlist is not a local file, it is downloaded first
static gboolean playlist_parse_xspf (PlayPlaylistData *data);

// Process a locally stored ASX playlist
static void playlist_parse_asx_file (PlayPlaylistData *data,
                                     const gchar *file);

// Start asynchronous reading from the playlist
// Function called after a local M3U playlist has been opened for reading
static void playlist_parse_m3u_read (GObject *source,
                                     GAsyncResult *result,
                                     PlayPlaylistData *data);

// Parse a single line of a M3U playlist
static void playlist_parse_m3u_line (GObject *source,
                                     GAsyncResult *result,
                                     PlayPlaylistData *data);

// Process a locally stored PLS playlist
static void playlist_parse_pls_file (PlayPlaylistData *data,
                                     const gchar *file);

// Process a locally stored XSPF playlist
static void playlist_parse_xspf_file (PlayPlaylistData *data,
                                      const gchar *file);

// Free memory allocated for a temporary data structure
static void playlist_free_data (PlayPlaylistData *data);

// Signals
enum {
    DOWNLOAD_PROGRESS,
    ERROR,
    FINISHED,
    QUEUE_ITEM,
    LAST_SIGNAL
};
static guint signals[LAST_SIGNAL];

// GObject/finalize
static void play_playlist_finalize (GObject *object)
{
    PlayPlaylist *playlist = PLAY_PLAYLIST (object);

    // Clean up
    g_object_unref (playlist->downloader);
    g_hash_table_destroy (playlist->data);

    // Chain up to the parent class
    G_OBJECT_CLASS (play_playlist_parent_class)->finalize (object);
}

// GObject/class init
static void play_playlist_class_init (PlayPlaylistClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

    gobject_class->finalize = play_playlist_finalize;

    signals[DOWNLOAD_PROGRESS] =
        g_signal_new ("download-progress",
                      G_TYPE_FROM_CLASS (gobject_class),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (PlayPlaylistClass, download_progress),
                      NULL,
                      NULL,
                      play_marshal_VOID__UINT_UINT64_UINT64_POINTER,
                      G_TYPE_NONE,
                      4,
                      G_TYPE_UINT,
                      G_TYPE_UINT64,
                      G_TYPE_UINT64,
                      G_TYPE_POINTER);
    signals[ERROR] =
        g_signal_new ("error",
                      G_TYPE_FROM_CLASS (gobject_class),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (PlayPlaylistClass, error),
                      NULL,
                      NULL,
                      play_marshal_VOID__UINT_STRING_POINTER,
                      G_TYPE_NONE,
                      3,
                      G_TYPE_UINT,
                      G_TYPE_STRING,
                      G_TYPE_POINTER);
    signals[FINISHED] =
        g_signal_new ("finished",
                      G_TYPE_FROM_CLASS (gobject_class),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (PlayPlaylistClass, finished),
                      NULL,
                      NULL,
                      g_cclosure_marshal_VOID__UINT_POINTER,
                      G_TYPE_NONE,
                      2,
                      G_TYPE_UINT,
                      G_TYPE_POINTER);
    signals[QUEUE_ITEM] =
        g_signal_new ("queue-item",
                      G_TYPE_FROM_CLASS (gobject_class),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (PlayPlaylistClass, queue_item),
                      NULL,
                      NULL,
                      play_marshal_VOID__UINT_OBJECT_POINTER,
                      G_TYPE_NONE,
                      3,
                      G_TYPE_UINT,
                      G_TYPE_OBJECT,
                      G_TYPE_POINTER);
}

// GObject/init
static void play_playlist_init (PlayPlaylist *playlist)
{
    playlist->data = g_hash_table_new_full (
        g_direct_hash,
        g_direct_equal,
        NULL,
        (GDestroyNotify) playlist_free_data);

    playlist->downloader = play_downloader_new ();
    g_signal_connect (
        playlist->downloader,
        "progress",
        G_CALLBACK (playlist_download_progress),
        playlist);
    g_signal_connect (
        playlist->downloader,
        "finished",
        G_CALLBACK (playlist_download_finished),
        playlist);
    g_signal_connect (
        playlist->downloader,
        "failed",
        G_CALLBACK (playlist_download_failed),
        playlist);

    // Initial ID
    playlist->id_next = 1;
}

// Create a new playlist object
PlayPlaylist *play_playlist_new (void)
{
    return PLAY_PLAYLIST (g_object_new (PLAY_TYPE_PLAYLIST, NULL));
}

// Retrieve a GFile of the given playlist item that is being processed
GFile *play_playlist_get_gfile (PlayPlaylist *playlist, guint id)
{
    PlayPlaylistData *data;

    g_return_val_if_fail (PLAY_IS_PLAYLIST (playlist), NULL);
    g_return_val_if_fail (id, NULL);

    data = g_hash_table_lookup (
        playlist->data,
        GUINT_TO_POINTER (id));
    if (data)
        return data->file;

    return NULL;
}

// Parse a playlist
// For each discovered media file or URI a signal is emitted
// Returns a playlist ID or 0 on error
// The request will be processed in the background and either the "finished"
// or the "error" signal will be emitted later
guint play_playlist_parse (PlayPlaylist *playlist,
                           const gchar *file_or_uri,
                           gpointer custom)
{
    GFile *file;
    guint  id;

    g_return_val_if_fail (PLAY_IS_PLAYLIST (playlist), 0);
    g_return_val_if_fail (file_or_uri, 0);

    file = g_file_new_for_commandline_arg (file_or_uri);
    id   = play_playlist_parse_gfile (playlist, file, custom);

    g_object_unref (file);
    return id;
}

// Parse a playlist pointed to by the speficied GFile
guint play_playlist_parse_gfile (PlayPlaylist *playlist,
                                 GFile *file,
                                 gpointer custom)
{
    PlayPlaylistType  type;
    PlayPlaylistData *data;
    gchar *name;

    g_return_val_if_fail (PLAY_IS_PLAYLIST (playlist), 0);
    g_return_val_if_fail (G_IS_FILE (file), 0);

    // Use GFile function to read the file name and check the suffix for
    // playlist type because the GFile might point to a URI that has
    // additional parts after the file name
    name = g_file_get_basename (file);
    type = playlist_get_type (name);
    g_free (name);

    // Unknown or no file suffix
    if (type == PLAY_PLAYLIST_TYPE_UNKNOWN)
        return 0;

    // Create a temporary structure to be passed around while
    // downloading and reading the playlist
    data = g_slice_new0 (PlayPlaylistData);
    data->id = playlist->id_next++;
    data->type = type;
    data->file = g_object_ref (file);
    data->playlist = playlist;
    data->custom = custom;

    switch (type) {
        case PLAY_PLAYLIST_TYPE_ASX:
            g_idle_add ((GSourceFunc) playlist_parse_asx, data);
            break;
        case PLAY_PLAYLIST_TYPE_M3U:
        case PLAY_PLAYLIST_TYPE_M3U_UTF8:
            g_idle_add ((GSourceFunc) playlist_parse_m3u, data);
            break;
        case PLAY_PLAYLIST_TYPE_PLS:
            g_idle_add ((GSourceFunc) playlist_parse_pls, data);
            break;
        case PLAY_PLAYLIST_TYPE_XSPF:
            g_idle_add ((GSourceFunc) playlist_parse_xspf, data);
            break;
        default:
            g_assert_not_reached ();
            playlist_free_data (data);
            return 0;
    }
    // Store the temporary data
    g_hash_table_insert (
        playlist->data,
        GUINT_TO_POINTER (data->id),
        data);
    return data->id;
}

// Return TRUE if the given file or URI is a supported playlist
gboolean play_playlist_file_is_playlist (const gchar *file)
{
    g_return_val_if_fail (file, FALSE);

    return playlist_get_type (file) != PLAY_PLAYLIST_TYPE_UNKNOWN;
}

// Return the playlist type based on the file suffix
static PlayPlaylistType playlist_get_type (const gchar *file_name)
{
    if (g_str_has_suffix (file_name, ".asx"))
        return PLAY_PLAYLIST_TYPE_ASX;
    if (g_str_has_suffix (file_name, ".pls"))
        return PLAY_PLAYLIST_TYPE_PLS;
    if (g_str_has_suffix (file_name, ".m3u"))
        return PLAY_PLAYLIST_TYPE_M3U;
    if (g_str_has_suffix (file_name, ".m3u8"))
        return PLAY_PLAYLIST_TYPE_M3U_UTF8;
    if (g_str_has_suffix (file_name, ".xspf"))
        return PLAY_PLAYLIST_TYPE_XSPF;

    // Not a playlist or not currently supported
    return PLAY_PLAYLIST_TYPE_UNKNOWN;
}

// Initiate a playlist download
static void playlist_download (PlayPlaylistData *data, const gchar *template)
{
    gchar *uri = g_file_get_uri (data->file);

    if (!play_downloader_download_temp (
            data->playlist->downloader,
            uri,
            template,
            GUINT_TO_POINTER (data->id))) {
        g_signal_emit (
            data->playlist,
            signals[ERROR],
            0,
            data->id,
            "Download has failed",
            data->custom);
        // Delete data of the current item
        g_hash_table_remove (
            data->playlist->data,
            GUINT_TO_POINTER (data->id));
    }
    g_free (uri);
}

// Playlist download progress indicator
static void playlist_download_progress (PlayDownloader *downloader,
                                        guint id,
                                        guint64 current_bytes,
                                        guint64 total_bytes,
                                        gpointer custom,
                                        PlayPlaylist *playlist)
{
    PlayPlaylistData *data;

    data = g_hash_table_lookup (playlist->data, custom);
    if (G_UNLIKELY (!data)) {
        g_assert_not_reached ();
        return;
    }
    g_signal_emit (
        G_OBJECT (playlist),
        signals[DOWNLOAD_PROGRESS],
        0,
        data->id,
        current_bytes,
        total_bytes,
        data->custom);
}

// Playlist download completion indicator
static void playlist_download_finished (PlayDownloader *downloader,
                                        guint id,
                                        GFile *destination,
                                        gpointer custom,
                                        PlayPlaylist *playlist)
{
    PlayPlaylistData *data;
    gchar *path;

    data = g_hash_table_lookup (playlist->data, custom);
    if (G_UNLIKELY (!data)) {
        g_assert_not_reached ();
        return;
    }
    path = g_file_get_path (destination);
    if (G_LIKELY (path)) {
        switch (data->type) {
            case PLAY_PLAYLIST_TYPE_ASX:
                playlist_parse_asx_file (data, path);
                break;
            case PLAY_PLAYLIST_TYPE_PLS:
                playlist_parse_pls_file (data, path);
                break;
            case PLAY_PLAYLIST_TYPE_XSPF:
                playlist_parse_xspf_file (data, path);
                break;
            default:
                g_assert_not_reached ();
                break;
        }
        g_free (path);
    } else {
        g_assert_not_reached ();
    }
    // Delete the downloaded file
    g_file_delete (destination, NULL, NULL);

    // Delete data of the current item
    g_hash_table_remove (playlist->data, custom);
}

// Playlist download failure indicator
static void playlist_download_failed (PlayDownloader *downloader,
                                      guint id,
                                      const gchar *error,
                                      gpointer custom,
                                      PlayPlaylist *playlist)
{
    PlayPlaylistData *data;

    data = g_hash_table_lookup (playlist->data, custom);
    if (G_UNLIKELY (!data)) {
        g_assert_not_reached ();
        return;
    }
    g_signal_emit (
        data->playlist,
        signals[ERROR],
        0,
        data->id,
        error,
        data->custom);

    // Delete data of the current item
    g_hash_table_remove (playlist->data, custom);
}

// Parse a playlist in the ASX file format
// If the playlist is not a local file, it is downloaded first
static gboolean playlist_parse_asx (PlayPlaylistData *data)
{
    char *path;

    path = g_file_get_path (data->file);
    if (path) {
        // Local file, parse it directly
        playlist_parse_asx_file (data, (const gchar *) path);
        g_free (path);
    } else {
        // Download the file to a temporary location
        playlist_download (data, "play-XXXXXX.asx");
    }
    // Return FALSE to stop the function from being called again
    return FALSE;
}

// Parse a playlist in the M3U file format
// If the playlist is not a local file, it is read and parsed directly
// from the network as this format allows line-by-line parsing
static gboolean playlist_parse_m3u (PlayPlaylistData *data)
{
    g_file_read_async (
        data->file,
        G_PRIORITY_DEFAULT,
        NULL,
        (GAsyncReadyCallback) playlist_parse_m3u_read,
        data);

    // Return FALSE to stop the function from being called again
    return FALSE;
}

// Parse a playlist in the PLS file format
// If the playlist is not a local file, it is downloaded first
static gboolean playlist_parse_pls (PlayPlaylistData *data)
{
    char *path;

    path = g_file_get_path (data->file);
    if (path) {
        // Local file, parse it directly
        playlist_parse_pls_file (data, (const gchar *) path);
        g_free (path);
    } else {
        // Download the file to a temporary location
        playlist_download (data, "play-XXXXXX.pls");
    }
    // Return FALSE to stop the function from being called again
    return FALSE;
}

// Parse a playlist in the XSPF file format
// If the playlist is not a local file, it is downloaded first
static gboolean playlist_parse_xspf (PlayPlaylistData *data)
{
    char *path;

    path = g_file_get_path (data->file);
    if (path) {
        // Local file, parse it directly
        playlist_parse_xspf_file (data, (const gchar *) path);
        g_free (path);
    } else {
        // Download the file to a temporary location
        playlist_download (data, "play-XXXXXX.xspf");
    }
    // Return FALSE to stop the function from being called again
    return FALSE;
}

// Process a locally stored ASX playlist
static void playlist_parse_asx_file (PlayPlaylistData *data, const gchar *file)
{
    xmlDocPtr doc;
    xmlNode  *root, *n1, *n2;

    doc = xmlReadFile (
        file,
        NULL,
        XML_PARSE_RECOVER | XML_PARSE_NOERROR | XML_PARSE_NOWARNING);
    if (!doc) {
        g_signal_emit (
            data->playlist,
            signals[ERROR],
            0,
            data->id,
            "Error parsing XML file format",
            data->custom);

        // Delete data of the current item
        g_hash_table_remove (
            data->playlist->data,
            GUINT_TO_POINTER (data->id));
        return;
    }
    // Get the root element - must be <asx>
    root = xmlDocGetRootElement (doc);
    if (!root ||
        !root->name ||
        xmlStrcasecmp (root->name, (const xmlChar *) "asx")) {
        xmlFreeDoc (doc);
        g_signal_emit (
            data->playlist,
            signals[ERROR],
            0,
            data->id,
            "Invalid file format",
            data->custom);

        // Delete data of the current item
        g_hash_table_remove (
            data->playlist->data,
            GUINT_TO_POINTER (data->id));
        return;
    }
    // Read all the child nodes inside the parent <asx>
    for (n1 = root->children; n1 != NULL; n1 = n1->next) {
        PlayQueueItem *item;

        // Only read <entry> nodes which contain playlist items
        if (g_ascii_strcasecmp((const gchar *) n1->name, "entry"))
            continue;

        item = play_queue_item_new ();

        // Read child nodes inside an <entry>
        for (n2 = n1->children; n2 != NULL; n2 = n2->next) {
            // <ref> specifies a URI
            if (!g_ascii_strcasecmp ((const gchar *) n2->name, "ref")) {
                xmlAttrPtr href = NULL;
                xmlAttrPtr tmp;

                // The URI is inside the href attribute
                tmp = n2->properties;
                while (tmp) {
                    if (!xmlStrcasecmp (tmp->name, (const xmlChar *) "href")) {
                        href = tmp;
                        break;
                    }
                    tmp = tmp->next;
                }
                if (href) {
                    xmlChar *location = xmlGetProp (n2, href->name);

                    if (G_LIKELY (location)) {
                        play_queue_item_set_file_or_uri (
                            item,
                            (const gchar *) location);
                        xmlFree (location);
                    }
                }
            }
            // Optional track artist
            if (!g_ascii_strcasecmp((const gchar *) n2->name, "author")) {
                xmlChar *content;

                content = xmlNodeGetContent (n2);
                if (content) {
                    play_queue_item_set_metadata (
                        item,
                        PLAY_METADATA_ARTIST,
                        (const gchar *) content);
                    xmlFree (content);
                }
            }
            // Optional track title
            if (!g_ascii_strcasecmp ((const gchar *) n2->name, "title")) {
                xmlChar *content;

                content = xmlNodeGetContent (n2);
                if (content) {
                    play_queue_item_set_metadata (
                        item,
                        PLAY_METADATA_TITLE,
                        (const gchar *) content);
                    xmlFree (content);
                }
            }
        }
        // Check if a URI was found in the current <entry> and if so,
        // add the item to the queue
        if (play_queue_item_is_valid (item))
            g_signal_emit (
                data->playlist,
                signals[QUEUE_ITEM],
                0,
                data->id,
                item,
                data->custom);
        g_object_unref (item);
    }
    xmlFreeDoc (doc);
    xmlCleanupParser ();

    // Signal that parsing of the playlist has finished
    g_signal_emit (
        data->playlist,
        signals[FINISHED],
        0,
        data->id,
        data->custom);

    // Delete data of the current item
    g_hash_table_remove (data->playlist->data, GUINT_TO_POINTER (data->id));
}

// Start asynchronous reading from the playlist
// Function called after a local M3U playlist has been opened for reading
static void playlist_parse_m3u_read (GObject *source,
                                     GAsyncResult *result,
                                     PlayPlaylistData *data)
{
    GFileInputStream *input;
    GError *error = NULL;

    input = g_file_read_finish (
        G_FILE (source),
        result,
        &error);
    if (!input) {
        g_signal_emit (
            data->playlist,
            signals[ERROR],
            0,
            data->id,
            error->message,
            data->custom);

        g_error_free (error);
        // Delete data of the current item
        g_hash_table_remove (
            data->playlist->data,
            GUINT_TO_POINTER (data->id));
        return;
    }
    // Asynchronously read a single line from the input stream
    g_data_input_stream_read_line_async (
        g_data_input_stream_new (G_INPUT_STREAM (input)),
        G_PRIORITY_DEFAULT,
        NULL,
        (GAsyncReadyCallback) playlist_parse_m3u_line,
        data);
    g_object_unref (input);
}

// Parse a single line of a M3U playlist
static void playlist_parse_m3u_line (GObject *source,
                                     GAsyncResult *result,
                                     PlayPlaylistData *data)
{
    char       *line;
    GError     *error = NULL;
    const char *charset = NULL;
    gboolean    is_utf8;

    // Read the result of the asynchronous operation
    line = g_data_input_stream_read_line_finish (
        G_DATA_INPUT_STREAM (source),
        result,
        NULL,
        &error);
    if (!line) {
        if (error) {
            g_signal_emit (
                data->playlist,
                signals[ERROR],
                0,
                data->id,
                error->message,
                data->custom);
            g_error_free (error);
        } else {
            g_signal_emit (
                data->playlist,
                signals[FINISHED],
                0,
                data->id,
                data->custom);
        }
        // Delete data of the current item
        g_hash_table_remove (
            data->playlist->data,
            GUINT_TO_POINTER (data->id));

        g_object_unref (source);
        return;
    }
    is_utf8 = g_get_charset (&charset);

    // Convert the line to encoding of the current system locale if the system
    // locale is not UTF-8
    if (data->type == PLAY_PLAYLIST_TYPE_M3U_UTF8) {
        if (!is_utf8) {
            gchar *converted = g_locale_from_utf8 (
                line, -1,
                NULL, NULL, NULL);
            if (converted) {
                g_free (line);
                line = converted;
            }
        }
    } else {
        // The original M3U file format should be in WINDOWS-1252 encoding
        // Convert the string to the one of the current system locale
        gchar *converted = g_convert (
            line, -1, charset, "WINDOWS-1252",
            NULL, NULL, NULL);
        if (converted) {
            g_free (line);
            line = converted;
        }
    }
    // Remove the trailing newline
    line = g_strchomp (line);

    if (!data->item)
        data->item = play_queue_item_new ();

    // Use extended information to read the track title
    if (g_str_has_prefix (line, "#EXTINF:")) {
        gchar **info;

        info = g_strsplit (line + 8, ",", 2);

        // Title
        if (g_strv_length (info) > 1) {
            play_queue_item_set_metadata (
                data->item,
                PLAY_METADATA_TITLE_FULL, g_strstrip (info[1]));
        }
        g_strfreev (info);
    }
    if (line[0] && !g_str_has_prefix (line, "#")) {
        // A non-information line and non-empty line must contain a file
        // path or a URI
        play_queue_item_set_file_or_uri (data->item, line);
        g_signal_emit (
            data->playlist,
            signals[QUEUE_ITEM],
            0,
            data->id,
            data->item,
            data->custom);

        g_object_unref (data->item);
        data->item = NULL;
    }
    // Read the next line from the playlist
    g_data_input_stream_read_line_async (
        G_DATA_INPUT_STREAM (source),
        G_PRIORITY_DEFAULT,
        NULL,
        (GAsyncReadyCallback) playlist_parse_m3u_line,
        data);

    g_free (line);
}

// Parse a locally stored PLS playlist
static void playlist_parse_pls_file (PlayPlaylistData *data, const gchar *file)
{
    GKeyFile *kf;
    GError *error = NULL;
    gint entries = 0;

    // Use the glib's ini file parser
    kf = g_key_file_new ();
    if (!g_key_file_load_from_file (kf, file, G_KEY_FILE_NONE, &error) ||
        !g_key_file_has_group (kf, "playlist")) {
        if (error) {
            g_signal_emit (
                data->playlist,
                signals[ERROR],
                0,
                data->id,
                error->message,
                data->custom);
            g_error_free (error);
        } else {
            g_signal_emit (
                data->playlist,
                signals[ERROR],
                0,
                data->id,
                "Invalid file format",
                data->custom);
        }
        g_key_file_free (kf);
        // Delete data of the current item
        g_hash_table_remove (
            data->playlist->data,
            GUINT_TO_POINTER (data->id));
        return;
    }
    // Read the number of entries
    entries = g_key_file_get_integer (
        kf,
        "playlist", "NumberOfEntries",
        NULL);
    if (!entries)
        entries = g_key_file_get_integer (
            kf,
            "playlist", "numberofentries",
            NULL);
    if (!entries)
        entries = g_key_file_get_integer (
            kf,
            "playlist", "NUMBEROFENTRIES",
            NULL);
    if (entries) {
        gint i;
        // Read all the entries
        for (i = 1; i <= entries; i++) {
            gchar  name[16];
            gchar *value;

            // File path
            g_snprintf (name, sizeof (name), "File%d", i);
            value = g_key_file_get_string (kf, "playlist", name, NULL);
            if (value) {
                // Create a new queue item for the read file name or URI
                if (!data->item)
                    data->item = play_queue_item_new ();

                play_queue_item_set_file_or_uri (
                    data->item,
                    value);
                g_free (value);
            } else {
                // File information is mandatory, others are optional
                continue;
            }

            // Optional title
            g_snprintf (name, sizeof (name), "Title%d", i);
            value = g_key_file_get_string (kf, "playlist", name, NULL);
            if (value) {
                play_queue_item_set_metadata (
                    data->item,
                    PLAY_METADATA_TITLE_FULL,
                    value);
                g_free (value);
            }
            g_signal_emit (
                data->playlist,
                signals[QUEUE_ITEM],
                0,
                data->id,
                data->item,
                data->custom);

            g_object_unref (data->item);
            data->item = NULL;
        }
    }
    g_signal_emit (
        data->playlist,
        signals[FINISHED],
        0,
        data->id,
        data->custom);

    g_key_file_free (kf);

    // Delete data of the current item
    g_hash_table_remove (data->playlist->data, GUINT_TO_POINTER (data->id));
}

// Process a locally stored XSPF playlist
static void playlist_parse_xspf_file (PlayPlaylistData *data, const gchar *file)
{
    xmlDocPtr doc;
    xmlNode  *root, *n1, *n2, *n3;
    xmlChar  *value;

    doc = xmlReadFile (
        file,
        NULL,
        XML_PARSE_RECOVER | XML_PARSE_NOERROR | XML_PARSE_NOWARNING);
    if (!doc) {
        g_signal_emit (
            data->playlist,
            signals[ERROR],
            0,
            data->id,
            "Error parsing XML file format",
            data->custom);

        // Delete data of the current item
        g_hash_table_remove (
            data->playlist->data,
            GUINT_TO_POINTER (data->id));
        return;
    }
    // Get the root element - must be called <playlist>
    root = xmlDocGetRootElement (doc);
    if (!root ||
        !root->name ||
        xmlStrcasecmp (root->name, (const xmlChar *) "playlist")) {
        xmlFreeDoc (doc);
        g_signal_emit (
            data->playlist,
            signals[ERROR],
            0,
            data->id,
            "Invalid file format",
            data->custom);

        // Delete data of the current item
        g_hash_table_remove (
            data->playlist->data,
            GUINT_TO_POINTER (data->id));
        return;
    }

    // Read all the child nodes inside the parent <playlist>
    for (n1 = root->children; n1 != NULL; n1 = n1->next) {
        // Only read nodes inside the <trackList>
        if (g_ascii_strcasecmp ((const gchar *) n1->name, "trackList"))
            continue;

        for (n2 = n1->children; n2 != NULL; n2 = n2->next) {
            PlayQueueItem *item;

            // Only read <track> nodes which contain playlist items
            if (g_ascii_strcasecmp ((const gchar *) n2->name, "track") ||
                !n2->children)
                continue;

            item = play_queue_item_new ();

            for (n3 = n2->children; n3 != NULL; n3 = n3->next) {
                value = xmlNodeGetContent (n3);
                if (G_UNLIKELY (!value))
                    continue;

                // Location
                if (!g_ascii_strcasecmp ((const gchar *) n3->name, "location")) {
                    gchar *unescaped = xmlURIUnescapeString(
                        (const char *) value, 0,
                        NULL);
                    if (G_LIKELY (unescaped)) {
                        play_queue_item_set_file_or_uri (
                            item,
                            (const gchar *) unescaped);
                        xmlFree (unescaped);
                    }
                }
                // Artist
                if (!g_ascii_strcasecmp ((const gchar *) n3->name, "creator")) {
                    play_queue_item_set_metadata (
                        item,
                        PLAY_METADATA_ARTIST,
                        (const gchar *) value);
                }
                // Track title
                if (!g_ascii_strcasecmp ((const gchar *) n3->name, "title")) {
                    play_queue_item_set_metadata (
                        item,
                        PLAY_METADATA_TITLE,
                        (const gchar *) value);
                }
                xmlFree (value);
            }
            // Check if a URI was found in the current <track> and if so,
            // add the item to the queue
            if (play_queue_item_is_valid (item))
                g_signal_emit (
                    data->playlist,
                    signals[QUEUE_ITEM],
                    0,
                    data->id,
                    item,
                    data->custom);
            g_object_unref (item);
        }
    }
    xmlFreeDoc (doc);
    xmlCleanupParser ();

    // Signal that parsing of the playlist has finished
    g_signal_emit (
        data->playlist,
        signals[FINISHED],
        0,
        data->id,
        data->custom);

    // Delete data of the current item
    g_hash_table_remove (data->playlist->data, GUINT_TO_POINTER (data->id));
}

// Free memory allocated for a temporary data structure
static void playlist_free_data (PlayPlaylistData *data)
{
    if (data->item)
        g_object_unref (data->item);

    g_object_unref (data->file);
    g_slice_free (PlayPlaylistData, data);
}
