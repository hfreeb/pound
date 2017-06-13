#include <stdlib.h>
#include <memory.h>

#include "row.h"

int editorRowCxToRx(struct EditorRow *row, int cx) {
    int rx = 0;
    int j;
    for (j = 0; j < cx; j++) {
        if (row->chars[j] == '\t')
            rx += (TAB_STOP - 1) - (rx % TAB_STOP);
        rx++;
    }
    return rx;
}

int editorRowRxToCx(struct EditorRow *row, int rx) {
    int curRx = 0;
    int cx;
    for (cx = 0; cx < row->size; cx++) {
        if (row->chars[cx] == '\t')
            curRx += (TAB_STOP - 1) - (curRx % TAB_STOP);
        curRx++;

        if (curRx > rx) return cx;
    }
    return cx;
}

void editorUpdateRowRender(struct EditorRow *row) {
    int tabs = 0;
    int j;
    for (j = 0; j < row->size; j++)
        if (row->chars[j] == '\t') tabs++;

    free(row->render);
    row->render = malloc((size_t) (row->size + tabs * (TAB_STOP - 1) + 1));

    int idx = 0;
    for (j = 0; j < row->size; j++) {
        if (row->chars[j] == '\t') {
            row->render[idx++] = ' ';
            while (idx % TAB_STOP != 0) row->render[idx++] = ' ';
        } else {
            row->render[idx++] = row->chars[j];
        }
    }
    row->render[idx] = '\0';
    row->rsize = idx;
}

void editorRowInsertChar(struct EditorRow *row, int at, int c) {
    if (at < 0 || at > row->size) at = row->size;
    row->chars = realloc(row->chars, (size_t) (row->size + 2));
    memmove(&row->chars[at + 1], &row->chars[at], (size_t) (row->size - at + 1));
    row->size++;
    row->chars[at] = (char) c;
}

void editorRowAppendString(struct EditorRow *row, char *s, size_t len) {
    row->chars = realloc(row->chars, row->size + len + 1);
    memcpy(&row->chars[row->size], s, len);
    row->size += len;
    row->chars[row->size] = '\0';
}

bool editorRowDelChar(struct EditorRow *row, int at) {
    if (at < 0 || at >= row->size) return false;
    memmove(&row->chars[at], &row->chars[at + 1], (size_t) (row->size - at));
    row->size--;
    return true;
}

void editorFreeRow(struct EditorRow *row) {
    free(row->render);
    free(row->chars);
    free(row->hl);
}
