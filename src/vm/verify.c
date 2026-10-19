/* Bytecode verifier — see verify.h */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "verify.h"
#include "vm.h"
#include "disasm.h"
#include "function.h"
#include "memory.h"

/* Opcodes the VM's dispatch switch handles. The disasm table also lists
 * OVER/INC/DEC, which have no handler. */
static bool executable(uint8_t op) {
    switch (op) {
        case OP_NOP: case OP_POP: case OP_DUP: case OP_SWAP:
        case OP_PUSH_NIL: case OP_PUSH_TRUE: case OP_PUSH_FALSE:
        case OP_PUSH_INT: case OP_PUSH_CONST:
        case OP_ADD: case OP_SUB: case OP_MUL: case OP_DIV: case OP_NEG:
        case OP_EQ: case OP_LT: case OP_GT:
        case OP_JUMP: case OP_JUMP_IF_FALSE:
        case OP_ENTER: case OP_CALL: case OP_TAIL_CALL: case OP_RETURN:
        case OP_LOAD_VAR: case OP_STORE_VAR:
        case OP_LOAD_LOCAL: case OP_STORE_LOCAL:
        case OP_LOAD_CLOSURE: case OP_LOAD_SELF: case OP_MAKE_CLOSURE:
        case OP_PUSH_HANDLER: case OP_PUSH_FINALLY: case OP_PUSH_INTERRUPT_CATCH: case OP_POP_HANDLER: case OP_THROW: case OP_LOAD_EXCEPTION:
        case OP_YIELD: case OP_SPAWN: case OP_AWAIT:
        case OP_CHAN_SEND: case OP_CHAN_RECV:
        case OP_HALT:
            return true;
        default:
            return false;
    }
}

/* Instructions after which execution never continues at the next byte */
static bool no_fallthrough(uint8_t op) {
    return op == OP_HALT || op == OP_RETURN || op == OP_JUMP || op == OP_THROW;
}

static uint16_t u16_at(const uint8_t* p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t u32_at(const uint8_t* p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

/* Linear sweep marking instruction starts; false at the first bad opcode or
 * truncated operand */
static bool mark_starts(const uint8_t* code, int code_size, uint8_t* starts,
                        char* err, size_t err_len) {
    int pc = 0;
    while (pc < code_size) {
        uint8_t op = code[pc];
        const OpcodeInfo* info = opcode_info_by_value(op);
        if (!info || !executable(op)) {
            snprintf(err, err_len, "invalid opcode 0x%02x at offset %d", op, pc);
            return false;
        }
        if (pc + info->total_size > code_size) {
            snprintf(err, err_len, "%s at offset %d is cut off by the end of the code",
                     info->name, pc);
            return false;
        }
        starts[pc] = 1;
        pc += info->total_size;
    }
    return true;
}

bool bytecode_is_instruction_start(const uint8_t* code, int code_size, uint32_t offset) {
    if (!code || code_size <= 0 || offset >= (uint32_t)code_size) return false;
    uint8_t* starts = xcalloc((size_t)code_size, 1);
    char err[8];
    bool ok = mark_starts(code, code_size, starts, err, sizeof(err)) && starts[offset];
    free(starts);
    return ok;
}

bool bytecode_verify(const uint8_t* code, int code_size,
                     const Value* constants, int num_constants,
                     char* err, size_t err_len) {
    if (!code || code_size <= 0) {
        snprintf(err, err_len, "empty code");
        return false;
    }
    uint8_t* starts = xcalloc((size_t)code_size, 1);
    if (!mark_starts(code, code_size, starts, err, err_len)) {
        free(starts);
        return false;
    }

    bool ok = true;
    int last = 0;
    for (int pc = 0; pc < code_size && ok; ) {
        uint8_t op = code[pc];
        int size = opcode_info_by_value(op)->total_size;
        const uint8_t* operand = code + pc + 1;
        last = pc;

        long target = -1;          /* a branch target to check, if any */
        const char* what = NULL;
        switch (op) {
            case OP_JUMP:
            case OP_JUMP_IF_FALSE:
                target = (long)pc + size + (int32_t)u32_at(operand);
                what = "jump target";
                break;
            case OP_PUSH_HANDLER:
            case OP_PUSH_FINALLY:
            case OP_PUSH_INTERRUPT_CATCH:
                target = (long)u32_at(operand);
                what = "catch target";
                break;
            case OP_MAKE_CLOSURE: {
                target = (long)u32_at(operand);
                what = "closure entry";
                uint16_t name_idx = u16_at(operand + 10);
                if (name_idx != 0xFFFF && name_idx >= (uint16_t)num_constants) {
                    snprintf(err, err_len, "MAKE_CLOSURE at offset %d: name constant %u out of range (%d constants)",
                             pc, name_idx, num_constants);
                    ok = false;
                }
                break;
            }
            case OP_PUSH_CONST: {
                uint32_t idx = u32_at(operand);
                if (idx >= (uint32_t)num_constants) {
                    snprintf(err, err_len, "PUSH_CONST at offset %d: constant %u out of range (%d constants)",
                             pc, idx, num_constants);
                    ok = false;
                }
                break;
            }
            case OP_LOAD_VAR:
            case OP_STORE_VAR: {
                uint16_t idx = u16_at(operand);
                if (idx >= (uint16_t)num_constants || num_constants <= 0) {
                    snprintf(err, err_len, "%s at offset %d: constant %u out of range (%d constants)",
                             op == OP_LOAD_VAR ? "LOAD_VAR" : "STORE_VAR", pc, idx, num_constants);
                    ok = false;
                } else if (!is_pointer(constants[idx]) || object_type(constants[idx]) != TYPE_SYMBOL) {
                    snprintf(err, err_len, "%s at offset %d: constant %u is not a symbol",
                             op == OP_LOAD_VAR ? "LOAD_VAR" : "STORE_VAR", pc, idx);
                    ok = false;
                }
                break;
            }
            default:
                break;
        }
        if (ok && target >= 0 && !(target < code_size && starts[target])) {
            snprintf(err, err_len, "%s %ld (from offset %d) is not the start of an instruction",
                     what, target, pc);
            ok = false;
        } else if (ok && what && target < 0) {
            snprintf(err, err_len, "%s %ld (from offset %d) is before the start of the code",
                     what, target, pc);
            ok = false;
        }
        pc += size;
    }

    if (ok && !no_fallthrough(code[last])) {
        snprintf(err, err_len, "the last instruction (offset %d) falls through past the end of the code",
                 last);
        ok = false;
    }

    /* Functions defined in this unit: their entries must be instruction starts */
    for (int i = 0; ok && i < num_constants; i++) {
        if (!is_function(constants[i])) continue;
        const Function* f = (const Function*)untag_pointer(constants[i]);
        if (f->code != NULL && f->code != code) continue;   /* another unit's */
        uint16_t n = f->n_arities ? f->n_arities : 1;
        for (uint16_t a = 0; ok && a < n; a++) {
            uint32_t entry = f->n_arities ? f->arities[a].code_offset : f->code_offset;
            if (entry >= (uint32_t)code_size || !starts[entry]) {
                snprintf(err, err_len, "function %s: entry %u is not the start of an instruction",
                         f->name, entry);
                ok = false;
            }
        }
    }

    free(starts);
    return ok;
}
