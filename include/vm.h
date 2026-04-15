/* Virtual Machine - Stack-based bytecode interpreter
 */

#ifndef BEERLANG_VM_H
#define BEERLANG_VM_H

#include <stdbool.h>
#include "value.h"

/* Bytecode instructions */
typedef enum {
    /* Stack operations (0x00 - 0x0F) */
    OP_NOP = 0x00,
    OP_POP = 0x01,
    OP_DUP = 0x02,
    OP_SWAP = 0x03,
    OP_OVER = 0x04,

    /* Constants & Literals (0x10 - 0x1F) */
    OP_PUSH_NIL = 0x10,
    OP_PUSH_TRUE = 0x11,
    OP_PUSH_FALSE = 0x12,
    OP_PUSH_CONST = 0x13,
    OP_PUSH_INT = 0x14,

    /* Arithmetic (0x30 - 0x3F) */
    OP_ADD = 0x30,
    OP_SUB = 0x31,
    OP_MUL = 0x32,
    OP_DIV = 0x33,
    OP_NEG = 0x35,
    OP_INC = 0x36,
    OP_DEC = 0x37,

    /* Comparison (0x40 - 0x4F) */
    OP_EQ = 0x40,
    OP_LT = 0x42,
    OP_GT = 0x44,

    /* Variables & Scope (0x20 - 0x2F) */
    OP_LOAD_VAR = 0x20,      /* Load from global var (operand: symbol index in constants) */
    OP_STORE_VAR = 0x21,     /* Store to global var (operand: symbol index in constants) */
    OP_LOAD_LOCAL = 0x22,
    OP_STORE_LOCAL = 0x23,
    OP_LOAD_CLOSURE = 0x24,
    OP_LOAD_SELF = 0x25,     /* Push current function onto stack (for named fn recursion) */

    /* Control flow (0x60 - 0x6F) */
    OP_JUMP = 0x60,
    OP_JUMP_IF_FALSE = 0x61,
    OP_CALL = 0x63,
    OP_TAIL_CALL = 0x64,
    OP_RETURN = 0x65,
    OP_ENTER = 0x67,
    OP_HALT = 0x6F,

    /* Exception handling (0x70 - 0x7F) */
    OP_PUSH_HANDLER = 0x70,    /* Push exception handler (uint32: catch_pc offset) */
    OP_POP_HANDLER = 0x71,     /* Pop exception handler */
    OP_THROW = 0x72,           /* Throw exception (value on stack, must be hashmap) */
    OP_LOAD_EXCEPTION = 0x73,  /* Push current exception onto stack */

    /* Functions & Closures (0x80 - 0x8F) */
    OP_MAKE_CLOSURE = 0x80,

    /* Concurrency (0x90 - 0x9F) */
    OP_YIELD = 0x90,        /* Explicit yield */
    OP_SPAWN = 0x91,        /* uint16 argc: pop fn+args, push Task */
    OP_AWAIT = 0x92,        /* pop Task, push result (may block) */
    OP_CHAN_SEND = 0x93,    /* pop val, pop ch, push true/nil (may block) */
    OP_CHAN_RECV = 0x94,    /* pop ch, push val (may block) */
} Opcode;

/* Exception handler - pushed by try, popped on normal exit or catch */
typedef struct ExceptionHandler {
    uint32_t catch_pc;       /* PC of catch block (absolute) */
    int stack_pointer;       /* SP to restore on throw */
    int frame_count;         /* Frame count to restore on throw */
} ExceptionHandler;

#define DEFAULT_HANDLER_CAPACITY 16

/* Call frame structure - one per function call */
typedef struct CallFrame {
    uint32_t return_pc;      /* Return address (PC to resume at) */
    int base_pointer;        /* Base of stack frame (for locals) */
    Value function;          /* Function being executed (for closures) */
    /* Saved caller execution context (restored on return) */
    uint8_t* caller_code;
    int caller_code_size;
    Value* caller_constants;
    int caller_num_constants;
} CallFrame;

/* VM structure */
typedef struct VM {
    /* Value stack */
    Value* stack;
    int stack_size;
    int stack_pointer;  /* Points to next free slot */

    /* Call frames */
    CallFrame* frames;
    int frame_capacity;
    int frame_count;         /* Number of active frames */

    /* Exception handlers */
    ExceptionHandler* handlers;
    int handler_count;
    int handler_capacity;
    Value exception;         /* Current exception value (for catch block) */

    /* Bytecode */
    uint8_t* code;
    int code_size;
    int pc;  /* Program counter */

    /* Constant pool */
    Value* constants;
    int num_constants;

    /* Cooperative multitasking */
    int yield_countdown;        /* Instructions until auto-yield (0 = disabled) */
    bool yielded;               /* Set by OP_YIELD or countdown expiry */
    bool native_blocked;        /* Set by native fn that needs I/O retry */
    struct Scheduler* scheduler; /* Back-pointer (NULL for standalone VMs) */

    /* State */
    bool running;
    bool error;
    const char* error_msg;
    char error_buf[256];  /* Buffer for dynamic error messages */
    bool native_throw;   /* Native requested a catchable throw; exception in vm->exception */
} VM;

/* VM API */
VM* vm_new(int stack_size);
void vm_free(VM* vm);

void vm_load_code(VM* vm, uint8_t* code, int size);
void vm_load_constants(VM* vm, Value* constants, int num);

void vm_run(VM* vm);
void vm_step(VM* vm);

Value vm_pop(VM* vm);
void vm_push(VM* vm, Value v);
Value vm_peek(VM* vm);

/* Stack operations */
bool vm_stack_empty(VM* vm);
bool vm_stack_full(VM* vm);

/* Error handling */
void vm_error(VM* vm, const char* msg);
/* Throw a catchable {:message msg} exception from a native function.
 * If no handler is active, falls back to vm_error (fatal). */
void vm_throw_error(VM* vm, const char* msg);

#endif /* BEERLANG_VM_H */
