/*** includes ***/

#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <ctype.h>
#include <stdbool.h>
#include <time.h>

#include "terminal.h"
#include "append_buffer.h"

/*** defines ***/

#define KILO_VERSION "0.0.1"
#define KILO_TAB_STOP 8
#define KILO_QUIT_TIMES 3

#define CTRL_KEY(k) ((k) & 0x1f)

enum editorKey {
    BACKSPACE = 127,
    ARROW_LEFT = 1000,
    ARROW_RIGHT,
    ARROW_UP,
    ARROW_DOWN,
    DEL_KEY,
    HOME_KEY,
    END_KEY,
    PAGE_UP,
    PAGE_DOWN
};

enum editorHighlight {
    HL_NORMAL = 0,
    HL_COMMENT,
    HL_MLCOMMENT,
    HL_KEYWORD1,
    HL_KEYWORD2,
    HL_STRING,
    HL_NUMBER,
    HL_MATCH
};

#define HL_HIGHLIGHT_NUMBERS (1<<0)
#define HL_HIGHLIGHT_STRINGS (1<<1)

/*** data ***/

struct EditorSyntax {
    char *filetype;
    char **fileMatch;
    char **keywords;
    char *singleLineCommentStart;
    char *multiLineCommentStart;
    char *multilineCommentEnd;
    int flags;
};

struct EditorRow {
    int idx;
    int size;
    int rsize;
    char *chars;
    char *render;
    unsigned char *hl;
    int hlOpenComment;
};

struct EdiorConfig {
    int cursorX, cursorY;
    int rx;
    int rowOffset;
    int colOffset;
    int screenRows;
    int screenCols;
    int numRows;
    struct EditorRow *row;
    int dirty;
    char *filename;
    char statusMsg[80];
    time_t statusMsgTime;
    struct EditorSyntax *syntax;
};

struct EdiorConfig config;

/*** filetypes ***/

char *C_HL_extensions[] = {".c", ".h", ".cpp", NULL};
char *C_HL_keywords[] = {
        "switch", "if", "while", "for", "break", "continue", "return", "else",
        "struct", "union", "typedef", "static", "enum", "class", "case",
        "int|", "long|", "double|", "float|", "char|", "unsigned|", "signed|",
        "void|", NULL
};

struct EditorSyntax HLDB[] = {
        {
                "c",
                C_HL_extensions,
                C_HL_keywords,
                "//", "/*", "*/",
                HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS
        },
};

#define HLDB_ENTRIES (sizeof(HLDB) / sizeof(HLDB[0]))

/*** prototypes ***/

void editorSetStatusMessage(const char *fmt, ...);

void editorRefreshScreen();

char *editorPrompt(char *prompt, void (*callback)(char *, int));

/*** terminal ***/

int editorReadKey() {
    int nread;
    char c;
    while ((nread = (int) read(STDIN_FILENO, &c, 1)) != 1) {
        if (nread == -1 && errno != EAGAIN) die("read");
    }

    if (c == '\x1b') {
        char seq[3];

        if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
        if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';

        if (seq[0] == '[') {
            if (seq[1] >= '0' && seq[1] <= '9') {
                if (read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';
                if (seq[2] == '~') {
                    switch (seq[1]) {
                        case '1':
                            return HOME_KEY;
                        case '3':
                            return DEL_KEY;
                        case '4':
                            return END_KEY;
                        case '5':
                            return PAGE_UP;
                        case '6':
                            return PAGE_DOWN;
                        case '7':
                            return HOME_KEY;
                        case '8':
                            return END_KEY;
                    }
                }
            } else {
                switch (seq[1]) {
                    case 'A':
                        return ARROW_UP;
                    case 'B':
                        return ARROW_DOWN;
                    case 'C':
                        return ARROW_RIGHT;
                    case 'D':
                        return ARROW_LEFT;
                    case 'H':
                        return HOME_KEY;
                    case 'F':
                        return END_KEY;
                }
            }
        } else if (seq[0] == 'O') {
            switch (seq[1]) {
                case 'H':
                    return HOME_KEY;
                case 'F':
                    return END_KEY;
            }
        }

        return '\x1b';
    } else {
        return c;
    }
}

int getCursorPosition(int *rows, int *cols) {
    char buf[32];
    unsigned int i = 0;

    if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1;

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

int getWindowSize(int *rows, int *cols) {
    struct winsize ws;

    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) return -1;
        return getCursorPosition(rows, cols);
    } else {
        *cols = ws.ws_col;
        *rows = ws.ws_row;
        return 0;
    }
}

/*** syntax highlighting ***/

bool isSeparator(int c) {
    return isspace(c) || c == '\0' || strchr(",.()+-/*=~%<>[];", c) != NULL;
}

void editorUpdateSyntax(struct EditorRow *row) {
    row->hl = realloc(row->hl, (size_t) row->rsize);
    memset(row->hl, HL_NORMAL, (size_t) row->rsize);

    if (config.syntax == NULL) return;

    char **keywords = config.syntax->keywords;

    char *scs = config.syntax->singleLineCommentStart;
    char *mcs = config.syntax->multiLineCommentStart;
    char *mce = config.syntax->multilineCommentEnd;

    int scsLen = (int) (scs ? strlen(scs) : 0);
    int mcsLen = (int) (mcs ? strlen(mcs) : 0);
    int mceLen = (int) (mce ? strlen(mce) : 0);

    bool prevSeperator = 1;
    int inString = 0;
    bool inComment = (row->idx > 0 && config.row[row->idx - 1].hlOpenComment);

    int i = 0;
    while (i < row->rsize) {
        char c = row->render[i];
        unsigned char prevHl = i > 0 ? row->hl[i - 1] : HL_NORMAL;

        if (scsLen && !inString && !inComment) {
            if (!strncmp(&row->render[i], scs, (size_t) scsLen)) {
                memset(&row->hl[i], HL_COMMENT, (size_t) (row->rsize - i));
                break;
            }
        }

        if (mcsLen && mceLen && !inString) {
            if (inComment) {
                row->hl[i] = HL_MLCOMMENT;
                if (!strncmp(&row->render[i], mce, (size_t) mceLen)) {
                    memset(&row->hl[i], HL_MLCOMMENT, (size_t) mceLen);
                    i += mceLen;
                    inComment = 0;
                    prevSeperator = 1;
                    continue;
                } else {
                    i++;
                    continue;
                }
            } else if (!strncmp(&row->render[i], mcs, (size_t) mcsLen)) {
                memset(&row->hl[i], HL_MLCOMMENT, (size_t) mcsLen);
                i += mcsLen;
                inComment = 1;
                continue;
            }
        }

        if (config.syntax->flags & HL_HIGHLIGHT_STRINGS) {
            if (inString) {
                row->hl[i] = HL_STRING;
                if (c == '\\' && i + 1 < row->rsize) {
                    row->hl[i + 1] = HL_STRING;
                    i += 2;
                    continue;
                }
                if (c == inString) inString = 0;
                i++;
                prevSeperator = 1;
                continue;
            } else {
                if (c == '"' || c == '\'') {
                    inString = c;
                    row->hl[i] = HL_STRING;
                    i++;
                    continue;
                }
            }
        }

        if (config.syntax->flags & HL_HIGHLIGHT_NUMBERS) {
            if ((isdigit(c) && (prevSeperator || prevHl == HL_NUMBER)) ||
                (c == '.' && prevHl == HL_NUMBER)) {
                row->hl[i] = HL_NUMBER;
                i++;
                prevSeperator = 0;
                continue;
            }
        }

        if (prevSeperator) {
            int kw;
            for (kw = 0; keywords[kw]; kw++) {
                int kwLen = (int) strlen(keywords[kw]);
                bool type = keywords[kw][kwLen - 1] == '|';
                if (type) kwLen--;

                if (!strncmp(&row->render[i], keywords[kw], (size_t) kwLen) &&
                    isSeparator(row->render[i + kwLen])) {
                    memset(&row->hl[i], type ? HL_KEYWORD2 : HL_KEYWORD1, (size_t) kwLen);
                    i += kwLen;
                    break;
                }
            }
            if (keywords[kw] != NULL) {
                prevSeperator = 0;
                continue;
            }
        }

        prevSeperator = isSeparator(c);
        i++;
    }

    int changed = (row->hlOpenComment != inComment);
    row->hlOpenComment = inComment;
    if (changed && row->idx + 1 < config.numRows)
        editorUpdateSyntax(&config.row[row->idx + 1]);
}

int editorSyntaxToColor(int hl) {
    switch (hl) {
        case HL_COMMENT:
        case HL_MLCOMMENT:
            return 36;
        case HL_KEYWORD1:
            return 33;
        case HL_KEYWORD2:
            return 32;
        case HL_STRING:
            return 35;
        case HL_NUMBER:
            return 31;
        case HL_MATCH:
            return 34;
        default:
            return 37;
    }
}

void editorSelectSyntaxHighlight() {
    config.syntax = NULL;
    if (config.filename == NULL) return;

    for (unsigned int j = 0; j < HLDB_ENTRIES; j++) {
        struct EditorSyntax *syntax = &HLDB[j];
        unsigned int i = 0;
        while (syntax->fileMatch[i]) {
            char *pattern = strstr(config.filename, syntax->fileMatch[i]);
            if (pattern != NULL) {
                int patternLen = (int) strlen(syntax->fileMatch[i]);
                if (syntax->fileMatch[i][0] != '.' || pattern[patternLen] == '\0') {
                    config.syntax = syntax;

                    for (int row = 0; row < config.numRows; row++) {
                        editorUpdateSyntax(&config.row[row]);
                    }

                    return;
                }
            }
            i++;
        }
    }
}

/*** row operations ***/

int editorRowCxToRx(struct EditorRow *row, int cx) {
    int rx = 0;
    int j;
    for (j = 0; j < cx; j++) {
        if (row->chars[j] == '\t')
            rx += (KILO_TAB_STOP - 1) - (rx % KILO_TAB_STOP);
        rx++;
    }
    return rx;
}

int editorRowRxToCx(struct EditorRow *row, int rx) {
    int curRx = 0;
    int cx;
    for (cx = 0; cx < row->size; cx++) {
        if (row->chars[cx] == '\t')
            curRx += (KILO_TAB_STOP - 1) - (curRx % KILO_TAB_STOP);
        curRx++;

        if (curRx > rx) return cx;
    }
    return cx;
}

void editorUpdateRow(struct EditorRow *row) {
    int tabs = 0;
    int j;
    for (j = 0; j < row->size; j++)
        if (row->chars[j] == '\t') tabs++;

    free(row->render);
    row->render = malloc((size_t) (row->size + tabs * (KILO_TAB_STOP - 1) + 1));

    int idx = 0;
    for (j = 0; j < row->size; j++) {
        if (row->chars[j] == '\t') {
            row->render[idx++] = ' ';
            while (idx % KILO_TAB_STOP != 0) row->render[idx++] = ' ';
        } else {
            row->render[idx++] = row->chars[j];
        }
    }
    row->render[idx] = '\0';
    row->rsize = idx;

    editorUpdateSyntax(row);
}

void editorInsertRow(int at, char *s, size_t len) {
    if (at < 0 || at > config.numRows) return;

    config.row = realloc(config.row, sizeof(struct EditorRow) * (config.numRows + 1));
    memmove(&config.row[at + 1], &config.row[at], sizeof(struct EditorRow) * (config.numRows - at));
    for (int j = at + 1; j <= config.numRows; j++) config.row[j].idx++;

    config.row[at].idx = at;

    config.row[at].size = (int) len;
    config.row[at].chars = malloc(len + 1);
    memcpy(config.row[at].chars, s, len);
    config.row[at].chars[len] = '\0';

    config.row[at].rsize = 0;
    config.row[at].render = NULL;
    config.row[at].hl = NULL;
    config.row[at].hlOpenComment = 0;
    editorUpdateRow(&config.row[at]);

    config.numRows++;
    config.dirty++;
}

void editorFreeRow(struct EditorRow *row) {
    free(row->render);
    free(row->chars);
    free(row->hl);
}

void editorDelRow(int at) {
    if (at < 0 || at >= config.numRows) return;
    editorFreeRow(&config.row[at]);
    memmove(&config.row[at], &config.row[at + 1], sizeof(struct EditorRow) * (config.numRows - at - 1));
    for (int j = at; j < config.numRows - 1; j++) config.row[j].idx--;
    config.numRows--;
    config.dirty++;
}

void editorRowInsertChar(struct EditorRow *row, int at, int c) {
    if (at < 0 || at > row->size) at = row->size;
    row->chars = realloc(row->chars, (size_t) (row->size + 2));
    memmove(&row->chars[at + 1], &row->chars[at], (size_t) (row->size - at + 1));
    row->size++;
    row->chars[at] = (char) c;
    editorUpdateRow(row);
    config.dirty++;
}

void editorRowAppendString(struct EditorRow *row, char *s, size_t len) {
    row->chars = realloc(row->chars, row->size + len + 1);
    memcpy(&row->chars[row->size], s, len);
    row->size += len;
    row->chars[row->size] = '\0';
    editorUpdateRow(row);
    config.dirty++;
}

void editorRowDelChar(struct EditorRow *row, int at) {
    if (at < 0 || at >= row->size) return;
    memmove(&row->chars[at], &row->chars[at + 1], (size_t) (row->size - at));
    row->size--;
    editorUpdateRow(row);
    config.dirty++;
}

/*** editor operations ***/

void editorInsertChar(int c) {
    if (config.cursorY == config.numRows) {
        editorInsertRow(config.numRows, "", 0);
    }
    editorRowInsertChar(&config.row[config.cursorY], config.cursorX, c);
    config.cursorX++;
}

void editorInsertNewline() {
    if (config.cursorX == 0) {
        editorInsertRow(config.cursorY, "", 0);
    } else {
        struct EditorRow *row = &config.row[config.cursorY];
        editorInsertRow(config.cursorY + 1, &row->chars[config.cursorX], (size_t) (row->size - config.cursorX));
        row = &config.row[config.cursorY];
        row->size = config.cursorX;
        row->chars[row->size] = '\0';
        editorUpdateRow(row);
    }
    config.cursorY++;
    config.cursorX = 0;
}

void editorDelChar() {
    if (config.cursorY == config.numRows) return;
    if (config.cursorX == 0 && config.cursorY == 0) return;

    struct EditorRow *row = &config.row[config.cursorY];
    if (config.cursorX > 0) {
        editorRowDelChar(row, config.cursorX - 1);
        config.cursorX--;
    } else {
        config.cursorX = config.row[config.cursorY - 1].size;
        editorRowAppendString(&config.row[config.cursorY - 1], row->chars, (size_t) row->size);
        editorDelRow(config.cursorY);
        config.cursorY--;
    }
}

/*** file i/o ***/

char *editorRowsToString(int *bufLen) {
    int totLen = 0;
    int j;
    for (j = 0; j < config.numRows; j++)
        totLen += config.row[j].size + 1;
    *bufLen = totLen;

    char *buf = malloc((size_t) totLen);
    char *p = buf;
    for (j = 0; j < config.numRows; j++) {
        memcpy(p, config.row[j].chars, (size_t) config.row[j].size);
        p += config.row[j].size;
        *p = '\n';
        p++;
    }

    return buf;
}

void editorOpen(char *filename) {
    free(config.filename);
    config.filename = strdup(filename);

    editorSelectSyntaxHighlight();

    FILE *fp = fopen(filename, "r");
    if (!fp) die("fopen");

    char *line = NULL;
    size_t lineCap = 0;
    ssize_t lineLen;
    while ((lineLen = getline(&line, &lineCap, fp)) != -1) {
        while (lineLen > 0 && (line[lineLen - 1] == '\n' ||
                               line[lineLen - 1] == '\r'))
            lineLen--;
        editorInsertRow(config.numRows, line, (size_t) lineLen);
    }
    free(line);
    fclose(fp);
    config.dirty = 0;
}

void editorSave() {
    if (config.filename == NULL) {
        config.filename = editorPrompt("Save as: %s (ESC to cancel)", NULL);
        if (config.filename == NULL) {
            editorSetStatusMessage("Save aborted");
            return;
        }
        editorSelectSyntaxHighlight();
    }

    int len;
    char *buf = editorRowsToString(&len);

    int fd = open(config.filename, O_RDWR | O_CREAT, 0644);
    if (fd != -1) {
        if (ftruncate(fd, len) != -1) {
            if (write(fd, buf, (size_t) len) == len) {
                close(fd);
                free(buf);
                config.dirty = 0;
                editorSetStatusMessage("%d bytes written to disk", len);
                return;
            }
        }
        close(fd);
    }

    free(buf);
    editorSetStatusMessage("Can't save! I/O error: %s", strerror(errno));
}

/*** find ***/

void editorFindCallback(char *query, int key) {
    static int lastMatch = -1;
    static int direction = 1;

    static int savedHlLine;
    static char *savedHl = NULL;

    if (savedHl) {
        memcpy(config.row[savedHlLine].hl, savedHl, (size_t) config.row[savedHlLine].rsize);
        free(savedHl);
        savedHl = NULL;
    }

    if (key == '\r' || key == '\x1b') {
        lastMatch = -1;
        direction = 1;
        return;
    } else if (key == ARROW_RIGHT || key == ARROW_DOWN) {
        direction = 1;
    } else if (key == ARROW_LEFT || key == ARROW_UP) {
        direction = -1;
    } else {
        lastMatch = -1;
        direction = 1;
    }

    if (lastMatch == -1) direction = 1;
    int current = lastMatch;
    int i;
    for (i = 0; i < config.numRows; i++) {
        current += direction;
        if (current == -1) current = config.numRows - 1;
        else if (current == config.numRows) current = 0;

        struct EditorRow *row = &config.row[current];
        char *match = strstr(row->render, query);
        if (match) {
            lastMatch = current;
            config.cursorY = current;
            config.cursorX = editorRowRxToCx(row, (int) (match - row->render));
            config.rowOffset = config.numRows;

            savedHlLine = current;
            savedHl = malloc((size_t) row->rsize);
            memcpy(savedHl, row->hl, (size_t) row->rsize);
            memset(&row->hl[match - row->render], HL_MATCH, strlen(query));
            break;
        }
    }
}

void editorFind() {
    int savedCx = config.cursorX;
    int savedCy = config.cursorY;
    int savedColOff = config.colOffset;
    int savedRowOff = config.rowOffset;

    char *query = editorPrompt("Search: %s (Use ESC/Arrows/Enter)",
                               editorFindCallback);

    if (query) {
        free(query);
    } else {
        config.cursorX = savedCx;
        config.cursorY = savedCy;
        config.colOffset = savedColOff;
        config.rowOffset = savedRowOff;
    }
}

/*** output ***/

void editorScroll() {
    config.rx = 0;
    if (config.cursorY < config.numRows) {
        config.rx = editorRowCxToRx(&config.row[config.cursorY], config.cursorX);
    }

    if (config.cursorY < config.rowOffset) {
        config.rowOffset = config.cursorY;
    }
    if (config.cursorY >= config.rowOffset + config.screenRows) {
        config.rowOffset = config.cursorY - config.screenRows + 1;
    }
    if (config.rx < config.colOffset) {
        config.colOffset = config.rx;
    }
    if (config.rx >= config.colOffset + config.screenCols) {
        config.colOffset = config.rx - config.screenCols + 1;
    }
}

void editorDrawRows(struct AppendBuffer *ab) {
    int y;
    for (y = 0; y < config.screenRows; y++) {
        int filerow = y + config.rowOffset;
        if (filerow >= config.numRows) {
            if (config.numRows == 0 && y == config.screenRows / 3) {
                char welcome[80];
                int welcomelen = snprintf(welcome, sizeof(welcome),
                                          "Kilo editor -- version %s", KILO_VERSION);
                if (welcomelen > config.screenCols) welcomelen = config.screenCols;
                int padding = (config.screenCols - welcomelen) / 2;
                if (padding) {
                    abAppend(ab, "~", 1);
                    padding--;
                }
                while (padding--) abAppend(ab, " ", 1);
                abAppend(ab, welcome, welcomelen);
            } else {
                abAppend(ab, "~", 1);
            }
        } else {
            int len = config.row[filerow].rsize - config.colOffset;
            if (len < 0) len = 0;
            if (len > config.screenCols) len = config.screenCols;
            char *c = &config.row[filerow].render[config.colOffset];
            unsigned char *hl = &config.row[filerow].hl[config.colOffset];
            int currentColor = -1;
            int j;
            for (j = 0; j < len; j++) {
                if (iscntrl(c[j])) {
                    char sym = (char) ((c[j] <= 26) ? '@' + c[j] : '?');
                    abAppend(ab, "\x1b[7m", 4);
                    abAppend(ab, &sym, 1);
                    abAppend(ab, "\x1b[m", 3);
                    if (currentColor != -1) {
                        char buf[16];
                        int clen = snprintf(buf, sizeof(buf), "\x1b[%dm", currentColor);
                        abAppend(ab, buf, clen);
                    }
                } else if (hl[j] == HL_NORMAL) {
                    if (currentColor != -1) {
                        abAppend(ab, "\x1b[39m", 5);
                        currentColor = -1;
                    }
                    abAppend(ab, &c[j], 1);
                } else {
                    int color = editorSyntaxToColor(hl[j]);
                    if (color != currentColor) {
                        currentColor = color;
                        char buf[16];
                        int colourLen = snprintf(buf, sizeof(buf), "\x1b[%dm", color);
                        abAppend(ab, buf, colourLen);
                    }
                    abAppend(ab, &c[j], 1);
                }
            }
            abAppend(ab, "\x1b[39m", 5);
        }

        abAppend(ab, "\x1b[K", 3);
        abAppend(ab, "\r\n", 2);
    }
}

void editorDrawStatusBar(struct AppendBuffer *ab) {
    abAppend(ab, "\x1b[7m", 4);
    char status[80], rstatus[80];
    int len = snprintf(status, sizeof(status), "%.20s - %d lines %s",
                       config.filename ? config.filename : "[No Name]", config.numRows,
                       config.dirty ? "(modified)" : "");
    int rlen = snprintf(rstatus, sizeof(rstatus), "%s | %d/%d",
                        config.syntax ? config.syntax->filetype : "no ft", config.cursorY + 1, config.numRows);
    if (len > config.screenCols) len = config.screenCols;
    abAppend(ab, status, len);
    while (len < config.screenCols) {
        if (config.screenCols - len == rlen) {
            abAppend(ab, rstatus, rlen);
            break;
        } else {
            abAppend(ab, " ", 1);
            len++;
        }
    }
    abAppend(ab, "\x1b[m", 3);
    abAppend(ab, "\r\n", 2);
}

void editorDrawMessageBar(struct AppendBuffer *ab) {
    abAppend(ab, "\x1b[K", 3);
    int msgLen = (int) strlen(config.statusMsg);
    if (msgLen > config.screenCols) msgLen = config.screenCols;
    if (msgLen && time(NULL) - config.statusMsgTime < 5)
        abAppend(ab, config.statusMsg, msgLen);
}

void editorRefreshScreen() {
    editorScroll();

    struct AppendBuffer ab = ABUF_INIT;

    abAppend(&ab, "\x1b[?25l", 6);
    abAppend(&ab, "\x1b[H", 3);

    editorDrawRows(&ab);
    editorDrawStatusBar(&ab);
    editorDrawMessageBar(&ab);

    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (config.cursorY - config.rowOffset) + 1,
             (config.rx - config.colOffset) + 1);
    abAppend(&ab, buf, (int) strlen(buf));

    abAppend(&ab, "\x1b[?25h", 6);

    write(STDOUT_FILENO, ab.data, (size_t) ab.len);
    abFree(&ab);
}

void editorSetStatusMessage(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(config.statusMsg, sizeof(config.statusMsg), fmt, ap);
    va_end(ap);
    config.statusMsgTime = time(NULL);
}

/*** input ***/

char *editorPrompt(char *prompt, void (*callback)(char *, int)) {
    size_t bufsize = 128;
    char *buf = malloc(bufsize);

    size_t buflen = 0;
    buf[0] = '\0';

    while (1) {
        editorSetStatusMessage(prompt, buf);
        editorRefreshScreen();

        int c = editorReadKey();
        if (c == DEL_KEY || c == CTRL_KEY('h') || c == BACKSPACE) {
            if (buflen != 0) buf[--buflen] = '\0';
        } else if (c == '\x1b') {
            editorSetStatusMessage("");
            if (callback) callback(buf, c);
            free(buf);
            return NULL;
        } else if (c == '\r') {
            if (buflen != 0) {
                editorSetStatusMessage("");
                if (callback) callback(buf, c);
                return buf;
            }
        } else if (!iscntrl(c) && c < 128) {
            if (buflen == bufsize - 1) {
                bufsize *= 2;
                buf = realloc(buf, bufsize);
            }
            buf[buflen++] = (char) c;
            buf[buflen] = '\0';
        }

        if (callback) callback(buf, c);
    }
}

void editorMoveCursor(int key) {
    struct EditorRow *row = (config.cursorY >= config.numRows) ? NULL : &config.row[config.cursorY];

    switch (key) {
        case ARROW_LEFT:
            if (config.cursorX != 0) {
                config.cursorX--;
            } else if (config.cursorY > 0) {
                config.cursorY--;
                config.cursorX = config.row[config.cursorY].size;
            }
            break;
        case ARROW_RIGHT:
            if (row && config.cursorX < row->size) {
                config.cursorX++;
            } else if (row && config.cursorX == row->size) {
                config.cursorY++;
                config.cursorX = 0;
            }
            break;
        case ARROW_UP:
            if (config.cursorY != 0) {
                config.cursorY--;
            }
            break;
        case ARROW_DOWN:
            if (config.cursorY < config.numRows) {
                config.cursorY++;
            }
            break;
    }

    row = (config.cursorY >= config.numRows) ? NULL : &config.row[config.cursorY];
    int rowlen = row ? row->size : 0;
    if (config.cursorX > rowlen) {
        config.cursorX = rowlen;
    }
}

void editorProcessKeypress() {
    static int quit_times = KILO_QUIT_TIMES;

    int c = editorReadKey();

    switch (c) {
        case '\r':
            editorInsertNewline();
            break;

        case CTRL_KEY('q'):
            if (config.dirty && quit_times > 0) {
                editorSetStatusMessage("WARNING!!! File has unsaved changes. "
                                               "Press Ctrl-Q %d more times to quit.", quit_times);
                quit_times--;
                return;
            }
            write(STDOUT_FILENO, "\x1b[2J", 4);
            write(STDOUT_FILENO, "\x1b[H", 3);
            exit(0);
            break;

        case CTRL_KEY('s'):
            editorSave();
            break;

        case HOME_KEY:
            config.cursorX = 0;
            break;

        case END_KEY:
            if (config.cursorY < config.numRows)
                config.cursorX = config.row[config.cursorY].size;
            break;

        case CTRL_KEY('f'):
            editorFind();
            break;

        case BACKSPACE:
        case CTRL_KEY('h'):
        case DEL_KEY:
            if (c == DEL_KEY) editorMoveCursor(ARROW_RIGHT);
            editorDelChar();
            break;

        case PAGE_UP:
        case PAGE_DOWN: {
            if (c == PAGE_UP) {
                config.cursorY = config.rowOffset;
            } else if (c == PAGE_DOWN) {
                config.cursorY = config.rowOffset + config.screenRows - 1;
                if (config.cursorY > config.numRows) config.cursorY = config.numRows;
            }

            int times = config.screenRows;
            while (times--)
                editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
        }
            break;

        case ARROW_UP:
        case ARROW_DOWN:
        case ARROW_LEFT:
        case ARROW_RIGHT:
            editorMoveCursor(c);
            break;

        case CTRL_KEY('l'):
        case '\x1b':
            break;

        default:
            editorInsertChar(c);
            break;
    }

    quit_times = KILO_QUIT_TIMES;
}

/*** init ***/

void initEditor() {
    config.cursorX = 0;
    config.cursorY = 0;
    config.rx = 0;
    config.rowOffset = 0;
    config.colOffset = 0;
    config.numRows = 0;
    config.row = NULL;
    config.dirty = 0;
    config.filename = NULL;
    config.statusMsg[0] = '\0';
    config.statusMsgTime = 0;
    config.syntax = NULL;

    if (getWindowSize(&config.screenRows, &config.screenCols) == -1) die("getWindowSize");
    config.screenRows -= 2;
}

int main(int argc, char *argv[]) {
    enableRawMode();
    initEditor();
    if (argc >= 2) {
        editorOpen(argv[1]);
    }

    editorSetStatusMessage("HELP: Ctrl-S = save | Ctrl-Q = quit | Ctrl-F = find");

    while (1) {
        editorRefreshScreen();
        editorProcessKeypress();
    }

    return 0;
}
