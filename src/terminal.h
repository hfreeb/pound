#pragma once

#include <stdio.h>

#define CLEAR_DISPLAY_CMD "\x1b[2J"

#define INVERT_COLOR_CMD "\x1b[7m", 4
#define RESET_RENDITION_CMD "\x1b[m", 3

#define RESET_COLOR_CMD "\x1b[39m", 5

#define CURSOR_HIDE_CMD "\x1b[?25l", 6
#define CURSOR_SHOW_CMD "\x1b[?25h", 6
#define CURSOR_POSITION_CMD "\x1b[6n", 5

#define ERASE_PAST_CURSOR_CMD "\x1b[K", 3

/**
 * Sets the specified string to the terminal command to set the text color.
 *
 * Uses the colors from the foreground codes specified in
 * https://en.wikipedia.org/wiki/ANSI_escape_code#Colors
 *
 * @param buf string
 * @param color to set to from the color list above
 * @return length of the string set in buf
 */
static inline int getSetColorCmd(char buf[16], int color) {
    return snprintf(buf, 16, "\x1b[%dm", color);
}

/**
 * Sets the specified string to the terminal command which
 * sets the position of the cursor to the specified y and x.
 *
 * @param buf string
 * @param y to set cursor to
 * @param x to set cursor to
 * @return length of the string set in buf
 */
static inline int getCursorSetPositionCmd(char *buf, int y, int x) {
    return snprintf(buf, 16, "\x1b[%d;%dH", y, x);
}

/**
 * Sets the specified string to the terminal command which
 * moves the cursor by the specified x and y amounts.
 *
 * @param buf string
 * @param x to move the cursor by (horizontally)
 * @param y to move the cursor by (vertically)
 * @return length of the string set in buf
 */
static inline int getCursorMoveCmd(char buf[16], int x, int y) {
    return snprintf(buf, 16, "\x1b[%dC\x1b[%dB", x, y);
}

void die(const char *s);

void enableRawMode();

int terminalWrite(char *s, int len);

int getCursorPosition(int *rows, int *cols);

int getWindowSize(int *rows, int *cols);
