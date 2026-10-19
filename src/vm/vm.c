/* Virtual Machine implementation */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "vm.h"
#include "beerlang.h"
#include "fixnum.h"
#include "bigint.h"
#include "function.h"
#include "native.h"
#include "namespace.h"
#include "symbol.h"
#include "bstring.h"
#include "verify.h"
#include "cons.h"
#include "hashmap.h"
#include "vector.h"
#include "scheduler.h"
#include "channel.h"

/* Helper: extract double from any numeric value */
static bool to_double(Value v, double* out) {
    if (is_float(v)) { *out = untag_float(v); return true; }
    if (is_fixnum(v)) { *out = (double)untag_fixnum(v); return true; }
    if (is_pointer(v) && object_type(v) == TYPE_BIGINT) {
        *out = bigint_to_double(v); return true;
    }
    return false;
}

/* Default stack size */
#define DEFAULT_STACK_SIZE 256
#define DEFAULT_FRAME_CAPACITY 64

#if defined(__GNUC__) || defined(__clang__)
#define VM_ALWAYS_INLINE inline __attribute__((always_inline))
#define VM_NOINLINE __attribute__((noinline))
#define VM_UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
#define VM_ALWAYS_INLINE inline
#define VM_NOINLINE
#define VM_UNLIKELY(x) (x)
#endif

/* The value stack and the frames start small (one VM per task) and double
 * on demand up to these, which only runaway recursion reaches */
#define MAX_STACK_SIZE (1 << 20)
#define MAX_FRAME_CAPACITY 100000
/* Each dynamically active try clause holds one handler */
#define MAX_HANDLER_CAPACITY 100000

/* Create a new VM */
VM* vm_new(int stack_size) {
    if (stack_size <= 0) {
        stack_size = DEFAULT_STACK_SIZE;
    }

    VM* vm = (VM*)malloc(sizeof(VM));
    if (!vm) {
        return NULL;
    }

    vm->stack = (Value*)malloc(sizeof(Value) * stack_size);
    if (!vm->stack) {
        free(vm);
        return NULL;
    }

    vm->stack_size = stack_size;
    vm->stack_pointer = 0;

    /* Allocate call frame stack */
    vm->frames = (CallFrame*)malloc(sizeof(CallFrame) * DEFAULT_FRAME_CAPACITY);
    if (!vm->frames) {
        free(vm->stack);
        free(vm);
        return NULL;
    }
    vm->frame_capacity = DEFAULT_FRAME_CAPACITY;
    vm->frame_count = 0;

    /* Allocate exception handler stack */
    vm->handlers = (ExceptionHandler*)malloc(sizeof(ExceptionHandler) * DEFAULT_HANDLER_CAPACITY);
    if (!vm->handlers) {
        free(vm->frames);
        free(vm->stack);
        free(vm);
        return NULL;
    }
    vm->handler_count = 0;
    vm->handler_capacity = DEFAULT_HANDLER_CAPACITY;
    vm->exception = VALUE_NIL;

    vm->code = NULL;
    vm->code_size = 0;
    vm->pc = 0;

    vm->constants = NULL;
    vm->num_constants = 0;

    vm->yield_countdown = 0;  /* Disabled by default for standalone VMs */
    vm->yielded = false;
    vm->native_blocked = false;
    vm->sleep_wake_at = 0;
    vm->write_resumed = false;
    vm->pending_fd = -1;
    vm->pending_deadline = 0;
    vm->read_deadline = 0;
    vm->native_throw = false;
    vm->call_requested = false;
    vm->call_fn = VALUE_NIL;
    vm->call_args = NULL;
    vm->call_argc = 0;
    vm->thrown_exception = VALUE_NIL;
    vm->scheduler = NULL;

    vm->running = false;
    vm->error = false;
    vm->state_pad = false;
    vm->needs_verify = false;
    vm->error_msg = NULL;

    return vm;
}

/* Free VM resources */
void vm_free(VM* vm) {
    if (!vm) {
        return;
    }

    /* Release all stack values */
    for (int i = 0; i < vm->stack_pointer; i++) {
        if (is_pointer(vm->stack[i])) {
            object_release(vm->stack[i]);
        }
    }

    /* Release all functions in frames */
    for (int i = 0; i < vm->frame_count; i++) {
        if (is_pointer(vm->frames[i].function)) {
            object_release(vm->frames[i].function);
        }
    }

    /* Release exception value */
    if (is_pointer(vm->exception)) {
        object_release(vm->exception);
    }

    /* Release saved thrown exception (from OP_THROW with no handler) */
    if (is_pointer(vm->thrown_exception)) {
        object_release(vm->thrown_exception);
    }

    if (vm->call_requested) {
        if (is_pointer(vm->call_fn)) object_release(vm->call_fn);
        for (int i = 0; i < vm->call_argc; i++)
            if (is_pointer(vm->call_args[i])) object_release(vm->call_args[i]);
    }
    free(vm->call_args);

    free(vm->handlers);
    free(vm->frames);
    free(vm->stack);
    free(vm);
}

/* Load bytecode into VM */
void vm_load_code(VM* vm, uint8_t* code, int size) {
    assert(vm != NULL);
    vm->code = code;
    vm->code_size = size;
    vm->pc = 0;
    vm->running = false;
    vm->error = false;
    vm->error_msg = NULL;
    vm->needs_verify = true;
}

/* Load constant pool */
void vm_load_constants(VM* vm, Value* constants, int num) {
    assert(vm != NULL);
    vm->constants = constants;
    vm->num_constants = num;
    vm->needs_verify = true;
}

/* Set error state */
void vm_error(VM* vm, const char* msg) {
    vm->error = true;
    vm->running = false;
    snprintf(vm->error_buf, sizeof(vm->error_buf), "%s", msg);
    vm->error_msg = vm->error_buf;
}

void vm_throw_error(VM* vm, const char* msg) {
    /* If no handler is active, fall back to fatal vm_error */
    if (vm->handler_count <= 0) {
        vm_error(vm, msg);
        return;
    }
    /* Build {:message msg} exception map */
    Value exc = hashmap_create_default();
    Value k   = keyword_intern("message");
    Value s   = string_from_cstr(msg);
    Value exc2 = hashmap_assoc(exc, k, s);
    object_release(exc);
    object_release(s);
    /* Store exception and signal OP_CALL to unwind */
    if (is_pointer(vm->exception)) object_release(vm->exception);
    vm->exception    = exc2;
    vm->native_throw = true;
}

/* Re-throw an already-owned exception Value in this VM.
 * Used by native_apply (and other temp-VM callers) to propagate exceptions
 * that crossed a temp-VM boundary without losing the original value. */
void vm_rethrow(VM* vm, Value exc) {
    if (vm->handler_count <= 0) {
        /* No handler here — save exc so a parent VM can re-throw it.
         * This handles nested temp-VMs (e.g. multi-arity inside multi-arity). */
        if (is_pointer(vm->thrown_exception)) object_release(vm->thrown_exception);
        vm->thrown_exception = exc;  /* transfer ownership — do NOT release */
        vm_error(vm, vm_is_interrupt(exc) ? "interrupted" : "Unhandled exception");
        return;
    }
    if (is_pointer(vm->exception)) object_release(vm->exception);
    vm->exception    = exc;  /* transfer ownership */
    vm->native_throw = true;
}

volatile sig_atomic_t vm_interrupt_requested = 0;

void vm_request_interrupt(void) {
    vm_interrupt_requested = 1;
}

Value vm_interrupt_exception(void) {
    static Value exc;
    static bool made = false;
    if (!made) {
        Value m = hashmap_create_default();
        Value m2 = hashmap_assoc(m, keyword_intern("type"), keyword_intern("interrupt"));
        object_release(m);
        Value msg = string_from_cstr("interrupted");
        exc = hashmap_assoc(m2, keyword_intern("message"), msg);
        object_release(msg);
        object_release(m2);
        object_make_immortal(exc);
        made = true;
    }
    return exc;
}

static CallFrame* pop_frame(VM* vm);

/* The namespace the running code was written in: the running function's
 * home ns, or the current ns for top-level code (looked up only then) */
static inline Namespace* code_home_ns(VM* vm) {
    if (vm->frame_count > 0) {
        Value fn_val = vm->frames[vm->frame_count - 1].function;
        if (is_function(fn_val) && function_ns_name(fn_val)) {
            Function* f = (Function*)untag_pointer(fn_val);
            if (!f->home_ns)
                f->home_ns = namespace_registry_get(global_namespace_registry, f->ns_name);
            if (f->home_ns) return f->home_ns;
        }
    }
    return namespace_registry_current(global_namespace_registry);
}

Namespace* vm_code_home_ns(VM* vm) {
    return code_home_ns(vm);
}

/* Pop the handler that receives exc, dropping the ones above it that
 * don't take it: the interrupt goes to the innermost finally or interrupt
 * catch, anything else to the innermost catch or finally. NULL when none
 * is left. */
static ExceptionHandler* pop_handler_for(VM* vm, Value exc) {
    uint8_t skip = vm_is_interrupt(exc) ? HANDLER_CATCH : HANDLER_INTERRUPT;
    while (vm->handler_count > 0 && vm->handlers[vm->handler_count - 1].kind == skip)
        vm->handler_count--;
    if (vm->handler_count <= 0) return NULL;
    return &vm->handlers[--vm->handler_count];
}

/* Pop frames and stack down to handler h */
static void unwind_to_handler(VM* vm, ExceptionHandler* h) {
    while (vm->frame_count > h->frame_count) {
        CallFrame* frame = pop_frame(vm);
        if (!frame) break;
        while (vm->stack_pointer > frame->base_pointer) vm_pop(vm);
        vm->code = frame->caller_code;
        vm->code_size = frame->caller_code_size;
        vm->constants = frame->caller_constants;
        vm->num_constants = frame->caller_num_constants;
    }
    while (vm->stack_pointer > h->stack_pointer) vm_pop(vm);
}

void vm_raise_interrupt(VM* vm) {
    Value exc = vm_interrupt_exception();
    ExceptionHandler* h = pop_handler_for(vm, exc);
    if (!h) {
        if (is_pointer(vm->thrown_exception)) object_release(vm->thrown_exception);
        vm->thrown_exception = exc;  /* immortal */
        vm_error(vm, "interrupted");
        return;
    }
    unwind_to_handler(vm, h);
    if (is_pointer(vm->exception)) object_release(vm->exception);
    vm->exception = exc;
    vm->pc = h->catch_pc;
}

void vm_request_call(VM* vm, Value fn, int argc, const Value* args) {
    vm->call_requested = true;
    vm->call_fn = fn;
    if (is_pointer(fn)) object_retain(fn);
    free(vm->call_args);
    vm->call_args = argc > 0 ? xmalloc(sizeof(Value) * (size_t)argc) : NULL;
    for (int i = 0; i < argc; i++) {
        vm->call_args[i] = args[i];
        if (is_pointer(args[i])) object_retain(args[i]);
    }
    vm->call_argc = argc;
}

/* Release builds run only verified bytecode (verify.c: valid opcodes,
 * operands inside the code, branch targets on instruction starts, no fall
 * off the end, constant indices in range), so the per-instruction checks
 * for those are compiled only into debug builds (make debug / asan). */
#ifdef DEBUG
#define VM_CHECKED 1
#endif
#ifdef VM_CHECKED
#define READ_CHECK() do { if (vm->error) return; } while (0)
#else
#define READ_CHECK() ((void)0)
#endif

/* Per-opcode tracing (--trace) is built into debug builds, or with -DVM_TRACE */
#if defined(DEBUG) || defined(VM_TRACE)
#define TRACE_OP(...) LOG_TRACE(__VA_ARGS__)
#else
#define TRACE_OP(...) ((void)0)
#endif

/* Instruction-stream registers: vm_run keeps pc and the current code
 * buffer in locals (registers) while it runs and writes pc back to vm when
 * it stops; step() reaches them through this struct, which never escapes
 * the inlined loop. Opcodes that switch code (call, return, unwind) update
 * both vm->code/code_size and these. */
typedef struct {
    uint32_t pc;
    const uint8_t* code;
    uint32_t code_size;
} Regs;

/* Read int64 from bytecode (8 bytes, little endian) */
static inline int64_t read_int64(VM* vm, Regs* r) {
#ifdef VM_CHECKED
    if (r->pc + 8 > r->code_size) {
        vm_error(vm, "Not enough bytes for int64");
        return 0;
    }
#else
    (void)vm;
#endif
    const uint8_t* b = r->code + r->pc;
    uint64_t value = (uint64_t)b[0] | (uint64_t)b[1] << 8 | (uint64_t)b[2] << 16 |
                     (uint64_t)b[3] << 24 | (uint64_t)b[4] << 32 | (uint64_t)b[5] << 40 |
                     (uint64_t)b[6] << 48 | (uint64_t)b[7] << 56;
    r->pc += 8;
    return (int64_t)value;
}

/* Read uint32 from bytecode (4 bytes, little endian) */
static inline uint32_t read_uint32(VM* vm, Regs* r) {
#ifdef VM_CHECKED
    if (r->pc + 4 > r->code_size) {
        vm_error(vm, "Not enough bytes for uint32");
        return 0;
    }
#else
    (void)vm;
#endif
    const uint8_t* b = r->code + r->pc;
    uint32_t value = (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 |
                     (uint32_t)b[3] << 24;
    r->pc += 4;
    return value;
}

/* Read int32 from bytecode (4 bytes, little endian) */
static inline int32_t read_int32(VM* vm, Regs* r) {
    return (int32_t)read_uint32(vm, r);
}

/* Read uint16 from bytecode (2 bytes, little endian) */
static inline uint16_t read_uint16(VM* vm, Regs* r) {
#ifdef VM_CHECKED
    if (r->pc + 2 > r->code_size) {
        vm_error(vm, "Not enough bytes for uint16");
        return 0;
    }
#else
    (void)vm;
#endif
    const uint8_t* b = r->code + r->pc;
    r->pc += 2;
    return (uint16_t)(b[0] | b[1] << 8);
}

/* LOAD_VAR resolution cache: (home ns, interned symbol) -> Var*.
 * Valid while namespace_generation is unchanged. */
typedef struct {
    Namespace* home;
    void* sym;
    Var* var;
    uint32_t gen;
} VarCacheEntry;

#define VAR_CACHE_SIZE 1024
static VarCacheEntry var_cache[VAR_CACHE_SIZE];

static inline size_t var_cache_slot(Namespace* home, void* sym) {
    uintptr_t h = ((uintptr_t)home >> 4) * 31 ^ ((uintptr_t)sym >> 4);
    return (size_t)((h ^ (h >> 10)) & (VAR_CACHE_SIZE - 1));
}


/* Make room for `need` more values on the stack. Nothing may hold a
 * pointer into vm->stack across a push (natives get argv into it, but
 * don't push on their own VM). */
static VM_NOINLINE bool grow_stack(VM* vm, int need) {
    int want = vm->stack_pointer + need;
    if (want <= vm->stack_size) return true;
    int size = vm->stack_size;
    while (size < want) size *= 2;
    if (size > MAX_STACK_SIZE) {
        vm_error(vm, "Stack overflow");
        return false;
    }
    Value* stack = realloc(vm->stack, sizeof(Value) * (size_t)size);
    if (!stack) {
        vm_error(vm, "Stack overflow (out of memory)");
        return false;
    }
    vm->stack = stack;
    vm->stack_size = size;
    return true;
}

static VM_NOINLINE bool grow_frames(VM* vm) {
    int cap = vm->frame_capacity * 2;
    if (cap > MAX_FRAME_CAPACITY) cap = MAX_FRAME_CAPACITY;
    if (vm->frame_count >= cap) {
        vm_error(vm, "Call stack overflow");
        return false;
    }
    CallFrame* frames = realloc(vm->frames, sizeof(CallFrame) * (size_t)cap);
    if (!frames) {
        vm_error(vm, "Call stack overflow (out of memory)");
        return false;
    }
    vm->frames = frames;
    vm->frame_capacity = cap;
    return true;
}

/* Room for one more exception handler. Handler pointers are only held
 * between a pop and the unwind that follows it, never across a push. */
static VM_NOINLINE bool grow_handlers(VM* vm) {
    int cap = vm->handler_capacity * 2;
    if (cap > MAX_HANDLER_CAPACITY) cap = MAX_HANDLER_CAPACITY;
    if (vm->handler_count >= cap) {
        vm_error(vm, "Exception handler stack overflow");
        return false;
    }
    ExceptionHandler* handlers = realloc(vm->handlers, sizeof(ExceptionHandler) * (size_t)cap);
    if (!handlers) {
        vm_error(vm, "Exception handler stack overflow (out of memory)");
        return false;
    }
    vm->handlers = handlers;
    vm->handler_capacity = cap;
    return true;
}

/* Push a new call frame, saving caller's execution context */
static void push_frame(VM* vm, Value function, uint32_t return_pc) {
    if (vm->frame_count >= vm->frame_capacity && !grow_frames(vm)) return;

    CallFrame* frame = &vm->frames[vm->frame_count++];
    frame->return_pc = return_pc;
    frame->base_pointer = vm->stack_pointer;
    frame->function = function;

    /* Save caller's execution context */
    frame->caller_code = vm->code;
    frame->caller_code_size = vm->code_size;
    frame->caller_constants = vm->constants;
    frame->caller_num_constants = vm->num_constants;

    /* Retain the function */
    if (is_pointer(function)) {
        object_retain(function);
    }
}

/* Pop the current call frame */
static CallFrame* pop_frame(VM* vm) {
    if (vm->frame_count <= 0) {
        vm_error(vm, "Call stack underflow");
        return NULL;
    }

    CallFrame* frame = &vm->frames[--vm->frame_count];

    /* Release the function */
    if (is_pointer(frame->function)) {
        object_release(frame->function);
    }

    return frame;
}

/* Get current call frame */
static CallFrame* current_frame(VM* vm) {
    if (vm->frame_count <= 0) {
        return NULL;
    }
    return &vm->frames[vm->frame_count - 1];
}

/* Push value onto stack. Inline here; vm_push is the exported wrapper. */
static VM_NOINLINE void push_value_grow(VM* vm, Value v) {
    if (!grow_stack(vm, 1)) return;
    if (is_pointer(v)) object_retain(v);
    vm->stack[vm->stack_pointer++] = v;
}

static inline void push_value(VM* vm, Value v) {
    /* Growing is out of line: the fast path never rejoins it */
    if (VM_UNLIKELY(vm->stack_pointer >= vm->stack_size)) {
        push_value_grow(vm, v);
        return;
    }

    /* Retain heap objects */
    if (is_pointer(v)) {
        object_retain(v);
    }

    vm->stack[vm->stack_pointer++] = v;
}

/* Pop value from stack (releases the stack's reference). */
static inline Value pop_value(VM* vm) {
    if (vm->stack_pointer <= 0) {
        vm_error(vm, "Stack underflow");
        return VALUE_NIL;
    }

    Value v = vm->stack[--vm->stack_pointer];

    /* Release heap objects when popping */
    if (is_pointer(v)) {
        object_release(v);
    }

    return v;
}

void vm_push(VM* vm, Value v) { push_value(vm, v); }
Value vm_pop(VM* vm) { return pop_value(vm); }

/* Inside this file the calls below inline */
#define vm_push(vm, v) push_value((vm), (v))
#define vm_pop(vm) pop_value(vm)

/* Peek at top of stack without popping */
Value vm_peek(VM* vm) {
    if (vm->stack_pointer <= 0) {
        vm_error(vm, "Stack empty");
        return VALUE_NIL;
    }
    return vm->stack[vm->stack_pointer - 1];
}

/* Check if stack is empty */
bool vm_stack_empty(VM* vm) {
    return vm->stack_pointer == 0;
}

/* Check if stack is full */
bool vm_stack_full(VM* vm) {
    return vm->stack_pointer >= vm->stack_size;
}

/* Try to invoke a non-function value in head position.
 * Keywords, hashmaps, and vectors are callable (like Clojure's IFn).
 *   (:key map)        → (get map :key)
 *   (:key map default) → (get map :key default)
 *   ({:a 1} :key)     → (get map :key)
 *   ([10 20] idx)     → (nth vec idx)
 * Returns true if handled (result written), false if not callable. */
static bool vm_invoke_value(VM* vm, Value head, int n_args, Value* args, Value* result) {
    /* Keyword as lookup function */
    if (is_pointer(head) && object_type(head) == TYPE_KEYWORD) {
        if (n_args < 1 || n_args > 2) {
            vm_error(vm, "Keyword lookup expects 1 or 2 arguments");
            return true;
        }
        Value def = (n_args == 2) ? args[1] : VALUE_NIL;
        /* Clojure: keyword lookup on nil returns nil (the default) */
        if (is_nil(args[0])) { *result = def; return true; }
        if (!is_hashmap(args[0])) { *result = def; return true; }
        *result = hashmap_get_default(args[0], head, def);
        if (is_pointer(*result)) object_retain(*result);
        return true;
    }

    /* Hashmap as lookup function */
    if (is_hashmap(head)) {
        if (n_args < 1 || n_args > 2) {
            vm_error(vm, "Map lookup expects 1 or 2 arguments");
            return true;
        }
        Value def = (n_args == 2) ? args[1] : VALUE_NIL;
        *result = hashmap_get_default(head, args[0], def);
        if (is_pointer(*result)) object_retain(*result);
        return true;
    }

    /* Set as membership function: (#{1 2} 1) => 1, (#{1 2} 3) => nil */
    if (is_set(head)) {
        if (n_args < 1 || n_args > 2) {
            vm_error(vm, "Set lookup expects 1 or 2 arguments");
            return true;
        }
        Value def = (n_args == 2) ? args[1] : VALUE_NIL;
        *result = set_contains(head, args[0]) ? set_get(head, args[0]) : def;
        if (is_pointer(*result)) object_retain(*result);
        return true;
    }

    /* Vector as index function */
    if (is_vector(head)) {
        if (n_args != 1) {
            vm_error(vm, "Vector lookup expects 1 argument");
            return true;
        }
        if (!is_fixnum(args[0])) {
            vm_error(vm, "Vector index must be an integer");
            return true;
        }
        int64_t idx = untag_fixnum(args[0]);
        if (idx < 0 || (size_t)idx >= vector_length(head)) {
            vm_error(vm, "Vector index out of bounds");
            return true;
        }
        *result = vector_get(head, (size_t)idx);
        if (is_pointer(*result)) object_retain(*result);
        return true;
    }

    return false;
}

/* Replace the Var on top of the stack (a call's head) with its value */
static VM_NOINLINE void deref_var_callee(VM* vm) {
    Value var = vm->stack[vm->stack_pointer - 1];
    Value v = var_get_value((Var*)untag_pointer(var));
    if (is_pointer(v)) object_retain(v);
    vm->stack[vm->stack_pointer - 1] = v;
    object_release(var);
}

/* A native asked for a call (vm_request_call): replace its call's
 * `popped` stack slots (args and the native) with the requested fn's args
 * and the fn. Returns the new arg count, or -1 on error. */
static VM_NOINLINE int take_call_request(VM* vm, int popped) {
    for (int i = 0; i < popped; i++) vm_pop(vm);
    int argc = vm->call_argc;
    Value fn = vm->call_fn;
    Value* args = vm->call_args;
    vm->call_requested = false;
    vm->call_fn = VALUE_NIL;
    vm->call_args = NULL;
    vm->call_argc = 0;
    if (!grow_stack(vm, argc + 1)) {
        if (is_pointer(fn)) object_release(fn);
        for (int i = 0; i < argc; i++) if (is_pointer(args[i])) object_release(args[i]);
        free(args);
        return -1;
    }
    /* The stack takes over the request's references */
    for (int i = 0; i < argc; i++) vm->stack[vm->stack_pointer++] = args[i];
    vm->stack[vm->stack_pointer++] = fn;
    free(args);
    return argc;
}

static VM_NOINLINE void arity_error(VM* vm, const char* op, Value fn, int n_args) {
    char buf[256];
    snprintf(buf, sizeof(buf), "%s: wrong number of arguments (%u) passed to %s",
             op, (unsigned)n_args, function_name(fn));
    vm_error(vm, buf);
}

/* Auto-yield countdown, taken at safe points only: OP_JUMP (every loop and
 * recur) and OP_ENTER (every bytecode fn entry, call or tail call). Any
 * unbounded run of instructions passes one, so a task can't starve others,
 * and straight-line code doesn't pay for the countdown. */
#define SAFE_POINT(vm) do { \
        if ((vm)->yield_countdown > 0 && --(vm)->yield_countdown == 0 && \
            (vm)->running && !(vm)->error) \
            (vm)->yielded = true; \
    } while (0)

/* Execute single instruction. Inlined into vm_run's loop, which already
 * checks running/error; vm_step below is the checked public entry. */
static VM_ALWAYS_INLINE void step(VM* vm, Regs* r) {
#ifdef VM_CHECKED
    if (r->pc >= r->code_size) {
        vm->running = false;
        return;
    }
#endif

    /* Capture PC before reading opcode for trace logging */
    uint32_t pc_at_opcode = r->pc;
    uint8_t op = r->code[r->pc++];

    switch (op) {
        /* Stack operations */
        case OP_NOP:
            TRACE_OP("PC=%04u OP=%02X (OP_NOP) sp=%d", pc_at_opcode, op, vm->stack_pointer);
            break;

        case OP_POP: {
            TRACE_OP("PC=%04u OP=%02X (OP_POP) sp=%d", pc_at_opcode, op, vm->stack_pointer);
            vm_pop(vm);
            break;
        }

        case OP_DUP: {
            TRACE_OP("PC=%04u OP=%02X (OP_DUP) sp=%d", pc_at_opcode, op, vm->stack_pointer);
            Value v = vm_peek(vm);
            vm_push(vm, v);
            break;
        }

        case OP_SWAP: {
            TRACE_OP("PC=%04u OP=%02X (OP_SWAP) sp=%d", pc_at_opcode, op, vm->stack_pointer);
            if (vm->stack_pointer < 2) {
                vm_error(vm, "SWAP requires 2 stack values");
                return;
            }
            /* Swap in place — no refcount changes needed */
            Value tmp = vm->stack[vm->stack_pointer - 1];
            vm->stack[vm->stack_pointer - 1] = vm->stack[vm->stack_pointer - 2];
            vm->stack[vm->stack_pointer - 2] = tmp;
            break;
        }

        /* Constants & Literals */
        case OP_PUSH_NIL:
            TRACE_OP("PC=%04u OP=%02X (OP_PUSH_NIL) sp=%d", pc_at_opcode, op, vm->stack_pointer);
            vm_push(vm, VALUE_NIL);
            break;

        case OP_PUSH_TRUE:
            TRACE_OP("PC=%04u OP=%02X (OP_PUSH_TRUE) sp=%d", pc_at_opcode, op, vm->stack_pointer);
            vm_push(vm, VALUE_TRUE);
            break;

        case OP_PUSH_FALSE:
            TRACE_OP("PC=%04u OP=%02X (OP_PUSH_FALSE) sp=%d", pc_at_opcode, op, vm->stack_pointer);
            vm_push(vm, VALUE_FALSE);
            break;

        case OP_PUSH_INT: {
            int64_t value = read_int64(vm, r);
            TRACE_OP("PC=%04u OP=%02X (OP_PUSH_INT) value=%lld sp=%d", pc_at_opcode, op, (long long)value, vm->stack_pointer);
            if (!vm->error) {
                vm_push(vm, make_fixnum(value));
            }
            break;
        }

        case OP_PUSH_CONST: {
            uint32_t idx = read_uint32(vm, r);
            TRACE_OP("PC=%04u OP=%02X (OP_PUSH_CONST) idx=%u sp=%d", pc_at_opcode, op, idx, vm->stack_pointer);
            READ_CHECK();
#ifdef VM_CHECKED
            if (idx >= (uint32_t)vm->num_constants) {
                vm_error(vm, "Constant index out of bounds");
                return;
            }
#endif
            vm_push(vm, vm->constants[idx]);
            break;
        }

        /* Arithmetic operations */
        case OP_ADD: {
            TRACE_OP("PC=%04u OP=%02X (OP_ADD) sp=%d", pc_at_opcode, op, vm->stack_pointer);
            if (vm->stack_pointer < 2) {
                vm_error(vm, "ADD requires 2 operands");
                return;
            }
            /* Read operands from stack (still alive while on stack) */
            Value b = vm->stack[vm->stack_pointer - 1];
            Value a = vm->stack[vm->stack_pointer - 2];
            Value add_result;

            /* Float promotion: if either is float, promote both */
            if (is_float(a) || is_float(b)) {
                double da, db;
                if (!to_double(a, &da) || !to_double(b, &db)) {
                    vm_error(vm, "ADD requires numeric operands");
                    return;
                }
                add_result = make_float(da + db);
            }
            /* Both fixnums? Try checked addition */
            else if (is_fixnum(a) && is_fixnum(b)) {
                if (!fixnum_add_checked(a, b, &add_result)) {
                    /* add_result already set */
                } else {
                    Value big_a = bigint_from_fixnum(a);
                    Value big_b = bigint_from_fixnum(b);
                    add_result = bigint_add(big_a, big_b);
                    object_release(big_a);
                    object_release(big_b);
                }
            }
            /* Handle bigints */
            else if ((is_fixnum(a) || (is_pointer(a) && object_type(a) == TYPE_BIGINT)) &&
                     (is_fixnum(b) || (is_pointer(b) && object_type(b) == TYPE_BIGINT))) {
                Value big_a = is_fixnum(a) ? bigint_from_fixnum(a) : a;
                Value big_b = is_fixnum(b) ? bigint_from_fixnum(b) : b;
                add_result = bigint_add(big_a, big_b);
                if (is_fixnum(a)) object_release(big_a);
                if (is_fixnum(b)) object_release(big_b);
            }
            else {
                vm_error(vm, "ADD requires numeric operands");
                return;
            }
            /* Pop operands and push result */
            vm_pop(vm); vm_pop(vm);
            vm_push(vm, add_result);
            break;
        }

        case OP_SUB: {
            TRACE_OP("PC=%04u OP=%02X (OP_SUB) sp=%d", pc_at_opcode, op, vm->stack_pointer);
            if (vm->stack_pointer < 2) {
                vm_error(vm, "SUB requires 2 operands");
                return;
            }
            Value b = vm->stack[vm->stack_pointer - 1];
            Value a = vm->stack[vm->stack_pointer - 2];

            if (is_float(a) || is_float(b)) {
                double da, db;
                if (!to_double(a, &da) || !to_double(b, &db)) {
                    vm_error(vm, "SUB requires numeric operands");
                    return;
                }
                vm_pop(vm); vm_pop(vm);
                vm_push(vm, make_float(da - db));
            }
            else if (is_fixnum(a) && is_fixnum(b)) {
                Value result;
                if (!fixnum_sub_checked(a, b, &result)) {
                    vm_pop(vm); vm_pop(vm);
                    vm_push(vm, result);
                } else {
                    Value big_a = bigint_from_fixnum(a);
                    Value big_b = bigint_from_fixnum(b);
                    Value result = bigint_sub(big_a, big_b);
                    object_release(big_a);
                    object_release(big_b);
                    vm_pop(vm); vm_pop(vm);
                    vm_push(vm, result);
                }
            }
            else if ((is_fixnum(a) || (is_pointer(a) && object_type(a) == TYPE_BIGINT)) &&
                     (is_fixnum(b) || (is_pointer(b) && object_type(b) == TYPE_BIGINT))) {
                Value big_a = is_fixnum(a) ? bigint_from_fixnum(a) : a;
                Value big_b = is_fixnum(b) ? bigint_from_fixnum(b) : b;
                Value result = bigint_sub(big_a, big_b);
                if (is_fixnum(a)) object_release(big_a);
                if (is_fixnum(b)) object_release(big_b);
                vm_pop(vm); vm_pop(vm);
                vm_push(vm, result);
            }
            else {
                vm_error(vm, "SUB requires numeric operands");
                return;
            }
            break;
        }

        case OP_MUL: {
            TRACE_OP("PC=%04u OP=%02X (OP_MUL) sp=%d", pc_at_opcode, op, vm->stack_pointer);
            if (vm->stack_pointer < 2) {
                vm_error(vm, "MUL requires 2 operands");
                return;
            }
            Value b = vm->stack[vm->stack_pointer - 1];
            Value a = vm->stack[vm->stack_pointer - 2];

            if (is_float(a) || is_float(b)) {
                double da, db;
                if (!to_double(a, &da) || !to_double(b, &db)) {
                    vm_error(vm, "MUL requires numeric operands");
                    return;
                }
                vm_pop(vm); vm_pop(vm);
                vm_push(vm, make_float(da * db));
            }
            else if (is_fixnum(a) && is_fixnum(b)) {
                Value result;
                if (!fixnum_mul_checked(a, b, &result)) {
                    vm_pop(vm); vm_pop(vm);
                    vm_push(vm, result);
                } else {
                    Value big_a = bigint_from_fixnum(a);
                    Value big_b = bigint_from_fixnum(b);
                    Value result = bigint_mul(big_a, big_b);
                    object_release(big_a);
                    object_release(big_b);
                    vm_pop(vm); vm_pop(vm);
                    vm_push(vm, result);
                }
            }
            else if ((is_fixnum(a) || (is_pointer(a) && object_type(a) == TYPE_BIGINT)) &&
                     (is_fixnum(b) || (is_pointer(b) && object_type(b) == TYPE_BIGINT))) {
                Value big_a = is_fixnum(a) ? bigint_from_fixnum(a) : a;
                Value big_b = is_fixnum(b) ? bigint_from_fixnum(b) : b;
                Value result = bigint_mul(big_a, big_b);
                if (is_fixnum(a)) object_release(big_a);
                if (is_fixnum(b)) object_release(big_b);
                vm_pop(vm); vm_pop(vm);
                vm_push(vm, result);
            }
            else {
                vm_error(vm, "MUL requires numeric operands");
                return;
            }
            break;
        }

        case OP_DIV: {
            TRACE_OP("PC=%04u OP=%02X (OP_DIV) sp=%d", pc_at_opcode, op, vm->stack_pointer);
            if (vm->stack_pointer < 2) {
                vm_error(vm, "DIV requires 2 operands");
                return;
            }
            Value b = vm->stack[vm->stack_pointer - 1];
            Value a = vm->stack[vm->stack_pointer - 2];

            /* Float path: if either is float, use doubles */
            if (is_float(a) || is_float(b)) {
                double da, db;
                if (!to_double(a, &da) || !to_double(b, &db)) {
                    vm_error(vm, "DIV requires numeric operands");
                    return;
                }
                if (db == 0.0) {
                    vm_error(vm, "Division by zero");
                    return;
                }
                vm_pop(vm); vm_pop(vm);
                vm_push(vm, make_float(da / db));
            }
            /* Check for division by zero */
            else if ((is_fixnum(b) && untag_fixnum(b) == 0) ||
                (is_pointer(b) && object_type(b) == TYPE_BIGINT && bigint_is_zero(b))) {
                vm_error(vm, "Division by zero");
                return;
            }
            else if (is_fixnum(a) && is_fixnum(b)) {
                int64_t av = untag_fixnum(a), bv = untag_fixnum(b);
                if (av % bv != 0) {
                    /* Non-exact: return float */
                    vm_pop(vm); vm_pop(vm);
                    vm_push(vm, make_float((double)av / (double)bv));
                } else {
                    vm_pop(vm); vm_pop(vm);
                    vm_push(vm, make_fixnum(av / bv));
                }
            }
            else if ((is_fixnum(a) || (is_pointer(a) && object_type(a) == TYPE_BIGINT)) &&
                     (is_fixnum(b) || (is_pointer(b) && object_type(b) == TYPE_BIGINT))) {
                Value big_a = is_fixnum(a) ? bigint_from_fixnum(a) : a;
                Value big_b = is_fixnum(b) ? bigint_from_fixnum(b) : b;
                Value result = bigint_div(big_a, big_b);
                if (is_fixnum(a)) object_release(big_a);
                if (is_fixnum(b)) object_release(big_b);
                vm_pop(vm); vm_pop(vm);
                vm_push(vm, result);
            }
            else {
                vm_error(vm, "DIV requires numeric operands");
                return;
            }
            break;
        }

        case OP_NEG: {
            TRACE_OP("PC=%04u OP=%02X (OP_NEG) sp=%d", pc_at_opcode, op, vm->stack_pointer);
            if (vm->stack_pointer < 1) {
                vm_error(vm, "NEG requires 1 operand");
                return;
            }
            Value a = vm->stack[vm->stack_pointer - 1];

            if (is_float(a)) {
                vm_pop(vm);
                vm_push(vm, make_float(-untag_float(a)));
            }
            else if (is_fixnum(a)) {
                Value result;
                if (!fixnum_neg_checked(a, &result)) {
                    vm_pop(vm);
                    vm_push(vm, result);
                } else {
                    Value big_a = bigint_from_fixnum(a);
                    Value result = bigint_neg(big_a);
                    object_release(big_a);
                    vm_pop(vm);
                    vm_push(vm, result);
                }
            }
            else if (is_pointer(a) && object_type(a) == TYPE_BIGINT) {
                Value result = bigint_neg(a);
                vm_pop(vm);
                vm_push(vm, result);
            }
            else {
                vm_error(vm, "NEG requires numeric operand");
                return;
            }
            break;
        }

        /* Comparison operations */
        case OP_EQ: {
            TRACE_OP("PC=%04u OP=%02X (OP_EQ) sp=%d", pc_at_opcode, op, vm->stack_pointer);
            if (vm->stack_pointer < 2) {
                vm_error(vm, "EQ requires 2 operands");
                return;
            }
            Value b = vm->stack[vm->stack_pointer - 1];
            Value a = vm->stack[vm->stack_pointer - 2];
            bool equal = value_equal(a, b);
            vm_pop(vm); vm_pop(vm);
            vm_push(vm, equal ? VALUE_TRUE : VALUE_FALSE);
            break;
        }

        case OP_LT: {
            TRACE_OP("PC=%04u OP=%02X (OP_LT) sp=%d", pc_at_opcode, op, vm->stack_pointer);
            if (vm->stack_pointer < 2) {
                vm_error(vm, "LT requires 2 operands");
                return;
            }
            Value b = vm->stack[vm->stack_pointer - 1];
            Value a = vm->stack[vm->stack_pointer - 2];

            if (is_float(a) || is_float(b)) {
                double da, db;
                if (!to_double(a, &da) || !to_double(b, &db)) {
                    vm_error(vm, "LT requires numeric operands");
                    return;
                }
                vm_pop(vm); vm_pop(vm);
                vm_push(vm, (da < db) ? VALUE_TRUE : VALUE_FALSE);
            }
            else if (is_fixnum(a) && is_fixnum(b)) {
                bool result = untag_fixnum(a) < untag_fixnum(b);
                vm_pop(vm); vm_pop(vm);
                vm_push(vm, result ? VALUE_TRUE : VALUE_FALSE);
            }
            else if ((is_fixnum(a) || (is_pointer(a) && object_type(a) == TYPE_BIGINT)) &&
                     (is_fixnum(b) || (is_pointer(b) && object_type(b) == TYPE_BIGINT))) {
                Value big_a = is_fixnum(a) ? bigint_from_fixnum(a) : a;
                Value big_b = is_fixnum(b) ? bigint_from_fixnum(b) : b;
                int cmp = bigint_cmp(big_a, big_b);
                if (is_fixnum(a)) object_release(big_a);
                if (is_fixnum(b)) object_release(big_b);
                vm_pop(vm); vm_pop(vm);
                vm_push(vm, (cmp < 0) ? VALUE_TRUE : VALUE_FALSE);
            }
            else {
                vm_error(vm, "LT requires numeric operands");
                return;
            }
            break;
        }

        case OP_GT: {
            TRACE_OP("PC=%04u OP=%02X (OP_GT) sp=%d", pc_at_opcode, op, vm->stack_pointer);
            if (vm->stack_pointer < 2) {
                vm_error(vm, "GT requires 2 operands");
                return;
            }
            Value b = vm->stack[vm->stack_pointer - 1];
            Value a = vm->stack[vm->stack_pointer - 2];

            if (is_float(a) || is_float(b)) {
                double da, db;
                if (!to_double(a, &da) || !to_double(b, &db)) {
                    vm_error(vm, "GT requires numeric operands");
                    return;
                }
                vm_pop(vm); vm_pop(vm);
                vm_push(vm, (da > db) ? VALUE_TRUE : VALUE_FALSE);
            }
            else if (is_fixnum(a) && is_fixnum(b)) {
                bool result = untag_fixnum(a) > untag_fixnum(b);
                vm_pop(vm); vm_pop(vm);
                vm_push(vm, result ? VALUE_TRUE : VALUE_FALSE);
            }
            else if ((is_fixnum(a) || (is_pointer(a) && object_type(a) == TYPE_BIGINT)) &&
                     (is_fixnum(b) || (is_pointer(b) && object_type(b) == TYPE_BIGINT))) {
                Value big_a = is_fixnum(a) ? bigint_from_fixnum(a) : a;
                Value big_b = is_fixnum(b) ? bigint_from_fixnum(b) : b;
                int cmp = bigint_cmp(big_a, big_b);
                if (is_fixnum(a)) object_release(big_a);
                if (is_fixnum(b)) object_release(big_b);
                vm_pop(vm); vm_pop(vm);
                vm_push(vm, (cmp > 0) ? VALUE_TRUE : VALUE_FALSE);
            }
            else {
                vm_error(vm, "GT requires numeric operands");
                return;
            }
            break;
        }

        /* Control flow */
        case OP_JUMP: {
            int32_t offset = read_int32(vm, r);
            TRACE_OP("PC=%04u OP=%02X (OP_JUMP) offset=%d target=%u sp=%d",
                      pc_at_opcode, op, offset, r->pc + offset, vm->stack_pointer);
            READ_CHECK();
            r->pc += offset;
            SAFE_POINT(vm);
            break;
        }

        case OP_JUMP_IF_FALSE: {
            int32_t offset = read_int32(vm, r);
            Value condition = vm_peek(vm);
            bool will_jump = is_false(condition) || is_nil(condition);
            TRACE_OP("PC=%04u OP=%02X (OP_JUMP_IF_FALSE) offset=%d will_jump=%d target=%u sp=%d",
                      pc_at_opcode, op, offset, will_jump, r->pc + offset, vm->stack_pointer);
            READ_CHECK();
            if (will_jump) {
                r->pc += offset;
            }
            break;
        }

        /* Function calls */
        case OP_ENTER: {
            /* Function prologue - allocate local variable slots */
            uint16_t n_locals = read_uint16(vm, r);
            TRACE_OP("PC=%04u OP=%02X (OP_ENTER) n_locals=%u sp=%d", pc_at_opcode, op, n_locals, vm->stack_pointer);
            READ_CHECK();

            /* Push nil for each local slot */
            for (uint16_t i = 0; i < n_locals; i++) {
                vm_push(vm, VALUE_NIL);
                if (vm->error) {
                    return;
                }
            }
            SAFE_POINT(vm);
            break;
        }

        case OP_CALL: {
            /* Call a function with n arguments */
            /* Stack layout: [... | arg0 | arg1 | ... | argN | func] */
            /* Function is on top, args below it */
            int n_args = read_uint16(vm, r);
            READ_CHECK();

            /* Function is on top of stack */
            if (vm->stack_pointer < (int)n_args + 1) {
                vm_error(vm, "CALL: not enough values on stack");
                return;
            }

            Value fn;
        call_dispatch:  /* again with the fn a native asked for (apply) */
            fn = vm->stack[vm->stack_pointer - 1];
            TRACE_OP("PC=%04u OP=%02X (OP_CALL) n_args=%u target=%u sp=%d",
                      pc_at_opcode, op, n_args,
                      is_function(fn) ? function_code_offset(fn) : 0, vm->stack_pointer);

            /* Check if it's a native function */
            if (is_native_function(fn)) {
                /* Native function call */
                int arity = native_function_arity(fn);
                if (arity >= 0 && arity != (int)n_args) {
                    vm_error(vm, "CALL: wrong number of arguments");
                    return;
                }

                /* Collect arguments into array (they're below the function on stack) */
                /* Stack layout: [... | arg0 | arg1 | ... | argN | func] */
                Value* args = &vm->stack[vm->stack_pointer - 1 - n_args];

                /* Call the native function */
                NativeFn native_fn = native_function_ptr(fn);
                Value result = native_fn(vm, (int)n_args, args);

                /* Check for errors */
                if (vm->error) {
                    return;
                }

                /* Native blocked on I/O — rewind PC, don't touch stack */
                if (vm->yielded) {
                    vm->native_blocked = false;
                    r->pc = pc_at_opcode;
                    break;
                }

                /* Native requested a catchable throw */
                if (vm->native_throw) {
                    vm->native_throw = false;
                    if (is_pointer(result)) object_release(result);
                    ExceptionHandler* h = pop_handler_for(vm, vm->exception);
                    if (!h) {
                        /* Only handlers that don't take it were left: an
                         * uncaught throw, reported with its own message */
                        bool intr = vm_is_interrupt(vm->exception);
                        Value msg = intr ? VALUE_NIL : hashmap_get(vm->exception, keyword_intern("message"));
                        if (is_pointer(vm->thrown_exception)) object_release(vm->thrown_exception);
                        vm->thrown_exception = vm->exception;
                        vm->exception = VALUE_NIL;
                        vm_error(vm, intr ? "interrupted"
                                     : is_string(msg) ? string_cstr(msg) : "Unhandled exception");
                        return;
                    }
                    unwind_to_handler(vm, h);
                    r->code = vm->code; r->code_size = (uint32_t)vm->code_size;
                    r->pc = h->catch_pc;
                    break;
                }

                /* Native asked to call a fn in its place (apply) */
                if (VM_UNLIKELY(vm->call_requested)) {
                    if (is_pointer(result)) object_release(result);
                    n_args = take_call_request(vm, (int)n_args + 1);
                    if (n_args < 0) return;
                    goto call_dispatch;
                }

                /* Pop args and function from stack */
                for (int i = 0; i < (int)n_args + 1; i++) {
                    vm_pop(vm);
                }

                /* Transfer native's owned ref directly to stack (no extra retain).
                 * The native returned an owned ref; we place it on the stack
                 * without vm_push's retain, so stack owns exactly 1 ref. */
                if (vm->stack_pointer >= vm->stack_size && !grow_stack(vm, 1)) {
                    if (is_pointer(result)) object_release(result);
                    return;
                }
                vm->stack[vm->stack_pointer++] = result;
                break;
            }

            /* Try callable non-function types (keyword, map, vector) */
            if (!is_function(fn)) {
                /* A var calls its current value (binding included): #'f */
                if (is_var(fn)) {
                    deref_var_callee(vm);
                    goto call_dispatch;
                }
                Value* args = &vm->stack[vm->stack_pointer - 1 - n_args];
                Value result = VALUE_NIL;
                if (vm_invoke_value(vm, fn, (int)n_args, args, &result)) {
                    if (vm->error) return;
                    /* Pop args and head from stack */
                    for (int i = 0; i < (int)n_args + 1; i++) {
                        vm_pop(vm);
                    }
                    /* Transfer owned ref to stack */
                    if (vm->stack_pointer >= vm->stack_size && !grow_stack(vm, 1)) {
                        if (is_pointer(result)) object_release(result);
                        return;
                    }
                    vm->stack[vm->stack_pointer++] = result;
                    break;
                }
                {
                    char buf[256];
                    int otype = is_pointer(fn) ? object_type(fn) : -1;
                    snprintf(buf, sizeof(buf), "CALL: not a function (tag=%d, otype=0x%02x, n_args=%d, sp=%d, pc=%u)",
                             fn.tag, otype, n_args, vm->stack_pointer, pc_at_opcode);
                    vm_error(vm, buf);
                }
                return;
            }

            /* Pick the entry for n_args (a multi-arity fn has several) */
            int arity;
            uint32_t entry_pc;
            if (!function_select_arity(fn, (int)n_args, &arity, &entry_pc) ||
                (arity >= 0 && arity != (int)n_args)) {
                arity_error(vm, "CALL", fn, n_args);
                return;
            } else if (arity < 0) {
                /* Variadic function: arity = -(required + 1) */
                int required = -(arity + 1);
                if ((int)n_args < required) {
                    vm_error(vm, "CALL: not enough arguments for variadic function");
                    return;
                }

                /* Collect extra args into a cons list */
                int args_base = vm->stack_pointer - 1 - (int)n_args;
                int extra = (int)n_args - required;
                Value rest_list = VALUE_NIL;
                for (int i = extra - 1; i >= 0; i--) {
                    Value arg = vm->stack[args_base + required + i];
                    Value new_cell = cons(arg, rest_list);
                    if (is_pointer(rest_list)) object_release(rest_list);
                    rest_list = new_cell;
                    /* Release the stack slot's reference (cons already retained the arg) */
                    if (is_pointer(arg)) object_release(arg);
                }

                /* Rewrite stack: replace extra args with rest_list, move func */
                /* Stack before: [... | arg0..arg_{req-1} | extra0..extraN | func] */
                /* Stack after:  [... | arg0..arg_{req-1} | rest_list | func] */
                /* The stack slot takes over rest_list's single reference
                 * (from creating its head cell) -- no extra retain. */
                vm->stack[args_base + required] = rest_list;
                Value fn_val = vm->stack[vm->stack_pointer - 1];
                vm->stack[args_base + required + 1] = fn_val;
                vm->stack_pointer = args_base + required + 2;
                n_args = (uint16_t)(required + 1);
            }

            /* Calculate base pointer (points to where args start) */
            /* Stack layout: [... | arg0 | arg1 | ... | argN | func] */
            /* We want base to point to arg0 */
            /* sp points after func, so func is at sp-1, and arg0 is at sp-1-n_args */
            int base = vm->stack_pointer - 1 - n_args;

            /* Retain fn before popping — vm_pop releases, which could free
             * a function whose only reference is the stack slot (e.g. from
             * a native call like asm).  push_frame will add its own retain. */
            if (is_pointer(fn)) object_retain(fn);

            /* Pop the function off the stack (it's saved in the frame) */
            vm_pop(vm);

            /* Push call frame (saves caller's code/constants) */
            push_frame(vm, fn, r->pc);

            /* Balance the extra retain — push_frame already retained */
            if (is_pointer(fn)) object_release(fn);
            if (vm->error) {
                return;
            }

            /* Set correct base pointer */
            vm->frames[vm->frame_count - 1].base_pointer = base;

            /* Switch to function's bytecode and constants if available */
            if (function_get_code(fn) != NULL) {
                vm->code = function_get_code(fn);
                vm->code_size = function_get_code_size(fn); r->code = vm->code; r->code_size = (uint32_t)vm->code_size;
                vm->constants = function_get_constants(fn);
                vm->num_constants = function_get_num_constants(fn);
            }

            /* Jump to function code */
            r->pc = entry_pc;
            break;
        }

        case OP_TAIL_CALL: {
            /* Tail call - reuse current frame */
            int n_args = read_uint16(vm, r);
            READ_CHECK();

            /* Function is on top of stack, args below it */
            /* Stack layout: [... | arg0 | arg1 | ... | argN | func] */
            if (vm->stack_pointer < (int)n_args + 1) {
                vm_error(vm, "TAIL_CALL: not enough values on stack");
                return;
            }

            Value fn;
        tail_call_dispatch:  /* again with the fn a native asked for (apply) */
            fn = vm->stack[vm->stack_pointer - 1];
            TRACE_OP("PC=%04u OP=%02X (OP_TAIL_CALL) n_args=%u target=%u sp=%d",
                      pc_at_opcode, op, n_args,
                      is_function(fn) ? function_code_offset(fn) : 0, vm->stack_pointer);

            /* Handle native function in tail position */
            if (is_native_function(fn)) {
                int arity = native_function_arity(fn);
                if (arity >= 0 && arity != (int)n_args) {
                    vm_error(vm, "TAIL_CALL: wrong number of arguments");
                    return;
                }

                /* Collect arguments (below function on stack) */
                Value* args = &vm->stack[vm->stack_pointer - 1 - n_args];

                /* Call the native function */
                NativeFn native_fn = native_function_ptr(fn);
                Value result = native_fn(vm, (int)n_args, args);

                if (vm->error) {
                    return;
                }

                /* Native blocked on I/O — rewind PC, don't touch stack */
                if (vm->yielded) {
                    vm->native_blocked = false;
                    r->pc = pc_at_opcode;
                    break;
                }

                /* Native requested a catchable throw */
                if (vm->native_throw) {
                    vm->native_throw = false;
                    if (is_pointer(result)) object_release(result);
                    ExceptionHandler* h = pop_handler_for(vm, vm->exception);
                    if (!h) {
                        /* Only handlers that don't take it were left: an
                         * uncaught throw, reported with its own message */
                        bool intr = vm_is_interrupt(vm->exception);
                        Value msg = intr ? VALUE_NIL : hashmap_get(vm->exception, keyword_intern("message"));
                        if (is_pointer(vm->thrown_exception)) object_release(vm->thrown_exception);
                        vm->thrown_exception = vm->exception;
                        vm->exception = VALUE_NIL;
                        vm_error(vm, intr ? "interrupted"
                                     : is_string(msg) ? string_cstr(msg) : "Unhandled exception");
                        return;
                    }
                    unwind_to_handler(vm, h);
                    r->code = vm->code; r->code_size = (uint32_t)vm->code_size;
                    r->pc = h->catch_pc;
                    break;
                }

                /* Native asked to call a fn in its place (apply): a tail call */
                if (VM_UNLIKELY(vm->call_requested)) {
                    if (is_pointer(result)) object_release(result);
                    n_args = take_call_request(vm, (int)n_args + 1);
                    if (n_args < 0) return;
                    goto tail_call_dispatch;
                }

                /* Pop args and function */
                for (int i = 0; i < (int)n_args + 1; i++) {
                    vm_pop(vm);
                }

                /* Transfer native's owned ref directly to stack (no extra retain). */
                if (vm->stack_pointer >= vm->stack_size && !grow_stack(vm, 1)) {
                    if (is_pointer(result)) object_release(result);
                    return;
                }
                vm->stack[vm->stack_pointer++] = result;
                break;
            }

            /* Try callable non-function types (keyword, map, vector) */
            if (!is_function(fn)) {
                /* A var calls its current value (binding included): #'f */
                if (is_var(fn)) {
                    deref_var_callee(vm);
                    goto tail_call_dispatch;
                }
                Value* args = &vm->stack[vm->stack_pointer - 1 - n_args];
                Value result = VALUE_NIL;
                if (vm_invoke_value(vm, fn, (int)n_args, args, &result)) {
                    if (vm->error) return;
                    for (int i = 0; i < (int)n_args + 1; i++) {
                        vm_pop(vm);
                    }
                    if (vm->stack_pointer >= vm->stack_size && !grow_stack(vm, 1)) {
                        if (is_pointer(result)) object_release(result);
                        return;
                    }
                    vm->stack[vm->stack_pointer++] = result;
                    break;
                }
                vm_error(vm, "TAIL_CALL: not a function");
                return;
            }

            /* Pick the entry for n_args (a multi-arity fn has several) */
            int arity;
            uint32_t entry_pc;
            if (!function_select_arity(fn, (int)n_args, &arity, &entry_pc) ||
                (arity >= 0 && arity != (int)n_args)) {
                arity_error(vm, "TAIL_CALL", fn, n_args);
                return;
            } else if (arity < 0) {
                /* Variadic function: arity = -(required + 1) */
                int required = -(arity + 1);
                if ((int)n_args < required) {
                    vm_error(vm, "TAIL_CALL: not enough arguments for variadic function");
                    return;
                }

                /* Collect extra args into a cons list */
                int args_base = vm->stack_pointer - 1 - (int)n_args;
                int extra = (int)n_args - required;
                Value rest_list = VALUE_NIL;
                for (int i = extra - 1; i >= 0; i--) {
                    Value arg = vm->stack[args_base + required + i];
                    Value new_cell = cons(arg, rest_list);
                    if (is_pointer(rest_list)) object_release(rest_list);
                    rest_list = new_cell;
                    /* Release the stack slot's reference (cons already retained the arg) */
                    if (is_pointer(arg)) object_release(arg);
                }

                /* Rewrite stack: replace extra args with rest_list, move func.
                 * The stack slot takes over rest_list's single reference. */
                vm->stack[args_base + required] = rest_list;
                Value fn_val = vm->stack[vm->stack_pointer - 1];
                vm->stack[args_base + required + 1] = fn_val;
                vm->stack_pointer = args_base + required + 2;
                n_args = (uint16_t)(required + 1);
            }

            /* Get current frame */
            CallFrame* frame = current_frame(vm);
            if (!frame) {
                vm_error(vm, "TAIL_CALL: no current frame");
                return;
            }

            /* Stack layout: [... | arg0 | arg1 | ... | argN | func]
             * Args start at sp - n_args - 1 */
            int args_start = vm->stack_pointer - (int)n_args - 1;
            int frame_base = frame->base_pointer;

            /* Release everything the old frame owns: its params, locals and
             * temporaries, i.e. exactly [frame_base, args_start). Releasing
             * per target slot while moving would be wrong: when the new call
             * has more args than the old frame had slots, target slots
             * overlap source arg slots that were already moved. */
            for (int i = frame_base; i < args_start; i++) {
                if (is_pointer(vm->stack[i])) {
                    object_release(vm->stack[i]);
                }
            }

            /* Move the new arguments down; references transfer as-is. */
            for (int i = 0; i < (int)n_args; i++) {
                vm->stack[frame_base + i] = vm->stack[args_start + i];
            }
            vm->stack_pointer = frame_base + n_args;

            /* The frame takes over the stack slot's reference to fn. */
            if (is_pointer(frame->function)) {
                object_release(frame->function);
            }
            frame->function = fn;

            /* Switch to function's bytecode and constants if available */
            if (function_get_code(fn) != NULL) {
                vm->code = function_get_code(fn);
                vm->code_size = function_get_code_size(fn); r->code = vm->code; r->code_size = (uint32_t)vm->code_size;
                vm->constants = function_get_constants(fn);
                vm->num_constants = function_get_num_constants(fn);
            }

            /* Jump to new function */
            r->pc = entry_pc;
            break;
        }

        case OP_RETURN: {
            /* Return from function */
            TRACE_OP("PC=%04u OP=%02X (OP_RETURN) return_pc=%u frame_count=%d sp=%d",
                      pc_at_opcode, op, current_frame(vm) ? current_frame(vm)->return_pc : 0,
                      vm->frame_count, vm->stack_pointer);

            CallFrame* frame = pop_frame(vm);
            if (!frame) {
                /* No frame - top level return, halt */
                vm->running = false;
                return;
            }

            /* Get return value — retain before pop to prevent freeing */
            Value ret_val = VALUE_NIL;
            if (vm->stack_pointer > frame->base_pointer) {
                ret_val = vm->stack[vm->stack_pointer - 1];
                if (is_pointer(ret_val)) object_retain(ret_val);
                vm_pop(vm);
            }

            /* Clean up stack frame (locals and arguments) */
            while (vm->stack_pointer > frame->base_pointer) {
                vm_pop(vm);
            }

            /* Push return value */
            vm_push(vm, ret_val);
            /* Release the protective retain (vm_push already retained) */
            if (is_pointer(ret_val)) object_release(ret_val);

            /* Restore caller's execution context */
            vm->code = frame->caller_code;
            vm->code_size = frame->caller_code_size; r->code = vm->code; r->code_size = (uint32_t)vm->code_size;
            vm->constants = frame->caller_constants;
            vm->num_constants = frame->caller_num_constants;

            /* Restore PC */
            r->pc = frame->return_pc;
            break;
        }

        /* Global variables */
        case OP_LOAD_VAR: {
            uint16_t const_idx = read_uint16(vm, r);
            TRACE_OP("PC=%04u OP=%02X (OP_LOAD_VAR) const_idx=%u sp=%d", pc_at_opcode, op, const_idx, vm->stack_pointer);
            READ_CHECK();

#ifdef VM_CHECKED
            /* Get symbol from constant pool */
            if (const_idx >= (uint16_t)vm->num_constants) {
                vm_error(vm, "LOAD_VAR: constant index out of bounds");
                return;
            }
#endif

            Value symbol = vm->constants[const_idx];

#ifdef VM_CHECKED
            if (!is_pointer(symbol) || object_type(symbol) != TYPE_SYMBOL) {
                vm_error(vm, "LOAD_VAR: constant is not a symbol");
                return;
            }
#endif

            /* Look up var in current namespace */
            if (!global_namespace_registry) {
                vm_error(vm, "LOAD_VAR: namespace system not initialized");
                return;
            }

            Namespace* home = code_home_ns(vm);
            if (!home) {
                vm_error(vm, "LOAD_VAR: no current namespace");
                return;
            }

            /* Resolve against the namespace the running code was written in:
             * the function's home ns, or the current ns for top-level code.
             * Never the caller's current ns, or redefining + in user would
             * change what beer.core/inc calls. */

            void* sym_ptr = untag_pointer(symbol);
            VarCacheEntry* ce = &var_cache[var_cache_slot(home, sym_ptr)];
            Var* var = (ce->gen == namespace_generation && ce->home == home && ce->sym == sym_ptr)
                ? ce->var : NULL;

            if (var) {
                /* cache hit */
            } else if (symbol_has_namespace(symbol)) {
                /* Qualified: foo/bar — resolve namespace */
                const char* ns_part = symbol_str(symbol);
                const char* slash = strchr(ns_part, '/');
                if (!slash) {
                    vm_error(vm, "LOAD_VAR: malformed qualified symbol");
                    return;
                }
                size_t ns_len = (size_t)(slash - ns_part);
                char ns_name[256];
                if (ns_len >= sizeof(ns_name)) {
                    vm_error(vm, "LOAD_VAR: namespace name too long");
                    return;
                }
                memcpy(ns_name, ns_part, ns_len);
                ns_name[ns_len] = '\0';

                Value alias_sym = symbol_intern(ns_name);
                const char* resolved = namespace_resolve_alias(home, alias_sym);
                const char* target_name = resolved ? resolved : ns_name;

                Namespace* target_ns = namespace_registry_get(global_namespace_registry, target_name);
                if (!target_ns) {
                    char buf[256];
                    snprintf(buf, sizeof(buf), "LOAD_VAR: namespace '%s' not found", target_name);
                    vm_error(vm, buf);
                    return;
                }

                /* Look up the name part */
                const char* name_part = slash + 1;
                Value name_sym = symbol_intern(name_part);
                var = namespace_lookup(target_ns, name_sym);
                if (!var) {
                    char buf[256];
                    snprintf(buf, sizeof(buf), "LOAD_VAR: '%s' not found in namespace '%s'", name_part, target_name);
                    vm_error(vm, buf);
                    return;
                }
            } else {
                /* Unqualified: home ns, then beer.core */
                var = namespace_lookup(home, symbol);
                if (!var) {
                    Namespace* core_ns = namespace_registry_get_core(global_namespace_registry);
                    if (core_ns && core_ns != home) {
                        var = namespace_lookup(core_ns, symbol);
                    }
                }
                if (!var) {
                    char buf[256];
                    snprintf(buf, sizeof(buf), "LOAD_VAR: undefined var '%s'", symbol_name(symbol));
                    vm_error(vm, buf);
                    return;
                }
            }
            ce->home = home;
            ce->sym = sym_ptr;
            ce->var = var;
            ce->gen = namespace_generation;

            Value value = var_get_value(var);
            vm_push(vm, value);
            break;
        }

        case OP_STORE_VAR: {
            uint16_t const_idx = read_uint16(vm, r);
            TRACE_OP("PC=%04u OP=%02X (OP_STORE_VAR) const_idx=%u sp=%d", pc_at_opcode, op, const_idx, vm->stack_pointer);
            READ_CHECK();

#ifdef VM_CHECKED
            /* Get symbol from constant pool */
            if (const_idx >= (uint16_t)vm->num_constants) {
                vm_error(vm, "STORE_VAR: constant index out of bounds");
                return;
            }
#endif

            Value symbol = vm->constants[const_idx];
#ifdef VM_CHECKED
            if (!is_pointer(symbol) || object_type(symbol) != TYPE_SYMBOL) {
                vm_error(vm, "STORE_VAR: constant is not a symbol");
                return;
            }
#endif

            /* Pop value from stack */
            if (vm->stack_pointer <= 0) {
                vm_error(vm, "STORE_VAR: stack underflow");
                return;
            }
            /* Retain before pop: namespace_define needs to access the value,
             * and vm_pop releases which may free if refcount reaches 0 */
            Value value = vm->stack[vm->stack_pointer - 1];
            if (is_pointer(value)) object_retain(value);
            vm_pop(vm);

            /* Define or update var in namespace */
            if (!global_namespace_registry) {
                if (is_pointer(value)) object_release(value);
                vm_error(vm, "STORE_VAR: namespace system not initialized");
                return;
            }

            Namespace* target_ns;
            Value def_symbol = symbol;
            if (symbol_has_namespace(symbol)) {
                /* Qualified symbol: def in the target namespace */
                const char* sym_str = symbol_str(symbol);
                const char* slash = strchr(sym_str, '/');
                if (!slash) {
                    if (is_pointer(value)) object_release(value);
                    vm_error(vm, "STORE_VAR: malformed qualified symbol");
                    return;
                }
                size_t ns_len = (size_t)(slash - sym_str);
                char ns_name_buf[256];
                if (ns_len >= sizeof(ns_name_buf)) {
                    if (is_pointer(value)) object_release(value);
                    vm_error(vm, "STORE_VAR: namespace name too long");
                    return;
                }
                memcpy(ns_name_buf, sym_str, ns_len);
                ns_name_buf[ns_len] = '\0';

                Namespace* home = code_home_ns(vm);
                Value alias_sym = symbol_intern(ns_name_buf);
                const char* resolved = home ? namespace_resolve_alias(home, alias_sym) : NULL;
                const char* target_name = resolved ? resolved : ns_name_buf;

                target_ns = namespace_registry_get(global_namespace_registry, target_name);
                if (!target_ns) {
                    if (is_pointer(value)) object_release(value);
                    char buf[256];
                    snprintf(buf, sizeof(buf), "STORE_VAR: namespace '%s' not found", target_name);
                    vm_error(vm, buf);
                    return;
                }
                /* Use unqualified name for the var */
                def_symbol = symbol_intern(slash + 1);
            } else {
                /* Like Clojure: a def in a fn defines in the fn's namespace,
                 * not the one current when it runs */
                target_ns = code_home_ns(vm);
                if (!target_ns) {
                    if (is_pointer(value)) object_release(value);
                    vm_error(vm, "STORE_VAR: no current namespace");
                    return;
                }
            }

            namespace_define(target_ns, def_symbol, value);

            /* Push the value back (def returns the value) */
            vm_push(vm, value);
            /* Release the protective retain (vm_push already retained) */
            if (is_pointer(value)) object_release(value);
            break;
        }

        /* Local variables */
        case OP_LOAD_LOCAL: {
            uint16_t idx = read_uint16(vm, r);
            TRACE_OP("PC=%04u OP=%02X (OP_LOAD_LOCAL) idx=%u sp=%d", pc_at_opcode, op, idx, vm->stack_pointer);
            READ_CHECK();

            CallFrame* frame = current_frame(vm);
            if (!frame) {
                vm_error(vm, "LOAD_LOCAL: no current frame");
                return;
            }

            int local_idx = frame->base_pointer + idx;
            if (local_idx >= vm->stack_pointer) {
                vm_error(vm, "LOAD_LOCAL: index out of bounds");
                return;
            }

            vm_push(vm, vm->stack[local_idx]);
            break;
        }

        case OP_STORE_LOCAL: {
            uint16_t idx = read_uint16(vm, r);
            TRACE_OP("PC=%04u OP=%02X (OP_STORE_LOCAL) idx=%u sp=%d", pc_at_opcode, op, idx, vm->stack_pointer);
            READ_CHECK();

            CallFrame* frame = current_frame(vm);
            if (!frame) {
                vm_error(vm, "STORE_LOCAL: no current frame");
                return;
            }

            /* Retain before pop, then transfer to local slot */
            Value val = vm->stack[vm->stack_pointer - 1];
            if (is_pointer(val)) object_retain(val);
            vm_pop(vm);

            int local_idx = frame->base_pointer + idx;
            if (local_idx >= vm->stack_size && !grow_stack(vm, local_idx - vm->stack_pointer + 1)) {
                if (is_pointer(val)) object_release(val);
                return;
            }

            /* Release old value */
            if (local_idx < vm->stack_pointer && is_pointer(vm->stack[local_idx])) {
                object_release(vm->stack[local_idx]);
            }

            /* Store new value (val owns one ref from our retain) */
            vm->stack[local_idx] = val;

            /* Ensure we're not writing beyond stack pointer */
            if (local_idx >= vm->stack_pointer) {
                vm->stack_pointer = local_idx + 1;
            }
            break;
        }

        /* Closures */
        case OP_LOAD_CLOSURE: {
            uint16_t idx = read_uint16(vm, r);
            TRACE_OP("PC=%04u OP=%02X (OP_LOAD_CLOSURE) idx=%u sp=%d", pc_at_opcode, op, idx, vm->stack_pointer);
            READ_CHECK();

            CallFrame* frame = current_frame(vm);
            if (!frame) {
                vm_error(vm, "LOAD_CLOSURE: no current frame");
                return;
            }

            if (!is_function(frame->function)) {
                vm_error(vm, "LOAD_CLOSURE: current frame has no function");
                return;
            }

            if (idx >= function_n_closed(frame->function)) {
                vm_error(vm, "LOAD_CLOSURE: index out of bounds");
                return;
            }

            Value closed = function_get_closed(frame->function, idx);
            vm_push(vm, closed);
            break;
        }

        case OP_LOAD_SELF: {
            /* Push the current function onto the stack (for named fn self-recursion) */
            TRACE_OP("PC=%04u OP=%02X (OP_LOAD_SELF) sp=%d", pc_at_opcode, op, vm->stack_pointer);
            CallFrame* frame = current_frame(vm);
            if (!frame || !is_function(frame->function)) {
                vm_error(vm, "LOAD_SELF: no current function");
                return;
            }
            vm_push(vm, frame->function);
            break;
        }

        case OP_MAKE_CLOSURE: {
            /* MAKE_CLOSURE <code_offset:u32> <n_locals:u16> <n_closed:u16> <arity:u16> <name_idx:u16> */
            uint32_t code_offset = read_uint32(vm, r);
            READ_CHECK();

            uint16_t n_locals = read_uint16(vm, r);
            READ_CHECK();

            uint16_t n_closed = read_uint16(vm, r);
            READ_CHECK();

            int16_t arity = (int16_t)read_uint16(vm, r);
            READ_CHECK();

            uint16_t name_idx = read_uint16(vm, r);
            READ_CHECK();

            TRACE_OP("PC=%04u OP=%02X (OP_MAKE_CLOSURE) code_offset=%u n_locals=%u n_closed=%u arity=%d name_idx=%u sp=%d",
                      pc_at_opcode, op, code_offset, n_locals, n_closed, arity, name_idx, vm->stack_pointer);

            /* name_idx names a string constant, or for a multi-arity fn
             * the template function: its name and arity table */
            const char* fn_name = "anonymous-fn";
            Value template_fn = VALUE_NIL;
            if (name_idx < (uint16_t)vm->num_constants) {
                Value name_val = vm->constants[name_idx];
                if (is_pointer(name_val) && object_type(name_val) == TYPE_STRING) {
                    fn_name = string_cstr(name_val);
                } else if (is_function(name_val)) {
                    template_fn = name_val;
                    fn_name = function_name(name_val);
                }
            }

            /* Pop closed-over values from stack */
            if (vm->stack_pointer < (int)n_closed) {
                vm_error(vm, "MAKE_CLOSURE: not enough values for closure");
                return;
            }

            Value* closed_values = NULL;
            if (n_closed > 0) {
                closed_values = (Value*)malloc(sizeof(Value) * n_closed);
                if (!closed_values) {
                    vm_error(vm, "MAKE_CLOSURE: allocation failed");
                    return;
                }

                /* Read in reverse order, retain before popping */
                for (int i = n_closed - 1; i >= 0; i--) {
                    closed_values[i] = vm->stack[vm->stack_pointer - (int)n_closed + i];
                    if (is_pointer(closed_values[i])) object_retain(closed_values[i]);
                }
                for (int i = 0; i < (int)n_closed; i++) {
                    vm_pop(vm);
                }
            }

            /* Create closure */
            Value closure = function_new_closure((int)arity, code_offset, n_locals,
                                                  n_closed, closed_values, fn_name);

            if (closed_values) {
                /* function_new_closure retained what it captured; drop the
                 * temporary retains taken above to survive the pops. */
                for (int i = 0; i < (int)n_closed; i++) {
                    if (is_pointer(closed_values[i])) object_release(closed_values[i]);
                }
                free(closed_values);
            }

            if (is_nil(closure)) {
                vm_error(vm, "MAKE_CLOSURE: failed to create closure");
                return;
            }
            if (!is_nil(template_fn)) {
                Function* t = (Function*)untag_pointer(template_fn);
                function_set_arities(closure, t->arities, t->n_arities);
            }

            /* Make closure self-contained (store bytecode + constants pointers) */
            function_set_code(closure, vm->code, vm->code_size,
                              vm->constants, vm->num_constants);

            /* Home ns: the enclosing function's, or the current ns for a
             * closure made by top-level code */
            const char* home_ns = NULL;
            if (vm->frame_count > 0) {
                Value enc_fn = vm->frames[vm->frame_count - 1].function;
                if (is_function(enc_fn)) home_ns = function_ns_name(enc_fn);
            }
            if (!home_ns && global_namespace_registry) {
                Namespace* cur = namespace_registry_current(global_namespace_registry);
                if (cur) home_ns = cur->name;
            }
            if (home_ns) function_set_ns_name(closure, home_ns);

            /* vm_push retains; the stack now holds the only reference */
            vm_push(vm, closure);
            object_release(closure);
            break;
        }

        /* Exception handling */
        case OP_PUSH_HANDLER:
        case OP_PUSH_FINALLY:
        case OP_PUSH_INTERRUPT_CATCH: {
            uint32_t catch_offset = read_uint32(vm, r);
            TRACE_OP("PC=%04u OP=%02X (OP_PUSH_HANDLER) catch_offset=%u sp=%d",
                      pc_at_opcode, op, catch_offset, vm->stack_pointer);
            READ_CHECK();

            if (vm->handler_count >= vm->handler_capacity && !grow_handlers(vm)) return;

            ExceptionHandler* h = &vm->handlers[vm->handler_count++];
            h->catch_pc = catch_offset;
            h->stack_pointer = vm->stack_pointer;
            h->frame_count = vm->frame_count;
            h->kind = op == OP_PUSH_FINALLY ? HANDLER_FINALLY
                    : op == OP_PUSH_INTERRUPT_CATCH ? HANDLER_INTERRUPT : HANDLER_CATCH;
            break;
        }

        case OP_POP_HANDLER: {
            TRACE_OP("PC=%04u OP=%02X (OP_POP_HANDLER) sp=%d", pc_at_opcode, op, vm->stack_pointer);
            if (vm->handler_count <= 0) {
                vm_error(vm, "No exception handler to pop");
                return;
            }
            vm->handler_count--;
            break;
        }

        case OP_THROW: {
            TRACE_OP("PC=%04u OP=%02X (OP_THROW) sp=%d", pc_at_opcode, op, vm->stack_pointer);
            if (vm->stack_pointer < 1) {
                vm_error(vm, "THROW: stack underflow");
                return;
            }

            Value exc = vm->stack[vm->stack_pointer - 1];

            /* Enforce hashmap-only exceptions */
            if (!is_hashmap(exc)) {
                vm_error(vm, "throw requires a map value");
                return;
            }

            /* Retain exception value before pop */
            if (is_pointer(exc)) object_retain(exc);
            vm_pop(vm);

            ExceptionHandler* h = pop_handler_for(vm, exc);
            if (!h) {
                /* No handler — save the exception value so a parent VM (e.g. apply's
                 * temp VM) can re-throw it rather than losing it behind "apply: ..." */
                if (is_pointer(vm->thrown_exception)) object_release(vm->thrown_exception);
                vm->thrown_exception = exc;  /* transfer ownership — do NOT release */
                /* Report the exception's own message when it has one (e.g. a
                 * runtime error rethrown after a finally) */
                Value msg = hashmap_get(exc, keyword_intern("message"));
                vm_error(vm, is_string(msg) ? string_cstr(msg) : "Unhandled exception");
                return;
            }

            unwind_to_handler(vm, h);
            r->code = vm->code; r->code_size = (uint32_t)vm->code_size;

            /* Store exception for OP_LOAD_EXCEPTION */
            if (is_pointer(vm->exception)) object_release(vm->exception);
            vm->exception = exc;

            /* Jump to catch block */
            r->pc = h->catch_pc;
            break;
        }

        case OP_LOAD_EXCEPTION: {
            TRACE_OP("PC=%04u OP=%02X (OP_LOAD_EXCEPTION) sp=%d", pc_at_opcode, op, vm->stack_pointer);
            vm_push(vm, vm->exception);
            break;
        }

        /* Concurrency operations */
        case OP_YIELD: {
            TRACE_OP("PC=%04u OP=%02X (OP_YIELD) sp=%d", pc_at_opcode, op, vm->stack_pointer);
            vm->yielded = true;
            vm_push(vm, VALUE_NIL);
            break;
        }

        case OP_SPAWN: {
            uint16_t n_args = read_uint16(vm, r);
            TRACE_OP("PC=%04u OP=%02X (OP_SPAWN) n_args=%u sp=%d", pc_at_opcode, op, n_args, vm->stack_pointer);
            READ_CHECK();

            if (vm->stack_pointer < (int)n_args + 1) {
                vm_error(vm, "SPAWN: not enough values on stack");
                return;
            }

            /* Stack: [... | arg0 | arg1 | ... | argN | func] */
            Value fn = vm->stack[vm->stack_pointer - 1];
            Value* args = &vm->stack[vm->stack_pointer - 1 - n_args];

            if (!vm->scheduler) {
                vm_error(vm, "SPAWN: no scheduler available");
                return;
            }

            Value task_val = scheduler_spawn(vm->scheduler, fn, (int)n_args, args);

            /* Pop fn + args */
            for (int i = 0; i < (int)n_args + 1; i++) {
                vm_pop(vm);
            }

            vm_push(vm, task_val);
            /* Release spawn's ownership (vm_push retained) */
            if (is_pointer(task_val)) object_release(task_val);
            break;
        }

        case OP_AWAIT: {
            TRACE_OP("PC=%04u OP=%02X (OP_AWAIT) sp=%d", pc_at_opcode, op, vm->stack_pointer);
            if (vm->stack_pointer < 1) {
                vm_error(vm, "AWAIT: stack underflow");
                return;
            }

            Value task_val = vm_peek(vm);
            if (!is_task(task_val)) {
                vm_error(vm, "AWAIT: not a task");
                return;
            }

            Task* target = task_get(task_val);
            if (target->state == TASK_DONE) {
                /* Task already done — extract result before popping task */
                Value result = target->result;
                if (is_pointer(result)) object_retain(result);
                vm_pop(vm);
                vm_push(vm, result);
                if (is_pointer(result)) object_release(result);
            } else {
                /* Inside a task — yield and re-execute when resumed */
                r->pc = pc_at_opcode;
                vm->yielded = true;
            }
            break;
        }

        case OP_CHAN_SEND: {
            TRACE_OP("PC=%04u OP=%02X (OP_CHAN_SEND) sp=%d", pc_at_opcode, op, vm->stack_pointer);
            if (vm->stack_pointer < 2) {
                vm_error(vm, "CHAN_SEND: requires channel and value");
                return;
            }

            Value val = vm->stack[vm->stack_pointer - 1];
            Value ch = vm->stack[vm->stack_pointer - 2];

            if (!is_channel(ch)) {
                vm_error(vm, "CHAN_SEND: not a channel");
                return;
            }

            if (!vm->scheduler) {
                vm_error(vm, "CHAN_SEND: no scheduler available");
                return;
            }

            /* Retain val before popping — channel_send may need it */
            if (is_pointer(val)) object_retain(val);
            vm_pop(vm);  /* pop val */

            /* Find current task */
            Task* cur = vm->scheduler->current;
            bool sent = channel_send(ch, val, vm->scheduler, cur);
            if (is_pointer(val)) object_release(val);
            if (sent) {
                /* Pop channel, push true */
                vm_pop(vm);
                vm_push(vm, VALUE_TRUE);
            } else {
                /* Blocked — rewind PC, push val back for retry */
                vm_push(vm, val);
                r->pc = pc_at_opcode;
                vm->yielded = true;
            }
            break;
        }

        case OP_CHAN_RECV: {
            TRACE_OP("PC=%04u OP=%02X (OP_CHAN_RECV) sp=%d", pc_at_opcode, op, vm->stack_pointer);
            if (vm->stack_pointer < 1) {
                vm_error(vm, "CHAN_RECV: requires channel");
                return;
            }

            Value ch = vm_peek(vm);
            if (!is_channel(ch)) {
                vm_error(vm, "CHAN_RECV: not a channel");
                return;
            }

            if (!vm->scheduler) {
                vm_error(vm, "CHAN_RECV: no scheduler available");
                return;
            }

            Task* cur = vm->scheduler->current;
            Value out;
            bool received = channel_recv(ch, vm->scheduler, cur, &out);
            if (received) {
                /* Pop channel, push received value */
                vm_pop(vm);
                vm_push(vm, out);
                if (is_pointer(out)) object_release(out);
            } else {
                /* Blocked — rewind PC for retry */
                r->pc = pc_at_opcode;
                vm->yielded = true;
            }
            break;
        }

        case OP_HALT:
            TRACE_OP("PC=%04u OP=%02X (OP_HALT) sp=%d", pc_at_opcode, op, vm->stack_pointer);
            vm->running = false;
            break;

        default:
            TRACE_OP("PC=%04u OP=%02X (INVALID) sp=%d", pc_at_opcode, op, vm->stack_pointer);
            vm_error(vm, "Invalid opcode");
            break;
    }
}

/* Code loaded with vm_load_code/vm_load_constants is verified the first
 * time it runs; resumed tasks don't pay again. False (with vm->error set)
 * if it is rejected. */
static bool verify_loaded(VM* vm) {
    if (!vm->needs_verify) return true;
    char err[200];
    if (!bytecode_verify(vm->code, vm->code_size, vm->constants, vm->num_constants,
                         err, sizeof(err))) {
        char msg[256];
        snprintf(msg, sizeof(msg), "invalid bytecode: %s", err);
        vm_error(vm, msg);
        return false;
    }
    vm->needs_verify = false;
    return true;
}

void vm_step(VM* vm) {
    if (!vm->running || vm->error) {
        return;
    }
    if (!verify_loaded(vm)) return;
    Regs r = { vm->pc, vm->code, (uint32_t)vm->code_size };
    step(vm, &r);
    vm->pc = r.pc;
}

/* A runtime error with a try/catch handler active: turn it into a
 * catchable {:message ...} exception and resume at the handler. false (the
 * error stands) when only interrupt catches are left. */
static VM_NOINLINE bool error_to_exception(VM* vm) {
    const char* emsg = vm->error_msg ? vm->error_msg : "runtime error";
    Value exc = hashmap_create_default();
    Value k   = keyword_intern("message");
    Value s   = string_from_cstr(emsg);
    Value exc2 = hashmap_assoc(exc, k, s);
    object_release(exc);
    object_release(s);

    ExceptionHandler* h = pop_handler_for(vm, exc2);
    if (!h) {
        object_release(exc2);
        return false;
    }
    unwind_to_handler(vm, h);
    if (is_pointer(vm->exception)) object_release(vm->exception);
    vm->exception = exc2;
    vm->pc  = h->catch_pc;
    vm->error   = false;
    vm->running = true;
    return true;
}

/* Run VM until halt or yield */
void vm_run(VM* vm) {
    vm->running = true;
    vm->error = false;
    vm->yielded = false;
    /* Don't reset pc - allow caller to set starting position */

    /* Errors are left in vm->error / error_msg for the caller to report */
    if (!verify_loaded(vm)) return;

    /* One compare per instruction: running, no error, not yielded */
    const uint32_t go = vm_run_state_go();
    Regs r = { vm->pc, vm->code, (uint32_t)vm->code_size };
    for (;;) {
        while (vm->run_state == go) {
            step(vm, &r);
        }
        /* A runtime error with a try/catch active becomes an exception */
        if (vm->error && vm->handler_count > 0) {
            vm->pc = r.pc;
            if (!error_to_exception(vm)) break;
            r.pc = vm->pc;
            r.code = vm->code;
            r.code_size = (uint32_t)vm->code_size;
            continue;
        }
        break;
    }
    vm->pc = r.pc;
}
