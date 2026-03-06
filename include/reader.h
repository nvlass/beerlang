/* Reader - Converts source text to Beerlang data structures
 *
 * Reads Clojure-syntax S-expressions and produces Value objects.
 *
 * Supports both string and file input through ReadBuffer abstraction.
 *
 * Phase 4.5 Implementation:
 * - String input: reader_new() - complete strings in memory
 * - File input: reader_new_file() - buffered file reading
 *
 * Future (Phase 6 - REPL):
 * - fd input: reader_new_fd() - interactive terminal input
 * - Incomplete detection: reader_is_incomplete() - multi-line prompts
 */

#ifndef BEERLANG_READER_H
#define BEERLANG_READER_H

#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include "value.h"

/* Forward declaration */
typedef struct ReadBuffer ReadBuffer;

/* Reader structure */
typedef struct Reader {
    ReadBuffer*  buffer;        /* Input buffer (owned) */
    size_t       pos;           /* Current position in buffer */
    size_t       line;          /* Current line number (1-based) */
    size_t       column;        /* Current column (0-based) */
    const char*  filename;      /* Source filename (for error messages) */

    /* Error state */
    bool         error;
    char         error_msg[512];
} Reader;

/* Reader API */

/* Create a new reader from source string
 * Source string must remain valid for lifetime of reader (non-owning)
 */
Reader* reader_new(const char* source, const char* filename);

/* Create a new reader from FILE*
 * Takes ownership of file, will close on reader_free()
 */
Reader* reader_new_file(FILE* file, const char* filename);

/* Free reader resources */
void reader_free(Reader* r);

/* Read one form from the source
 * Returns VALUE_NIL on EOF (without error)
 * Returns VALUE_NIL on error (check r->error)
 */
Value reader_read(Reader* r);

/* Read all forms from the source into a vector
 * Useful for reading entire files
 */
Value reader_read_all(Reader* r);

/* Check if reader has an error */
bool reader_has_error(Reader* r);

/* Get error message (valid if reader_has_error() is true) */
const char* reader_error_msg(Reader* r);

/* Read a string and return the parsed form (convenience function) */
Value read_string(const char* source);

#endif /* BEERLANG_READER_H */
