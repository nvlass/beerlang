/* Stream implementation - Buffered I/O over file descriptors */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include "stream.h"
#include "memory.h"
#include "bstring.h"
#include "symbol.h"
#include "namespace.h"

/* Destructor: flush, close fd if owned, free buffers */
static void stream_destructor(struct Object* obj) {
    Stream* s = (Stream*)obj;
    if (!s->closed) {
        /* Flush write buffer */
        if (s->write_buf && s->write_len > 0 && s->writable) {
            size_t written = 0;
            while (written < s->write_len) {
                ssize_t n = write(s->fd, s->write_buf + written, s->write_len - written);
                if (n <= 0) break;
                written += (size_t)n;
            }
        }
        if (s->owns_fd && s->fd >= 0) {
            close(s->fd);
        }
        s->closed = true;
    }
    free(s->read_buf);
    free(s->write_buf);
}

void stream_init(void) {
    object_register_destructor(TYPE_STREAM, stream_destructor);
}

Value stream_from_fd(int fd, bool readable, bool writable, bool owns_fd, StreamKind kind) {
    Stream* s = (Stream*)object_alloc(TYPE_STREAM, sizeof(Stream));
    Value v = tag_pointer(s);
    s->kind = kind;
    s->fd = fd;
    s->readable = readable;
    s->writable = writable;
    s->closed = false;
    s->owns_fd = owns_fd;
    s->nonblocking = false;
    s->read_buf = readable ? malloc(STREAM_BUF_SIZE) : NULL;
    s->read_pos = 0;
    s->read_len = 0;
    s->write_buf = writable ? malloc(STREAM_BUF_SIZE) : NULL;
    s->write_len = 0;
    s->write_flush_pos = 0;
    s->line_buffered = (kind == STREAM_STDOUT || kind == STREAM_STDERR);
    s->blocked_task = NULL;
    return v;
}

Value stream_open(const char* path, const char* mode) {
    int flags = 0;
    bool readable = false, writable = false;

    if (strcmp(mode, "r") == 0) {
        flags = O_RDONLY;
        readable = true;
    } else if (strcmp(mode, "w") == 0) {
        flags = O_WRONLY | O_CREAT | O_TRUNC;
        writable = true;
    } else if (strcmp(mode, "a") == 0) {
        flags = O_WRONLY | O_CREAT | O_APPEND;
        writable = true;
    } else {
        return VALUE_NIL;
    }

    int fd = open(path, flags, 0644);
    if (fd < 0) return VALUE_NIL;

    /* Set non-blocking for file fds (scheduler can retry on EAGAIN) */
    int fl = fcntl(fd, F_GETFL);
    if (fl >= 0) {
        fcntl(fd, F_SETFL, fl | O_NONBLOCK);
    }

    Value v = stream_from_fd(fd, readable, writable, true, STREAM_FILE);
    Stream* s = (Stream*)untag_pointer(v);
    s->nonblocking = true;
    return v;
}

int stream_close(Value stream) {
    Stream* s = (Stream*)untag_pointer(stream);
    if (s->closed) return 0;

    /* Flush write buffer */
    if (s->write_buf && s->write_len > 0) {
        stream_flush(stream);
    }

    int ret = 0;
    if (s->owns_fd && s->fd >= 0) {
        ret = close(s->fd);
    }
    s->closed = true;
    return ret;
}

/* Refill read buffer from fd.
 * Returns bytes read, 0 on EOF, -1 on error, STREAM_WOULDBLOCK on EAGAIN. */
static ssize_t stream_refill(Stream* s) {
    s->read_pos = 0;
    ssize_t n = read(s->fd, s->read_buf, STREAM_BUF_SIZE);
    if (n < 0) {
        s->read_len = 0;
        if (s->nonblocking && (errno == EAGAIN || errno == EWOULDBLOCK))
            return STREAM_WOULDBLOCK;
        return -1;
    }
    s->read_len = (size_t)n;
    return n;
}

Value stream_read_line(Value stream) {
    Stream* s = (Stream*)untag_pointer(stream);
    if (s->closed || !s->readable) return VALUE_NIL;

    /* Accumulate into a growable buffer */
    size_t cap = 128;
    char* buf = malloc(cap);
    size_t len = 0;

    for (;;) {
        /* If buffer exhausted, refill */
        if (s->read_pos >= s->read_len) {
            ssize_t n = stream_refill(s);
            if (n <= 0) {
                /* EOF or error */
                if (len == 0) {
                    free(buf);
                    return VALUE_NIL; /* EOF with no data */
                }
                break; /* Return what we have */
            }
        }

        /* Scan for newline in current buffer */
        while (s->read_pos < s->read_len) {
            uint8_t c = s->read_buf[s->read_pos++];
            if (c == '\n') {
                goto done;
            }
            if (len + 1 >= cap) {
                cap *= 2;
                buf = realloc(buf, cap);
            }
            buf[len++] = (char)c;
        }
    }

done:
    buf[len] = '\0';
    Value result = string_from_buffer(buf, len);
    free(buf);
    return result;
}

Value stream_read_line_nb(Value stream, bool* would_block) {
    *would_block = false;
    Stream* s = (Stream*)untag_pointer(stream);
    if (s->closed || !s->readable) return VALUE_NIL;

    size_t cap = 128;
    char* buf = malloc(cap);
    size_t len = 0;

    for (;;) {
        if (s->read_pos >= s->read_len) {
            ssize_t n = stream_refill(s);
            if (n == STREAM_WOULDBLOCK) {
                if (len == 0) {
                    /* No data accumulated — signal would-block */
                    free(buf);
                    *would_block = true;
                    return VALUE_NIL;
                }
                break;  /* Return partial line */
            }
            if (n <= 0) {
                if (len == 0) { free(buf); return VALUE_NIL; }
                break;
            }
        }

        while (s->read_pos < s->read_len) {
            uint8_t c = s->read_buf[s->read_pos++];
            if (c == '\n') goto done;
            if (len + 1 >= cap) { cap *= 2; buf = realloc(buf, cap); }
            buf[len++] = (char)c;
        }
    }

done:
    buf[len] = '\0';
    Value result = string_from_buffer(buf, len);
    free(buf);
    return result;
}

int stream_write_string(Value stream, const char* str, size_t len) {
    Stream* s = (Stream*)untag_pointer(stream);
    if (s->closed || !s->writable) return -1;

    for (size_t i = 0; i < len; i++) {
        if (s->write_len >= STREAM_BUF_SIZE) {
            if (stream_flush(stream) != 0) return -1;
        }
        s->write_buf[s->write_len++] = (uint8_t)str[i];

        /* Line-buffered: flush on newline */
        if (s->line_buffered && str[i] == '\n') {
            if (stream_flush(stream) != 0) return -1;
        }
    }
    return 0;
}

int stream_flush(Value stream) {
    Stream* s = (Stream*)untag_pointer(stream);
    if (s->closed || !s->writable || s->write_len == 0) return 0;

    size_t written = 0;
    while (written < s->write_len) {
        ssize_t n = write(s->fd, s->write_buf + written, s->write_len - written);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        written += (size_t)n;
    }
    s->write_len = 0;
    return 0;
}

int stream_flush_nb(Value stream) {
    Stream* s = (Stream*)untag_pointer(stream);
    if (s->closed || !s->writable || s->write_len == 0) return STREAM_OK;

    while (s->write_flush_pos < s->write_len) {
        ssize_t n = write(s->fd, s->write_buf + s->write_flush_pos,
                          s->write_len - s->write_flush_pos);
        if (n < 0) {
            if (errno == EINTR) continue;
            if (s->nonblocking && (errno == EAGAIN || errno == EWOULDBLOCK))
                return STREAM_WOULDBLOCK;
            return STREAM_ERROR;
        }
        s->write_flush_pos += (size_t)n;
    }
    s->write_len = 0;
    s->write_flush_pos = 0;
    return STREAM_OK;
}

Value stream_slurp(const char* path) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return VALUE_NIL;

    /* Read entire file */
    size_t cap = 4096;
    char* buf = malloc(cap);
    size_t len = 0;

    for (;;) {
        if (len + 4096 > cap) {
            cap *= 2;
            buf = realloc(buf, cap);
        }
        ssize_t n = read(fd, buf + len, cap - len);
        if (n < 0) {
            if (errno == EINTR) continue;
            free(buf);
            close(fd);
            return VALUE_NIL;
        }
        if (n == 0) break;
        len += (size_t)n;
    }
    close(fd);

    buf[len] = '\0';
    Value result = string_from_buffer(buf, len);
    free(buf);
    return result;
}

int stream_spit(const char* path, const char* content, size_t len, bool append) {
    int flags = O_WRONLY | O_CREAT;
    flags |= append ? O_APPEND : O_TRUNC;

    int fd = open(path, flags, 0644);
    if (fd < 0) return -1;

    size_t written = 0;
    while (written < len) {
        ssize_t n = write(fd, content + written, len - written);
        if (n < 0) {
            if (errno == EINTR) continue;
            close(fd);
            return -1;
        }
        written += (size_t)n;
    }
    close(fd);
    return 0;
}

/* Standard streams — global Values so print/println can find them */
static Value g_stdin_stream = { .tag = TAG_NIL };
static Value g_stdout_stream = { .tag = TAG_NIL };
static Value g_stderr_stream = { .tag = TAG_NIL };

Value stream_get_stdout(void) { return g_stdout_stream; }

void stream_init_standard(void) {
    g_stdin_stream  = stream_from_fd(0, true,  false, false, STREAM_STDIN);
    g_stdout_stream = stream_from_fd(1, false, true,  false, STREAM_STDOUT);
    g_stderr_stream = stream_from_fd(2, false, true,  false, STREAM_STDERR);

    /* Define *in*, *out*, *err* in beer.core namespace */
    Namespace* core_ns = namespace_registry_get_or_create(global_namespace_registry, "beer.core");
    if (!core_ns) return;

    Value in_sym  = symbol_intern("*in*");
    Value out_sym = symbol_intern("*out*");
    Value err_sym = symbol_intern("*err*");

    namespace_define(core_ns, in_sym,  g_stdin_stream);
    namespace_define(core_ns, out_sym, g_stdout_stream);
    namespace_define(core_ns, err_sym, g_stderr_stream);

    /* Don't release — these stay alive for the process lifetime.
     * The namespace retains them via Var. */
}
