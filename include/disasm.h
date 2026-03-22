/* Bytecode Disassembler */

#ifndef BEERLANG_DISASM_H
#define BEERLANG_DISASM_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* Disassemble entire bytecode chunk to stdout */
void disassemble_code(const uint8_t* code, size_t code_size, const char* name);

/* Disassemble entire bytecode chunk to file */
void disassemble_code_to_file(FILE* fp, const uint8_t* code, size_t code_size, const char* name);

/* Disassemble single instruction to stdout, returns bytes consumed */
size_t disassemble_instruction(const uint8_t* code, size_t offset, size_t code_size);

/* Disassemble single instruction to file, returns bytes consumed */
size_t disassemble_instruction_to_file(FILE* fp, const uint8_t* code, size_t offset, size_t code_size);

/* Opcode info for asm/disasm */
typedef struct {
    uint8_t opcode;
    const char* name;       /* "ENTER", "ADD", etc. */
    int total_size;         /* instruction size in bytes (opcode + operands) */
} OpcodeInfo;

/* Look up opcode info by name (e.g. "ENTER"). Returns NULL if not found. */
const OpcodeInfo* opcode_info_by_name(const char* name);

/* Look up opcode info by opcode value. Returns NULL if not found. */
const OpcodeInfo* opcode_info_by_value(uint8_t op);

#endif /* BEERLANG_DISASM_H */
