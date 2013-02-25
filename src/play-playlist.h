/**
 * PLAY
 * play-playlist.h: Parse playlists
 * Copyright (C) 2011-2013 Michal Ratajsky <michal.ratajsky@gmail.com>
 */
#ifndef _PLAY_PLAYLIST_H_
#define _PLAY_PLAYLIST_H_

#include "play-common.h"
#include "play-downloader.h"
#include "play-queue-item.h"

G_BEGIN_DECLS

// Playlist types
typedef enum {
    PLAY_PLAYLIST_TYPE_UNKNOWN,
    PLAY_PLAYLIST_TYPE_ASX,
    PLAY_PLAYLIST_TYPE_M3U,
    PLAY_PLAYLIST_TYPE_M3U_UTF8,
    PLAY_PLAYLIST_TYPE_PLS,
    PLAY_PLAYLIST_TYPE_XSPF
} PlayPlaylistType;

#define PLAY_TYPE_PLAYLIST                     \
    (play_playlist_get_type())
#define PLAY_PLAYLIST(o)                       \
    (G_TYPE_CHECK_INSTANCE_CAST((o), PLAY_TYPE_PLAYLIST, PlayPlaylist))
#define PLAY_PLAYLIST_CLASS(k)                 \
    (G_TYPE_CHECK_CLASS_CAST((k), PLAY_TYPE_PLAYLIST, PlayPlaylistClass))
#define PLAY_IS_PLAYLIST(o)                    \
    (G_TYPE_CHECK_INSTANCE_TYPE((o), PLAY_TYPE_PLAYLIST))
#define PLAY_IS_PLAYLIST_CLASS(k)              \
    (G_TYPE_CHECK_CLASS_TYPE((k), PLAY_TYPE_PLAYLIST))
#define PLAY_PLAYLIST_GET_CLASS(o)             \
    (G_TYPE_INSTANCE_GET_CLASS((o), PLAY_TYPE_PLAYLIST, PlayPlaylistClass))

typedef struct {
    GObject         parent_instance;
    guint           id_next;
    PlayDownloader *downloader;
    GHashTable     *data;
} PlayPlaylist;

typedef struct {
    GObjectClass    parent_class;

    // Signals
    // Playlist download progress indicator
    void (*download_progress) (PlayPlaylist *playlist,
                               guint id,
                               guint64 current_bytes,
                               guint64 total_bytes,
                               gpointer custom);

    // Playlist could not be read or downloaded
    // After this signal the playlist is discarded
    void (*error) (PlayPlaylist *playlist,
                   guint id,
                   const gchar *error,
                   gpointer custom);

    // Playlist has been successfully processed
    // After this signal the playlist is discarded
    void (*finished) (PlayPlaylist *playlist,
                      guint id,
                      gpointer custom);

    // A file or URI entry has been found in the playlist
    void (*queue_item) (PlayPlaylist *playlist,
                        guint id,
                        PlayQueueItem *item,
                        gpointer custom);
} PlayPlaylistClass;

extern GType play_playlist_get_type (void);

// Global function
// Return TRUE if the given file or URI is a supported playlist
extern gboolean play_playlist_file_is_playlist (const gchar *file);

// Create a new playlist object
extern PlayPlaylist *play_playlist_new (void);

// Retrieve a GFile of the given playlist item that is being processed
extern GFile *play_playlist_get_gfile (PlayPlaylist *playlist, guint id);

// Parse a playlist
// Returns a playlist ID or 0 on error
// The request will be processed in the background and either the "finished"
// or the "error" signal will be emitted later
extern guint play_playlist_parse (PlayPlaylist *playlist,
                                  const gchar *file_or_uri,
                                  gpointer custom);

// Parse a playlist pointed to by the speficied GFile
extern guint play_playlist_parse_gfile (PlayPlaylist *playlist,
                                        GFile *file,
                                        gpointer custom);

// Return TRUE if the given file or URI is a supported playlist
extern gboolean play_playlist_file_is_playlist (const gchar *file);

G_END_DECLS

#endif // _PLAY_PLAYLIST_H_
