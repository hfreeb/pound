#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "terminal.h"

static struct termios origTerminal;

/**
 * Clears the terminal and kills the program
 *
 * @param s error message
 */
void die(const char *s) {
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);

    perror(s);
    exit(1);
}

/**
 * Returns to the original terminal config
 */
static void restoreTerminal() {
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &origTerminal) == -1) {
        die("failed to restore terminal config");
    }
}

/**
 * Changes the terminal mode from canonical mode, which only sends keyboard
 * input when the user presses Enter, into raw mode which sends keyboard input
 * on key press.
 */
void enableRawMode() {
    //Get terminal config
    if (tcgetattr(STDIN_FILENO, &origTerminal) == -1) {
        die("failed to get terminal config");
    }

    //Restore the previous terminal settings on exit
    atexit(restoreTerminal);

    struct termios raw = origTerminal;

    //Disables:
    //BRKINT -> causes break conditions to send terminate signals
    //ICRNL -> converts a carriage return (CTRL-M or 13) into a new line (CTRL-J or 10)
    //INPCK -> enables parity checking (probably already turned off)
    //ISTRIP -> Strips the 8th bit of each input byte (probably already turned off)
    //IXON -> allows software flow control actions: CTRL-S to stop data being transmitted to the terminal
    //and CTRL-Q to resume data transmition
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);

    //Disable output processing features, e.g. translation from "\n" to "\r\n"
    raw.c_oflag &= ~(OPOST);

    //Sets the character size to 8 bits per byte (probably the default)
    raw.c_cflag |= (CS8);

    //Disables:
    //ECHO -> prints each key typed
    //ICANON -> canonical mode, which only sends keyboard input when the user presses Enter
    //IEXTEN -> allows a control character to be written to the terminal by pressing CTRL-V and then the
    //character to send literally. Also locks up CTRL-O in MacOS
    //ISIG -> sends a terminate signal on CTRL-C and a suspend signal on CTRL-Z
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);

    //Sets the minimum number of bytes needed to be inputted before read() can return,
    //setting this to 0 causes read() to return as soon as there is any input to be read
    raw.c_cc[VMIN] = 0;
    //Sets the timeout time for read() to 1/10th of a second, not setting the character reference provided if timed out
    raw.c_cc[VTIME] = 1;

    //Update terminal config
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {
        die("failed to update terminal config");
    }
}

/**
 * Writes the specified command into he terminal.
 *
 * @param s to write
 * @return 0 if successful, -1 otherwise
 */
int terminalWrite(char *s, int len) {
    ssize_t result = write(STDOUT_FILENO, s, (size_t) len);
    return (result >= 0 && result == len) ? 0 : -1;
}

/**
 * Sets the row and column values of the position of the cursor
 * to the specified pointers.
 *
 * @param rows pointer to be set
 * @param cols pointer to be set
 * @return 0 if successful, -1 otherwise
 */
int getCursorPosition(int *rows, int *cols) {
    char buf[32];
    unsigned int i = 0;

    if (terminalWrite(CURSOR_POSITION_CMD)) {
        return -1;
    }

    while (i < sizeof(buf) - 1) {
        if (read(STDIN_FILENO, &buf[i], 1) != 1) break;
        if (buf[i] == 'R') break;
        i++;
    }
    buf[i] = '\0';

    if (buf[0] != '\x1b' || buf[1] != '[') return -1;
    if (sscanf(&buf[2], "%d;%d", rows, cols) != 2) return -1;

    return 0;
}

/**
 * Sets the row and column values of the size of the window
 * to the specified pointers.
 *
 * @param rows
 * @param cols
 * @return
 */
int getWindowSize(int *rows, int *cols) {
    struct winsize ws;

    //TIOCGWINSZ = Terminal Input Output Get WINdow SiZe
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        char cmdBuf[16];
        int cmdLen = getCursorMoveCmd(cmdBuf, 999, 999);
        if (terminalWrite(cmdBuf, cmdLen) == -1) return -1;
        return getCursorPosition(rows, cols);
    } else {
        *cols = ws.ws_col;
        *rows = ws.ws_row;
        return 0;
    }
}
