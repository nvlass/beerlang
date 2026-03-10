/* Tar Index — parse ustar tar files and index .beer entries */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include "tarindex.h"

TarIndex global_tar_index;

static int tar_entry_cmp(const void* a, const void* b) {
    return strcmp(((const TarEntry*)a)->ns_path,
                 ((const TarEntry*)b)->ns_path);
}

/* Parse octal field from tar header */
static size_t parse_octal(const char* s, int len) {
    size_t val = 0;
    for (int i = 0; i < len && s[i] >= '0' && s[i] <= '7'; i++) {
        val = val * 8 + (size_t)(s[i] - '0');
    }
    return val;
}

void tar_index_init(TarIndex* idx) {
    idx->entries = NULL;
    idx->count = 0;
    idx->capacity = 0;
}

void tar_index_free(TarIndex* idx) {
    free(idx->entries);
    idx->entries = NULL;
    idx->count = 0;
    idx->capacity = 0;
}

static void tar_index_add(TarIndex* idx, const char* ns_path,
                           const char* tar_path, size_t data_offset,
                           size_t file_size) {
    if (idx->count >= idx->capacity) {
        int new_cap = idx->capacity ? idx->capacity * 2 : 32;
        idx->entries = realloc(idx->entries, (size_t)new_cap * sizeof(TarEntry));
        idx->capacity = new_cap;
    }
    TarEntry* e = &idx->entries[idx->count++];
    snprintf(e->ns_path, sizeof(e->ns_path), "%s", ns_path);
    snprintf(e->tar_path, sizeof(e->tar_path), "%s", tar_path);
    e->data_offset = data_offset;
    e->file_size = file_size;
}

/* Parse a tar file and add all .beer entries to the index */
static int tar_scan_file(TarIndex* idx, const char* tar_path) {
    FILE* f = fopen(tar_path, "rb");
    if (!f) return -1;

    char header[512];
    int added = 0;
    int zero_blocks = 0;

    while (fread(header, 1, 512, f) == 512) {
        /* Check for end-of-archive (two zero blocks) */
        int all_zero = 1;
        for (int i = 0; i < 512; i++) {
            if (header[i] != 0) { all_zero = 0; break; }
        }
        if (all_zero) {
            zero_blocks++;
            if (zero_blocks >= 2) break;
            continue;
        }
        zero_blocks = 0;

        /* Extract name (bytes 0-99) */
        char name[256];
        /* Check for ustar prefix (bytes 345-499) */
        char prefix[156];
        memset(name, 0, sizeof(name));
        memset(prefix, 0, sizeof(prefix));
        memcpy(name, header, 100);
        name[100] = '\0';

        /* ustar magic at offset 257 */
        if (memcmp(header + 257, "ustar", 5) == 0) {
            memcpy(prefix, header + 345, 155);
            prefix[155] = '\0';
            if (prefix[0]) {
                char full[256];
                snprintf(full, sizeof(full), "%s/%s", prefix, name);
                snprintf(name, sizeof(name), "%s", full);
            }
        }

        /* Type flag at offset 156: '0' or '\0' = regular file */
        char typeflag = header[156];
        size_t file_size = parse_octal(header + 124, 12);

        /* Data starts right after this header */
        long data_offset = ftell(f);

        /* Skip data blocks */
        size_t data_blocks = (file_size + 511) / 512;
        fseek(f, (long)(data_blocks * 512), SEEK_CUR);

        /* Only index regular files ending in .beer */
        if (typeflag != '0' && typeflag != '\0') continue;
        size_t nlen = strlen(name);
        /* Strip trailing slash if present */
        while (nlen > 0 && name[nlen - 1] == '/') name[--nlen] = '\0';
        if (nlen < 6) continue;
        if (strcmp(name + nlen - 5, ".beer") != 0) continue;

        if (idx) {
            tar_index_add(idx, name, tar_path, (size_t)data_offset, file_size);
        }
        added++;
    }

    fclose(f);
    return added;
}

int tar_index_scan_dir(TarIndex* idx, const char* dir) {
    DIR* d = opendir(dir);
    if (!d) return 0;

    struct dirent* ent;
    int total = 0;
    while ((ent = readdir(d)) != NULL) {
        size_t nlen = strlen(ent->d_name);
        if (nlen < 5 || strcmp(ent->d_name + nlen - 4, ".tar") != 0)
            continue;
        char full_path[1024];
        size_t dlen = strlen(dir);
        if (dlen > 0 && dir[dlen - 1] == '/') {
            snprintf(full_path, sizeof(full_path), "%s%s", dir, ent->d_name);
        } else {
            snprintf(full_path, sizeof(full_path), "%s/%s", dir, ent->d_name);
        }
        int n = tar_scan_file(idx, full_path);
        if (n > 0) total += n;
    }
    closedir(d);

    /* Sort entries by ns_path for binary search */
    if (idx->count > 1) {
        qsort(idx->entries, (size_t)idx->count, sizeof(TarEntry), tar_entry_cmp);
    }

    return total;
}

TarEntry* tar_index_lookup(TarIndex* idx, const char* rel_path) {
    /* Binary search */
    int lo = 0, hi = idx->count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        int c = strcmp(idx->entries[mid].ns_path, rel_path);
        if (c == 0) return &idx->entries[mid];
        if (c < 0) lo = mid + 1;
        else hi = mid - 1;
    }
    return NULL;
}

char* tar_index_read_entry(const TarEntry* entry) {
    FILE* f = fopen(entry->tar_path, "rb");
    if (!f) return NULL;

    char* buf = malloc(entry->file_size + 1);
    if (!buf) { fclose(f); return NULL; }

    fseek(f, (long)entry->data_offset, SEEK_SET);
    size_t n = fread(buf, 1, entry->file_size, f);
    buf[n] = '\0';
    fclose(f);
    return buf;
}

/* --- Standalone functions for beer.tar namespace --- */

int tar_list_entries(const char* tar_path, TarEntry** out_entries) {
    /* First pass: count entries */
    TarIndex tmp;
    tar_index_init(&tmp);
    /* Reuse tar_scan_file but index ALL files, not just .beer */
    FILE* fp = fopen(tar_path, "rb");
    if (!fp) { *out_entries = NULL; return 0; }

    char header[512];
    int zero_blocks = 0;

    while (fread(header, 1, 512, fp) == 512) {
        int all_zero = 1;
        for (int i = 0; i < 512; i++) {
            if (header[i] != 0) { all_zero = 0; break; }
        }
        if (all_zero) { zero_blocks++; if (zero_blocks >= 2) break; continue; }
        zero_blocks = 0;

        char name[256];
        memset(name, 0, sizeof(name));
        memcpy(name, header, 100);
        name[100] = '\0';
        if (memcmp(header + 257, "ustar", 5) == 0) {
            char prefix[156];
            memset(prefix, 0, sizeof(prefix));
            memcpy(prefix, header + 345, 155);
            prefix[155] = '\0';
            if (prefix[0]) {
                char full[256];
                snprintf(full, sizeof(full), "%s/%s", prefix, name);
                snprintf(name, sizeof(name), "%s", full);
            }
        }

        char typeflag = header[156];
        size_t file_size = parse_octal(header + 124, 12);
        long data_offset = ftell(fp);
        size_t data_blocks = (file_size + 511) / 512;
        fseek(fp, (long)(data_blocks * 512), SEEK_CUR);

        if (typeflag != '0' && typeflag != '\0') continue;
        size_t nlen = strlen(name);
        while (nlen > 0 && name[nlen - 1] == '/') name[--nlen] = '\0';
        if (nlen == 0) continue;

        tar_index_add(&tmp, name, tar_path, (size_t)data_offset, file_size);
    }
    fclose(fp);

    *out_entries = tmp.entries;
    return tmp.count;
}

char* tar_read_file(const char* tar_path, const char* target_name) {
    FILE* fp = fopen(tar_path, "rb");
    if (!fp) return NULL;

    char header[512];
    int zero_blocks = 0;

    while (fread(header, 1, 512, fp) == 512) {
        int all_zero = 1;
        for (int i = 0; i < 512; i++) {
            if (header[i] != 0) { all_zero = 0; break; }
        }
        if (all_zero) { zero_blocks++; if (zero_blocks >= 2) break; continue; }
        zero_blocks = 0;

        char name[256];
        memset(name, 0, sizeof(name));
        memcpy(name, header, 100);
        name[100] = '\0';
        if (memcmp(header + 257, "ustar", 5) == 0) {
            char prefix[156];
            memset(prefix, 0, sizeof(prefix));
            memcpy(prefix, header + 345, 155);
            prefix[155] = '\0';
            if (prefix[0]) {
                char full[256];
                snprintf(full, sizeof(full), "%s/%s", prefix, name);
                snprintf(name, sizeof(name), "%s", full);
            }
        }

        char typeflag = header[156];
        size_t file_size = parse_octal(header + 124, 12);
        long data_offset = ftell(fp);
        size_t data_blocks = (file_size + 511) / 512;

        if ((typeflag == '0' || typeflag == '\0') && strcmp(name, target_name) == 0) {
            char* buf = malloc(file_size + 1);
            if (!buf) { fclose(fp); return NULL; }
            fseek(fp, data_offset, SEEK_SET);
            size_t n = fread(buf, 1, file_size, fp);
            buf[n] = '\0';
            fclose(fp);
            return buf;
        }

        fseek(fp, (long)(data_blocks * 512), SEEK_CUR);
    }
    fclose(fp);
    return NULL;
}

int tar_create(const char* tar_path, const char** names, const char** contents, int count) {
    FILE* fp = fopen(tar_path, "wb");
    if (!fp) return -1;

    for (int i = 0; i < count; i++) {
        char header[512];
        memset(header, 0, 512);

        size_t nlen = strlen(names[i]);
        size_t clen = strlen(contents[i]);

        /* Name (0-99) */
        if (nlen > 99) {
            /* Use ustar prefix for long names */
            const char* slash = NULL;
            for (size_t j = 0; j < nlen && j < 155; j++) {
                if (names[i][j] == '/') slash = names[i] + j;
            }
            if (slash && (size_t)(slash - names[i]) <= 155 && nlen - (size_t)(slash - names[i]) - 1 <= 100) {
                memcpy(header + 345, names[i], (size_t)(slash - names[i]));
                memcpy(header, slash + 1, nlen - (size_t)(slash - names[i]) - 1);
            } else {
                /* Name too long */
                fclose(fp);
                return -1;
            }
        } else {
            memcpy(header, names[i], nlen);
        }

        /* Mode (100-107) */
        snprintf(header + 100, 8, "%07o", 0644);
        /* UID (108-115) */
        snprintf(header + 108, 8, "%07o", 0);
        /* GID (116-123) */
        snprintf(header + 116, 8, "%07o", 0);
        /* Size (124-135) */
        snprintf(header + 124, 12, "%011lo", (unsigned long)clen);
        /* Mtime (136-147) */
        snprintf(header + 136, 12, "%011o", 0);
        /* Type flag (156) */
        header[156] = '0';
        /* ustar magic (257-262) */
        memcpy(header + 257, "ustar", 5);
        header[262] = ' ';
        /* Version (263-264) */
        header[263] = ' ';

        /* Compute checksum (148-155): sum of all bytes with checksum field as spaces */
        memset(header + 148, ' ', 8);
        unsigned int cksum = 0;
        for (int j = 0; j < 512; j++) cksum += (unsigned char)header[j];
        snprintf(header + 148, 7, "%06o", cksum);
        header[155] = '\0';

        fwrite(header, 1, 512, fp);
        if (clen > 0) fwrite(contents[i], 1, clen, fp);

        /* Pad to 512 boundary */
        size_t rem = clen % 512;
        if (rem > 0) {
            char pad[512];
            memset(pad, 0, 512 - rem);
            fwrite(pad, 1, 512 - rem, fp);
        }
    }

    /* Two zero blocks to end archive */
    char zero[1024];
    memset(zero, 0, 1024);
    fwrite(zero, 1, 1024, fp);
    fclose(fp);
    return 0;
}
