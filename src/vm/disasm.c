/* Bytecode Disassembler Implementation */

#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>
#include "disasm.h"
#include "vm.h"

/* Helper to read int64 (8 bytes, little endian) */
static int64_t read_int64(const uint8_t* code, size_t offset, size_t code_size) {
    if (offset + 8 > code_size) {
        return 0;
    }
    int64_t value = 0;
    for (int i = 0; i < 8; i++) {
        value |= ((int64_t)code[offset + i]) << (i * 8);
    }
    return value;
}

/* Helper to read uint32 (4 bytes, little endian) */
static uint32_t read_uint32(const uint8_t* code, size_t offset, size_t code_size) {
    if (offset + 4 > code_size) {
        return 0;
    }
    uint32_t value = 0;
    for (int i = 0; i < 4; i++) {
        value |= ((uint32_t)code[offset + i]) << (i * 8);
    }
    return value;
}

/* Helper to read int32 (4 bytes, little endian) */
static int32_t read_int32(const uint8_t* code, size_t offset, size_t code_size) {
    return (int32_t)read_uint32(code, offset, code_size);
}

/* Helper to read uint16 (2 bytes, little endian) */
static uint16_t read_uint16(const uint8_t* code, size_t offset, size_t code_size) {
    if (offset + 2 > code_size) {
        return 0;
    }
    uint16_t value = 0;
    for (int i = 0; i < 2; i++) {
        value |= ((uint16_t)code[offset + i]) << (i * 8);
    }
    return value;
}

/* Simple instruction (no operands) */
static size_t simple_instruction(FILE* fp, const char* name, size_t offset) {
    fprintf(fp, "%s\n", name);
    return offset + 1;
}

/* Instruction with uint16 operand */
static size_t uint16_instruction(FILE* fp, const char* name, const uint8_t* code,
                                  size_t offset, size_t code_size) {
    if (offset + 3 > code_size) {
        fprintf(fp, "%s <incomplete>\n", name);
        return code_size;
    }
    uint16_t operand = read_uint16(code, offset + 1, code_size);
    fprintf(fp, "%-16s %u\n", name, operand);
    return offset + 3;
}

/* Instruction with int32 operand */
static size_t int32_instruction(FILE* fp, const char* name, const uint8_t* code,
                                 size_t offset, size_t code_size) {
    if (offset + 5 > code_size) {
        fprintf(fp, "%s <incomplete>\n", name);
        return code_size;
    }
    int32_t operand = read_int32(code, offset + 1, code_size);
    fprintf(fp, "%-16s %d\n", name, operand);
    return offset + 5;
}

/* Instruction with uint32 operand */
static size_t uint32_instruction(FILE* fp, const char* name, const uint8_t* code,
                                  size_t offset, size_t code_size) {
    if (offset + 5 > code_size) {
        fprintf(fp, "%s <incomplete>\n", name);
        return code_size;
    }
    uint32_t operand = read_uint32(code, offset + 1, code_size);
    fprintf(fp, "%-16s %u\n", name, operand);
    return offset + 5;
}

/* Instruction with int64 operand */
static size_t int64_instruction(FILE* fp, const char* name, const uint8_t* code,
                                 size_t offset, size_t code_size) {
    if (offset + 9 > code_size) {
        fprintf(fp, "%s <incomplete>\n", name);
        return code_size;
    }
    int64_t operand = read_int64(code, offset + 1, code_size);
    fprintf(fp, "%-16s %" PRId64 "\n", name, operand);
    return offset + 9;
}

/* MAKE_CLOSURE instruction (uint32 + uint16 + uint16) */
static size_t make_closure_instruction(FILE* fp, const uint8_t* code,
                                        size_t offset, size_t code_size) {
    if (offset + 9 > code_size) {
        fprintf(fp, "MAKE_CLOSURE <incomplete>\n");
        return code_size;
    }
    uint32_t code_offset = read_uint32(code, offset + 1, code_size);
    uint16_t n_locals = read_uint16(code, offset + 5, code_size);
    uint16_t n_closed = read_uint16(code, offset + 7, code_size);
    fprintf(fp, "%-16s offset=%u n_locals=%u n_closed=%u\n",
            "MAKE_CLOSURE", code_offset, n_locals, n_closed);
    return offset + 9;
}

/* Disassemble single instruction to file */
size_t disassemble_instruction_to_file(FILE* fp, const uint8_t* code, size_t offset, size_t code_size) {
    if (offset >= code_size) {
        return code_size;
    }

    /* Print offset and opcode byte */
    fprintf(fp, "%04zx  %02x  ", offset, code[offset]);

    uint8_t opcode = code[offset];

    switch (opcode) {
        /* Stack operations */
        case OP_NOP:         return simple_instruction(fp, "NOP", offset);
        case OP_POP:         return simple_instruction(fp, "POP", offset);
        case OP_DUP:         return simple_instruction(fp, "DUP", offset);
        case OP_SWAP:        return simple_instruction(fp, "SWAP", offset);
        case OP_OVER:        return simple_instruction(fp, "OVER", offset);

        /* Constants & Literals */
        case OP_PUSH_NIL:    return simple_instruction(fp, "PUSH_NIL", offset);
        case OP_PUSH_TRUE:   return simple_instruction(fp, "PUSH_TRUE", offset);
        case OP_PUSH_FALSE:  return simple_instruction(fp, "PUSH_FALSE", offset);
        case OP_PUSH_CONST:  return uint32_instruction(fp, "PUSH_CONST", code, offset, code_size);
        case OP_PUSH_INT:    return int64_instruction(fp, "PUSH_INT", code, offset, code_size);

        /* Arithmetic */
        case OP_ADD:         return simple_instruction(fp, "ADD", offset);
        case OP_SUB:         return simple_instruction(fp, "SUB", offset);
        case OP_MUL:         return simple_instruction(fp, "MUL", offset);
        case OP_DIV:         return simple_instruction(fp, "DIV", offset);
        case OP_NEG:         return simple_instruction(fp, "NEG", offset);
        case OP_INC:         return simple_instruction(fp, "INC", offset);
        case OP_DEC:         return simple_instruction(fp, "DEC", offset);

        /* Comparison */
        case OP_EQ:          return simple_instruction(fp, "EQ", offset);
        case OP_LT:          return simple_instruction(fp, "LT", offset);
        case OP_GT:          return simple_instruction(fp, "GT", offset);

        /* Variables & Scope */
        case OP_LOAD_VAR:    return uint16_instruction(fp, "LOAD_VAR", code, offset, code_size);
        case OP_STORE_VAR:   return uint16_instruction(fp, "STORE_VAR", code, offset, code_size);
        case OP_LOAD_LOCAL:  return uint16_instruction(fp, "LOAD_LOCAL", code, offset, code_size);
        case OP_STORE_LOCAL: return uint16_instruction(fp, "STORE_LOCAL", code, offset, code_size);
        case OP_LOAD_CLOSURE:return uint16_instruction(fp, "LOAD_CLOSURE", code, offset, code_size);
        case OP_LOAD_SELF:   return simple_instruction(fp, "LOAD_SELF", offset);

        /* Control flow */
        case OP_JUMP:        return int32_instruction(fp, "JUMP", code, offset, code_size);
        case OP_JUMP_IF_FALSE: return int32_instruction(fp, "JUMP_IF_FALSE", code, offset, code_size);
        case OP_CALL:        return uint16_instruction(fp, "CALL", code, offset, code_size);
        case OP_TAIL_CALL:   return uint16_instruction(fp, "TAIL_CALL", code, offset, code_size);
        case OP_RETURN:      return simple_instruction(fp, "RETURN", offset);
        case OP_ENTER:       return uint16_instruction(fp, "ENTER", code, offset, code_size);
        case OP_HALT:        return simple_instruction(fp, "HALT", offset);

        /* Exception handling */
        case OP_PUSH_HANDLER: return uint32_instruction(fp, "PUSH_HANDLER", code, offset, code_size);
        case OP_POP_HANDLER:  return simple_instruction(fp, "POP_HANDLER", offset);
        case OP_THROW:        return simple_instruction(fp, "THROW", offset);
        case OP_LOAD_EXCEPTION: return simple_instruction(fp, "LOAD_EXCEPTION", offset);

        /* Functions & Closures */
        case OP_MAKE_CLOSURE: return make_closure_instruction(fp, code, offset, code_size);

        default:
            fprintf(fp, "UNKNOWN (0x%02x)\n", opcode);
            return offset + 1;
    }
}

/* Disassemble single instruction to stdout */
size_t disassemble_instruction(const uint8_t* code, size_t offset, size_t code_size) {
    return disassemble_instruction_to_file(stdout, code, offset, code_size);
}

/* Disassemble entire bytecode chunk to file */
void disassemble_code_to_file(FILE* fp, const uint8_t* code, size_t code_size, const char* name) {
    if (name) {
        fprintf(fp, "== %s ==\n", name);
    }

    size_t offset = 0;
    while (offset < code_size) {
        offset = disassemble_instruction_to_file(fp, code, offset, code_size);
    }
}

/* Disassemble entire bytecode chunk to stdout */
void disassemble_code(const uint8_t* code, size_t code_size, const char* name) {
    disassemble_code_to_file(stdout, code, code_size, name);
}
