#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

#include "terminal.h"

static struct termios origTerminal;

/**
 * die - clears the terminal and kills the program
 *
 * @param s error m
 */
void die(const char *s) {
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);

    perror(s);
    exit(1);
}

/**
 * disableRawMode - Returns to the original terminal config
 */
static void restoreTerminal() {
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &origTerminal) == -1) {
        die("failed to restore terminal config");
    }
}

/**
 * enableRawMode - Changes the terminal mode from canonical to raw mode
 *
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
