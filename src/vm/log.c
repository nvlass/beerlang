/* Beerlang Logging implementation */

#include <stdio.h>
#include <stdbool.h>
#include "ulog.h"
#include "log.h"

/* Initialize logging system */
void log_init(void) {
#ifndef LOG_DISABLED
    /* Set default log level to INFO in release, DEBUG in debug builds */
#ifdef DEBUG
    ulog_output_level_set_all(ULOG_LEVEL_DEBUG);
#else
    ulog_output_level_set_all(ULOG_LEVEL_INFO);
#endif

    LOG_DEBUG("Logging system initialized");
#endif
}

/* Set log level */
void log_set_level(int level) {
#ifndef LOG_DISABLED
    ulog_output_level_set_all((ulog_level)level);
#else
    (void)level;  /* Suppress unused parameter warning */
#endif
}

/* Add file output for logs */
void log_add_file(const char* filename, int level) {
#ifndef LOG_DISABLED
    FILE* fp = fopen(filename, "a");
    if (fp) {
        ulog_output_add_file(fp, (ulog_level)level);
        LOG_DEBUG("Added log file: %s (level=%d)", filename, level);
    } else {
        LOG_ERROR("Failed to open log file: %s", filename);
    }
#else
    (void)filename;
    (void)level;
#endif
}
