CONSOLE
=======

The console is a REPL for mpv input commands. It is displayed on the video
window. It also shows log messages. It can be disabled entirely using the
``--load-osd-console=no`` option.

Keybindings
-----------

| Key | Action |
| ---: | :--- |
| <kbd>`</kbd> | Show the console |
| <kbd>Esc</kbd> | Hide the console |
| <kbd>Enter</kbd> | Run the typed command |
| <kbd>Shift</kbd> + <kbd>Enter</kbd> | Type a literal newline character |
| <kbd>Ctrl</kbd> + <kbd>←</kbd>, <kbd>Ctrl</kbd> + <kbd>→</kbd> | Move cursor to previous/next word |
| <kbd>↑</kbd>, <kbd>↓</kbd> | Navigate command history |
| <kbd>PgUp</kbd> | Go to the first command in the history |
| <kbd>PgDn</kbd> | Stop navigating command history |
| <kbd>Insert</kbd> | Toggle insert mode |
| <kbd>Shift</kbd> + <kbd>Insert</kbd> | Paste text (uses the primary selection on X11) |
| <kbd>Tab</kbd> | Complete the command or property name at the cursor |
| <kbd>Ctrl</kbd> + <kbd>C</kbd> | Clear current line |
| <kbd>Ctrl</kbd> + <kbd>K</kbd> | Delete text from the cursor to the end of the line |
| <kbd>Ctrl</kbd> + <kbd>L</kbd> | Clear all log messages from the console |
| <kbd>Ctrl</kbd> + <kbd>U</kbd> | Delete text from the cursor to the beginning of the line |
| <kbd>Ctrl</kbd> + <kbd>V</kbd> | Paste text (uses the clipboard on X11) |
| <kbd>Ctrl</kbd> + <kbd>W</kbd> | Delete text from the cursor to the beginning of the current word |

Commands
--------

| Command | Action |
| :--- | :--- |
| ``script-message-to console type "<text>"`` | Show the console and pre-fill it with the provided text |

Known issues
------------

- Pasting text is slow on Windows
- Non-ASCII keyboard input has restrictions
- The cursor keys move between Unicode code-points, not grapheme clusters

Configuration
-------------

This script can be customized through a config file ``script-opts/console.conf``
placed in mpv's user directory and through the ``--script-opts`` command-line
option. The configuration syntax is described in `ON SCREEN CONTROLLER`_.

Key bindings can be changed in a standard way, see for example stats.lua
documentation.

Configurable Options
~~~~~~~~~~~~~~~~~~~~

``scale``
    Default: 1

    All drawing is scaled by this value, including the text borders and the
    cursor. Change it if you have a high-DPI display.

``font``
    Default: unset (picks a hardcoded font depending on detected platform)

    Set the font used for the REPL and the console. This probably doesn't
    have to be a monospaced font.

``font_size``
    Default: 16

    Set the font size used for the REPL and the console. This will be
    multiplied by "scale."
