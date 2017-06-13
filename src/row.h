#pragma once

#include <stdbool.h>

#define TAB_STOP 8

struct EditorRow {
    //Index in rows array
    int idx;
    //Size of chars in line
    int size;
    //size of the chars rendered in the line
    int rsize;
    char *chars;
    //chars rendered
    char *render;
    //highlighting color for each char
    unsigned char *hl;
    //Whether the previous line is part of an unclosed multi-line comment
    bool hlOpenComment;
};

int editorRowCxToRx(struct EditorRow *row, int cx);

int editorRowRxToCx(struct EditorRow *row, int rx);

void editorUpdateRowRender(struct EditorRow *row);

void editorRowInsertChar(struct EditorRow *row, int at, int c);

void editorRowAppendString(struct EditorRow *row, char *s, size_t len);

bool editorRowDelChar(struct EditorRow *row, int at);

void editorFreeRow(struct EditorRow *row);
