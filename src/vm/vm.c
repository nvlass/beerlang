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
    vm->native_throw = false;
    vm->scheduler = NULL;

    vm->running = false;
    vm->error = false;
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
}

/* Load constant pool */
void vm_load_constants(VM* vm, Value* constants, int num) {
    assert(vm != NULL);
    vm->constants = constants;
    vm->num_constants = num;
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

/* Read next byte from bytecode */
static inline uint8_t read_byte(VM* vm) {
    if (vm->pc >= vm->code_size) {
        vm_error(vm, "PC out of bounds");
        return 0;
    }
    return vm->code[vm->pc++];
}

/* Read int64 from bytecode (8 bytes, little endian) */
static int64_t read_int64(VM* vm) {
    if (vm->pc + 8 > vm->code_size) {
        vm_error(vm, "Not enough bytes for int64");
        return 0;
    }

    int64_t value = 0;
    for (int i = 0; i < 8; i++) {
        value |= ((int64_t)vm->code[vm->pc++]) << (i * 8);
    }
    return value;
}

/* Read uint32 from bytecode (4 bytes, little endian) */
static uint32_t read_uint32(VM* vm) {
    if (vm->pc + 4 > vm->code_size) {
        vm_error(vm, "Not enough bytes for uint32");
        return 0;
    }

    uint32_t value = 0;
    for (int i = 0; i < 4; i++) {
        value |= ((uint32_t)vm->code[vm->pc++]) << (i * 8);
    }
    return value;
}

/* Read int32 from bytecode (4 bytes, little endian) */
static int32_t read_int32(VM* vm) {
    return (int32_t)read_uint32(vm);
}

/* Read uint16 from bytecode (2 bytes, little endian) */
static uint16_t read_uint16(VM* vm) {
    if (vm->pc + 2 > vm->code_size) {
        vm_error(vm, "Not enough bytes for uint16");
        return 0;
    }

    uint16_t value = 0;
    for (int i = 0; i < 2; i++) {
        value |= ((uint16_t)vm->code[vm->pc++]) << (i * 8);
    }
    return value;
}

/* Push a new call frame, saving caller's execution context */
static void push_frame(VM* vm, Value function, uint32_t return_pc) {
    if (vm->frame_count >= vm->frame_capacity) {
        vm_error(vm, "Call stack overflow");
        return;
    }

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

/* Push value onto stack */
void vm_push(VM* vm, Value v) {
    if (vm->stack_pointer >= vm->stack_size) {
        vm_error(vm, "Stack overflow");
        return;
    }

    /* Retain heap objects */
    if (is_pointer(v)) {
        object_retain(v);
    }

    vm->stack[vm->stack_pointer++] = v;
}

/* Pop value from stack (releases the stack's reference). */
Value vm_pop(VM* vm) {
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

/* Execute single instruction */
void vm_step(VM* vm) {
    if (!vm->running || vm->error) {
        return;
    }

    if (vm->pc >= vm->code_size) {
        vm->running = false;
        return;
    }

    /* Capture PC before reading opcode for trace logging */
    uint32_t pc_at_opcode = vm->pc;
    uint8_t op = read_byte(vm);

    switch (op) {
        /* Stack operations */
        case OP_NOP:
            LOG_TRACE("PC=%04u OP=%02X (OP_NOP) sp=%d", pc_at_opcode, op, vm->stack_pointer);
            break;

        case OP_POP: {
            LOG_TRACE("PC=%04u OP=%02X (OP_POP) sp=%d", pc_at_opcode, op, vm->stack_pointer);
            vm_pop(vm);
            break;
        }

        case OP_DUP: {
            LOG_TRACE("PC=%04u OP=%02X (OP_DUP) sp=%d", pc_at_opcode, op, vm->stack_pointer);
            Value v = vm_peek(vm);
            vm_push(vm, v);
            break;
        }

        case OP_SWAP: {
            LOG_TRACE("PC=%04u OP=%02X (OP_SWAP) sp=%d", pc_at_opcode, op, vm->stack_pointer);
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
            LOG_TRACE("PC=%04u OP=%02X (OP_PUSH_NIL) sp=%d", pc_at_opcode, op, vm->stack_pointer);
            vm_push(vm, VALUE_NIL);
            break;

        case OP_PUSH_TRUE:
            LOG_TRACE("PC=%04u OP=%02X (OP_PUSH_TRUE) sp=%d", pc_at_opcode, op, vm->stack_pointer);
            vm_push(vm, VALUE_TRUE);
            break;

        case OP_PUSH_FALSE:
            LOG_TRACE("PC=%04u OP=%02X (OP_PUSH_FALSE) sp=%d", pc_at_opcode, op, vm->stack_pointer);
            vm_push(vm, VALUE_FALSE);
            break;

        case OP_PUSH_INT: {
            int64_t value = read_int64(vm);
            LOG_TRACE("PC=%04u OP=%02X (OP_PUSH_INT) value=%lld sp=%d", pc_at_opcode, op, (long long)value, vm->stack_pointer);
            if (!vm->error) {
                vm_push(vm, make_fixnum(value));
            }
            break;
        }

        case OP_PUSH_CONST: {
            uint32_t idx = read_uint32(vm);
            LOG_TRACE("PC=%04u OP=%02X (OP_PUSH_CONST) idx=%u sp=%d", pc_at_opcode, op, idx, vm->stack_pointer);
            if (vm->error) {
                return;
            }
            if (idx >= (uint32_t)vm->num_constants) {
                vm_error(vm, "Constant index out of bounds");
                return;
            }
            vm_push(vm, vm->constants[idx]);
            break;
        }

        /* Arithmetic operations */
        case OP_ADD: {
            LOG_TRACE("PC=%04u OP=%02X (OP_ADD) sp=%d", pc_at_opcode, op, vm->stack_pointer);
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
            LOG_TRACE("PC=%04u OP=%02X (OP_SUB) sp=%d", pc_at_opcode, op, vm->stack_pointer);
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
            LOG_TRACE("PC=%04u OP=%02X (OP_MUL) sp=%d", pc_at_opcode, op, vm->stack_pointer);
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
            LOG_TRACE("PC=%04u OP=%02X (OP_DIV) sp=%d", pc_at_opcode, op, vm->stack_pointer);
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
            LOG_TRACE("PC=%04u OP=%02X (OP_NEG) sp=%d", pc_at_opcode, op, vm->stack_pointer);
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
            LOG_TRACE("PC=%04u OP=%02X (OP_EQ) sp=%d", pc_at_opcode, op, vm->stack_pointer);
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
            LOG_TRACE("PC=%04u OP=%02X (OP_LT) sp=%d", pc_at_opcode, op, vm->stack_pointer);
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
            LOG_TRACE("PC=%04u OP=%02X (OP_GT) sp=%d", pc_at_opcode, op, vm->stack_pointer);
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
            int32_t offset = read_int32(vm);
            LOG_TRACE("PC=%04u OP=%02X (OP_JUMP) offset=%d target=%u sp=%d",
                      pc_at_opcode, op, offset, vm->pc + offset, vm->stack_pointer);
            if (vm->error) {
                return;
            }
            vm->pc += offset;
            break;
        }

        case OP_JUMP_IF_FALSE: {
            int32_t offset = read_int32(vm);
            Value condition = vm_peek(vm);
            bool will_jump = is_false(condition) || is_nil(condition);
            LOG_TRACE("PC=%04u OP=%02X (OP_JUMP_IF_FALSE) offset=%d will_jump=%d target=%u sp=%d",
                      pc_at_opcode, op, offset, will_jump, vm->pc + offset, vm->stack_pointer);
            if (vm->error) {
                return;
            }
            if (will_jump) {
                vm->pc += offset;
            }
            break;
        }

        /* Function calls */
        case OP_ENTER: {
            /* Function prologue - allocate local variable slots */
            uint16_t n_locals = read_uint16(vm);
            LOG_TRACE("PC=%04u OP=%02X (OP_ENTER) n_locals=%u sp=%d", pc_at_opcode, op, n_locals, vm->stack_pointer);
            if (vm->error) {
                return;
            }

            /* Push nil for each local slot */
            for (uint16_t i = 0; i < n_locals; i++) {
                vm_push(vm, VALUE_NIL);
                if (vm->error) {
                    return;
                }
            }
            break;
        }

        case OP_CALL: {
            /* Call a function with n arguments */
            /* Stack layout: [... | arg0 | arg1 | ... | argN | func] */
            /* Function is on top, args below it */
            uint16_t n_args = read_uint16(vm);
            if (vm->error) {
                return;
            }

            /* Function is on top of stack */
            if (vm->stack_pointer < (int)n_args + 1) {
                vm_error(vm, "CALL: not enough values on stack");
                return;
            }

            Value fn = vm->stack[vm->stack_pointer - 1];
            uint32_t target = is_function(fn) ? function_code_offset(fn) : 0;
            LOG_TRACE("PC=%04u OP=%02X (OP_CALL) n_args=%u target=%u sp=%d",
                      pc_at_opcode, op, n_args, target, vm->stack_pointer);

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
                    vm->pc = pc_at_opcode;
                    break;
                }

                /* Native requested a catchable throw */
                if (vm->native_throw) {
                    vm->native_throw = false;
                    if (is_pointer(result)) object_release(result);
                    if (vm->handler_count <= 0) {
                        vm_error(vm, "Unhandled exception");
                        return;
                    }
                    ExceptionHandler* h = &vm->handlers[--vm->handler_count];
                    while (vm->frame_count > h->frame_count) {
                        CallFrame* fr = pop_frame(vm);
                        if (!fr) break;
                        while (vm->stack_pointer > fr->base_pointer) vm_pop(vm);
                        vm->code = fr->caller_code;
                        vm->code_size = fr->caller_code_size;
                        vm->constants = fr->caller_constants;
                        vm->num_constants = fr->caller_num_constants;
                    }
                    while (vm->stack_pointer > h->stack_pointer) vm_pop(vm);
                    vm->pc = h->catch_pc;
                    break;
                }

                /* Pop args and function from stack */
                for (int i = 0; i < (int)n_args + 1; i++) {
                    vm_pop(vm);
                }

                /* Transfer native's owned ref directly to stack (no extra retain).
                 * The native returned an owned ref; we place it on the stack
                 * without vm_push's retain, so stack owns exactly 1 ref. */
                if (vm->stack_pointer >= vm->stack_size) {
                    if (is_pointer(result)) object_release(result);
                    vm_error(vm, "Stack overflow");
                    return;
                }
                vm->stack[vm->stack_pointer++] = result;
                break;
            }

            /* Try callable non-function types (keyword, map, vector) */
            if (!is_function(fn)) {
                Value* args = &vm->stack[vm->stack_pointer - 1 - n_args];
                Value result = VALUE_NIL;
                if (vm_invoke_value(vm, fn, (int)n_args, args, &result)) {
                    if (vm->error) return;
                    /* Pop args and head from stack */
                    for (int i = 0; i < (int)n_args + 1; i++) {
                        vm_pop(vm);
                    }
                    /* Transfer owned ref to stack */
                    if (vm->stack_pointer >= vm->stack_size) {
                        if (is_pointer(result)) object_release(result);
                        vm_error(vm, "Stack overflow");
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

            /* Check arity */
            int arity = function_arity(fn);
            if (arity >= 0 && arity != (int)n_args) {
                vm_error(vm, "CALL: wrong number of arguments");
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
                vm->stack[args_base + required] = rest_list;
                /* Retain for stack ownership (rest_list has refcount=1 from cons creation) */
                if (is_pointer(rest_list)) object_retain(rest_list);
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
            push_frame(vm, fn, vm->pc);

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
                vm->code_size = function_get_code_size(fn);
                vm->constants = function_get_constants(fn);
                vm->num_constants = function_get_num_constants(fn);
            }

            /* Jump to function code */
            vm->pc = function_code_offset(fn);
            break;
        }

        case OP_TAIL_CALL: {
            /* Tail call - reuse current frame */
            uint16_t n_args = read_uint16(vm);
            if (vm->error) {
                return;
            }

            /* Function is on top of stack, args below it */
            /* Stack layout: [... | arg0 | arg1 | ... | argN | func] */
            if (vm->stack_pointer < (int)n_args + 1) {
                vm_error(vm, "TAIL_CALL: not enough values on stack");
                return;
            }

            Value fn = vm->stack[vm->stack_pointer - 1];
            uint32_t target = is_function(fn) ? function_code_offset(fn) : 0;
            LOG_TRACE("PC=%04u OP=%02X (OP_TAIL_CALL) n_args=%u target=%u sp=%d",
                      pc_at_opcode, op, n_args, target, vm->stack_pointer);

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
                    vm->pc = pc_at_opcode;
                    break;
                }

                /* Native requested a catchable throw */
                if (vm->native_throw) {
                    vm->native_throw = false;
                    if (is_pointer(result)) object_release(result);
                    if (vm->handler_count <= 0) {
                        vm_error(vm, "Unhandled exception");
                        return;
                    }
                    ExceptionHandler* h = &vm->handlers[--vm->handler_count];
                    while (vm->frame_count > h->frame_count) {
                        CallFrame* fr = pop_frame(vm);
                        if (!fr) break;
                        while (vm->stack_pointer > fr->base_pointer) vm_pop(vm);
                        vm->code = fr->caller_code;
                        vm->code_size = fr->caller_code_size;
                        vm->constants = fr->caller_constants;
                        vm->num_constants = fr->caller_num_constants;
                    }
                    while (vm->stack_pointer > h->stack_pointer) vm_pop(vm);
                    vm->pc = h->catch_pc;
                    break;
                }

                /* Pop args and function */
                for (int i = 0; i < (int)n_args + 1; i++) {
                    vm_pop(vm);
                }

                /* Transfer native's owned ref directly to stack (no extra retain). */
                if (vm->stack_pointer >= vm->stack_size) {
                    if (is_pointer(result)) object_release(result);
                    vm_error(vm, "Stack overflow");
                    return;
                }
                vm->stack[vm->stack_pointer++] = result;
                break;
            }

            /* Try callable non-function types (keyword, map, vector) */
            if (!is_function(fn)) {
                Value* args = &vm->stack[vm->stack_pointer - 1 - n_args];
                Value result = VALUE_NIL;
                if (vm_invoke_value(vm, fn, (int)n_args, args, &result)) {
                    if (vm->error) return;
                    for (int i = 0; i < (int)n_args + 1; i++) {
                        vm_pop(vm);
                    }
                    if (vm->stack_pointer >= vm->stack_size) {
                        if (is_pointer(result)) object_release(result);
                        vm_error(vm, "Stack overflow");
                        return;
                    }
                    vm->stack[vm->stack_pointer++] = result;
                    break;
                }
                vm_error(vm, "TAIL_CALL: not a function");
                return;
            }

            /* Check arity */
            int arity = function_arity(fn);
            if (arity >= 0 && arity != (int)n_args) {
                vm_error(vm, "TAIL_CALL: wrong number of arguments");
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

                /* Rewrite stack: replace extra args with rest_list, move func */
                vm->stack[args_base + required] = rest_list;
                /* Retain for stack ownership (rest_list has refcount=1 from cons creation) */
                if (is_pointer(rest_list)) object_retain(rest_list);
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

            /* Release extra locals beyond params (e.g. self-ref slot in named fn) */
            for (int i = frame_base + (int)n_args; i < args_start; i++) {
                if (is_pointer(vm->stack[i])) {
                    object_release(vm->stack[i]);
                }
            }

            /* Copy new arguments over old frame (skip function) */
            for (int i = 0; i < (int)n_args; i++) {
                Value v = vm->stack[args_start + i];
                /* Release old value at target position */
                if (frame_base + i < vm->stack_pointer) {
                    if (is_pointer(vm->stack[frame_base + i])) {
                        object_release(vm->stack[frame_base + i]);
                    }
                }
                vm->stack[frame_base + i] = v;
                /* Don't retain - we're moving the reference */
            }

            /* Adjust stack pointer (just the args, no function) */
            vm->stack_pointer = frame_base + n_args;

            /* Update frame function (release old, retain new) */
            if (is_pointer(frame->function)) {
                object_release(frame->function);
            }
            frame->function = fn;
            if (is_pointer(fn)) {
                object_retain(fn);
            }

            /* Switch to function's bytecode and constants if available */
            if (function_get_code(fn) != NULL) {
                vm->code = function_get_code(fn);
                vm->code_size = function_get_code_size(fn);
                vm->constants = function_get_constants(fn);
                vm->num_constants = function_get_num_constants(fn);
            }

            /* Jump to new function */
            vm->pc = function_code_offset(fn);
            break;
        }

        case OP_RETURN: {
            /* Return from function */
            CallFrame* frame = current_frame(vm);
            uint32_t return_pc = frame ? frame->return_pc : 0;
            LOG_TRACE("PC=%04u OP=%02X (OP_RETURN) return_pc=%u frame_count=%d sp=%d",
                      pc_at_opcode, op, return_pc, vm->frame_count, vm->stack_pointer);

            frame = pop_frame(vm);
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
            vm->code_size = frame->caller_code_size;
            vm->constants = frame->caller_constants;
            vm->num_constants = frame->caller_num_constants;

            /* Restore PC */
            vm->pc = frame->return_pc;
            break;
        }

        /* Global variables */
        case OP_LOAD_VAR: {
            uint16_t const_idx = read_uint16(vm);
            LOG_TRACE("PC=%04u OP=%02X (OP_LOAD_VAR) const_idx=%u sp=%d", pc_at_opcode, op, const_idx, vm->stack_pointer);
            if (vm->error) {
                return;
            }

            /* Get symbol from constant pool */
            if (const_idx >= (uint16_t)vm->num_constants) {
                vm_error(vm, "LOAD_VAR: constant index out of bounds");
                return;
            }

            Value symbol = vm->constants[const_idx];

            if (!is_pointer(symbol) || object_type(symbol) != TYPE_SYMBOL) {
                vm_error(vm, "LOAD_VAR: constant is not a symbol");
                return;
            }

            /* Look up var in current namespace */
            if (!global_namespace_registry) {
                vm_error(vm, "LOAD_VAR: namespace system not initialized");
                return;
            }

            Namespace* ns = namespace_registry_current(global_namespace_registry);
            if (!ns) {
                vm_error(vm, "LOAD_VAR: no current namespace");
                return;
            }

            Var* var = NULL;

            if (symbol_has_namespace(symbol)) {
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

                /* Check aliases: current ns first, then function's home ns */
                Value alias_sym = symbol_intern(ns_name);
                const char* resolved = namespace_resolve_alias(ns, alias_sym);
                if (!resolved && vm->frame_count > 0) {
                    Value fn_val = vm->frames[vm->frame_count - 1].function;
                    if (is_function(fn_val)) {
                        const char* fn_ns = function_ns_name(fn_val);
                        if (fn_ns) {
                            Namespace* fn_home = namespace_registry_get(global_namespace_registry, fn_ns);
                            if (fn_home && fn_home != ns) {
                                resolved = namespace_resolve_alias(fn_home, alias_sym);
                            }
                        }
                    }
                }
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
                /* Unqualified: try current ns, then function's home ns, then beer.core */
                var = namespace_lookup(ns, symbol);
                if (!var && vm->frame_count > 0) {
                    /* Try function's defining namespace */
                    Value fn_val = vm->frames[vm->frame_count - 1].function;
                    if (is_function(fn_val)) {
                        const char* fn_ns = function_ns_name(fn_val);
                        if (fn_ns) {
                            Namespace* fn_home = namespace_registry_get(global_namespace_registry, fn_ns);
                            if (fn_home && fn_home != ns) {
                                var = namespace_lookup(fn_home, symbol);
                            }
                        }
                    }
                }
                if (!var) {
                    Namespace* core_ns = namespace_registry_get_core(global_namespace_registry);
                    if (core_ns && core_ns != ns) {
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

            Value value = var_get_value(var);
            vm_push(vm, value);
            break;
        }

        case OP_STORE_VAR: {
            uint16_t const_idx = read_uint16(vm);
            LOG_TRACE("PC=%04u OP=%02X (OP_STORE_VAR) const_idx=%u sp=%d", pc_at_opcode, op, const_idx, vm->stack_pointer);
            if (vm->error) {
                return;
            }

            /* Get symbol from constant pool */
            if (const_idx >= (uint16_t)vm->num_constants) {
                vm_error(vm, "STORE_VAR: constant index out of bounds");
                return;
            }

            Value symbol = vm->constants[const_idx];
            if (!is_pointer(symbol) || object_type(symbol) != TYPE_SYMBOL) {
                vm_error(vm, "STORE_VAR: constant is not a symbol");
                return;
            }

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

                Namespace* cur_ns = namespace_registry_current(global_namespace_registry);
                Value alias_sym = symbol_intern(ns_name_buf);
                const char* resolved = cur_ns ? namespace_resolve_alias(cur_ns, alias_sym) : NULL;
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
                target_ns = namespace_registry_current(global_namespace_registry);
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
            uint16_t idx = read_uint16(vm);
            LOG_TRACE("PC=%04u OP=%02X (OP_LOAD_LOCAL) idx=%u sp=%d", pc_at_opcode, op, idx, vm->stack_pointer);
            if (vm->error) {
                return;
            }

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
            uint16_t idx = read_uint16(vm);
            LOG_TRACE("PC=%04u OP=%02X (OP_STORE_LOCAL) idx=%u sp=%d", pc_at_opcode, op, idx, vm->stack_pointer);
            if (vm->error) {
                return;
            }

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
            if (local_idx >= vm->stack_size) {
                if (is_pointer(val)) object_release(val);
                vm_error(vm, "STORE_LOCAL: index out of bounds");
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
            uint16_t idx = read_uint16(vm);
            LOG_TRACE("PC=%04u OP=%02X (OP_LOAD_CLOSURE) idx=%u sp=%d", pc_at_opcode, op, idx, vm->stack_pointer);
            if (vm->error) {
                return;
            }

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
            LOG_TRACE("PC=%04u OP=%02X (OP_LOAD_SELF) sp=%d", pc_at_opcode, op, vm->stack_pointer);
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
            uint32_t code_offset = read_uint32(vm);
            if (vm->error) return;

            uint16_t n_locals = read_uint16(vm);
            if (vm->error) return;

            uint16_t n_closed = read_uint16(vm);
            if (vm->error) return;

            int16_t arity = (int16_t)read_uint16(vm);
            if (vm->error) return;

            uint16_t name_idx = read_uint16(vm);
            if (vm->error) return;

            LOG_TRACE("PC=%04u OP=%02X (OP_MAKE_CLOSURE) code_offset=%u n_locals=%u n_closed=%u arity=%d name_idx=%u sp=%d",
                      pc_at_opcode, op, code_offset, n_locals, n_closed, arity, name_idx, vm->stack_pointer);

            /* Get function name from constants */
            const char* fn_name = "anonymous-fn";
            if (name_idx < (uint16_t)vm->num_constants) {
                Value name_val = vm->constants[name_idx];
                if (is_pointer(name_val) && object_type(name_val) == TYPE_STRING) {
                    fn_name = string_cstr(name_val);
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
                free(closed_values);
            }

            if (is_nil(closure)) {
                vm_error(vm, "MAKE_CLOSURE: failed to create closure");
                return;
            }

            /* Make closure self-contained (store bytecode + constants pointers) */
            function_set_code(closure, vm->code, vm->code_size,
                              vm->constants, vm->num_constants);

            /* Inherit ns_name from enclosing function (if any) */
            if (vm->frame_count > 0) {
                Value enc_fn = vm->frames[vm->frame_count - 1].function;
                if (is_function(enc_fn)) {
                    const char* enc_ns = function_ns_name(enc_fn);
                    if (enc_ns) function_set_ns_name(closure, enc_ns);
                }
            }

            vm_push(vm, closure);
            break;
        }

        /* Exception handling */
        case OP_PUSH_HANDLER: {
            uint32_t catch_offset = read_uint32(vm);
            LOG_TRACE("PC=%04u OP=%02X (OP_PUSH_HANDLER) catch_offset=%u sp=%d",
                      pc_at_opcode, op, catch_offset, vm->stack_pointer);
            if (vm->error) return;

            if (vm->handler_count >= vm->handler_capacity) {
                vm_error(vm, "Exception handler stack overflow");
                return;
            }

            ExceptionHandler* h = &vm->handlers[vm->handler_count++];
            h->catch_pc = catch_offset;
            h->stack_pointer = vm->stack_pointer;
            h->frame_count = vm->frame_count;
            break;
        }

        case OP_POP_HANDLER: {
            LOG_TRACE("PC=%04u OP=%02X (OP_POP_HANDLER) sp=%d", pc_at_opcode, op, vm->stack_pointer);
            if (vm->handler_count <= 0) {
                vm_error(vm, "No exception handler to pop");
                return;
            }
            vm->handler_count--;
            break;
        }

        case OP_THROW: {
            LOG_TRACE("PC=%04u OP=%02X (OP_THROW) sp=%d", pc_at_opcode, op, vm->stack_pointer);
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

            if (vm->handler_count <= 0) {
                /* No handler — set error state */
                /* Release the extra retain */
                if (is_pointer(exc)) object_release(exc);
                vm_error(vm, "Unhandled exception");
                return;
            }

            ExceptionHandler* h = &vm->handlers[--vm->handler_count];

            /* Unwind frames */
            while (vm->frame_count > h->frame_count) {
                CallFrame* frame = pop_frame(vm);
                if (!frame) break;
                /* Clean up stack for this frame (locals and args) */
                while (vm->stack_pointer > frame->base_pointer) {
                    vm_pop(vm);
                }
                /* Restore caller context */
                vm->code = frame->caller_code;
                vm->code_size = frame->caller_code_size;
                vm->constants = frame->caller_constants;
                vm->num_constants = frame->caller_num_constants;
            }

            /* Clean up remaining stack down to handler's SP */
            while (vm->stack_pointer > h->stack_pointer) {
                vm_pop(vm);
            }

            /* Store exception for OP_LOAD_EXCEPTION */
            if (is_pointer(vm->exception)) object_release(vm->exception);
            vm->exception = exc;

            /* Jump to catch block */
            vm->pc = h->catch_pc;
            break;
        }

        case OP_LOAD_EXCEPTION: {
            LOG_TRACE("PC=%04u OP=%02X (OP_LOAD_EXCEPTION) sp=%d", pc_at_opcode, op, vm->stack_pointer);
            vm_push(vm, vm->exception);
            break;
        }

        /* Concurrency operations */
        case OP_YIELD: {
            LOG_TRACE("PC=%04u OP=%02X (OP_YIELD) sp=%d", pc_at_opcode, op, vm->stack_pointer);
            vm->yielded = true;
            vm_push(vm, VALUE_NIL);
            break;
        }

        case OP_SPAWN: {
            uint16_t n_args = read_uint16(vm);
            LOG_TRACE("PC=%04u OP=%02X (OP_SPAWN) n_args=%u sp=%d", pc_at_opcode, op, n_args, vm->stack_pointer);
            if (vm->error) return;

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
            LOG_TRACE("PC=%04u OP=%02X (OP_AWAIT) sp=%d", pc_at_opcode, op, vm->stack_pointer);
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
                vm->pc = pc_at_opcode;
                vm->yielded = true;
            }
            break;
        }

        case OP_CHAN_SEND: {
            LOG_TRACE("PC=%04u OP=%02X (OP_CHAN_SEND) sp=%d", pc_at_opcode, op, vm->stack_pointer);
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
                vm->pc = pc_at_opcode;
                vm->yielded = true;
            }
            break;
        }

        case OP_CHAN_RECV: {
            LOG_TRACE("PC=%04u OP=%02X (OP_CHAN_RECV) sp=%d", pc_at_opcode, op, vm->stack_pointer);
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
                vm->pc = pc_at_opcode;
                vm->yielded = true;
            }
            break;
        }

        case OP_HALT:
            LOG_TRACE("PC=%04u OP=%02X (OP_HALT) sp=%d", pc_at_opcode, op, vm->stack_pointer);
            vm->running = false;
            break;

        default:
            LOG_TRACE("PC=%04u OP=%02X (INVALID) sp=%d", pc_at_opcode, op, vm->stack_pointer);
            vm_error(vm, "Invalid opcode");
            break;
    }
}

/* Run VM until halt or yield */
void vm_run(VM* vm) {
    vm->running = true;
    vm->error = false;
    vm->yielded = false;
    /* Don't reset pc - allow caller to set starting position */

    while (vm->running && !vm->error && !vm->yielded) {
        vm_step(vm);

        /* Auto-yield countdown */
        if (vm->yield_countdown > 0) {
            vm->yield_countdown--;
            if (vm->yield_countdown == 0 && vm->running && !vm->error) {
                vm->yielded = true;
            }
        }
    }

    if (vm->error) {
        fprintf(stderr, "VM Error: %s\n", vm->error_msg ? vm->error_msg : "Unknown error");
    }
}
