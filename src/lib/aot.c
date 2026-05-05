/* src/lib/aot.c — AOT compilation: .beerc format + beer.beerc natives
 *
 * .beerc binary format (all integers little-endian):
 *
 *   [4]  magic "BEER"
 *   [1]  version = 0x01
 *   [8]  source mtime nanoseconds (int64)
 *   [4]  source CRC32 (uint32)
 *   [4]  n_forms (uint32)
 *   --- per form: ---
 *   [4]  bytecode_size (uint32)
 *   [N]  bytecode bytes
 *   [4]  n_constants (uint32)
 *   --- per constant: ---
 *   [1]  type tag
 *        0x00 NIL
 *        0x01 TRUE
 *        0x02 FALSE
 *        0x03 FIXNUM  → [8] int64
 *        0x04 FLOAT   → [8] double (raw IEEE-754 bytes)
 *        0x05 CHAR    → [4] uint32
 *        0x10 STRING  → [4] byte_len, [byte_len] utf-8 bytes
 *        0x11 SYMBOL  → [4] name_len, [name_len] bytes
 *        0x12 KEYWORD → [4] name_len, [name_len] bytes
 *        0x13 BIGINT  → [4] str_len, [str_len] decimal ASCII
 *        0x20 FUNCTION→ [4] arity (int32), [4] code_offset (uint32),
 *                        [2] n_locals (uint16), [2] n_closed (uint16),
 *                        [4] name_len, [name_len] bytes
 *
 * Registered natives (beer.beerc namespace):
 *   file-mtime   path       → int64 ns or nil
 *   crc32        path       → int64 (uint32 CRC) or nil
 *   compile-file! path      → nil (writes .beerc alongside .beer)
 *   read-header  path       → {:mtime int64 :crc32 int64 :n-forms int} or nil
 *   file-exists? path       → true/false
 *   list-dir     path       → vector of filenames or nil
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>

#include "value.h"
#include "vm.h"
#include "memory.h"
#include "bstring.h"
#include "symbol.h"
#include "hashmap.h"
#include "vector.h"
#include "bigint.h"
#include "function.h"
#include "namespace.h"
#include "native.h"
#include "reader.h"
#include "compiler.h"

/* Forward declaration */
extern NamespaceRegistry* global_namespace_registry;

/* ------------------------------------------------------------------ */
/* CRC32                                                               */
/* ------------------------------------------------------------------ */

static uint32_t crc32_table[256];
static bool crc32_initialized = false;

static void init_crc32(void) {
    if (crc32_initialized) return;
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int j = 0; j < 8; j++)
            c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        crc32_table[i] = c;
    }
    crc32_initialized = true;
}

static uint32_t crc32_of_data(const uint8_t* data, size_t len) {
    init_crc32();
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++)
        crc = (crc >> 8) ^ crc32_table[(crc ^ data[i]) & 0xFF];
    return crc ^ 0xFFFFFFFFu;
}

static uint32_t crc32_of_file(const char* path) {
    init_crc32();
    FILE* f = fopen(path, "rb");
    if (!f) return 0;
    uint32_t crc = 0xFFFFFFFFu;
    uint8_t buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        for (size_t i = 0; i < n; i++)
            crc = (crc >> 8) ^ crc32_table[(crc ^ buf[i]) & 0xFF];
    }
    fclose(f);
    return crc ^ 0xFFFFFFFFu;
}

/* ------------------------------------------------------------------ */
/* File mtime                                                          */
/* ------------------------------------------------------------------ */

static int64_t file_mtime_ns(const char* path) {
    struct stat st;
    if (stat(path, &st) != 0) return -1;
#ifdef __APPLE__
    return (int64_t)st.st_mtimespec.tv_sec * 1000000000LL
         + (int64_t)st.st_mtimespec.tv_nsec;
#else
    return (int64_t)st.st_mtim.tv_sec * 1000000000LL
         + (int64_t)st.st_mtim.tv_nsec;
#endif
}

/* ------------------------------------------------------------------ */
/* Binary write helpers                                                */
/* ------------------------------------------------------------------ */

static void write_u8(FILE* f, uint8_t v)  { fputc((int)v, f); }

static void write_u16le(FILE* f, uint16_t v) {
    fputc((int)(v & 0xFF), f);
    fputc((int)((v >> 8) & 0xFF), f);
}

static void write_u32le(FILE* f, uint32_t v) {
    fputc((int)( v        & 0xFF), f);
    fputc((int)((v >>  8) & 0xFF), f);
    fputc((int)((v >> 16) & 0xFF), f);
    fputc((int)((v >> 24) & 0xFF), f);
}

static void write_u64le(FILE* f, uint64_t v) {
    for (int i = 0; i < 8; i++)
        fputc((int)((v >> (i * 8)) & 0xFF), f);
}

static void write_bytes_prefixed(FILE* f, const char* data, size_t len) {
    write_u32le(f, (uint32_t)len);
    fwrite(data, 1, len, f);
}

/* ------------------------------------------------------------------ */
/* Constant serialization                                              */
/* ------------------------------------------------------------------ */

#define BEERC_NIL      0x00
#define BEERC_TRUE     0x01
#define BEERC_FALSE    0x02
#define BEERC_FIXNUM   0x03
#define BEERC_FLOAT    0x04
#define BEERC_CHAR     0x05
#define BEERC_STRING   0x10
#define BEERC_SYMBOL   0x11
#define BEERC_KEYWORD  0x12
#define BEERC_BIGINT   0x13
#define BEERC_FUNCTION 0x20

static int write_constant(FILE* f, Value v) {
    switch (v.tag) {
    case TAG_NIL:
        write_u8(f, BEERC_NIL);
        return 0;
    case TAG_TRUE:
        write_u8(f, BEERC_TRUE);
        return 0;
    case TAG_FALSE:
        write_u8(f, BEERC_FALSE);
        return 0;
    case TAG_FIXNUM:
        write_u8(f, BEERC_FIXNUM);
        write_u64le(f, (uint64_t)(int64_t)v.as.fixnum);
        return 0;
    case TAG_FLOAT: {
        uint64_t raw;
        memcpy(&raw, &v.as.floatnum, 8);
        write_u8(f, BEERC_FLOAT);
        write_u64le(f, raw);
        return 0;
    }
    case TAG_CHAR:
        write_u8(f, BEERC_CHAR);
        write_u32le(f, v.as.character);
        return 0;
    case TAG_OBJECT: {
        int t = object_type(v);
        if (t == TYPE_STRING) {
            write_u8(f, BEERC_STRING);
            size_t bl = string_byte_length(v);
            write_bytes_prefixed(f, string_cstr(v), bl);
            return 0;
        }
        if (t == TYPE_SYMBOL) {
            write_u8(f, BEERC_SYMBOL);
            const char* s = symbol_str(v);
            write_bytes_prefixed(f, s, strlen(s));
            return 0;
        }
        if (t == TYPE_KEYWORD) {
            write_u8(f, BEERC_KEYWORD);
            const char* s = keyword_name(v);
            write_bytes_prefixed(f, s, strlen(s));
            return 0;
        }
        if (t == TYPE_BIGINT) {
            char* s = bigint_to_string(v, 10);
            write_u8(f, BEERC_BIGINT);
            write_bytes_prefixed(f, s, strlen(s));
            free(s);
            return 0;
        }
        if (t == TYPE_FUNCTION) {
            int arity       = function_arity(v);
            uint32_t offset = function_code_offset(v);
            uint16_t nloc   = function_n_locals(v);
            uint16_t nclos  = function_n_closed(v);
            const char* nm  = function_name(v);
            write_u8(f, BEERC_FUNCTION);
            write_u32le(f, (uint32_t)(int32_t)arity);
            write_u32le(f, offset);
            write_u16le(f, nloc);
            write_u16le(f, nclos);
            write_bytes_prefixed(f, nm, strlen(nm));
            return 0;
        }
        /* Unserializable heap type (e.g. vector, cons) — store as nil.
         * These are reconstructed at runtime; not needed for the loader yet. */
        write_u8(f, BEERC_NIL);
        return 0;
    }
    }
    write_u8(f, BEERC_NIL);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Form serialization                                                  */
/* ------------------------------------------------------------------ */

static int write_form(FILE* f, CompiledCode* code) {
    write_u32le(f, (uint32_t)code->code_size);
    fwrite(code->bytecode, 1, code->code_size, f);
    int nc = (int)vector_length(code->constants);
    write_u32le(f, (uint32_t)nc);
    for (int i = 0; i < nc; i++) {
        if (write_constant(f, vector_get(code->constants, (size_t)i)) != 0)
            return -1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* .beerc write                                                        */
/* ------------------------------------------------------------------ */

static int write_beerc(const char* dst, int64_t src_mtime, uint32_t src_crc,
                       CompiledCode** forms, int n_forms) {
    FILE* f = fopen(dst, "wb");
    if (!f) return -1;

    fwrite("BEER", 1, 4, f);
    write_u8(f,  0x01);                         /* version */
    write_u64le(f, (uint64_t)src_mtime);        /* source mtime ns */
    write_u32le(f, src_crc);                    /* source CRC32 */
    write_u32le(f, (uint32_t)n_forms);          /* number of forms */

    for (int i = 0; i < n_forms; i++) {
        if (write_form(f, forms[i]) != 0) {
            fclose(f);
            remove(dst);                        /* remove incomplete file */
            return -1;
        }
    }
    fclose(f);
    return 0;
}

/* ------------------------------------------------------------------ */
/* .beerc header read                                                  */
/* ------------------------------------------------------------------ */

static int read_beerc_header(const char* path,
                             int64_t* mtime_out,
                             uint32_t* crc_out,
                             int*      n_forms_out) {
    FILE* f = fopen(path, "rb");
    if (!f) return -1;

    char magic[4];
    if (fread(magic, 1, 4, f) != 4 || memcmp(magic, "BEER", 4) != 0) {
        fclose(f); return -1;
    }
    int ver = fgetc(f);
    if (ver != 0x01) { fclose(f); return -1; }

    /* mtime: 8 bytes LE */
    uint64_t mtime_raw = 0;
    for (int i = 0; i < 8; i++) {
        int b = fgetc(f);
        if (b == EOF) { fclose(f); return -1; }
        mtime_raw |= (uint64_t)(uint8_t)b << (i * 8);
    }
    *mtime_out = (int64_t)mtime_raw;

    /* CRC: 4 bytes LE */
    uint32_t crc = 0;
    for (int i = 0; i < 4; i++) {
        int b = fgetc(f);
        if (b == EOF) { fclose(f); return -1; }
        crc |= (uint32_t)(uint8_t)b << (i * 8);
    }
    *crc_out = crc;

    /* n_forms: 4 bytes LE */
    uint32_t nf = 0;
    for (int i = 0; i < 4; i++) {
        int b = fgetc(f);
        if (b == EOF) { fclose(f); return -1; }
        nf |= (uint32_t)(uint8_t)b << (i * 8);
    }
    *n_forms_out = (int)nf;

    fclose(f);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Natives                                                             */
/* ------------------------------------------------------------------ */

/* (beerc/file-mtime path) → int64 nanoseconds or nil */
static Value native_file_mtime(VM* vm, int argc, Value* argv) {
    if (argc != 1 || !is_string(argv[0])) {
        vm_error(vm, "beerc/file-mtime: requires 1 string argument");
        return VALUE_NIL;
    }
    int64_t mtime = file_mtime_ns(string_cstr(argv[0]));
    if (mtime < 0) return VALUE_NIL;
    return make_fixnum(mtime);
}

/* (beerc/crc32 path) → int64 or nil */
static Value native_crc32(VM* vm, int argc, Value* argv) {
    if (argc != 1 || !is_string(argv[0])) {
        vm_error(vm, "beerc/crc32: requires 1 string argument");
        return VALUE_NIL;
    }
    struct stat st;
    if (stat(string_cstr(argv[0]), &st) != 0) return VALUE_NIL;
    uint32_t crc = crc32_of_file(string_cstr(argv[0]));
    return make_fixnum((int64_t)crc);
}

/* (beerc/file-exists? path) → true/false */
static Value native_file_exists(VM* vm, int argc, Value* argv) {
    if (argc != 1 || !is_string(argv[0])) {
        vm_error(vm, "beerc/file-exists?: requires 1 string argument");
        return VALUE_NIL;
    }
    struct stat st;
    return (stat(string_cstr(argv[0]), &st) == 0) ? VALUE_TRUE : VALUE_FALSE;
}

/* (beerc/list-dir path) → vector of filenames or nil */
static Value native_list_dir(VM* vm, int argc, Value* argv) {
    if (argc != 1 || !is_string(argv[0])) {
        vm_error(vm, "beerc/list-dir: requires 1 string argument");
        return VALUE_NIL;
    }
    DIR* d = opendir(string_cstr(argv[0]));
    if (!d) return VALUE_NIL;

    Value result = vector_create(16);
    struct dirent* ent;
    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.') continue;  /* skip . .. and hidden */
        Value name = string_from_cstr(ent->d_name);
        vector_push(result, name);
        object_release(name);
    }
    closedir(d);
    return result;
}

/* (beerc/read-header path) → {:mtime int64 :crc32 int64 :n-forms int} or nil */
static Value native_read_header(VM* vm, int argc, Value* argv) {
    if (argc != 1 || !is_string(argv[0])) {
        vm_error(vm, "beerc/read-header: requires 1 string argument");
        return VALUE_NIL;
    }
    int64_t mtime;
    uint32_t crc;
    int n_forms;
    if (read_beerc_header(string_cstr(argv[0]), &mtime, &crc, &n_forms) != 0)
        return VALUE_NIL;

    Value map = hashmap_create_default();
    hashmap_set(map, keyword_intern("mtime"),   make_fixnum(mtime));
    hashmap_set(map, keyword_intern("crc32"),   make_fixnum((int64_t)crc));
    hashmap_set(map, keyword_intern("n-forms"), make_fixnum((int64_t)n_forms));
    return map;
}

/* (beerc/compile-file! path) → nil (writes .beerc alongside .beer) */
static Value native_compile_file(VM* vm, int argc, Value* argv) {
    if (argc != 1 || !is_string(argv[0])) {
        vm_error(vm, "beerc/compile-file!: requires 1 string path argument");
        return VALUE_NIL;
    }
    const char* src_path = string_cstr(argv[0]);

    /* Read source */
    FILE* sf = fopen(src_path, "r");
    if (!sf) {
        char msg[512];
        snprintf(msg, sizeof(msg), "beerc/compile-file!: cannot open: %s", src_path);
        vm_error(vm, msg);
        return VALUE_NIL;
    }
    fseek(sf, 0, SEEK_END);
    long file_sz = ftell(sf);
    rewind(sf);
    char* src_buf = malloc((size_t)file_sz + 1);
    if (!src_buf) { fclose(sf); vm_error(vm, "beerc/compile-file!: out of memory"); return VALUE_NIL; }
    size_t n_read = fread(src_buf, 1, (size_t)file_sz, sf);
    src_buf[n_read] = '\0';
    fclose(sf);

    /* Compute mtime + CRC before parsing (so they reflect the source as read) */
    int64_t src_mtime = file_mtime_ns(src_path);
    if (src_mtime < 0) src_mtime = 0;
    uint32_t src_crc = crc32_of_data((const uint8_t*)src_buf, n_read);

    /* Parse */
    Reader* reader = reader_new(src_buf, src_path);
    Value forms_vec = reader_read_all(reader);
    free(src_buf);

    if (reader_has_error(reader)) {
        char msg[512];
        snprintf(msg, sizeof(msg), "beerc/compile-file!: read error in %s: %s",
                 src_path, reader_error_msg(reader));
        reader_free(reader);
        object_release(forms_vec);
        vm_error(vm, msg);
        return VALUE_NIL;
    }
    reader_free(reader);

    size_t n_forms = vector_length(forms_vec);

    /* Compile each form — macro expansion uses already-loaded namespaces */
    CompiledCode** codes = malloc(n_forms * sizeof(CompiledCode*));
    if (!codes && n_forms > 0) {
        object_release(forms_vec);
        vm_error(vm, "beerc/compile-file!: out of memory");
        return VALUE_NIL;
    }

    for (size_t i = 0; i < n_forms; i++) {
        Value form = vector_get(forms_vec, i);
        Compiler* compiler = compiler_new(src_path);
        codes[i] = compile(compiler, form);

        if (compiler_has_error(compiler)) {
            char msg[512];
            snprintf(msg, sizeof(msg), "beerc/compile-file!: compile error in %s (form %zu): %s",
                     src_path, i + 1, compiler_error_msg(compiler));
            compiled_code_free(codes[i]);
            compiler_free(compiler);
            for (size_t j = 0; j < i; j++) compiled_code_free(codes[j]);
            free(codes);
            object_release(forms_vec);
            vm_error(vm, msg);
            return VALUE_NIL;
        }
        compiler_free(compiler);
    }
    object_release(forms_vec);

    /* Determine .beerc path: replace trailing .beer with .beerc */
    size_t src_len = strlen(src_path);
    char* dst_path = malloc(src_len + 2);  /* +2: extra 'c' + '\0' */
    if (!dst_path) {
        for (size_t i = 0; i < n_forms; i++) compiled_code_free(codes[i]);
        free(codes);
        vm_error(vm, "beerc/compile-file!: out of memory");
        return VALUE_NIL;
    }
    memcpy(dst_path, src_path, src_len + 1);  /* include '\0' */
    if (src_len >= 5 && strcmp(src_path + src_len - 5, ".beer") == 0) {
        /* Replace .beer with .beerc */
        dst_path[src_len] = 'c';
        dst_path[src_len + 1] = '\0';
    } else {
        /* Non-.beer file: append .beerc */
        memcpy(dst_path + src_len, ".beerc", 7);
    }

    int err = write_beerc(dst_path, src_mtime, src_crc, codes, (int)n_forms);
    const char* saved_dst = dst_path;  /* capture before free for error msg */

    for (size_t i = 0; i < n_forms; i++) compiled_code_free(codes[i]);
    free(codes);

    if (err != 0) {
        char msg[512];
        snprintf(msg, sizeof(msg), "beerc/compile-file!: failed to write %s", saved_dst);
        free(dst_path);
        vm_error(vm, msg);
        return VALUE_NIL;
    }
    free(dst_path);
    return VALUE_NIL;
}

/* ------------------------------------------------------------------ */
/* Registration                                                        */
/* ------------------------------------------------------------------ */

static void register_native_in(Namespace* ns, const char* name, NativeFn fn) {
    Value fn_val = native_function_new(-1, fn, name);
    Value sym    = symbol_intern(name);
    namespace_define(ns, sym, fn_val);
    object_release(fn_val);
}

void core_register_aot(void) {
    Namespace* ns = namespace_registry_get_or_create(global_namespace_registry, "beer.beerc");
    if (!ns) return;
    register_native_in(ns, "file-mtime",    native_file_mtime);
    register_native_in(ns, "crc32",         native_crc32);
    register_native_in(ns, "file-exists?",  native_file_exists);
    register_native_in(ns, "list-dir",      native_list_dir);
    register_native_in(ns, "read-header",   native_read_header);
    register_native_in(ns, "compile-file!", native_compile_file);
}
