/* Stream - File descriptor based I/O streams
 *
 * Buffered read/write over Unix file descriptors.
 * Supports files and standard streams (stdin/stdout/stderr).
 */

#ifndef BEERLANG_STREAM_H
#define BEERLANG_STREAM_H

#include "value.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    STREAM_FILE,
    STREAM_STDIN,
    STREAM_STDOUT,
    STREAM_STDERR,
} StreamKind;

/* Stream operation result codes */
#define STREAM_OK         0
#define STREAM_EOF       -1
#define STREAM_ERROR     -2
#define STREAM_WOULDBLOCK -3

#define STREAM_BUF_SIZE 8192

typedef struct {
    struct Object header;
    StreamKind kind;
    int fd;
    bool readable;
    bool writable;
    bool closed;
    bool owns_fd;          /* false for stdin/stdout/stderr */
    bool nonblocking;      /* O_NONBLOCK set on fd */
    /* Read buffer */
    uint8_t* read_buf;
    size_t read_pos;
    size_t read_len;       /* valid bytes in read_buf */
    /* Write buffer */
    uint8_t* write_buf;
    size_t write_len;
    size_t write_flush_pos; /* partial flush progress */
    bool line_buffered;    /* flush on newline (stdout/stderr) */
    /* I/O reactor blocking — track which task is waiting on this stream */
    struct Task* blocked_task; /* non-NULL if a task is blocked on I/O */
} Stream;

/* Type check */
static inline bool is_stream(Value v) {
    return is_pointer(v) && object_type(v) == TYPE_STREAM;
}

/* Create a stream from an existing fd */
Value stream_from_fd(int fd, bool readable, bool writable, bool owns_fd, StreamKind kind);

/* Open a file. mode: "r", "w", "a" */
Value stream_open(const char* path, const char* mode);

/* Close a stream (flush + close fd if owns_fd). Returns 0 on success. */
int stream_close(Value stream);

/* Read a line (up to newline or EOF). Returns string Value or nil at EOF. */
Value stream_read_line(Value stream);

/* Non-blocking read-line. Sets *would_block = true if EAGAIN encountered
 * with no data accumulated yet. Returns VALUE_NIL when would-block. */
Value stream_read_line_nb(Value stream, bool* would_block);

/* Write a string to the stream (buffered). */
int stream_write_string(Value stream, const char* s, size_t len);

/* Flush the write buffer. Returns 0 on success, -1 on error. */
int stream_flush(Value stream);

/* Non-blocking flush. Returns STREAM_OK, STREAM_ERROR, or STREAM_WOULDBLOCK. */
int stream_flush_nb(Value stream);

/* Read entire file into a string. */
Value stream_slurp(const char* path);

/* Write string to file. If append is true, appends. */
int stream_spit(const char* path, const char* content, size_t len, bool append);

/* Initialize standard streams and define *in*, *out*, *err* vars. */
void stream_init_standard(void);

/* Get the global stdout stream (for print/println/prn). */
Value stream_get_stdout(void);

/* Initialize stream type (register destructor). */
void stream_init(void);

/* Register stream native functions in the user namespace. */
void core_register_streams(void);

#endif /* BEERLANG_STREAM_H */
