/**
 * PLAY
 * play-common.h: Common includes
 * Copyright (C) 2011-2013 Michal Ratajsky <michal.ratajsky@gmail.com>
 */
#ifndef _PLAY_COMMON_H_
#define _PLAY_COMMON_H_

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <termios.h>
#include <signal.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <glib/gprintf.h>
#include <gio/gio.h>
#include <gst/gst.h>
#include "marshal.h"

#endif // _PLAY_COMMON_H_
