/* beer.crypto namespace — cryptographic primitives
 *
 * Self-contained SHA-256 and HMAC-SHA256 (no external deps).
 * Registers in beer.crypto:
 *   sha256           (str) → hex-string
 *   hmac-sha256      (key-str msg-str) → hex-string
 *   constant-time-eq? (a b) → bool
 *   random-bytes     (n) → byte-string
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "native.h"
#include "vm.h"
#include "value.h"
#include "namespace.h"
#include "symbol.h"
#include "bstring.h"
#include "memory.h"
#include "core.h"
#include "vector.h"

extern NamespaceRegistry* global_namespace_registry;

static void register_native_in_ns(Namespace* ns, const char* name, NativeFn fn) {
    Value fn_val = native_function_new(-1, fn, name);
    Value sym = symbol_intern(name);
    namespace_define(ns, sym, fn_val);
    object_release(fn_val);
}

/* ================================================================
 * SHA-256  (FIPS 180-4, public domain)
 * ================================================================ */

typedef struct {
    uint32_t state[8];
    uint64_t count;      /* total bytes processed */
    uint8_t  buf[64];    /* partial block buffer */
} SHA256Ctx;

static const uint32_t SHA256_K[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
    0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
    0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
    0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
    0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
    0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
    0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
    0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
};

#define ROR32(x, n) (((x) >> (n)) | ((x) << (32u - (n))))
#define CH(x,y,z)  (((x) & (y)) ^ (~(x) & (z)))
#define MAJ(x,y,z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define SIG0(x)    (ROR32(x, 2)  ^ ROR32(x,13) ^ ROR32(x,22))
#define SIG1(x)    (ROR32(x, 6)  ^ ROR32(x,11) ^ ROR32(x,25))
#define sig0(x)    (ROR32(x, 7)  ^ ROR32(x,18) ^ ((x) >>  3))
#define sig1(x)    (ROR32(x,17)  ^ ROR32(x,19) ^ ((x) >> 10))

static void sha256_transform(SHA256Ctx* ctx, const uint8_t data[64]) {
    uint32_t W[64], a, b, c, d, e, f, g, h, T1, T2;
    int i;

    for (i = 0; i < 16; i++) {
        W[i] = ((uint32_t)data[i*4+0] << 24)
             | ((uint32_t)data[i*4+1] << 16)
             | ((uint32_t)data[i*4+2] <<  8)
             | ((uint32_t)data[i*4+3]);
    }
    for (i = 16; i < 64; i++) {
        W[i] = sig1(W[i-2]) + W[i-7] + sig0(W[i-15]) + W[i-16];
    }

    a = ctx->state[0]; b = ctx->state[1];
    c = ctx->state[2]; d = ctx->state[3];
    e = ctx->state[4]; f = ctx->state[5];
    g = ctx->state[6]; h = ctx->state[7];

    for (i = 0; i < 64; i++) {
        T1 = h + SIG1(e) + CH(e,f,g)  + SHA256_K[i] + W[i];
        T2 = SIG0(a) + MAJ(a,b,c);
        h = g; g = f; f = e; e = d + T1;
        d = c; c = b; b = a; a = T1 + T2;
    }

    ctx->state[0] += a; ctx->state[1] += b;
    ctx->state[2] += c; ctx->state[3] += d;
    ctx->state[4] += e; ctx->state[5] += f;
    ctx->state[6] += g; ctx->state[7] += h;
}

static void sha256_init(SHA256Ctx* ctx) {
    ctx->state[0] = 0x6a09e667u; ctx->state[1] = 0xbb67ae85u;
    ctx->state[2] = 0x3c6ef372u; ctx->state[3] = 0xa54ff53au;
    ctx->state[4] = 0x510e527fu; ctx->state[5] = 0x9b05688cu;
    ctx->state[6] = 0x1f83d9abu; ctx->state[7] = 0x5be0cd19u;
    ctx->count = 0;
    memset(ctx->buf, 0, sizeof(ctx->buf));
}

static void sha256_update(SHA256Ctx* ctx, const uint8_t* data, size_t len) {
    size_t buflen = (size_t)(ctx->count & 63u);
    ctx->count += (uint64_t)len;

    if (buflen > 0 && buflen + len >= 64) {
        size_t fill = 64 - buflen;
        memcpy(ctx->buf + buflen, data, fill);
        sha256_transform(ctx, ctx->buf);
        data += fill;
        len  -= fill;
        buflen = 0;
    }

    while (len >= 64) {
        sha256_transform(ctx, data);
        data += 64;
        len  -= 64;
    }

    if (len > 0) {
        memcpy(ctx->buf + buflen, data, len);
    }
}

static void sha256_final(SHA256Ctx* ctx, uint8_t digest[32]) {
    uint8_t  pad[64];
    uint64_t total_bits = ctx->count * 8u;
    size_t   buflen     = (size_t)(ctx->count & 63u);
    size_t   padlen     = (buflen < 56) ? (56 - buflen) : (120 - buflen);

    memset(pad, 0, sizeof(pad));
    pad[0] = 0x80;
    sha256_update(ctx, pad, padlen);

    uint8_t len_bytes[8];
    for (int i = 7; i >= 0; i--) {
        len_bytes[i] = (uint8_t)(total_bits & 0xFFu);
        total_bits >>= 8;
    }
    sha256_update(ctx, len_bytes, 8);

    for (int i = 0; i < 8; i++) {
        digest[i*4+0] = (uint8_t)(ctx->state[i] >> 24);
        digest[i*4+1] = (uint8_t)(ctx->state[i] >> 16);
        digest[i*4+2] = (uint8_t)(ctx->state[i] >>  8);
        digest[i*4+3] = (uint8_t)(ctx->state[i]);
    }
}

/* Compute SHA-256 of (data, len) into digest[32] */
static void sha256_hash(const uint8_t* data, size_t len, uint8_t digest[32]) {
    SHA256Ctx ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, data, len);
    sha256_final(&ctx, digest);
}

/* Format 32-byte digest as 64-char lowercase hex string */
static void bytes_to_hex(const uint8_t* bytes, size_t nbytes, char* out) {
    static const char HEX[] = "0123456789abcdef";
    for (size_t i = 0; i < nbytes; i++) {
        out[i*2+0] = HEX[(bytes[i] >> 4) & 0xF];
        out[i*2+1] = HEX[bytes[i] & 0xF];
    }
    out[nbytes*2] = '\0';
}

/* ================================================================
 * HMAC-SHA256
 * ================================================================ */

static void hmac_sha256(const uint8_t* key, size_t key_len,
                        const uint8_t* msg, size_t msg_len,
                        uint8_t digest[32]) {
    uint8_t k_pad[64];
    uint8_t inner[32];
    uint8_t ipad[64], opad[64];
    SHA256Ctx ctx;

    /* If key > block size, hash it first */
    if (key_len > 64) {
        sha256_hash(key, key_len, k_pad);
        memset(k_pad + 32, 0, 32);
    } else {
        memcpy(k_pad, key, key_len);
        memset(k_pad + key_len, 0, 64 - key_len);
    }

    for (int i = 0; i < 64; i++) {
        ipad[i] = k_pad[i] ^ 0x36u;
        opad[i] = k_pad[i] ^ 0x5Cu;
    }

    /* Inner: H(ipad || msg) */
    sha256_init(&ctx);
    sha256_update(&ctx, ipad, 64);
    sha256_update(&ctx, msg, msg_len);
    sha256_final(&ctx, inner);

    /* Outer: H(opad || inner) */
    sha256_init(&ctx);
    sha256_update(&ctx, opad, 64);
    sha256_update(&ctx, inner, 32);
    sha256_final(&ctx, digest);
}

/* ================================================================
 * Beerlang native functions
 * ================================================================ */

/* (sha256 str) → hex-string (64 chars) */
static Value native_sha256(VM* vm, int argc, Value* argv) {
    if (argc != 1) {
        vm_error(vm, "sha256: requires exactly 1 argument");
        return VALUE_NIL;
    }
    if (!is_string(argv[0])) {
        vm_error(vm, "sha256: argument must be a string");
        return VALUE_NIL;
    }
    const char* data = string_cstr(argv[0]);
    size_t      len  = string_byte_length(argv[0]);

    uint8_t digest[32];
    sha256_hash((const uint8_t*)data, len, digest);

    char hex[65];
    bytes_to_hex(digest, 32, hex);
    return string_from_buffer(hex, 64);
}

/* (hmac-sha256 key-str msg-str) → hex-string (64 chars) */
static Value native_hmac_sha256(VM* vm, int argc, Value* argv) {
    if (argc != 2) {
        vm_error(vm, "hmac-sha256: requires exactly 2 arguments (key message)");
        return VALUE_NIL;
    }
    if (!is_string(argv[0]) || !is_string(argv[1])) {
        vm_error(vm, "hmac-sha256: both arguments must be strings");
        return VALUE_NIL;
    }
    const char* key     = string_cstr(argv[0]);
    size_t      key_len = string_byte_length(argv[0]);
    const char* msg     = string_cstr(argv[1]);
    size_t      msg_len = string_byte_length(argv[1]);

    uint8_t digest[32];
    hmac_sha256((const uint8_t*)key, key_len,
                (const uint8_t*)msg, msg_len,
                digest);

    char hex[65];
    bytes_to_hex(digest, 32, hex);
    return string_from_buffer(hex, 64);
}

/* (constant-time-eq? a b) → bool
 * Compares two strings in constant time (prevents timing attacks).
 * Returns false if lengths differ (length itself is not secret here). */
static Value native_constant_time_eq(VM* vm, int argc, Value* argv) {
    (void)vm;
    if (argc != 2) return VALUE_FALSE;
    if (!is_string(argv[0]) || !is_string(argv[1])) return VALUE_FALSE;

    size_t la = string_byte_length(argv[0]);
    size_t lb = string_byte_length(argv[1]);

    if (la != lb) return VALUE_FALSE;

    const uint8_t* a = (const uint8_t*)string_cstr(argv[0]);
    const uint8_t* b = (const uint8_t*)string_cstr(argv[1]);
    uint8_t diff = 0;
    for (size_t i = 0; i < la; i++) {
        diff |= (a[i] ^ b[i]);
    }
    return diff == 0 ? VALUE_TRUE : VALUE_FALSE;
}

/* (random-bytes n) → string of n random bytes (from /dev/urandom) */
static Value native_random_bytes(VM* vm, int argc, Value* argv) {
    if (argc != 1) {
        vm_error(vm, "random-bytes: requires exactly 1 argument");
        return VALUE_NIL;
    }
    if (!is_fixnum(argv[0])) {
        vm_error(vm, "random-bytes: argument must be an integer");
        return VALUE_NIL;
    }
    int64_t n = untag_fixnum(argv[0]);
    if (n < 0 || n > 65536) {
        vm_error(vm, "random-bytes: n must be in [0, 65536]");
        return VALUE_NIL;
    }

    char* buf = malloc((size_t)n + 1);
    if (!buf) {
        vm_error(vm, "random-bytes: allocation failed");
        return VALUE_NIL;
    }

    FILE* f = fopen("/dev/urandom", "rb");
    if (!f) {
        free(buf);
        vm_error(vm, "random-bytes: could not open /dev/urandom");
        return VALUE_NIL;
    }
    size_t got = fread(buf, 1, (size_t)n, f);
    fclose(f);

    if ((int64_t)got != n) {
        free(buf);
        vm_error(vm, "random-bytes: short read from /dev/urandom");
        return VALUE_NIL;
    }

    /* Return a vector of fixnums (0-255), one per byte.
     * Using a vector avoids UTF-8 validation issues with raw binary data. */
    Value vec = vector_create((size_t)n);
    for (int64_t i = 0; i < n; i++) {
        vector_push(vec, make_fixnum((uint8_t)buf[i]));
    }
    free(buf);
    return vec;
}

void core_register_crypto(void) {
    Namespace* crypto_ns = namespace_registry_get_or_create(global_namespace_registry, "beer.crypto");
    if (!crypto_ns) return;

    register_native_in_ns(crypto_ns, "sha256",            native_sha256);
    register_native_in_ns(crypto_ns, "hmac-sha256",       native_hmac_sha256);
    register_native_in_ns(crypto_ns, "constant-time-eq?", native_constant_time_eq);
    register_native_in_ns(crypto_ns, "random-bytes",      native_random_bytes);
}
