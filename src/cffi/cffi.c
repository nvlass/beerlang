#ifdef BEER_CFFI

#include <ffi.h>
#include <dlfcn.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include "value.h"
#include "memory.h"
#include "namespace.h"
#include "symbol.h"
#include "bstring.h"
#include "native.h"
#include "vm.h"
#include "vector.h"
#include "cpointer.h"

/* ===================================================================
 * Type descriptor helpers
 * =================================================================== */

static ffi_type* name_to_ffi_type(const char* n) {
    if (strcmp(n, "void")    == 0) return &ffi_type_void;
    if (strcmp(n, "bool")    == 0) return &ffi_type_sint32;
    if (strcmp(n, "int8")    == 0) return &ffi_type_sint8;
    if (strcmp(n, "uint8")   == 0) return &ffi_type_uint8;
    if (strcmp(n, "int16")   == 0) return &ffi_type_sint16;
    if (strcmp(n, "uint16")  == 0) return &ffi_type_uint16;
    if (strcmp(n, "int32")   == 0) return &ffi_type_sint32;
    if (strcmp(n, "uint32")  == 0) return &ffi_type_uint32;
    if (strcmp(n, "int64")   == 0) return &ffi_type_sint64;
    if (strcmp(n, "uint64")  == 0) return &ffi_type_uint64;
    if (strcmp(n, "float")   == 0) return &ffi_type_float;
    if (strcmp(n, "double")  == 0) return &ffi_type_double;
    if (strcmp(n, "pointer") == 0) return &ffi_type_pointer;
    if (strcmp(n, "string")  == 0) return &ffi_type_pointer;  /* char* */
    return NULL;
}

static ffi_type* desc_to_ffi_type(Value v) {
    if (is_pointer(v) && object_type(v) == TYPE_KEYWORD)
        return name_to_ffi_type(keyword_name(v));
    return NULL;
}

/* ===================================================================
 * Marshalling: beerlang Value → C buffer (always 8 bytes)
 *
 * For :string, a temporary null-terminated copy is made; the pointer
 * is recorded in temp_strs[(*nts)++] for cleanup after ffi_call.
 * =================================================================== */

static bool marshal_to_c(VM* vm, Value val, const char* tn,
                          void* buf, char** temp_strs, int* nts) {
    memset(buf, 0, 8);

    if (strcmp(tn, "bool") == 0) {
        int32_t b = is_true(val) ? 1 : 0;
        memcpy(buf, &b, sizeof b);
        return true;
    }
    if (strcmp(tn, "int8")  == 0 || strcmp(tn, "int16") == 0 ||
        strcmp(tn, "int32") == 0 || strcmp(tn, "int64") == 0) {
        int64_t n = is_fixnum(val) ? untag_fixnum(val)
                  : is_float(val)  ? (int64_t)untag_float(val) : 0;
        memcpy(buf, &n, sizeof n);
        return true;
    }
    if (strcmp(tn, "uint8")  == 0 || strcmp(tn, "uint16") == 0 ||
        strcmp(tn, "uint32") == 0 || strcmp(tn, "uint64") == 0) {
        uint64_t n = is_fixnum(val) ? (uint64_t)untag_fixnum(val)
                   : is_float(val)  ? (uint64_t)untag_float(val) : 0;
        memcpy(buf, &n, sizeof n);
        return true;
    }
    if (strcmp(tn, "float") == 0) {
        float f = is_float(val)  ? (float)untag_float(val)
                : is_fixnum(val) ? (float)untag_fixnum(val) : 0.0f;
        memcpy(buf, &f, sizeof f);
        return true;
    }
    if (strcmp(tn, "double") == 0) {
        double d = is_float(val)  ? untag_float(val)
                 : is_fixnum(val) ? (double)untag_fixnum(val) : 0.0;
        memcpy(buf, &d, sizeof d);
        return true;
    }
    if (strcmp(tn, "string") == 0) {
        char* s = NULL;
        if (!is_nil(val)) {
            if (!is_string(val)) {
                vm_error(vm, "ffi/call: :string arg must be a string or nil");
                return false;
            }
            s = strdup(string_cstr(val));
            if (!s) { vm_error(vm, "ffi/call: out of memory for string arg"); return false; }
            temp_strs[(*nts)++] = s;
        }
        memcpy(buf, &s, sizeof s);
        return true;
    }
    if (strcmp(tn, "pointer") == 0) {
        void* ptr = is_nil(val)      ? NULL
                  : is_cpointer(val) ? cpointer_get(val)
                  : NULL;
        if (!is_nil(val) && !is_cpointer(val)) {
            vm_error(vm, "ffi/call: :pointer arg must be a cpointer or nil");
            return false;
        }
        memcpy(buf, &ptr, sizeof ptr);
        return true;
    }
    vm_error(vm, "ffi/call: unknown arg type descriptor");
    return false;
}

/* ===================================================================
 * Marshalling: C return buffer → beerlang Value
 *
 * libffi stores integer return values in an ffi_arg-sized slot
 * (at least 8 bytes on 64-bit), sign/zero-extended.  We read 8 bytes
 * as uint64_t and then cast to the correct C type before boxing.
 * =================================================================== */

static Value marshal_from_c(const char* tn, const void* buf) {
    if (strcmp(tn, "void") == 0) return VALUE_NIL;

    uint64_t raw; memcpy(&raw, buf, sizeof raw);

    if (strcmp(tn, "bool")   == 0) return raw ? VALUE_TRUE : VALUE_FALSE;
    if (strcmp(tn, "int8")   == 0) return make_fixnum((int64_t)(int8_t)raw);
    if (strcmp(tn, "uint8")  == 0) return make_fixnum((int64_t)(uint8_t)raw);
    if (strcmp(tn, "int16")  == 0) return make_fixnum((int64_t)(int16_t)raw);
    if (strcmp(tn, "uint16") == 0) return make_fixnum((int64_t)(uint16_t)raw);
    if (strcmp(tn, "int32")  == 0) return make_fixnum((int64_t)(int32_t)raw);
    if (strcmp(tn, "uint32") == 0) return make_fixnum((int64_t)(uint32_t)raw);
    if (strcmp(tn, "int64")  == 0) return make_fixnum((int64_t)raw);
    if (strcmp(tn, "uint64") == 0) return make_fixnum((int64_t)raw);
    if (strcmp(tn, "float")  == 0) {
        float f; memcpy(&f, buf, sizeof f);
        return make_float((double)f);
    }
    if (strcmp(tn, "double") == 0) {
        double d; memcpy(&d, buf, sizeof d);
        return make_float(d);
    }
    if (strcmp(tn, "string") == 0) {
        void* ptr; memcpy(&ptr, buf, sizeof ptr);
        return ptr ? string_from_cstr((const char*)ptr) : VALUE_NIL;
    }
    if (strcmp(tn, "pointer") == 0) {
        void* ptr; memcpy(&ptr, buf, sizeof ptr);
        return ptr ? cpointer_new(ptr) : VALUE_NIL;
    }
    return VALUE_NIL;
}

/* ===================================================================
 * Natives
 * =================================================================== */

/* (ffi/open path) → cpointer */
static Value native_ffi_open(VM* vm, int argc, Value* argv) {
    if (argc != 1 || !is_string(argv[0])) {
        vm_error(vm, "ffi/open: expects 1 string argument (library path)");
        return VALUE_NIL;
    }
    dlerror();
    void* handle = dlopen(string_cstr(argv[0]), RTLD_LAZY | RTLD_GLOBAL);
    if (!handle) {
        char buf[512];
        snprintf(buf, sizeof buf, "ffi/open: %s", dlerror());
        vm_error(vm, buf);
        return VALUE_NIL;
    }
    return cpointer_new(handle);
}

/* (ffi/sym handle sym-name) → cpointer */
static Value native_ffi_sym(VM* vm, int argc, Value* argv) {
    if (argc != 2 || !is_cpointer(argv[0]) || !is_string(argv[1])) {
        vm_error(vm, "ffi/sym: expects (cpointer string)");
        return VALUE_NIL;
    }
    dlerror();
    void* sym = dlsym(cpointer_get(argv[0]), string_cstr(argv[1]));
    const char* err = dlerror();
    if (err) {
        char buf[512];
        snprintf(buf, sizeof buf, "ffi/sym: %s", err);
        vm_error(vm, buf);
        return VALUE_NIL;
    }
    return cpointer_new(sym);
}

/* (ffi/close handle) → nil */
static Value native_ffi_close(VM* vm, int argc, Value* argv) {
    if (argc != 1 || !is_cpointer(argv[0])) {
        vm_error(vm, "ffi/close: expects 1 cpointer argument");
        return VALUE_NIL;
    }
    dlclose(cpointer_get(argv[0]));
    return VALUE_NIL;
}

/* (ffi/call fn-ptr arg-types ret-type args) → value
 *
 *   fn-ptr    — cpointer to the C function
 *   arg-types — vector of type keywords, e.g. [:double :int32]
 *   ret-type  — type keyword, e.g. :double  (or :void)
 *   args      — vector of beerlang values matching arg-types
 */
static Value native_ffi_call(VM* vm, int argc, Value* argv) {
    if (argc != 4) {
        vm_error(vm, "ffi/call: expects (fn-ptr arg-types ret-type args)");
        return VALUE_NIL;
    }
    if (!is_cpointer(argv[0])) {
        vm_error(vm, "ffi/call: fn-ptr must be a cpointer"); return VALUE_NIL;
    }
    if (!is_vector(argv[1])) {
        vm_error(vm, "ffi/call: arg-types must be a vector"); return VALUE_NIL;
    }
    if (!is_pointer(argv[2]) || object_type(argv[2]) != TYPE_KEYWORD) {
        vm_error(vm, "ffi/call: ret-type must be a keyword"); return VALUE_NIL;
    }
    if (!is_vector(argv[3])) {
        vm_error(vm, "ffi/call: args must be a vector"); return VALUE_NIL;
    }

    int nargs = (int)vector_length(argv[1]);
    if ((int)vector_length(argv[3]) != nargs) {
        vm_error(vm, "ffi/call: arg-types and args must have the same length");
        return VALUE_NIL;
    }

    /* Build ffi_type* array */
    ffi_type** atypes = nargs ? malloc((size_t)nargs * sizeof(ffi_type*)) : NULL;
    if (nargs && !atypes) { vm_error(vm, "ffi/call: out of memory"); return VALUE_NIL; }

    for (int i = 0; i < nargs; i++) {
        Value td = vector_get(argv[1], (size_t)i);
        ffi_type* ft = desc_to_ffi_type(td);
        if (!ft) {
            char buf[128];
            snprintf(buf, sizeof buf, "ffi/call: unknown arg type at index %d", i);
            free(atypes);
            vm_error(vm, buf);
            return VALUE_NIL;
        }
        atypes[i] = ft;
    }

    const char* rtn = keyword_name(argv[2]);
    ffi_type* rtype = name_to_ffi_type(rtn);
    if (!rtype) {
        free(atypes);
        vm_error(vm, "ffi/call: unknown return type keyword");
        return VALUE_NIL;
    }

    ffi_cif cif;
    if (ffi_prep_cif(&cif, FFI_DEFAULT_ABI, (unsigned int)nargs, rtype, atypes) != FFI_OK) {
        free(atypes);
        vm_error(vm, "ffi/call: ffi_prep_cif failed");
        return VALUE_NIL;
    }

    /* Marshal arguments into flat 8-byte-per-slot store */
    uint8_t* astore = nargs ? calloc((size_t)nargs, 8) : NULL;
    void**   aptrs  = nargs ? malloc((size_t)nargs * sizeof(void*)) : NULL;
    char*    tstr[256];
    int      ntstr = 0;

    if (nargs && (!astore || !aptrs)) {
        free(atypes); free(astore); free(aptrs);
        vm_error(vm, "ffi/call: out of memory");
        return VALUE_NIL;
    }

    for (int i = 0; i < nargs; i++) {
        const char* tn = keyword_name(vector_get(argv[1], (size_t)i));
        Value val      = vector_get(argv[3], (size_t)i);
        void* slot     = astore + i * 8;
        if (!marshal_to_c(vm, val, tn, slot, tstr, &ntstr)) {
            for (int j = 0; j < ntstr; j++) free(tstr[j]);
            free(atypes); free(astore); free(aptrs);
            return VALUE_NIL;
        }
        aptrs[i] = slot;
    }

    /* Call — return buffer must be at least sizeof(ffi_arg) == 8 bytes */
    uint8_t rbuf[16] = {0};
    ffi_call(&cif, FFI_FN(cpointer_get(argv[0])), rbuf, nargs ? aptrs : NULL);

    for (int i = 0; i < ntstr; i++) free(tstr[i]);
    free(atypes); free(astore); free(aptrs);

    return marshal_from_c(rtn, rbuf);
}

/* (ffi/malloc nbytes) → cpointer (zero-initialised) */
static Value native_ffi_malloc(VM* vm, int argc, Value* argv) {
    if (argc != 1 || !is_fixnum(argv[0])) {
        vm_error(vm, "ffi/malloc: expects 1 integer (byte count)");
        return VALUE_NIL;
    }
    int64_t n = untag_fixnum(argv[0]);
    if (n <= 0) { vm_error(vm, "ffi/malloc: size must be positive"); return VALUE_NIL; }
    void* ptr = calloc(1, (size_t)n);
    if (!ptr) { vm_error(vm, "ffi/malloc: out of memory"); return VALUE_NIL; }
    return cpointer_new(ptr);
}

/* (ffi/free ptr) → nil */
static Value native_ffi_free(VM* vm, int argc, Value* argv) {
    if (argc != 1 || !is_cpointer(argv[0])) {
        vm_error(vm, "ffi/free: expects 1 cpointer argument");
        return VALUE_NIL;
    }
    free(cpointer_get(argv[0]));
    return VALUE_NIL;
}

/* (ffi/cget ptr type-keyword byte-offset) → value */
static Value native_ffi_cget(VM* vm, int argc, Value* argv) {
    if (argc != 3 || !is_cpointer(argv[0])
        || !is_pointer(argv[1]) || object_type(argv[1]) != TYPE_KEYWORD
        || !is_fixnum(argv[2])) {
        vm_error(vm, "ffi/cget: expects (ptr type-keyword byte-offset)");
        return VALUE_NIL;
    }
    uint8_t*    base   = (uint8_t*)cpointer_get(argv[0]);
    const char* tn     = keyword_name(argv[1]);
    size_t      off    = (size_t)untag_fixnum(argv[2]);
    uint8_t     buf[8] = {0};

    if      (strcmp(tn, "int8")    == 0) { int8_t   v; memcpy(&v, base+off, 1); return make_fixnum((int64_t)v); }
    else if (strcmp(tn, "uint8")   == 0) { uint8_t  v; memcpy(&v, base+off, 1); return make_fixnum((int64_t)v); }
    else if (strcmp(tn, "int16")   == 0) { int16_t  v; memcpy(&v, base+off, 2); return make_fixnum((int64_t)v); }
    else if (strcmp(tn, "uint16")  == 0) { uint16_t v; memcpy(&v, base+off, 2); return make_fixnum((int64_t)v); }
    else if (strcmp(tn, "int32")   == 0) { int32_t  v; memcpy(&v, base+off, 4); return make_fixnum((int64_t)v); }
    else if (strcmp(tn, "uint32")  == 0) { uint32_t v; memcpy(&v, base+off, 4); return make_fixnum((int64_t)v); }
    else if (strcmp(tn, "int64")   == 0) { int64_t  v; memcpy(&v, base+off, 8); return make_fixnum(v); }
    else if (strcmp(tn, "uint64")  == 0) { uint64_t v; memcpy(&v, base+off, 8); return make_fixnum((int64_t)v); }
    else if (strcmp(tn, "float")   == 0) { float  f; memcpy(&f, base+off, 4); return make_float((double)f); }
    else if (strcmp(tn, "double")  == 0) { double d; memcpy(&d, base+off, 8); return make_float(d); }
    else if (strcmp(tn, "string")  == 0) {
        void* ptr; memcpy(&ptr, base+off, sizeof ptr);
        return ptr ? string_from_cstr((const char*)ptr) : VALUE_NIL;
    }
    else if (strcmp(tn, "pointer") == 0) {
        void* ptr; memcpy(&ptr, base+off, sizeof ptr);
        return ptr ? cpointer_new(ptr) : VALUE_NIL;
    }
    (void)buf;
    vm_error(vm, "ffi/cget: unknown type keyword");
    return VALUE_NIL;
}

/* (ffi/cset! ptr type-keyword byte-offset value) → nil */
static Value native_ffi_cset(VM* vm, int argc, Value* argv) {
    if (argc != 4 || !is_cpointer(argv[0])
        || !is_pointer(argv[1]) || object_type(argv[1]) != TYPE_KEYWORD
        || !is_fixnum(argv[2])) {
        vm_error(vm, "ffi/cset!: expects (ptr type-keyword byte-offset value)");
        return VALUE_NIL;
    }
    uint8_t*    base = (uint8_t*)cpointer_get(argv[0]);
    const char* tn   = keyword_name(argv[1]);
    size_t      off  = (size_t)untag_fixnum(argv[2]);
    Value       val  = argv[3];

    if (strcmp(tn, "int8")   == 0 || strcmp(tn, "uint8")  == 0) {
        uint8_t v = (uint8_t)(is_fixnum(val) ? untag_fixnum(val) : 0);
        memcpy(base+off, &v, 1);
    } else if (strcmp(tn, "int16") == 0 || strcmp(tn, "uint16") == 0) {
        uint16_t v = (uint16_t)(is_fixnum(val) ? untag_fixnum(val) : 0);
        memcpy(base+off, &v, 2);
    } else if (strcmp(tn, "int32") == 0 || strcmp(tn, "uint32") == 0) {
        uint32_t v = (uint32_t)(is_fixnum(val) ? untag_fixnum(val) : 0);
        memcpy(base+off, &v, 4);
    } else if (strcmp(tn, "int64") == 0 || strcmp(tn, "uint64") == 0) {
        int64_t v = is_fixnum(val) ? untag_fixnum(val) : 0;
        memcpy(base+off, &v, 8);
    } else if (strcmp(tn, "float") == 0) {
        float f = is_float(val)  ? (float)untag_float(val)
                : is_fixnum(val) ? (float)untag_fixnum(val) : 0.0f;
        memcpy(base+off, &f, 4);
    } else if (strcmp(tn, "double") == 0) {
        double d = is_float(val)  ? untag_float(val)
                 : is_fixnum(val) ? (double)untag_fixnum(val) : 0.0;
        memcpy(base+off, &d, 8);
    } else if (strcmp(tn, "string") == 0) {
        /* Stores the raw char* — caller must ensure the string outlives the memory */
        const char* s = is_string(val) ? string_cstr(val) : NULL;
        memcpy(base+off, &s, sizeof s);
    } else if (strcmp(tn, "pointer") == 0) {
        void* ptr = is_cpointer(val) ? cpointer_get(val) : NULL;
        memcpy(base+off, &ptr, sizeof ptr);
    } else {
        vm_error(vm, "ffi/cset!: unknown type keyword");
        return VALUE_NIL;
    }
    return VALUE_NIL;
}

/* (ffi/cpointer? x) → bool */
static Value native_ffi_is_cpointer(VM* vm, int argc, Value* argv) {
    (void)vm;
    if (argc != 1) return VALUE_FALSE;
    return is_cpointer(argv[0]) ? VALUE_TRUE : VALUE_FALSE;
}

/* (ffi/cnull? ptr) → bool — true if ptr is nil OR points to NULL */
static Value native_ffi_is_cnull(VM* vm, int argc, Value* argv) {
    (void)vm;
    if (argc != 1) return VALUE_TRUE;
    if (is_nil(argv[0])) return VALUE_TRUE;
    if (!is_cpointer(argv[0])) return VALUE_FALSE;
    return cpointer_get(argv[0]) == NULL ? VALUE_TRUE : VALUE_FALSE;
}

/* ===================================================================
 * Registration
 * =================================================================== */

static void reg(Namespace* ns, const char* name, NativeFn fn) {
    Value sym    = symbol_intern(name);
    Value native = native_function_new(-1, fn, name);
    namespace_define(ns, sym, native);
    object_release(native);
}

void core_register_ffi(void) {
    extern NamespaceRegistry* global_namespace_registry;
    Namespace* ns = namespace_registry_get_or_create(global_namespace_registry, "beer.ffi");
    if (!ns) return;

    reg(ns, "open",      native_ffi_open);
    reg(ns, "sym",       native_ffi_sym);
    reg(ns, "close",     native_ffi_close);
    reg(ns, "call",      native_ffi_call);
    reg(ns, "malloc",    native_ffi_malloc);
    reg(ns, "free",      native_ffi_free);
    reg(ns, "cget",      native_ffi_cget);
    reg(ns, "cset!",     native_ffi_cset);
    reg(ns, "cpointer?", native_ffi_is_cpointer);
    reg(ns, "cnull?",    native_ffi_is_cnull);
}

#endif /* BEER_CFFI */
