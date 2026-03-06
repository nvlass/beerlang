/* ReadBuffer - Buffered input abstraction for Reader
 *
 * Provides a unified interface for reading from strings and files,
 * with automatic buffering and refilling.
 *
 * Design Philosophy:
 * - Separates I/O (buffering) from parsing (reader logic)
 * - Supports both complete strings and streaming file input
 * - Simple API: buffer_at() hides all complexity
 *
 * Current Implementation (Phase 4.5):
 * - String input: Direct access to source string
 * - File input: Buffered reading with automatic refill
 *
 * Future Extension (Phase 6 - REPL):
 * - fd input: For interactive terminals
 * - Incomplete form detection: For multi-line REPL prompts
 * - Line editing integration: Readline/linenoise support
 */

#ifndef BEERLANG_BUFFER_H
#define BEERLANG_BUFFER_H

#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>

/* Buffer input source type */
typedef enum {
    BUFFER_STRING,    /* Reading from const char* (non-owning) */
    BUFFER_FILE,      /* Reading from FILE* (buffered) */
    /* BUFFER_FD - Future: for REPL interactive input */
} BufferSourceType;

/* Buffered input reader */
typedef struct ReadBuffer {
    BufferSourceType type;

    /* String source (type == BUFFER_STRING) */
    const char* string;      /* Non-owning pointer to string */
    size_t string_len;       /* Cached length */

    /* File source (type == BUFFER_FILE) */
    FILE* file;              /* Owned FILE* pointer */
    char* data;              /* Owned buffer */
    size_t size;             /* Valid data in buffer */
    size_t capacity;         /* Total buffer capacity */

    /* State */
    bool eof;                /* Reached end of input */
    bool error;              /* I/O error occurred */
} ReadBuffer;

/* Create buffer from string (non-owning) */
ReadBuffer* buffer_new_string(const char* source);

/* Create buffer from FILE* (takes ownership) */
ReadBuffer* buffer_new_file(FILE* file);

/* Free buffer resources */
void buffer_free(ReadBuffer* buf);

/* Get character at position
 * Returns '\0' if position is past end of input
 * Automatically refills buffer if needed (for file input)
 */
char buffer_at(ReadBuffer* buf, size_t pos);

/* Check if buffer has error */
bool buffer_has_error(ReadBuffer* buf);

/* Check if buffer is at EOF */
bool buffer_is_eof(ReadBuffer* buf);

#endif /* BEERLANG_BUFFER_H */
