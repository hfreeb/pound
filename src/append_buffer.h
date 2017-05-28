#pragma once

struct AppendBuffer {
    char *data;
    int len;
};

#define ABUF_INIT {NULL, 0}

void abAppend(struct AppendBuffer *ab, const char *s, int len);

void abFree(struct AppendBuffer *ab);
