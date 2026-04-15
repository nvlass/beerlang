/* Core Native Functions
 *
 * Essential native functions provided by the beerlang runtime.
 */

#ifndef BEERLANG_CORE_H
#define BEERLANG_CORE_H

/* Register arithmetic operations (+, -, *, /) in the 'user' namespace */
void core_register_arithmetic(void);

/* Register I/O functions (print, println) in the 'user' namespace */
void core_register_io(void);

/* Register comparison functions (=, <, >) in the 'user' namespace */
void core_register_comparison(void);

/* Register collection functions (list, vector, hash-map, cons, first, rest, etc.) */
void core_register_collections(void);

/* Register type predicates (nil?, number?, string?, etc.) */
void core_register_predicates(void);

/* Register utility functions (not, str, type, apply) */
void core_register_utility(void);

/* Register stream/I/O functions (open, close, read-line, slurp, spit, etc.) */
void core_register_streams(void);

/* Register concurrency functions (chan, >!, <!, close!, channel?, task?) */
void core_register_concurrency(void);

/* Register beer.tar namespace (tar/list, tar/read-entry, tar/create) */
void core_register_tar(void);

/* Register beer.shell namespace (shell/exec) */
void core_register_shell(void);

/* Register beer.tcp namespace (tcp/listen, tcp/accept, etc.) */
void core_register_tcp(void);

/* Register atom functions (atom, deref, reset!, swap!, compare-and-set!, atom?) */
void core_register_atoms(void);

/* Register metadata functions (meta, with-meta, alter-meta!, __print-doc) */
void core_register_metadata(void);

/* Register core macros (defn, when, and, or) - compiled from beerlang source */
void core_register_macros(void);

/* Register bitwise operations (bit-and, bit-or, bit-xor, bit-not, bit-shift-left,
 * bit-shift-right) and character utilities (char, char-code) in beer.core */
void core_register_bits(void);

/* Register crypto primitives (sha256, hmac-sha256, constant-time-eq?, random-bytes)
 * in beer.crypto namespace */
void core_register_crypto(void);

/* Load and execute a beerlang source file */
#include "value.h"
#include "vm.h"
Value native_load(VM* vm, int argc, Value* argv);

#endif /* BEERLANG_CORE_H */
