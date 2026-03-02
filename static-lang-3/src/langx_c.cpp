//
//  langx_c.cpp
//  Language X — C API implementation
//

#include "langx.h"
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

struct langx_type_universe {
    ts::TypeUniverse impl;
};

struct langx_compiler {
    ts::Compiler impl;
    explicit langx_compiler(ts::TypeUniverse& types) : impl(types) {}
};

struct langx_vm_target {
    ts::VMTarget impl;
};

struct langx_vm {
    ts::VM impl;
    langx_vm(usize poolSize, ts::TypeUniverse& types, const ts::VMTarget& target)
        : impl(poolSize, types, target) {}
};

struct langx_compile_result {
    ts::CompileResult impl;
    // Cache formatted error messages for C string access
    std::vector<std::string> errorMessages;
};

// --- TypeUniverse ---

langx_type_universe* langx_type_universe_create(void) {
    return new langx_type_universe();
}

void langx_type_universe_destroy(langx_type_universe* types) {
    delete types;
}

// --- Compiler ---

langx_compiler* langx_compiler_create(langx_type_universe* types) {
    return new langx_compiler(types->impl);
}

void langx_compiler_destroy(langx_compiler* compiler) {
    delete compiler;
}

langx_vm_target* langx_create_target(langx_compiler* compiler, int rt_restricted) {
    auto* target = new langx_vm_target();
    target->impl = compiler->impl.createTarget(rt_restricted != 0);
    return target;
}

void langx_target_destroy(langx_vm_target* target) {
    delete target;
}

langx_compile_result* langx_compile(langx_compiler* compiler,
                                     const char* source,
                                     const char* filename,
                                     langx_vm_target* target) {
    auto* result = new langx_compile_result();
    result->impl = compiler->impl.compile(
        std::string(source), std::string(filename), target->impl);

    // Cache formatted error messages (plain text, no ANSI colors)
    result->errorMessages = ts::formatErrorsPlain(
        result->impl.errors, std::string(source), std::string(filename));

    return result;
}

// --- CompileResult ---

int langx_compile_result_success(const langx_compile_result* result) {
    return result->impl.success ? 1 : 0;
}

int langx_compile_result_error_count(const langx_compile_result* result) {
    return (int)result->errorMessages.size();
}

const char* langx_compile_result_error(const langx_compile_result* result, int index) {
    if (index < 0 || index >= (int)result->errorMessages.size()) return "";
    return result->errorMessages[index].c_str();
}

void langx_compile_result_destroy(langx_compile_result* result) {
    delete result;
}

// --- VM ---

langx_vm* langx_vm_create(size_t pool_size, langx_type_universe* types, langx_vm_target* target) {
    auto* vm = new langx_vm(pool_size, types->impl, target ? target->impl : ts::VMTarget{});
    // Store wrapper pointer as userData so C FFI trampoline can recover it
    vm->impl.setUserData(vm);
    return vm;
}

void langx_vm_destroy(langx_vm* vm) {
    delete vm;
}

void langx_vm_make_current(langx_vm* vm) {
    vm->impl.makeCurrent();
}

void langx_vm_install(langx_vm* vm, const langx_compile_result* result) {
    vm->impl.install(result->impl);
}

void langx_vm_execute(langx_vm* vm, const langx_compile_result* result) {
    vm->impl.makeCurrent();
    vm->impl.execute(result->impl.mainBlock);
}

void langx_vm_gc_heartbeat(langx_vm* vm) {
    vm->impl.gcHeartbeat();
}

uint32_t langx_vm_num_globals(const langx_vm* vm) {
    return vm->impl.numGlobals();
}

void langx_vm_set_print_output(langx_vm* vm, FILE* f) {
    vm->impl.setPrintOutput(f);
}

// --- Stats ---

size_t langx_vm_allocated(const langx_vm* vm) {
    return vm->impl.allocator().getAllocated();
}

size_t langx_vm_pool_size(const langx_vm* vm) {
    return vm->impl.allocator().getPoolSize();
}

uint32_t langx_vm_num_live_objects(const langx_vm* vm) {
    return vm->impl.getNumLiveObjects();
}

uint32_t langx_vm_num_live_words(const langx_vm* vm) {
    return vm->impl.getNumLiveWords();
}

// --- Type handles ---

langx_type_handle langx_type_int(langx_type_universe* types) {
    return langx_type_handle{types->impl.types().intType};
}

langx_type_handle langx_type_float(langx_type_universe* types) {
    return langx_type_handle{types->impl.types().floatType};
}

langx_type_handle langx_type_bool(langx_type_universe* types) {
    return langx_type_handle{types->impl.types().boolType};
}

langx_type_handle langx_type_string(langx_type_universe* types) {
    return langx_type_handle{types->impl.types().stringType};
}

langx_type_handle langx_type_symbol(langx_type_universe* types) {
    return langx_type_handle{types->impl.types().symbolType};
}

langx_type_handle langx_type_void(langx_type_universe* types) {
    return langx_type_handle{types->impl.types().voidType};
}

// --- Foreign function registration ---

// Map from ts::VM* to langx_vm* for C API trampoline.
// Since langx_vm is a simple wrapper with impl as first member after no vtable,
// we can store the mapping in the VM's userData.
static langx_vm* to_langx_vm(ts::VM* vm) {
    return static_cast<langx_vm*>(vm->userData());
}

// C FFI trampoline: recovers the C callback from the Primitive's ffiData_
static void c_ffi_trampoline(ts::VM& vm, u16 dst, u16 argc, u16 argBase) {
    auto* prim = vm.currentPrimitive();
    auto cfun = reinterpret_cast<langx_cfun>(prim->ffiData_);
    auto* wrapper = to_langx_vm(&vm);
    cfun(wrapper, dst, argc, argBase);
}

int langx_register_foreign_function(
    langx_compiler* compiler,
    const char* name,
    langx_type_handle return_type,
    const langx_type_handle* param_types,
    int param_count,
    langx_cfun cfun,
    int pure,
    int rt_safe) {

    std::vector<ts::Type*> params;
    params.reserve(param_count);
    for (int i = 0; i < param_count; i++) {
        params.push_back(static_cast<ts::Type*>(param_types[i].ptr));
    }

    // Use a trampoline so C callbacks get langx_vm* instead of ts::VM&.
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

// --- User data ---

void langx_vm_set_user_data(langx_vm* vm, void* data) {
    vm->impl.setUserData(data);
}

void* langx_vm_get_user_data(langx_vm* vm) {
    return vm->impl.userData();
}

// --- Register access ---

int64_t langx_vm_reg_int(langx_vm* vm, uint16_t base, uint16_t reg) {
    (void)base;
    return vm->impl.reg(reg).i;
}

double langx_vm_reg_float(langx_vm* vm, uint16_t base, uint16_t reg) {
    (void)base;
    return vm->impl.reg(reg).f;
}

void langx_vm_set_reg_int(langx_vm* vm, uint16_t reg, int64_t val) {
    vm->impl.reg(reg).i = val;
}

void langx_vm_set_reg_float(langx_vm* vm, uint16_t reg, double val) {
    vm->impl.reg(reg).f = val;
}

// --- Object register access ---

langx_obj_handle langx_vm_reg_obj(langx_vm* vm, uint16_t base, uint16_t reg) {
    (void)base;
    return langx_obj_handle{vm->impl.reg(reg).o};
}

void langx_vm_set_reg_obj(langx_vm* vm, uint16_t reg, langx_obj_handle obj) {
    vm->impl.reg(reg).o = static_cast<ts::Obj*>(obj.ptr);
}

// --- Object accessor API ---

const char* langx_string_data(langx_obj_handle obj) {
    auto* s = static_cast<ts::StringObj*>(static_cast<ts::Obj*>(obj.ptr));
    return s->s.data();
}

size_t langx_string_size(langx_obj_handle obj) {
    auto* s = static_cast<ts::StringObj*>(static_cast<ts::Obj*>(obj.ptr));
    return s->s.size();
}

int64_t langx_fraction_numer(langx_obj_handle obj) {
    auto* f = static_cast<ts::Fraction*>(static_cast<ts::Obj*>(obj.ptr));
    return f->r.numer();
}

int64_t langx_fraction_denom(langx_obj_handle obj) {
    auto* f = static_cast<ts::Fraction*>(static_cast<ts::Obj*>(obj.ptr));
    return f->r.denom();
}

double langx_complex_real(langx_obj_handle obj) {
    auto* c = static_cast<ts::Complex*>(static_cast<ts::Obj*>(obj.ptr));
    return c->x.real();
}

double langx_complex_imag(langx_obj_handle obj) {
    auto* c = static_cast<ts::Complex*>(static_cast<ts::Obj*>(obj.ptr));
    return c->x.imag();
}

size_t langx_array_size(langx_obj_handle obj) {
    auto* o = static_cast<ts::Obj*>(obj.ptr);
    auto* arrayType = static_cast<ts::ArrayType*>(o->type_);
    if (arrayType->elemType_->isObjType()) {
        return static_cast<ts::ObjArray*>(o)->v.size();
    } else {
        return static_cast<ts::PodArray<i64>*>(o)->v.size();
    }
}

int64_t langx_array_get_int(langx_obj_handle obj, size_t index) {
    auto* arr = static_cast<ts::PodArray<i64>*>(static_cast<ts::Obj*>(obj.ptr));
    if (index >= arr->v.size()) return 0;
    return arr->v[index];
}

double langx_array_get_float(langx_obj_handle obj, size_t index) {
    auto* arr = static_cast<ts::PodArray<f64>*>(static_cast<ts::Obj*>(obj.ptr));
    if (index >= arr->v.size()) return 0.0;
    return arr->v[index];
}

langx_obj_handle langx_array_get_obj(langx_obj_handle obj, size_t index) {
    auto* arr = static_cast<ts::ObjArray*>(static_cast<ts::Obj*>(obj.ptr));
    if (index >= arr->v.size()) return langx_obj_handle{nullptr};
    return langx_obj_handle{arr->v[index]};
}

size_t langx_tuple_size(langx_obj_handle obj) {
    auto* t = static_cast<ts::Tuple*>(static_cast<ts::Obj*>(obj.ptr));
    return t->numFields_;
}

langx_word langx_tuple_get(langx_obj_handle obj, size_t index) {
    auto* t = static_cast<ts::Tuple*>(static_cast<ts::Obj*>(obj.ptr));
    langx_word out;
    if (index >= t->numFields_) { out.i = 0; return out; }
    std::memcpy(&out, &t->v[index], sizeof(langx_word));
    return out;
}

size_t langx_struct_field_count(langx_obj_handle obj) {
    auto* s = static_cast<ts::Struct*>(static_cast<ts::Obj*>(obj.ptr));
    return s->numFields_;
}

langx_word langx_struct_get_field(langx_obj_handle obj, size_t index) {
    auto* s = static_cast<ts::Struct*>(static_cast<ts::Obj*>(obj.ptr));
    langx_word out;
    if (index >= s->numFields_) { out.i = 0; return out; }
    std::memcpy(&out, &s->v[index], sizeof(langx_word));
    return out;
}

// --- Host-to-VM calling ---

langx_word langx_vm_call(langx_vm* vm,
                          const langx_compile_result* result,
                          const char* func_name,
                          const langx_word* args,
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
            langx_word out;
            std::memcpy(&out, &r, sizeof(langx_word));
            return out;
        }

        auto* block = static_cast<ts::CodeBlock*>(gval.p);
        ts::Word argWords[16];
        for (int i = 0; i < argc && i < 16; i++) {
            std::memcpy(&argWords[i], &args[i], sizeof(ts::Word));
        }
        ts::Word r = vm->impl.callFunction(block, argWords, (u16)argc);
        langx_word out;
        std::memcpy(&out, &r, sizeof(langx_word));
        return out;
    }

    // Not found
    langx_word zero;
    zero.i = 0;
    return zero;
}
