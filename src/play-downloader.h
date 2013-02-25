/**
 * PLAY
 * play-downloader.h: Download remote files
 * Copyright (C) 2011-2013 Michal Ratajsky <michal.ratajsky@gmail.com>
 */
#ifndef _PLAY_DOWNLOADER_H_
#define _PLAY_DOWNLOADER_H_

#include "play-common.h"

G_BEGIN_DECLS

#define PLAY_TYPE_DOWNLOADER                     \
    (play_downloader_get_type())
#define PLAY_DOWNLOADER(o)                       \
    (G_TYPE_CHECK_INSTANCE_CAST((o), PLAY_TYPE_DOWNLOADER, PlayDownloader))
#define PLAY_DOWNLOADER_CLASS(k)                 \
    (G_TYPE_CHECK_CLASS_CAST((k), PLAY_TYPE_DOWNLOADER, PlayDownloaderClass))
#define PLAY_IS_DOWNLOADER(o)                    \
    (G_TYPE_CHECK_INSTANCE_TYPE((o), PLAY_TYPE_DOWNLOADER))
#define PLAY_IS_DOWNLOADER_CLASS(k)              \
    (G_TYPE_CHECK_CLASS_TYPE((k), PLAY_TYPE_DOWNLOADER))
#define PLAY_DOWNLOADER_GET_CLASS(o)             \
    (G_TYPE_INSTANCE_GET_CLASS((o), PLAY_TYPE_DOWNLOADER, PlayDownloaderClass))

typedef struct {
    GObject         parent_instance;
    guint           id_next;
    GHashTable     *data;
} PlayDownloader;

typedef struct {
    GObjectClass    parent_class;

    // Signals
    // Progress of the current download
    void (*progress) (PlayDownloader *downloader,
                      guint id,
                      guint64 current_bytes,
                      guint64 total_bytes,
                      gpointer custom,
                      gpointer user_data);

    // Download has finished
    void (*finished) (PlayDownloader *downloader,
                      guint id,
                      GFile *destination,
                      gpointer custom,
                      gpointer user_data);

    // Download has failed
    void (*failed) (PlayDownloader *downloader,
                    guint id,
                    const gchar *error,
                    gpointer custom,
                    gpointer user_data);
} PlayDownloaderClass;

extern GType play_downloader_get_type (void);

// Create a new downloader object
extern PlayDownloader *play_downloader_new (void);

// Download a file to the specified path, which must include the file name
// Returns a download ID
extern guint play_downloader_download (PlayDownloader *downloader,
                                       const gchar *uri,
                                       const gchar *destination_file,
                                       gpointer data);

// Download a file to a temporary location specified as a template
// used by g_file_open_tmp()
// The real destination file name is later returned by the finished signal
// Returns a download ID
extern guint play_downloader_download_temp (PlayDownloader *downloader,
                                            const gchar *uri,
                                            const gchar *template,
                                            gpointer data);

// Cancel an ongoing download specified by the download ID
// Returns TRUE on success
extern gboolean play_downloader_cancel (PlayDownloader *downloader, guint id);

G_END_DECLS

#endif // _PLAY_DOWNLOADER_H_
