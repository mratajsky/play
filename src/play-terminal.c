/**
 * PLAY
 * play-terminal.c: Terminal input and related stuff
 * Copyright (C) 2011-2013 Michal Ratajsky <michal.ratajsky@gmail.com>
 */
#include "play-common.h"
#include "play-terminal.h"

G_DEFINE_TYPE (PlayTerminal, play_terminal, G_TYPE_OBJECT);

// Read the current width of the terminal screen and return it
static guint terminal_read_width (void);

// Read terminal input and emit input-read signal when a known character or
// character sequence is read
static gboolean terminal_input_read (PlayTerminal *terminal);

// Non-blocking read of a single character from STDIN
// Returns the character or 0 on error or when there is no input to be read
static guchar terminal_input_read_character (void);

// Signals
enum {
    INPUT_READ,
    LAST_SIGNAL
};
static guint signals[LAST_SIGNAL];

// GObject/finalize
static void play_terminal_finalize (GObject *object)
{
    PlayTerminal *terminal = PLAY_TERMINAL (object);

    // Clean up
    if (terminal->initialized)
        play_terminal_stop_listening (terminal);
    if (terminal->tty_old)
        g_free (terminal->tty_old);

    // Chain up to the parent class
    G_OBJECT_CLASS (play_terminal_parent_class)->finalize (object);
}

// GObject/class init
static void play_terminal_class_init (PlayTerminalClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

    gobject_class->finalize = play_terminal_finalize;

    signals[INPUT_READ] =
        g_signal_new ("input-read",
                      G_TYPE_FROM_CLASS (gobject_class),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (PlayTerminalClass, input_read),
                      NULL,
                      NULL,
                      g_cclosure_marshal_VOID__INT,
                      G_TYPE_NONE,
                      1,
                      G_TYPE_INT);
}

// GObject/init
static void play_terminal_init (PlayTerminal *terminal)
{
}

// Create a new terminal object
PlayTerminal *play_terminal_new (void)
{
    return PLAY_TERMINAL (g_object_new (PLAY_TYPE_TERMINAL, NULL));
}

// Start listening for terminal input
// For each read character or escape sequence an "input-read" signal is emitted
gboolean play_terminal_listen (PlayTerminal *terminal)
{
    g_return_val_if_fail (PLAY_IS_TERMINAL (terminal), FALSE);

    if (!terminal->initialized) {
        struct termios tty;

        // Get the current terminal capabilities
        if (tcgetattr (STDIN_FILENO, &tty) != 0)
            return FALSE;
        // Keep the previous settings for future restoration
        if (G_UNLIKELY (terminal->tty_old))
            terminal->tty_old = memcpy (
                terminal->tty_old,
                &tty,
                sizeof (struct termios));
        else
            terminal->tty_old = g_memdup (&tty, sizeof (struct termios));

        // Update capabilities
        tty.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
        tty.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
        tty.c_cflag &= ~(CSIZE | PARENB);
        tty.c_cflag |= CS8;
        tty.c_cc[VMIN]  = 1;
        tty.c_cc[VTIME] = 0;

        // Set the new capabilities
        if (tcsetattr (STDIN_FILENO, TCSANOW, &tty) != 0) {
            g_free (terminal->tty_old);
            terminal->tty_old = NULL;
            return FALSE;
        }
        terminal->initialized = TRUE;
    }
    if (!terminal->listener) {
        terminal->listener = g_timeout_add (
            50,
            (GSourceFunc) terminal_input_read,
            terminal);
    }
    return TRUE;
}

// Stop listening for terminal input
gboolean play_terminal_stop_listening (PlayTerminal *terminal)
{
    g_return_val_if_fail (PLAY_IS_TERMINAL (terminal), FALSE);

    if (terminal->initialized) {
        tcsetattr (STDIN_FILENO, TCSAFLUSH, terminal->tty_old);
        terminal->initialized = FALSE;
    }
    if (terminal->listener) {
        g_source_remove (terminal->listener);
        terminal->listener = 0;
    }
    return TRUE;
}

// Return the current cached terminal screen width
guint play_terminal_get_width (PlayTerminal *terminal)
{
    return terminal->width;
}

// Update the cached terminal screen width
// This function should be called before play_terminal_get_width() is used and
// when the SIGWINCH signal is received
void play_terminal_update_width (PlayTerminal *terminal)
{
    g_return_if_fail (PLAY_IS_TERMINAL (terminal));

    terminal->width = terminal_read_width ();
}

// Read the current width of the terminal screen and return it
static guint terminal_read_width (void)
{
#ifdef TIOCGWINSZ
    struct winsize ws;
    if (!ioctl (STDIN_FILENO, TIOCGWINSZ, &ws))
        return ws.ws_col;
#endif
#ifdef TIOCGSIZE
    struct ttysize ts;
    if (!ioctl (STDIN_FILENO, TIOCGSIZE, &ts))
        return ts.ts_cols;
#endif
    do {
        const char *columns = getenv ("COLUMNS");
        if (columns)
            return (guint) atoi (columns);
    } while (0);
    // Default width as fallback
    return 80;
}

// Read terminal input and emit input-read signal when a known character or
// character sequence is read
static gboolean terminal_input_read (PlayTerminal *terminal)
{
    guchar          chars[16];
    guint           length = 0;
    PlayTerminalKey result = PLAY_TERMINAL_KEY_NONE;

    while (1) {
        guchar key = terminal_input_read_character ();
        if (!key)
            break;
        if (!length && key >= 0x20 && key < 0x80) {
            // A regular character
            g_signal_emit (
                G_OBJECT (terminal),
                signals[INPUT_READ],
                0,
                key);
            return TRUE;
        }
        if (length >= sizeof (chars))
            length = 0;

        chars[length++] = key;
    }
    if (!length)
        return TRUE;

    // Check the read character sequence
    switch (chars[0]) {
        case 3:
            result = PLAY_TERMINAL_KEY_CTRL_C;
            break;
        case 9:
            result = PLAY_TERMINAL_KEY_TAB;
            break;
        case 13:
            result = PLAY_TERMINAL_KEY_ENTER;
            break;
        case 27:
            // Escape sequence
            if (length == 1) {
                result = PLAY_TERMINAL_KEY_ESC;
                break;
            }
            if (length == 2)
                break;
            switch (chars[1]) {
                case 79:
                    switch (chars[2]) {
                        case 70:
                            result = PLAY_TERMINAL_KEY_HOME;
                            break;
                        case 72:
                            result = PLAY_TERMINAL_KEY_END;
                            break;
                        default:
                            break;
                    }
                    break;
                case 91:
                    switch (chars[2]) {
                        case 53:
                            if (length > 3 && chars[3] == 126) {
                                result = PLAY_TERMINAL_KEY_PAGE_UP;
                            }
                            break;
                        case 54:
                            if (length > 3 && chars[3] == 126) {
                                result = PLAY_TERMINAL_KEY_PAGE_DOWN;
                            }
                            break;
                        case 65:
                            result = PLAY_TERMINAL_KEY_UP;
                            break;
                        case 66:
                            result = PLAY_TERMINAL_KEY_DOWN;
                            break;
                        case 67:
                            result = PLAY_TERMINAL_KEY_RIGHT;
                            break;
                        case 68:
                            result = PLAY_TERMINAL_KEY_LEFT;
                            break;
                        default:
                            break;
                    }
                    break;
                default:
                    break;
            }
            break;
        case 127:
            result = PLAY_TERMINAL_KEY_BACKSPACE;
            break;
        default:
            break;
    }
    if (result != PLAY_TERMINAL_KEY_NONE)
        g_signal_emit (
            G_OBJECT (terminal),
            signals[INPUT_READ],
            0,
            result);
    return TRUE;
}

// Non-blocking read of a single character from STDIN
// Returns the character or 0 on error or when there is no input to be read
static guchar terminal_input_read_character (void)
{
    unsigned char   ch;
    fd_set          fds;
    struct timeval  tv;

    FD_ZERO (&fds);
    FD_SET (STDIN_FILENO, &fds);

    tv.tv_sec  = 0;
    tv.tv_usec = 0;

    if (select (1, &fds, NULL, NULL, &tv) < 0)
        return 0;
    if (FD_ISSET (STDIN_FILENO, &fds)) {
        if (read (STDIN_FILENO, &ch, 1) < 1)
            return 0;
        return ch;
    }
    return 0;
}
