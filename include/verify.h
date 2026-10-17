/* Bytecode verifier
 *
 * Checks a code unit once, before it runs, for what the interpreter no
 * longer checks per instruction in release builds:
 *   - every opcode is one the VM executes, and its operands fit in the code
 *   - jump, catch, closure and function entry targets are instruction starts
 *   - the last instruction can't fall through past the end
 *   - constant indices are in range; LOAD_VAR/STORE_VAR name a symbol
 * Stack depth and local indices are still checked at run time.
 */

#ifndef BEERLANG_VERIFY_H
#define BEERLANG_VERIFY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "value.h"

/* True if the unit is safe to run; otherwise writes the reason to err */
bool bytecode_verify(const uint8_t* code, int code_size,
                     const Value* constants, int num_constants,
                     char* err, size_t err_len);

/* Extra entry point to check (e.g. an asm function's offset 0); true if
 * offset is an instruction start of a verified unit */
bool bytecode_is_instruction_start(const uint8_t* code, int code_size, uint32_t offset);

#endif
