#include "append_buffer.h"
#include <ctype.h>
#include <malloc.h>
#include <memory.h>

void abAppend(struct AppendBuffer *ab, const char *s, int len) {
    char *new = realloc(ab->data, (size_t) (ab->len + len));

    if (new == NULL) return;
    memcpy(&new[ab->len], s, (size_t) len);
    ab->data = new;
    ab->len += len;
}

void abFree(struct AppendBuffer *ab) {
    free(ab->data);
}
