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

//
//  tzpl_c.cpp
//  Tzopilotl — C API implementation
//

#include "tzpl.h"
#include "vm.hpp"
#include "compiler.hpp"
#include "type_universe.hpp"
#include "value.hpp"
#include "type_system.hpp"
#include "diagnostic.hpp"

#include <string>
#include <vector>
#include <cstring>

// --- Wrapper structs (match the opaque C typedefs) ---

struct tzpl_type_universe {
    ts::TypeUniverse impl;
};

struct tzpl_compiler {
    ts::Compiler impl;
    explicit tzpl_compiler(ts::TypeUniverse& types) : impl(types) {}
};

struct tzpl_vm_target {
    ts::VMTarget impl;
};

struct tzpl_vm {
    ts::VM impl;
    tzpl_vm(usize poolSize, ts::TypeUniverse& types, const ts::VMTarget& target)
        : impl(poolSize, types, target) {}
};

struct tzpl_compile_result {
    ts::CompileResult impl;
    // Cache formatted error messages for C string access
    std::vector<std::string> errorMessages;
};

// --- TypeUniverse ---

tzpl_type_universe* tzpl_type_universe_create(void) {
    return new tzpl_type_universe();
}

void tzpl_type_universe_destroy(tzpl_type_universe* types) {
    delete types;
}

// --- Compiler ---

tzpl_compiler* tzpl_compiler_create(tzpl_type_universe* types) {
    return new tzpl_compiler(types->impl);
}

void tzpl_compiler_destroy(tzpl_compiler* compiler) {
    delete compiler;
}

tzpl_vm_target* tzpl_create_target(tzpl_compiler* compiler, int rt_restricted) {
    auto* target = new tzpl_vm_target();
    target->impl = compiler->impl.createTarget(rt_restricted != 0);
    return target;
}

void tzpl_target_destroy(tzpl_vm_target* target) {
    delete target;
}

tzpl_compile_result* tzpl_compile(tzpl_compiler* compiler,
                                     const char* source,
                                     const char* filename,
                                     tzpl_vm_target* target) {
    auto* result = new tzpl_compile_result();
    result->impl = compiler->impl.compile(
        std::string(source), std::string(filename), target->impl);

    // Cache formatted error messages (plain text, no ANSI colors)
    result->errorMessages = ts::formatErrorsPlain(
        result->impl.errors, std::string(source), std::string(filename));

    return result;
}

// --- CompileResult ---

int tzpl_compile_result_success(const tzpl_compile_result* result) {
    return result->impl.success ? 1 : 0;
}

int tzpl_compile_result_error_count(const tzpl_compile_result* result) {
    return (int)result->errorMessages.size();
}

const char* tzpl_compile_result_error(const tzpl_compile_result* result, int index) {
    if (index < 0 || index >= (int)result->errorMessages.size()) return "";
    return result->errorMessages[index].c_str();
}

void tzpl_compile_result_destroy(tzpl_compile_result* result) {
    delete result;
}

// --- VM ---

tzpl_vm* tzpl_vm_create(size_t pool_size, tzpl_type_universe* types, tzpl_vm_target* target) {
    auto* vm = new tzpl_vm(pool_size, types->impl, target ? target->impl : ts::VMTarget{});
    // Store wrapper pointer as userData so C FFI trampoline can recover it
    vm->impl.setUserData(vm);
    return vm;
}

void tzpl_vm_destroy(tzpl_vm* vm) {
    delete vm;
}

void tzpl_vm_make_current(tzpl_vm* vm) {
    vm->impl.makeCurrent();
}

void tzpl_vm_install(tzpl_vm* vm, const tzpl_compile_result* result) {
    vm->impl.install(result->impl);
}

void tzpl_vm_execute(tzpl_vm* vm, const tzpl_compile_result* result) {
    vm->impl.makeCurrent();
    vm->impl.execute(result->impl.mainBlock);
}

void tzpl_vm_gc_heartbeat(tzpl_vm* vm) {
    vm->impl.gcHeartbeat();
}

uint32_t tzpl_vm_num_globals(const tzpl_vm* vm) {
    return vm->impl.numGlobals();
}

void tzpl_vm_set_print_output(tzpl_vm* vm, FILE* f) {
    vm->impl.setPrintOutput(f);
}

// --- Stats ---

size_t tzpl_vm_allocated(const tzpl_vm* vm) {
    return vm->impl.allocator().getAllocated();
}

size_t tzpl_vm_pool_size(const tzpl_vm* vm) {
    return vm->impl.allocator().getPoolSize();
}

uint32_t tzpl_vm_num_live_objects(const tzpl_vm* vm) {
    // Last completed cycle's live-set size. Zero if no cycle has run yet.
    return const_cast<ts::VM&>(vm->impl).tracingGC().lastBlackCount();
}

uint32_t tzpl_vm_num_live_words(const tzpl_vm* vm) {
    // Last completed cycle's reclaimed-set size. Zero if no cycle has run yet.
    return const_cast<ts::VM&>(vm->impl).tracingGC().lastWhiteCount();
}

// --- Type handles ---

tzpl_type_handle tzpl_type_int(tzpl_type_universe* types) {
    return tzpl_type_handle{types->impl.types().intType};
}

tzpl_type_handle tzpl_type_float(tzpl_type_universe* types) {
    return tzpl_type_handle{types->impl.types().floatType};
}

tzpl_type_handle tzpl_type_bool(tzpl_type_universe* types) {
    return tzpl_type_handle{types->impl.types().boolType};
}

tzpl_type_handle tzpl_type_string(tzpl_type_universe* types) {
    return tzpl_type_handle{types->impl.types().stringType};
}

tzpl_type_handle tzpl_type_symbol(tzpl_type_universe* types) {
    return tzpl_type_handle{types->impl.types().symbolType};
}

tzpl_type_handle tzpl_type_void(tzpl_type_universe* types) {
    return tzpl_type_handle{types->impl.types().voidType};
}

// --- Foreign function registration ---

// Map from ts::VM* to tzpl_vm* for C API trampoline.
// Since tzpl_vm is a simple wrapper with impl as first member after no vtable,
// we can store the mapping in the VM's userData.
static tzpl_vm* to_tzpl_vm(ts::VM* vm) {
    return static_cast<tzpl_vm*>(vm->userData());
}

// C FFI trampoline: recovers the C callback from the Primitive's ffiData_
static void c_ffi_trampoline(ts::VM& vm, u16 dst, u16 argc, u16 argBase) {
    auto* prim = vm.currentPrimitive();
    auto cfun = reinterpret_cast<tzpl_cfun>(prim->ffiData_);
    auto* wrapper = to_tzpl_vm(&vm);
    cfun(wrapper, dst, argc, argBase);
}

int tzpl_register_foreign_function(
    tzpl_compiler* compiler,
    const char* name,
    tzpl_type_handle return_type,
    const tzpl_type_handle* param_types,
    int param_count,
    tzpl_cfun cfun,
    int pure,
    int rt_safe) {

    std::vector<ts::Type*> params;
    params.reserve(param_count);
    for (int i = 0; i < param_count; i++) {
        params.push_back(static_cast<ts::Type*>(param_types[i].ptr));
    }

    // Use a trampoline so C callbacks get tzpl_vm* instead of ts::VM&.
    // Store the original C callback as ffiData on the Primitive.
    compiler->impl.registerForeignFunction(
        std::string(name),
        static_cast<ts::Type*>(return_type.ptr),
        std::move(params),
        c_ffi_trampoline,
        pure != 0,
        rt_safe != 0);

    // Set the ffiData on the last-added entry to store the C callback pointer
    auto& entries = const_cast<std::vector<ts::Compiler::ForeignFuncEntry>&>(
        compiler->impl.foreignFunctions());
    entries.back().ffiData = reinterpret_cast<void*>(cfun);

    return 1;
}

// --- Foreign module registration ---

struct tzpl_module {
    tzpl_compiler* compiler;
    std::string moduleName;
};

tzpl_module* tzpl_create_foreign_module(tzpl_compiler* compiler,
                                           const char* module_name) {
    auto* mod = new tzpl_module();
    mod->compiler = compiler;
    mod->moduleName = module_name;
    return mod;
}

int tzpl_module_add_function(
    tzpl_module* mod,
    const char* name,
    tzpl_type_handle return_type,
    const tzpl_type_handle* param_types,
    int param_count,
    tzpl_cfun cfun,
    int pure,
    int rt_safe) {

    std::vector<ts::Type*> params;
    params.reserve(param_count);
    for (int i = 0; i < param_count; i++) {
        params.push_back(static_cast<ts::Type*>(param_types[i].ptr));
    }

    mod->compiler->impl.registerForeignModuleFunction(
        mod->moduleName,
        std::string(name),
        static_cast<ts::Type*>(return_type.ptr),
        std::move(params),
        c_ffi_trampoline,
        pure != 0,
        rt_safe != 0);

    // Set the ffiData on the last-added entry to store the C callback pointer
    auto* entries = const_cast<std::vector<ts::Compiler::ForeignFuncEntry>*>(
        mod->compiler->impl.foreignModuleFunctions(mod->moduleName));
    entries->back().ffiData = reinterpret_cast<void*>(cfun);

    return 1;
}

void tzpl_module_destroy(tzpl_module* mod) {
    delete mod;
}

// --- User data ---

void tzpl_vm_set_user_data(tzpl_vm* vm, void* data) {
    vm->impl.setUserData(data);
}

void* tzpl_vm_get_user_data(tzpl_vm* vm) {
    return vm->impl.userData();
}

// --- Register access ---

int64_t tzpl_vm_reg_int(tzpl_vm* vm, uint16_t base, uint16_t reg) {
    (void)base;
    return vm->impl.reg(reg).i;
}

double tzpl_vm_reg_float(tzpl_vm* vm, uint16_t base, uint16_t reg) {
    (void)base;
    return vm->impl.reg(reg).f;
}

void tzpl_vm_set_reg_int(tzpl_vm* vm, uint16_t reg, int64_t val) {
    vm->impl.reg(reg).i = val;
}

void tzpl_vm_set_reg_float(tzpl_vm* vm, uint16_t reg, double val) {
    vm->impl.reg(reg).f = val;
}

// --- Object register access ---

tzpl_obj_handle tzpl_vm_reg_obj(tzpl_vm* vm, uint16_t base, uint16_t reg) {
    (void)base;
    return tzpl_obj_handle{vm->impl.reg(reg).o};
}

void tzpl_vm_set_reg_obj(tzpl_vm* vm, uint16_t reg, tzpl_obj_handle obj) {
    vm->impl.reg(reg).o = static_cast<ts::Obj*>(obj.ptr);
}

// --- Object accessor API ---

const char* tzpl_string_data(tzpl_obj_handle obj) {
    auto* s = static_cast<ts::StringObj*>(static_cast<ts::Obj*>(obj.ptr));
    return s->s.data();
}

size_t tzpl_string_size(tzpl_obj_handle obj) {
    auto* s = static_cast<ts::StringObj*>(static_cast<ts::Obj*>(obj.ptr));
    return s->s.size();
}

int64_t tzpl_fraction_numer(tzpl_obj_handle obj) {
    auto* f = static_cast<ts::Fraction*>(static_cast<ts::Obj*>(obj.ptr));
    return f->r.numer();
}

int64_t tzpl_fraction_denom(tzpl_obj_handle obj) {
    auto* f = static_cast<ts::Fraction*>(static_cast<ts::Obj*>(obj.ptr));
    return f->r.denom();
}

double tzpl_complex_real(tzpl_obj_handle obj) {
    auto* c = static_cast<ts::Complex*>(static_cast<ts::Obj*>(obj.ptr));
    return c->x.real();
}

double tzpl_complex_imag(tzpl_obj_handle obj) {
    auto* c = static_cast<ts::Complex*>(static_cast<ts::Obj*>(obj.ptr));
    return c->x.imag();
}

size_t tzpl_array_size(tzpl_obj_handle obj) {
    auto* o = static_cast<ts::Obj*>(obj.ptr);
    auto* arrayType = static_cast<ts::ArrayType*>(o->type_);
    if (arrayType->elemType_->isObjType()) {
        return static_cast<ts::ObjArray*>(o)->size();
    } else {
        return static_cast<ts::PodArray<i64>*>(o)->v.size();
    }
}

int64_t tzpl_array_get_int(tzpl_obj_handle obj, size_t index) {
    auto* arr = static_cast<ts::PodArray<i64>*>(static_cast<ts::Obj*>(obj.ptr));
    if (index >= arr->v.size()) return 0;
    return arr->v[index];
}

double tzpl_array_get_float(tzpl_obj_handle obj, size_t index) {
    auto* arr = static_cast<ts::PodArray<f64>*>(static_cast<ts::Obj*>(obj.ptr));
    if (index >= arr->v.size()) return 0.0;
    return arr->v[index];
}

tzpl_obj_handle tzpl_array_get_obj(tzpl_obj_handle obj, size_t index) {
    auto* arr = static_cast<ts::ObjArray*>(static_cast<ts::Obj*>(obj.ptr));
    if (index >= arr->size()) return tzpl_obj_handle{nullptr};
    return tzpl_obj_handle{arr->get(index)};
}

size_t tzpl_tuple_size(tzpl_obj_handle obj) {
    auto* t = static_cast<ts::Tuple*>(static_cast<ts::Obj*>(obj.ptr));
    return t->numFields_;
}

tzpl_word tzpl_tuple_get(tzpl_obj_handle obj, size_t index) {
    auto* t = static_cast<ts::Tuple*>(static_cast<ts::Obj*>(obj.ptr));
    tzpl_word out;
    if (index >= t->numFields_) { out.i = 0; return out; }
    // Phase 4g.13: heap Tuple stores fields at layout-aware offsets. Return
    // the first word of the field; multi-word Inline composite fields are
    // not fully representable through this 1-word C API and would require
    // a separate accessor.
    auto* tt = static_cast<ts::TupleType*>(t->type_);
    std::memcpy(&out, &t->v[tt->layout_[index].wordOffset], sizeof(tzpl_word));
    return out;
}

size_t tzpl_struct_field_count(tzpl_obj_handle obj) {
    auto* s = static_cast<ts::Struct*>(static_cast<ts::Obj*>(obj.ptr));
    return s->numFields_;
}

tzpl_word tzpl_struct_get_field(tzpl_obj_handle obj, size_t index) {
    auto* s = static_cast<ts::Struct*>(static_cast<ts::Obj*>(obj.ptr));
    tzpl_word out;
    if (index >= s->numFields_) { out.i = 0; return out; }
    // Phase 4g.13: heap Struct stores fields at layout-aware offsets.
    auto* st = static_cast<ts::StructType*>(s->type_);
    std::memcpy(&out, &s->v[st->layout_[index].wordOffset], sizeof(tzpl_word));
    return out;
}

// --- Host-to-VM calling ---

tzpl_word tzpl_vm_call(tzpl_vm* vm,
                          const tzpl_compile_result* result,
                          const char* func_name,
                          const tzpl_word* args,
                          int argc) {
    vm->impl.makeCurrent();

    // Find the exported function by name
    for (auto& ef : result->impl.exportedFunctions) {
        if (ef.name != func_name) continue;
        if ((int)ef.paramTypes.size() != argc) continue;

        // Get the CodeBlock from the global
        ts::Word gval = vm->impl.global(ef.globalIndex);
        if (!ef.isCodeBlock) {
            // Primitive: call directly
            auto* prim = static_cast<ts::Primitive*>(static_cast<ts::Callable*>(gval.o));
            ts::Word argWords[16];
            for (int i = 0; i < argc && i < 16; i++) {
                std::memcpy(&argWords[i], &args[i], sizeof(ts::Word));
            }
            ts::Word r = vm->impl.evalPrimitive(prim, argWords, (u16)argc);
            tzpl_word out;
            std::memcpy(&out, &r, sizeof(tzpl_word));
            return out;
        }

        auto* block = static_cast<ts::CodeBlock*>(gval.p);
        ts::Word argWords[16];
        for (int i = 0; i < argc && i < 16; i++) {
            std::memcpy(&argWords[i], &args[i], sizeof(ts::Word));
        }
        ts::Word r = vm->impl.callFunction(block, argWords, (u16)argc);
        tzpl_word out;
        std::memcpy(&out, &r, sizeof(tzpl_word));
        return out;
    }

    // Not found
    tzpl_word zero;
    zero.i = 0;
    return zero;
}
