/* ReadBuffer implementation */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "buffer.h"

/* Initial buffer size for file reading */
#define INITIAL_BUFFER_SIZE 4096

/* Buffer growth chunk size */
#define BUFFER_CHUNK_SIZE 4096

/* Create buffer from string */
ReadBuffer* buffer_new_string(const char* source) {
    if (!source) {
        return NULL;
    }

    ReadBuffer* buf = (ReadBuffer*)malloc(sizeof(ReadBuffer));
    if (!buf) {
        return NULL;
    }

    buf->type = BUFFER_STRING;
    buf->string = source;
    buf->string_len = strlen(source);
    buf->file = NULL;
    buf->data = NULL;
    buf->size = 0;
    buf->capacity = 0;
    buf->eof = true;  /* String is complete, so EOF is always true */
    buf->error = false;

    return buf;
}

/* Create buffer from FILE* */
ReadBuffer* buffer_new_file(FILE* file) {
    if (!file) {
        return NULL;
    }

    ReadBuffer* buf = (ReadBuffer*)malloc(sizeof(ReadBuffer));
    if (!buf) {
        return NULL;
    }

    buf->type = BUFFER_FILE;
    buf->string = NULL;
    buf->string_len = 0;
    buf->file = file;
    buf->data = (char*)malloc(INITIAL_BUFFER_SIZE);
    if (!buf->data) {
        free(buf);
        return NULL;
    }
    buf->capacity = INITIAL_BUFFER_SIZE;
    buf->size = 0;
    buf->eof = false;
    buf->error = false;

    return buf;
}

/* Free buffer resources */
void buffer_free(ReadBuffer* buf) {
    if (!buf) {
        return;
    }

    /* Close file if we own it */
    if (buf->type == BUFFER_FILE && buf->file) {
        fclose(buf->file);
    }

    /* Free owned buffer data */
    if (buf->data) {
        free(buf->data);
    }

    free(buf);
}

/* Ensure buffer has data up to position 'pos'
 * Returns true if position is available, false if past EOF
 */
static bool buffer_ensure(ReadBuffer* buf, size_t pos) {
    assert(buf != NULL);

    /* String source - check bounds */
    if (buf->type == BUFFER_STRING) {
        return pos < buf->string_len;
    }

    /* File source */
    assert(buf->type == BUFFER_FILE);

    /* Already have it? */
    if (pos < buf->size) {
        return true;
    }

    /* Already at EOF? */
    if (buf->eof) {
        return false;
    }

    /* I/O error? */
    if (buf->error) {
        return false;
    }

    /* Need to read more data */
    size_t needed = pos - buf->size + 1;
    size_t to_read = needed < BUFFER_CHUNK_SIZE ? BUFFER_CHUNK_SIZE : needed;

    /* Grow buffer if needed */
    if (buf->size + to_read > buf->capacity) {
        size_t new_capacity = buf->size + to_read;
        char* new_data = (char*)realloc(buf->data, new_capacity);
        if (!new_data) {
            buf->error = true;
            return false;
        }
        buf->data = new_data;
        buf->capacity = new_capacity;
    }

    /* Read from file */
    size_t nread = fread(buf->data + buf->size, 1, to_read, buf->file);

    if (nread == 0) {
        /* Check for error vs EOF */
        if (ferror(buf->file)) {
            buf->error = true;
        } else {
            buf->eof = true;
        }
        return false;
    }

    buf->size += nread;

    /* Check if we got less than requested (might be EOF) */
    if (nread < to_read) {
        if (feof(buf->file)) {
            buf->eof = true;
        }
    }

    return pos < buf->size;
}

/* Get character at position */
char buffer_at(ReadBuffer* buf, size_t pos) {
    assert(buf != NULL);

    /* String source - direct access */
    if (buf->type == BUFFER_STRING) {
        if (pos >= buf->string_len) {
            return '\0';
        }
        return buf->string[pos];
    }

    /* File source - ensure data available */
    assert(buf->type == BUFFER_FILE);

    if (!buffer_ensure(buf, pos)) {
        return '\0';
    }

    return buf->data[pos];
}

/* Check if buffer has error */
bool buffer_has_error(ReadBuffer* buf) {
    return buf ? buf->error : false;
}

/* Check if buffer is at EOF */
bool buffer_is_eof(ReadBuffer* buf) {
    return buf ? buf->eof : true;
}
