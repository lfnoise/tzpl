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
//  opcodes.hpp
//  lang
//
//  Direct-threaded instruction handlers for register-based VM
//  Each handler tail-calls the next via [[clang::musttail]]
//

#ifndef opcodes_hpp
#define opcodes_hpp

#include "vm.hpp"

namespace ts {

// --- Load/Store ---
void op_load_int_const(VM& vm, Code* pc);     // LOAD_INT_CONST Rd, K (3 words)
void op_load_float_const(VM& vm, Code* pc);   // LOAD_FLOAT_CONST Rd, K (3 words)
void op_load_bool_true(VM& vm, Code* pc);     // LOAD_BOOL_TRUE Rd (2 words)
void op_load_bool_false(VM& vm, Code* pc);    // LOAD_BOOL_FALSE Rd (2 words)
void op_load_nil(VM& vm, Code* pc);           // LOAD_NIL Rd (2 words)
void op_load_obj(VM& vm, Code* pc);           // LOAD_OBJ Rd, idx (2 words, idx in regs[1])
void op_mov(VM& vm, Code* pc);               // MOV Rd, Ra (2 words)
void op_move_n(VM& vm, Code* pc);             // MOV_N Rd, Ra, N (3 words: copy N consecutive Words)
void op_load_global(VM& vm, Code* pc);        // LOAD_GLOBAL Rd, K (3 words, K is global index)
void op_store_global(VM& vm, Code* pc);       // STORE_GLOBAL Ra, K (3 words, K is global index)
void op_store_global_obj(VM& vm, Code* pc);   // STORE_GLOBAL_OBJ Ra, K (3 words, retains new, releases old)
void op_init_global_obj(VM& vm, Code* pc);    // INIT_GLOBAL_OBJ Ra, K (3 words, retains new only)

// Phase 4g.5: inline-composite globals hold their payload inline across
// sizeWords_ consecutive globals_ slots. Type* on the instruction stream
// tells the handler the layout (for ARC) and the word count (for copy).
void op_load_global_inline(VM& vm, Code* pc);  // LOAD_GLOBAL_I  Rd, K (4 words: op, regs, K, Type*)
void op_store_global_inline(VM& vm, Code* pc); // STORE_GLOBAL_I Ra, K (4 words: op, regs, K, Type*) -- release old, retain new
void op_init_global_inline(VM& vm, Code* pc);  // INIT_GLOBAL_I  Ra, K (4 words: op, regs, K, Type*) -- retain new only

// --- Integer Arithmetic ---
void op_add_int(VM& vm, Code* pc);            // ADD_INT Rd, Ra, Rb (2 words)
void op_sub_int(VM& vm, Code* pc);            // SUB_INT Rd, Ra, Rb (2 words)
void op_mul_int(VM& vm, Code* pc);            // MUL_INT Rd, Ra, Rb (2 words)
void op_div_int(VM& vm, Code* pc);            // DIV_INT Rd, Ra, Rb (2 words)
void op_mod_int(VM& vm, Code* pc);            // MOD_INT Rd, Ra, Rb (2 words)
void op_neg_int(VM& vm, Code* pc);            // NEG_INT Rd, Ra (2 words)

// --- Float Arithmetic ---
void op_add_float(VM& vm, Code* pc);          // ADD_FLOAT Rd, Ra, Rb (2 words)
void op_sub_float(VM& vm, Code* pc);          // SUB_FLOAT Rd, Ra, Rb (2 words)
void op_mul_float(VM& vm, Code* pc);          // MUL_FLOAT Rd, Ra, Rb (2 words)
void op_div_float(VM& vm, Code* pc);          // DIV_FLOAT Rd, Ra, Rb (2 words)
void op_neg_float(VM& vm, Code* pc);          // NEG_FLOAT Rd, Ra (2 words)

// --- Fraction Arithmetic ---
void op_add_fraction(VM& vm, Code* pc);       // ADD_FRACTION Rd, Ra, Rb (2 words)
void op_sub_fraction(VM& vm, Code* pc);       // SUB_FRACTION Rd, Ra, Rb (2 words)
void op_mul_fraction(VM& vm, Code* pc);       // MUL_FRACTION Rd, Ra, Rb (2 words)
void op_div_fraction(VM& vm, Code* pc);       // DIV_FRACTION Rd, Ra, Rb (2 words)
void op_neg_fraction(VM& vm, Code* pc);       // NEG_FRACTION Rd, Ra (2 words)

// --- Fraction Comparison ---
void op_cmp_eq_fraction(VM& vm, Code* pc);    // CMP_EQ_FRACTION Rd, Ra, Rb (2 words)
void op_cmp_ne_fraction(VM& vm, Code* pc);    // CMP_NE_FRACTION Rd, Ra, Rb (2 words)
void op_cmp_lt_fraction(VM& vm, Code* pc);    // CMP_LT_FRACTION Rd, Ra, Rb (2 words)
void op_cmp_le_fraction(VM& vm, Code* pc);    // CMP_LE_FRACTION Rd, Ra, Rb (2 words)
void op_cmp_gt_fraction(VM& vm, Code* pc);    // CMP_GT_FRACTION Rd, Ra, Rb (2 words)
void op_cmp_ge_fraction(VM& vm, Code* pc);    // CMP_GE_FRACTION Rd, Ra, Rb (2 words)

// --- Complex Arithmetic ---
void op_add_complex(VM& vm, Code* pc);        // ADD_COMPLEX Rd, Ra, Rb (2 words)
void op_sub_complex(VM& vm, Code* pc);        // SUB_COMPLEX Rd, Ra, Rb (2 words)
void op_mul_complex(VM& vm, Code* pc);        // MUL_COMPLEX Rd, Ra, Rb (2 words)
void op_div_complex(VM& vm, Code* pc);        // DIV_COMPLEX Rd, Ra, Rb (2 words)
void op_neg_complex(VM& vm, Code* pc);        // NEG_COMPLEX Rd, Ra (2 words)

// --- Complex Comparison ---
void op_cmp_eq_complex(VM& vm, Code* pc);     // CMP_EQ_COMPLEX Rd, Ra, Rb (2 words)
void op_cmp_ne_complex(VM& vm, Code* pc);     // CMP_NE_COMPLEX Rd, Ra, Rb (2 words)

// --- Complex / Fraction Boxing (Phase 4f) ---
void op_box_complex(VM& vm, Code* pc);        // dst.o = new Complex(src[0].f, src[1].f)
void op_unbox_complex(VM& vm, Code* pc);      // dst[0..1].f = ((Complex*)src.o)->x
void op_box_fraction(VM& vm, Code* pc);       // dst.o = new Fraction(src[0].i, src[1].i)
void op_unbox_fraction(VM& vm, Code* pc);     // dst[0..1].i = ((Fraction*)src.o)->r

// --- Generic boxing for inline structs / tuples (Phase 4g.2) ---
// Same shape as the Complex/Fraction box ops but parameterised by the
// composite's StructType* / TupleType* on the instruction stream so the
// handler knows the slot's sizeWords. Storage boundaries (globals, ObjArray
// elements, Map/Set keys/values) keep using a 1-word Obj* slot containing
// the boxed Struct*/Tuple*; codegen unboxes back into the multi-word
// inline slot on read.
void op_box_struct(VM& vm, Code* pc);         // dst.o = boxed Struct of N inline words
void op_unbox_struct(VM& vm, Code* pc);       // dst[0..N-1] = ((Struct*)src.o)->v[0..N-1]
void op_box_tuple(VM& vm, Code* pc);          // dst.o = boxed Tuple of N inline words
void op_unbox_tuple(VM& vm, Code* pc);        // dst[0..N-1] = ((Tuple*)src.o)->v[0..N-1]

// --- Complex Inline Arithmetic (Phase 4f scaffolding) ---
// Operand and dst regs name the FIRST word of a 2-word slot:
//   word[0] = real (f64), word[1] = imag (f64).
void op_add_complex_inline(VM& vm, Code* pc);
void op_sub_complex_inline(VM& vm, Code* pc);
void op_mul_complex_inline(VM& vm, Code* pc);
void op_div_complex_inline(VM& vm, Code* pc);
void op_neg_complex_inline(VM& vm, Code* pc);
void op_cmp_eq_complex_inline(VM& vm, Code* pc);
void op_cmp_ne_complex_inline(VM& vm, Code* pc);

// --- Conversion ---
void op_int_to_float(VM& vm, Code* pc);       // INT_TO_FLOAT Rd, Ra (2 words)
void op_float_to_int(VM& vm, Code* pc);       // FLOAT_TO_INT Rd, Ra (2 words)
void op_int_to_fraction(VM& vm, Code* pc);    // INT_TO_FRACTION Rd, Ra (2 words)
void op_int_to_complex(VM& vm, Code* pc);     // INT_TO_COMPLEX Rd, Ra (2 words)
void op_fraction_to_float(VM& vm, Code* pc);  // FRACTION_TO_FLOAT Rd, Ra (2 words)
void op_fraction_to_complex(VM& vm, Code* pc);// FRACTION_TO_COMPLEX Rd, Ra (2 words)
void op_float_to_complex(VM& vm, Code* pc);   // FLOAT_TO_COMPLEX Rd, Ra (2 words)
void op_fraction_to_int(VM& vm, Code* pc);    // FRACTION_TO_INT Rd, Ra (2 words)
void op_complex_to_float(VM& vm, Code* pc);   // COMPLEX_TO_FLOAT Rd, Ra (2 words)
void op_complex_to_int(VM& vm, Code* pc);     // COMPLEX_TO_INT Rd, Ra (2 words)
void op_complex_to_fraction(VM& vm, Code* pc); // COMPLEX_TO_FRACTION Rd, Ra (2 words)

// --- Construction ---
void op_make_complex(VM& vm, Code* pc);       // MAKE_COMPLEX Rd, Ra(real), Rb(imag) (2 words)
void op_int_div(VM& vm, Code* pc);            // INT_DIV Rd, Ra, Rb (2 words) - truncating integer division

// --- Integer Comparison ---
void op_cmp_eq_int(VM& vm, Code* pc);         // CMP_EQ_INT Rd, Ra, Rb (2 words)
void op_cmp_ne_int(VM& vm, Code* pc);         // CMP_NE_INT Rd, Ra, Rb (2 words)
void op_cmp_lt_int(VM& vm, Code* pc);         // CMP_LT_INT Rd, Ra, Rb (2 words)
void op_cmp_le_int(VM& vm, Code* pc);         // CMP_LE_INT Rd, Ra, Rb (2 words)
void op_cmp_gt_int(VM& vm, Code* pc);         // CMP_GT_INT Rd, Ra, Rb (2 words)
void op_cmp_ge_int(VM& vm, Code* pc);         // CMP_GE_INT Rd, Ra, Rb (2 words)

// --- Float Comparison ---
void op_cmp_eq_float(VM& vm, Code* pc);       // CMP_EQ_FLOAT Rd, Ra, Rb (2 words)
void op_cmp_ne_float(VM& vm, Code* pc);       // CMP_NE_FLOAT Rd, Ra, Rb (2 words)
void op_cmp_lt_float(VM& vm, Code* pc);       // CMP_LT_FLOAT Rd, Ra, Rb (2 words)
void op_cmp_le_float(VM& vm, Code* pc);       // CMP_LE_FLOAT Rd, Ra, Rb (2 words)
void op_cmp_gt_float(VM& vm, Code* pc);       // CMP_GT_FLOAT Rd, Ra, Rb (2 words)
void op_cmp_ge_float(VM& vm, Code* pc);       // CMP_GE_FLOAT Rd, Ra, Rb (2 words)

// --- Bitwise Integer ---
void op_bitand_int(VM& vm, Code* pc);         // BITAND_INT Rd, Ra, Rb (2 words)
void op_bitor_int(VM& vm, Code* pc);          // BITOR_INT Rd, Ra, Rb (2 words)
void op_bitxor_int(VM& vm, Code* pc);         // BITXOR_INT Rd, Ra, Rb (2 words)
void op_bitnot_int(VM& vm, Code* pc);         // BITNOT_INT Rd, Ra (2 words)
void op_shl_int(VM& vm, Code* pc);            // SHL_INT Rd, Ra, Rb (2 words)
void op_shr_int(VM& vm, Code* pc);            // SHR_INT Rd, Ra, Rb (2 words)
void op_ushr_int(VM& vm, Code* pc);           // USHR_INT Rd, Ra, Rb (2 words)

// --- Logic ---
void op_not_bool(VM& vm, Code* pc);           // NOT_BOOL Rd, Ra (2 words)
void op_and_bool(VM& vm, Code* pc);           // AND_BOOL Rd, Ra, Rb (2 words)
void op_or_bool(VM& vm, Code* pc);            // OR_BOOL Rd, Ra, Rb (2 words)

// --- Control Flow ---
void op_jump(VM& vm, Code* pc);               // JUMP L (2 words, L is Code*)
void op_jump_if_true(VM& vm, Code* pc);       // JUMP_IF_TRUE Ra, L (3 words)
void op_jump_if_false(VM& vm, Code* pc);      // JUMP_IF_FALSE Ra, L (3 words)

// CALL Rd, callee_global, argc (4 words: op, regs{Rd,argc,argBase}, callee_global_idx, unused)
void op_call(VM& vm, Code* pc);
// CALL_PRIMITIVE Rd, argc, argBase, global_idx (3 words: op, regs{Rd,argc,argBase}, global_idx)
void op_call_primitive(VM& vm, Code* pc);
void op_return(VM& vm, Code* pc);             // RETURN Ra (2 words)
void op_return_void(VM& vm, Code* pc);        // RETURN_VOID (1 word)
void op_halt(VM& vm, Code* pc);               // HALT (1 word)

// --- Debug/Print ---
void op_print_int(VM& vm, Code* pc);          // PRINT_INT Ra (2 words)
void op_print_float(VM& vm, Code* pc);        // PRINT_FLOAT Ra (2 words)
void op_print_bool(VM& vm, Code* pc);         // PRINT_BOOL Ra (2 words)
void op_print_obj(VM& vm, Code* pc);          // PRINT_OBJ Ra (2 words)
void op_println(VM& vm, Code* pc);            // PRINTLN (1 word) - prints newline

// --- Generic Object Comparison ---
void op_cmp_eq_obj(VM& vm, Code* pc);         // CMP_EQ_OBJ Rd, Ra, Rb (3 words: op, regs, operandType*)
void op_cmp_ne_obj(VM& vm, Code* pc);         // CMP_NE_OBJ Rd, Ra, Rb (3 words: op, regs, operandType*)

// --- Symbol Print ---
void op_print_symbol(VM& vm, Code* pc);       // PRINT_SYMBOL Ra (2 words)

// --- String Operations ---
void op_concat_str(VM& vm, Code* pc);         // CONCAT_STR Rd, Ra, Rb (2 words)
void op_cmp_eq_str(VM& vm, Code* pc);         // CMP_EQ_STR Rd, Ra, Rb (2 words)
void op_cmp_ne_str(VM& vm, Code* pc);         // CMP_NE_STR Rd, Ra, Rb (2 words)
void op_cmp_lt_str(VM& vm, Code* pc);         // CMP_LT_STR Rd, Ra, Rb (2 words)
void op_cmp_le_str(VM& vm, Code* pc);         // CMP_LE_STR Rd, Ra, Rb (2 words)
void op_cmp_gt_str(VM& vm, Code* pc);         // CMP_GT_STR Rd, Ra, Rb (2 words)
void op_cmp_ge_str(VM& vm, Code* pc);         // CMP_GE_STR Rd, Ra, Rb (2 words)
void op_string_get_byte(VM& vm, Code* pc);    // STRING_GET_BYTE Rd, Rs, Ri (2 words: op, regs{dst, str, idx})

// --- Composite (Array/Tuple) Arithmetic ---
// Binary: 5 words [op][regs: Rd, Ra, Rb][resultType*][aType*][bType*]
void op_add_composite(VM& vm, Code* pc);
void op_sub_composite(VM& vm, Code* pc);
void op_mul_composite(VM& vm, Code* pc);
void op_div_composite(VM& vm, Code* pc);

// Unary: 4 words [op][regs: Rd, Ra][resultType*][aType*]
void op_neg_composite(VM& vm, Code* pc);
void op_not_composite(VM& vm, Code* pc);
void op_bitnot_composite(VM& vm, Code* pc);

// Comparison: 5 words [op][regs: Rd, Ra, Rb][resultType*][aType*][bType*]
void op_cmp_eq_composite(VM& vm, Code* pc);
void op_cmp_ne_composite(VM& vm, Code* pc);
void op_cmp_lt_composite(VM& vm, Code* pc);
void op_cmp_le_composite(VM& vm, Code* pc);
void op_cmp_gt_composite(VM& vm, Code* pc);
void op_cmp_ge_composite(VM& vm, Code* pc);

// --- Inline-storage variants (Phase 4g.7) ---
// Operate directly on multi-word inline tuple/struct register slots
// without boxing/unboxing. Result type must be Repr::Inline (not Complex/
// Fraction). Operands may be Inline composites, heap Tuples, or scalars
// (broadcast). Same encoding as the heap variants.
void op_add_composite_inline(VM& vm, Code* pc);
void op_sub_composite_inline(VM& vm, Code* pc);
void op_mul_composite_inline(VM& vm, Code* pc);
void op_div_composite_inline(VM& vm, Code* pc);
void op_neg_composite_inline(VM& vm, Code* pc);
void op_not_composite_inline(VM& vm, Code* pc);
void op_bitnot_composite_inline(VM& vm, Code* pc);
void op_cmp_eq_composite_inline(VM& vm, Code* pc);
void op_cmp_ne_composite_inline(VM& vm, Code* pc);
void op_cmp_lt_composite_inline(VM& vm, Code* pc);
void op_cmp_le_composite_inline(VM& vm, Code* pc);
void op_cmp_gt_composite_inline(VM& vm, Code* pc);
void op_cmp_ge_composite_inline(VM& vm, Code* pc);

// --- Array/Tuple/List Concatenation ---
void op_concat_array(VM& vm, Code* pc);    // CONCAT_ARRAY Rd, Ra, Rb (3 words: op, regs, ArrayType*)
void op_concat_tuple(VM& vm, Code* pc);    // CONCAT_TUPLE Rd, Ra, Rb (5 words: op, regs, resultType*, leftType*, rightType*)
void op_concat_list(VM& vm, Code* pc);     // CONCAT_LIST Rd, Ra, Rb (3 words: op, regs, ListType*)

// --- Array/Tuple/Struct Access/Construction ---
void op_make_array(VM& vm, Code* pc);         // MAKE_ARRAY Rd, firstSrc, numElems (3 words: op, regs, ArrayType*)
void op_tuple_get(VM& vm, Code* pc);          // TUPLE_GET Rd, Ra, fieldIdx (3 words: op, regs, TupleType*)
void op_tuple_slice(VM& vm, Code* pc);        // TUPLE_SLICE Rd, Ra, startIdx (3 words: op, regs, TupleType*)
void op_make_tuple(VM& vm, Code* pc);         // MAKE_TUPLE Rd, firstSrc, numFields (3 words: op, regs, TupleType*)
void op_make_tuple_heap(VM& vm, Code* pc);    // MAKE_TUPLE_HEAP Rd, firstSrc, numFields (3 words) -- always heap, ignores Inline classification (for variadic packs)
void op_make_struct(VM& vm, Code* pc);        // MAKE_STRUCT Rd, firstSrc, numFields (3 words: op, regs, StructType*)
void op_struct_get(VM& vm, Code* pc);         // STRUCT_GET Rd, Ra, fieldIdx (3 words: op, regs, StructType*)
void op_inline_tuple_get(VM& vm, Code* pc);   // I_TUPLE_GET Rd, Ra, fieldIdx (3 words: op, regs, TupleType*)
void op_inline_struct_get(VM& vm, Code* pc);  // I_STRUCT_GET Rd, Ra, fieldIdx (3 words: op, regs, StructType*)

// --- Array Destructuring ---
void op_array_get(VM& vm, Code* pc);          // ARRAY_GET Rd, Ra, idx (3 words: op, regs, ArrayType*)
void op_array_slice(VM& vm, Code* pc);        // ARRAY_SLICE Rd, Ra, startIdx (3 words: op, regs, ArrayType*)
void op_array_length(VM& vm, Code* pc);       // ARRAY_LENGTH Rd, Ra (3 words: op, regs, ArrayType*)

// --- Enum Construction/Access ---
void op_make_enum(VM& vm, Code* pc);          // MAKE_ENUM Rd, valSrc, caseIdx (3 words: op, regs, EnumType*)
void op_make_enum_nodata(VM& vm, Code* pc);   // MAKE_ENUM_NODATA Rd, caseIdx (3 words: op, regs, EnumType*)
void op_enum_get_which(VM& vm, Code* pc);     // ENUM_GET_WHICH Rd, Ra (2 words) - get case index as int
void op_enum_get_value(VM& vm, Code* pc);     // ENUM_GET_VALUE Rd, Ra (3 words: op, regs, caseType*)

// --- Inline Enum Construction/Access (Phase 4g.4) ---
// Inline enum slot layout: word 0 = i64 discriminant; words 1..1+P = payload
// where P = sizeWords of the active case payload (0 for Void cases).
// Tag-read is just MOV from word 0; payload-read is MOVE_N from word 1.
// Construction/box/unbox need dedicated handlers because the payload width
// is per-case.
void op_make_inline_enum(VM& vm, Code* pc);        // MAKE_INLINE_ENUM Rd, valSrc, caseIdx (3 words: op, regs, EnumType*)
void op_make_inline_enum_nodata(VM& vm, Code* pc); // MAKE_INLINE_ENUM_NODATA Rd, caseIdx (3 words: op, regs, EnumType*)
void op_box_enum(VM& vm, Code* pc);                // BOX_ENUM Rd, Ra (3 words: op, regs, EnumType*) - inline -> heap Enum*
void op_unbox_enum(VM& vm, Code* pc);              // UNBOX_ENUM Rd, Ra (3 words: op, regs, EnumType*) - heap Enum* -> inline

// --- Dynamic Array Operations (for auto-mapping) ---
void op_array_alloc(VM& vm, Code* pc);        // ARRAY_ALLOC Rd, Rn (3 words: op, regs{dst, len_reg}, ArrayType*)
void op_array_set(VM& vm, Code* pc);          // ARRAY_SET Ra, Rb_idx, Rc_val (3 words: op, regs{arr, idx_reg, val_reg}, ArrayType*)
void op_array_get_dyn(VM& vm, Code* pc);      // ARRAY_GET_DYN Rd, Ra, Rb_idx (3 words: op, regs{dst, arr, idx_reg}, ArrayType*)

// --- List ---
void op_cons(VM& vm, Code* pc);            // CONS Rd, Rhead, Rtail (3 words: op, regs{dst, head, tail}, ListType*)
void op_make_list(VM& vm, Code* pc);       // MAKE_LIST Rd, firstSrc, count (3 words: op, regs{dst, firstSrc, count}, ListType*)
void op_list_head(VM& vm, Code* pc);       // LIST_HEAD Rd, Ra (2 words)
void op_list_tail(VM& vm, Code* pc);       // LIST_TAIL Rd, Ra (2 words)
void op_list_is_nil(VM& vm, Code* pc);     // LIST_IS_NIL Rd, Ra (2 words) - sets dst to 1 if src is null, 0 otherwise

// --- List Print Limit ---
void op_get_list_print_limit(VM& vm, Code* pc);   // GET_LIST_PRINT_LIMIT Rd (2 words)
void op_set_list_print_limit(VM& vm, Code* pc);   // SET_LIST_PRINT_LIMIT Rd, Ra (2 words) - returns old value

// --- Range ---
void op_make_range(VM& vm, Code* pc);     // MAKE_RANGE Rd, Rstart, Rend, Rstep (4 words: op, regs, RangeType*, flags)

// --- Lambda ---
void op_make_lambda(VM& vm, Code* pc);        // MAKE_LAMBDA Rd, captureBase, numFreeVars (4 words: op, regs, CodeBlock*, LambdaType*)
void op_call_lambda(VM& vm, Code* pc);        // CALL_LAMBDA Rd, argc, argBase, calleeReg (2 words: op, regs)
void op_func_ref(VM& vm, Code* pc);           // FUNC_REF Rd (3 words: op, regs, globalIndex as i64, LambdaType*)

// --- Template Lambda ---
void op_make_template_lambda(VM& vm, Code* pc);        // MAKE_TEMPLATE_LAMBDA Rd, captureBase, numFreeVars (3 words: op, regs, TemplateLambdaType*)
void op_call_template_lambda(VM& vm, Code* pc);        // CALL_TEMPLATE_LAMBDA Rd, argc, argBase, calleeReg (3 words: op, regs, CodeBlock*)
void op_tail_call_template_lambda(VM& vm, Code* pc);   // TAIL_CALL_TEMPLATE_LAMBDA unused, argc, argBase, calleeReg (3 words: op, regs, CodeBlock*)
void op_specialize_lambda(VM& vm, Code* pc);           // SPECIALIZE_LAMBDA Rd, srcReg (3 words: op, regs, LambdaType*)

// --- Tail Calls ---
void op_tail_call(VM& vm, Code* pc);          // TAIL_CALL unused, argc, argBase, callee_global (3 words: op, regs, global_idx)
void op_tail_call_lambda(VM& vm, Code* pc);   // TAIL_CALL_LAMBDA unused, argc, argBase, calleeReg (2 words: op, regs)

// --- Lazy auto-map ---
void op_make_lazy_automap(VM& vm, Code* pc);  // MAKE_LAZY_AUTOMAP Rd, RsrcList, RbroadcastBase, numBroadcast (3 words: op, regs, AutoMapCallInfo*)

// --- Map ---
void op_make_map(VM& vm, Code* pc);       // MAKE_MAP Rd, firstKeyReg, numPairs (3 words: op, regs, MapType*)
void op_map_get(VM& vm, Code* pc);        // MAP_GET Rd, Ra(map), Rb(key) (3 words: op, regs, MapType*)
void op_map_get_option(VM& vm, Code* pc); // MAP_GET_OPTION Rd, Ra(map), Rb(key) (3 words: op, regs, EnumType*)

// --- Set ---
void op_make_set(VM& vm, Code* pc);       // MAKE_SET Rd, firstSrc, numElems (3 words: op, regs, SetType*)

// --- Ref ---
void op_make_ref(VM& vm, Code* pc);           // MAKE_REF Rd, Rval (3 words: op, regs{dst, val}, RefType*)
void op_ref_get(VM& vm, Code* pc);            // REF_GET Rd, Ra (2 words: op, regs{dst, ref})
void op_ref_set(VM& vm, Code* pc);            // REF_SET Rd, Ra, Rb (3 words: op, regs{dst, ref, val}, RefType*)

// Phase 4g.5: Ref to an inline composite. The payload lives inline in
// InlineRef::v[]; ops copy multi-word slots in/out without per-store
// heap allocation. Same operand shape as the 1-word ops; the RefType*
// on the instruction stream tells the handler the slot's sizeWords.
void op_make_ref_inline(VM& vm, Code* pc);    // MAKE_REF_INLINE Rd, Rval (3 words: op, regs, RefType*)
void op_ref_get_inline(VM& vm, Code* pc);     // REF_GET_INLINE Rd, Ra (3 words: op, regs, RefType*)
void op_ref_set_inline(VM& vm, Code* pc);     // REF_SET_INLINE Rd, Ra, Rb (3 words: op, regs, RefType*)

// --- Coroutines ---
void op_coro_create(VM& vm, Code* pc);        // CORO_CREATE Rd, argBase, argc (4 words: op, regs, global_idx, CoroutineType*)
void op_coro_create_lambda(VM& vm, Code* pc); // CORO_CREATE_LAMBDA Rd, argBase, argc, lambdaReg (3 words: op, regs, CoroutineType*)
void op_coro_resume(VM& vm, Code* pc);        // CORO_RESUME Rd, Rcoro (2 words: op, regs{dst, coroReg})
void op_yield(VM& vm, Code* pc);              // YIELD Rsrc, gcMapIndex (2 words: op, regs{src, gcMapIdx})
void op_coro_done(VM& vm, Code* pc);          // CORO_DONE (1 word: op)
void op_coro_is_done(VM& vm, Code* pc);       // CORO_IS_DONE Rd, Rcoro (2 words: op, regs{dst, coroReg})
void op_coro_wrap_option(VM& vm, Code* pc);   // CORO_WRAP_OPTION Rd, Rval, Rcoro (3 words: op, regs{dst, val, coro}, optionType*)

// --- Dynamic Scope ---
void op_load_dynamic(VM& vm, Code* pc);       // LOAD_DYNAMIC Rd, K (3 words, K is dynvar index)
void op_store_dynamic(VM& vm, Code* pc);      // STORE_DYNAMIC Ra, K (3 words, K is dynvar index)
void op_store_dynamic_obj(VM& vm, Code* pc);  // STORE_DYNAMIC_OBJ Ra, K (3 words, retains new, releases old)
void op_init_dynamic_obj(VM& vm, Code* pc);   // INIT_DYNAMIC_OBJ Ra, K (3 words, retains new only)
void op_dynscope_push(VM& vm, Code* pc);      // DYNSCOPE_PUSH Ra, K (3 words: save current, set new)

// Phase 4g.5: inline-composite dynvars hold their payload inline across
// sizeWords_ consecutive dynVars_ slots. Type* on the instruction stream
// gives the layout for ARC walking and the word count for copy.
void op_load_dynamic_inline(VM& vm, Code* pc);  // LOAD_DYNAMIC_I  Rd, K (4 words: op, regs, K, Type*)
void op_store_dynamic_inline(VM& vm, Code* pc); // STORE_DYNAMIC_I Ra, K (4 words: op, regs, K, Type*) -- release old, retain new
void op_init_dynamic_inline(VM& vm, Code* pc);  // INIT_DYNAMIC_I  Ra, K (4 words: op, regs, K, Type*) -- retain new only
void op_dynscope_push_inline(VM& vm, Code* pc); // DYNSCOPE_PUSH_I Ra, K (4 words: op, regs, K, Type*) -- save N words, set N words

// --- Any ---
void op_make_any(VM& vm, Code* pc);          // MAKE_ANY Rd, Rsrc, isObj (3 words: op, regs{dst, src, isObj}, Type*)
void op_any_get_value(VM& vm, Code* pc);     // ANY_GET_VALUE Rd, Ra (2 words)
void op_any_get_type_ptr(VM& vm, Code* pc);  // ANY_GET_TYPE_PTR Rd, Ra (2 words)

} // namespace ts

#endif /* opcodes_hpp */
