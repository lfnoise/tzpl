// Tzopilotl
// Copyright (C) 2026 James McCartney
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

/*
 *  tzpl.h
 *  Tzopilotl — C embedding API
 *
 *  Opaque handles and functions for embedding Tzopilotl from C.
 *
 *  Usage:
 *      tzpl_type_universe* types = tzpl_type_universe_create();
 *      tzpl_compiler* compiler = tzpl_compiler_create(types);
 *      tzpl_vm_target* target = tzpl_create_target(compiler, 0);
 *      tzpl_vm* vm = tzpl_vm_create(64 * 1024 * 1024, types, target);
 *
 *      tzpl_compile_result* result = tzpl_compile(compiler, source, "file.x", target);
 *      if (tzpl_compile_result_success(result)) {
 *          tzpl_vm_install(vm, result);
 *          tzpl_vm_execute(vm, result);
 *      }
 *      tzpl_compile_result_destroy(result);
 *
 *      tzpl_vm_destroy(vm);
 *      tzpl_target_destroy(target);
 *      tzpl_compiler_destroy(compiler);
 *      tzpl_type_universe_destroy(types);
 */

#ifndef TZPL_H
#define TZPL_H

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque handles */
typedef struct tzpl_type_universe tzpl_type_universe;
typedef struct tzpl_compiler tzpl_compiler;
typedef struct tzpl_vm tzpl_vm;
typedef struct tzpl_compile_result tzpl_compile_result;
typedef struct tzpl_vm_target tzpl_vm_target;
typedef struct tzpl_module tzpl_module;

/* --- TypeUniverse --- */

/* Create a shared TypeUniverse. One per process. */
tzpl_type_universe* tzpl_type_universe_create(void);

/* Destroy a TypeUniverse. */
void tzpl_type_universe_destroy(tzpl_type_universe* types);

/* --- Compiler --- */

/* Create a Compiler. One per process, runs on a non-real-time thread. */
tzpl_compiler* tzpl_compiler_create(tzpl_type_universe* types);

/* Destroy a Compiler. */
void tzpl_compiler_destroy(tzpl_compiler* compiler);

/* Create a shared compilation target. rt_restricted: non-zero to enable RT safety checking. */
tzpl_vm_target* tzpl_create_target(tzpl_compiler* compiler, int rt_restricted);

/* Destroy a compilation target. */
void tzpl_target_destroy(tzpl_vm_target* target);

/* Compile source code for a specific VM target. Caller owns the returned result. */
tzpl_compile_result* tzpl_compile(tzpl_compiler* compiler,
                                     const char* source,
                                     const char* filename,
                                     tzpl_vm_target* target);

/* --- CompileResult --- */

/* Returns 1 if compilation succeeded, 0 otherwise. */
int tzpl_compile_result_success(const tzpl_compile_result* result);

/* Number of compile errors. */
int tzpl_compile_result_error_count(const tzpl_compile_result* result);

/* Get the error message at the given index. The pointer is valid until the result is destroyed. */
const char* tzpl_compile_result_error(const tzpl_compile_result* result, int index);

/* Destroy a CompileResult. */
void tzpl_compile_result_destroy(tzpl_compile_result* result);


/* --- Type handles (for foreign function registration) --- */

typedef struct { void* ptr; } tzpl_type_handle;

tzpl_type_handle tzpl_type_int(tzpl_type_universe* types);
tzpl_type_handle tzpl_type_float(tzpl_type_universe* types);
tzpl_type_handle tzpl_type_bool(tzpl_type_universe* types);
tzpl_type_handle tzpl_type_string(tzpl_type_universe* types);
tzpl_type_handle tzpl_type_symbol(tzpl_type_universe* types);
tzpl_type_handle tzpl_type_void(tzpl_type_universe* types);

/* --- Foreign function registration --- */

/* Foreign function signature for C API */
typedef void (*tzpl_cfun)(tzpl_vm* vm, uint16_t result_reg,
                            uint16_t argc, uint16_t arg_base);

/* Register a foreign (host-provided) function with the compiler.
 * The function is placed in the global namespace.
 * Returns 1 on success, 0 on failure. */
int tzpl_register_foreign_function(
    tzpl_compiler* compiler,
    const char* name,
    tzpl_type_handle return_type,
    const tzpl_type_handle* param_types,
    int param_count,
    tzpl_cfun cfun,
    int pure,
    int rt_safe);

/* --- Foreign module registration ---
 *
 * Create a named module and add foreign functions to it. The module
 * behaves like a normal .x module: users import it with
 *     import mymod;           -- qualified access: mymod.func()
 *     import mymod.*;         -- unqualified
 *     import mymod.{f1, f2};  -- selective
 *
 * Functions whose names begin with '_' are private to the module.
 * If a .x file with the same module name exists, the foreign functions
 * are injected into it so user code can wrap or extend them.
 */

/* Create a foreign module builder. */
tzpl_module* tzpl_create_foreign_module(tzpl_compiler* compiler,
                                           const char* module_name);

/* Add a function to a foreign module.
 * Returns 1 on success, 0 on failure. */
int tzpl_module_add_function(
    tzpl_module* mod,
    const char* name,
    tzpl_type_handle return_type,
    const tzpl_type_handle* param_types,
    int param_count,
    tzpl_cfun cfun,
    int pure,
    int rt_safe);

/* Destroy a foreign module builder (the registered functions persist). */
void tzpl_module_destroy(tzpl_module* mod);

/* --- VM --- */

/* Create a VM with the given memory pool size (bytes), shared TypeUniverse, and target. */
tzpl_vm* tzpl_vm_create(size_t pool_size, tzpl_type_universe* types, tzpl_vm_target* target);

/* Destroy a VM. */
void tzpl_vm_destroy(tzpl_vm* vm);

/* Make this VM current for the calling thread. */
void tzpl_vm_make_current(tzpl_vm* vm);

/* Install compiled code into the VM. */
void tzpl_vm_install(tzpl_vm* vm, const tzpl_compile_result* result);

/* Execute the main block from a compile result. */
void tzpl_vm_execute(tzpl_vm* vm, const tzpl_compile_result* result);

/* Drive one GC heartbeat. Call on timer or audio callback. */
void tzpl_vm_gc_heartbeat(tzpl_vm* vm);

/* User data (opaque pointer for foreign functions to access host state) */
void  tzpl_vm_set_user_data(tzpl_vm* vm, void* data);
void* tzpl_vm_get_user_data(tzpl_vm* vm);

/* Register access (for use within foreign functions) */
int64_t  tzpl_vm_reg_int(tzpl_vm* vm, uint16_t base, uint16_t reg);
double   tzpl_vm_reg_float(tzpl_vm* vm, uint16_t base, uint16_t reg);
void     tzpl_vm_set_reg_int(tzpl_vm* vm, uint16_t reg, int64_t val);
void     tzpl_vm_set_reg_float(tzpl_vm* vm, uint16_t reg, double val);

/* Opaque object handle (wraps an internal Obj pointer) */
typedef struct { void* ptr; } tzpl_obj_handle;

/* Host-to-VM calling */
typedef union { int64_t i; double f; void* p; } tzpl_word;

/* Read/write object registers */
tzpl_obj_handle tzpl_vm_reg_obj(tzpl_vm* vm, uint16_t base, uint16_t reg);
void tzpl_vm_set_reg_obj(tzpl_vm* vm, uint16_t reg, tzpl_obj_handle obj);

/* --- Object accessor API --- */

/* String */
const char* tzpl_string_data(tzpl_obj_handle obj);
size_t      tzpl_string_size(tzpl_obj_handle obj);

/* Fraction */
int64_t tzpl_fraction_numer(tzpl_obj_handle obj);
int64_t tzpl_fraction_denom(tzpl_obj_handle obj);

/* Complex */
double tzpl_complex_real(tzpl_obj_handle obj);
double tzpl_complex_imag(tzpl_obj_handle obj);

/* Array */
size_t           tzpl_array_size(tzpl_obj_handle obj);
int64_t          tzpl_array_get_int(tzpl_obj_handle obj, size_t index);
double           tzpl_array_get_float(tzpl_obj_handle obj, size_t index);
tzpl_obj_handle tzpl_array_get_obj(tzpl_obj_handle obj, size_t index);

/* Tuple */
size_t     tzpl_tuple_size(tzpl_obj_handle obj);
tzpl_word tzpl_tuple_get(tzpl_obj_handle obj, size_t index);

/* Struct */
size_t     tzpl_struct_field_count(tzpl_obj_handle obj);
tzpl_word tzpl_struct_get_field(tzpl_obj_handle obj, size_t index);

tzpl_word tzpl_vm_call(tzpl_vm* vm,
                          const tzpl_compile_result* result,
                          const char* func_name,
                          const tzpl_word* args,
                          int argc);

/* Number of global variables in the VM. */
uint32_t tzpl_vm_num_globals(const tzpl_vm* vm);

/* Redirect print output to the given FILE stream. */
void tzpl_vm_set_print_output(tzpl_vm* vm, FILE* f);

/* --- Memory and GC stats --- */

/* Bytes currently allocated in the VM's pool. */
size_t tzpl_vm_allocated(const tzpl_vm* vm);

/* Total pool size in bytes. */
size_t tzpl_vm_pool_size(const tzpl_vm* vm);

/* Number of live GC objects. */
uint32_t tzpl_vm_num_live_objects(const tzpl_vm* vm);

/* Number of live GC words (object slots). */
uint32_t tzpl_vm_num_live_words(const tzpl_vm* vm);

#ifdef __cplusplus
}
#endif

#endif /* TZPL_H */
