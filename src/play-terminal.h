/**
 * PLAY
 * play-terminal.h: Terminal input and related stuff
 * Copyright (C) 2011-2013 Michal Ratajsky <michal.ratajsky@gmail.com>
 */
#ifndef _PLAY_TERMINAL_H_
#define _PLAY_TERMINAL_H_

#include "play-common.h"

G_BEGIN_DECLS

// List of terminal keys the terminal handler is able to read
typedef enum {
    PLAY_TERMINAL_KEY_NONE,
    PLAY_TERMINAL_KEY_UP,
    PLAY_TERMINAL_KEY_DOWN,
    PLAY_TERMINAL_KEY_RIGHT,
    PLAY_TERMINAL_KEY_LEFT,
    PLAY_TERMINAL_KEY_ESC,
    PLAY_TERMINAL_KEY_PAGE_UP,
    PLAY_TERMINAL_KEY_PAGE_DOWN,
    PLAY_TERMINAL_KEY_HOME,
    PLAY_TERMINAL_KEY_END,
    PLAY_TERMINAL_KEY_BACKSPACE,
    PLAY_TERMINAL_KEY_SPACE,
    PLAY_TERMINAL_KEY_ENTER,
    PLAY_TERMINAL_KEY_TAB,
    PLAY_TERMINAL_KEY_CTRL_C
} PlayTerminalKey;

#ifndef STDIN_FILENO
#  define STDIN_FILENO 0
#endif

#define PLAY_TYPE_TERMINAL                     \
    (play_terminal_get_type())
#define PLAY_TERMINAL(o)                       \
    (G_TYPE_CHECK_INSTANCE_CAST((o), PLAY_TYPE_TERMINAL, PlayTerminal))
#define PLAY_TERMINAL_CLASS(k)                 \
    (G_TYPE_CHECK_CLASS_CAST((k), PLAY_TYPE_TERMINAL, PlayTerminalClass))
#define PLAY_IS_TERMINAL(o)                    \
    (G_TYPE_CHECK_INSTANCE_TYPE((o), PLAY_TYPE_TERMINAL))
#define PLAY_IS_TERMINAL_CLASS(k)              \
    (G_TYPE_CHECK_CLASS_TYPE((k), PLAY_TYPE_TERMINAL))
#define PLAY_TERMINAL_GET_CLASS(o)             \
    (G_TYPE_INSTANCE_GET_CLASS((o), PLAY_TYPE_TERMINAL, PlayTerminalClass))

typedef struct {
    GObject         parent_instance;
    guint           width;
    gboolean        initialized;
    guint           listener;
    struct termios *tty_old;
} PlayTerminal;

typedef struct {
    GObjectClass  parent_class;

    // Signals
    void (*input_read) (PlayTerminal *terminal, gint result);
} PlayTerminalClass;

extern GType play_terminal_get_type (void);

// Create a new terminal object
extern PlayTerminal *play_terminal_new (void);

// Return the current cached terminal screen width
extern guint play_terminal_get_width (PlayTerminal *terminal);

// Start listening for terminal input
// For each read character or escape sequence an "input-read" signal is emitted
extern gboolean play_terminal_listen (PlayTerminal *terminal);

// Stop listening for terminal input
extern gboolean play_terminal_stop_listening (PlayTerminal *terminal);

// Update the cached terminal screen width
// This function should be called before play_terminal_get_width() is used and
// when the SIGWINCH signal is received
extern void play_terminal_update_width (PlayTerminal *terminal);

G_END_DECLS

#endif // _PLAY_TERMINAL_H_
