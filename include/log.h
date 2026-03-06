/* Beerlang Logging - Wrapper around microlog (ulog)
 *
 * Provides convenient logging macros for VM debugging and diagnostics.
 *
 * Usage:
 *   LOG_TRACE("Detailed trace: value=%d", val);
 *   LOG_DEBUG("Debug info: pc=%d, sp=%d", vm->pc, vm->sp);
 *   LOG_INFO("VM initialized successfully");
 *   LOG_WARN("Stack usage high: %d/%d", used, total);
 *   LOG_ERROR("Invalid opcode: 0x%02x", opcode);
 *   LOG_FATAL("Critical VM error, halting");
 *
 * Logging can be disabled at compile time with -DLOG_DISABLED
 */

#ifndef BEERLANG_LOG_H
#define BEERLANG_LOG_H

#include "ulog.h"

/* Initialize logging system - call once at startup */
void log_init(void);

/* Set log level (ULOG_TRACE_LEVEL, ULOG_DEBUG_LEVEL, ULOG_INFO_LEVEL, etc.) */
void log_set_level(int level);

/* Add file output for logs */
void log_add_file(const char* filename, int level);

/* Convenient macros using ulog */
#ifdef LOG_DISABLED
  #define LOG_TRACE(...) ((void)0)
  #define LOG_DEBUG(...) ((void)0)
  #define LOG_INFO(...)  ((void)0)
  #define LOG_WARN(...)  ((void)0)
  #define LOG_ERROR(...) ((void)0)
  #define LOG_FATAL(...) ((void)0)
#else
  #define LOG_TRACE(...) ulog_trace(__VA_ARGS__)
  #define LOG_DEBUG(...) ulog_debug(__VA_ARGS__)
  #define LOG_INFO(...)  ulog_info(__VA_ARGS__)
  #define LOG_WARN(...)  ulog_warn(__VA_ARGS__)
  #define LOG_ERROR(...) ulog_error(__VA_ARGS__)
  #define LOG_FATAL(...) ulog_fatal(__VA_ARGS__)
#endif

/* Topic-based logging for specific subsystems */
#define LOG_VM_TRACE(...)    ulog_topic_trace("VM", __VA_ARGS__)
#define LOG_VM_DEBUG(...)    ulog_topic_debug("VM", __VA_ARGS__)
#define LOG_MEM_TRACE(...)   ulog_topic_trace("MEM", __VA_ARGS__)
#define LOG_MEM_DEBUG(...)   ulog_topic_debug("MEM", __VA_ARGS__)
#define LOG_COMP_TRACE(...)  ulog_topic_trace("COMPILER", __VA_ARGS__)
#define LOG_COMP_DEBUG(...)  ulog_topic_debug("COMPILER", __VA_ARGS__)

#endif /* BEERLANG_LOG_H */
