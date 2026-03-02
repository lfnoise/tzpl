/*
 *  langx.h
 *  Language X — C embedding API
 *
 *  Opaque handles and functions for embedding Language X from C.
 *
 *  Usage:
 *      langx_type_universe* types = langx_type_universe_create();
 *      langx_compiler* compiler = langx_compiler_create(types);
 *      langx_vm_target* target = langx_create_target(compiler, 0);
 *      langx_vm* vm = langx_vm_create(64 * 1024 * 1024, types, target);
 *
 *      langx_compile_result* result = langx_compile(compiler, source, "file.x", target);
 *      if (langx_compile_result_success(result)) {
 *          langx_vm_install(vm, result);
 *          langx_vm_execute(vm, result);
 *      }
 *      langx_compile_result_destroy(result);
 *
 *      langx_vm_destroy(vm);
 *      langx_target_destroy(target);
 *      langx_compiler_destroy(compiler);
 *      langx_type_universe_destroy(types);
 */

#ifndef LANGX_H
#define LANGX_H

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque handles */
typedef struct langx_type_universe langx_type_universe;
typedef struct langx_compiler langx_compiler;
typedef struct langx_vm langx_vm;
typedef struct langx_compile_result langx_compile_result;
typedef struct langx_vm_target langx_vm_target;

/* --- TypeUniverse --- */

/* Create a shared TypeUniverse. One per process. */
langx_type_universe* langx_type_universe_create(void);

/* Destroy a TypeUniverse. */
void langx_type_universe_destroy(langx_type_universe* types);

/* --- Compiler --- */

/* Create a Compiler. One per process, runs on a non-real-time thread. */
langx_compiler* langx_compiler_create(langx_type_universe* types);

/* Destroy a Compiler. */
void langx_compiler_destroy(langx_compiler* compiler);

/* Create a shared compilation target. rt_restricted: non-zero to enable RT safety checking. */
langx_vm_target* langx_create_target(langx_compiler* compiler, int rt_restricted);

/* Destroy a compilation target. */
void langx_target_destroy(langx_vm_target* target);

/* Compile source code for a specific VM target. Caller owns the returned result. */
langx_compile_result* langx_compile(langx_compiler* compiler,
                                     const char* source,
                                     const char* filename,
                                     langx_vm_target* target);

/* --- CompileResult --- */

/* Returns 1 if compilation succeeded, 0 otherwise. */
int langx_compile_result_success(const langx_compile_result* result);

/* Number of compile errors. */
int langx_compile_result_error_count(const langx_compile_result* result);

/* Get the error message at the given index. The pointer is valid until the result is destroyed. */
const char* langx_compile_result_error(const langx_compile_result* result, int index);

/* Destroy a CompileResult. */
void langx_compile_result_destroy(langx_compile_result* result);


/* --- Type handles (for foreign function registration) --- */

typedef struct { void* ptr; } langx_type_handle;

langx_type_handle langx_type_int(langx_type_universe* types);
langx_type_handle langx_type_float(langx_type_universe* types);
langx_type_handle langx_type_bool(langx_type_universe* types);
langx_type_handle langx_type_string(langx_type_universe* types);
langx_type_handle langx_type_symbol(langx_type_universe* types);
langx_type_handle langx_type_void(langx_type_universe* types);

/* --- Foreign function registration --- */

/* Foreign function signature for C API */
typedef void (*langx_cfun)(langx_vm* vm, uint16_t result_reg,
                            uint16_t argc, uint16_t arg_base);

/* Register a foreign (host-provided) function with the compiler.
 * Returns 1 on success, 0 on failure. */
int langx_register_foreign_function(
    langx_compiler* compiler,
    const char* name,
    langx_type_handle return_type,
    const langx_type_handle* param_types,
    int param_count,
    langx_cfun cfun,
    int pure,
    int rt_safe);

/* --- VM --- */

/* Create a VM with the given memory pool size (bytes), shared TypeUniverse, and target. */
langx_vm* langx_vm_create(size_t pool_size, langx_type_universe* types, langx_vm_target* target);

/* Destroy a VM. */
void langx_vm_destroy(langx_vm* vm);

/* Make this VM current for the calling thread. */
void langx_vm_make_current(langx_vm* vm);

/* Install compiled code into the VM. */
void langx_vm_install(langx_vm* vm, const langx_compile_result* result);

/* Execute the main block from a compile result. */
void langx_vm_execute(langx_vm* vm, const langx_compile_result* result);

/* Drive one GC heartbeat. Call on timer or audio callback. */
void langx_vm_gc_heartbeat(langx_vm* vm);

/* User data (opaque pointer for foreign functions to access host state) */
void  langx_vm_set_user_data(langx_vm* vm, void* data);
void* langx_vm_get_user_data(langx_vm* vm);

/* Register access (for use within foreign functions) */
int64_t  langx_vm_reg_int(langx_vm* vm, uint16_t base, uint16_t reg);
double   langx_vm_reg_float(langx_vm* vm, uint16_t base, uint16_t reg);
void     langx_vm_set_reg_int(langx_vm* vm, uint16_t reg, int64_t val);
void     langx_vm_set_reg_float(langx_vm* vm, uint16_t reg, double val);

/* Opaque object handle (wraps an internal Obj pointer) */
typedef struct { void* ptr; } langx_obj_handle;

/* Host-to-VM calling */
typedef union { int64_t i; double f; void* p; } langx_word;

/* Read/write object registers */
langx_obj_handle langx_vm_reg_obj(langx_vm* vm, uint16_t base, uint16_t reg);
void langx_vm_set_reg_obj(langx_vm* vm, uint16_t reg, langx_obj_handle obj);

/* --- Object accessor API --- */

/* String */
const char* langx_string_data(langx_obj_handle obj);
size_t      langx_string_size(langx_obj_handle obj);

/* Fraction */
int64_t langx_fraction_numer(langx_obj_handle obj);
int64_t langx_fraction_denom(langx_obj_handle obj);

/* Complex */
double langx_complex_real(langx_obj_handle obj);
double langx_complex_imag(langx_obj_handle obj);

/* Array */
size_t           langx_array_size(langx_obj_handle obj);
int64_t          langx_array_get_int(langx_obj_handle obj, size_t index);
double           langx_array_get_float(langx_obj_handle obj, size_t index);
langx_obj_handle langx_array_get_obj(langx_obj_handle obj, size_t index);

/* Tuple */
size_t     langx_tuple_size(langx_obj_handle obj);
langx_word langx_tuple_get(langx_obj_handle obj, size_t index);

/* Struct */
size_t     langx_struct_field_count(langx_obj_handle obj);
langx_word langx_struct_get_field(langx_obj_handle obj, size_t index);

langx_word langx_vm_call(langx_vm* vm,
                          const langx_compile_result* result,
                          const char* func_name,
                          const langx_word* args,
                          int argc);

/* Number of global variables in the VM. */
uint32_t langx_vm_num_globals(const langx_vm* vm);

/* Redirect print output to the given FILE stream. */
void langx_vm_set_print_output(langx_vm* vm, FILE* f);

/* --- Memory and GC stats --- */

/* Bytes currently allocated in the VM's pool. */
size_t langx_vm_allocated(const langx_vm* vm);

/* Total pool size in bytes. */
size_t langx_vm_pool_size(const langx_vm* vm);

/* Number of live GC objects. */
uint32_t langx_vm_num_live_objects(const langx_vm* vm);

/* Number of live GC words (object slots). */
uint32_t langx_vm_num_live_words(const langx_vm* vm);

#ifdef __cplusplus
}
#endif

#endif /* LANGX_H */
