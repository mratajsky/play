What is play?
=============

Play is a small and simple command-line audio player.

The important features are:
  * Plays music from files and online radios
  * Reads PLS, M3U, ASX and XSPF playlists, both local and remote
  * Simple user interface and keyboard controls
  * Works on Linux and BSD
  * Does not require a graphical user interface (X11)

Keyboard controls
=================

  * Left/Right arrow:    Seek 10 seconds backward/forward
  * Down/Up arrow:       Seek 1 minute backward/forward
  * Page up/Page down:   Switch to the next/previous track
  * + and - keys:        Increase or decrease the volume
  * P key or space:      Pause/unpause
  * M key:               Mute/unmute
  * Q key or ESC:        Quit the program

Required libraries
==================

  * GLib version 2.18 or newer
  * Libxml2
  * GStreamer version 1.x with at least the "base" and "good" plugin sets

Optional libraries
==================

  * GStreamer "bad" and "ugly" plugin sets to handle various file formats
  * GVFS to allow access to remote playlists

How to report bugs and suggest new features? 
============================================

Visit http://github.com/mratajsky/play
