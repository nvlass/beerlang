/* Tar Index — index .tar files on BEERPATH for transparent require */

#ifndef BEERLANG_TARINDEX_H
#define BEERLANG_TARINDEX_H

#include <stddef.h>

typedef struct {
    char ns_path[256];      /* e.g. "mylib/utils.beer" */
    char tar_path[1024];    /* e.g. "/home/user/libs/mylib.tar" */
    size_t data_offset;     /* byte offset of file data in tar */
    size_t file_size;       /* from tar header */
} TarEntry;

typedef struct {
    TarEntry* entries;
    int count;
    int capacity;
} TarIndex;

void tar_index_init(TarIndex* idx);
void tar_index_free(TarIndex* idx);

/* Scan directory for *.tar files and index their .beer entries */
int tar_index_scan_dir(TarIndex* idx, const char* dir);

/* Look up a relative path (e.g. "mylib/utils.beer") */
TarEntry* tar_index_lookup(TarIndex* idx, const char* rel_path);

/* Read entry contents into malloc'd NUL-terminated buffer. Caller frees. */
char* tar_index_read_entry(const TarEntry* entry);

/* List all .beer entries in a single tar file. Returns count, fills entries. */
int tar_list_entries(const char* tar_path, TarEntry** out_entries);

/* Read a named file from a tar. Returns malloc'd NUL-terminated buffer. */
char* tar_read_file(const char* tar_path, const char* name);

/* Create a tar file from name/content pairs. Returns 0 on success. */
int tar_create(const char* tar_path, const char** names, const char** contents, int count);

/* Global tar index, initialized at startup */
extern TarIndex global_tar_index;

#endif /* BEERLANG_TARINDEX_H */
