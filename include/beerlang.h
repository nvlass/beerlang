/* Beerlang - Main header file
 *
 * A LISP-family language with Clojure syntax,
 * cache-efficient VM, and cooperative multitasking.
 */

#ifndef BEERLANG_H
#define BEERLANG_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* Version */
#define BEERLANG_VERSION_MAJOR 0
#define BEERLANG_VERSION_MINOR 1
#define BEERLANG_VERSION_PATCH 0

/* Forward declarations */
typedef struct Value Value;
typedef struct Object Object;
typedef struct VM VM;

/* Include subsystem headers */
#include "log.h"
#include "value.h"
#include "fixnum.h"
#include "memory.h"
#include "bigint.h"
#include "bstring.h"
#include "symbol.h"
#include "cons.h"
#include "vector.h"
#include "hashmap.h"
#include "function.h"
#include "namespace.h"
#include "vm.h"
#include "disasm.h"
#include "buffer.h"
#include "reader.h"
#include "compiler.h"
#include "stream.h"
#include "task.h"
#include "channel.h"
#include "scheduler.h"
#include "reactor.h"
#include "io_reactor.h"

#endif /* BEERLANG_H */
