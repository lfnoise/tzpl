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
//  disassemble.cpp
//  lang
//
//  Disassembler for CodeBlock instruction streams.
//  Maps direct-threaded Operation function pointers back to names.
//

#include "disassemble.hpp"
#include "opcodes.hpp"
#include "value.hpp"
#include "type_system.hpp"
#include <unordered_map>
#include <cstdio>
#include <format>
#include <vector>

namespace ts {

// Instruction format categories -- controls how extra words after the regs are printed
enum class OpFmt {
    None,           // 1 word: just opcode
    Regs,           // 2 words: op + regs
    Regs_Imm16,     // 2 words: op + regs{dst, src, (u16)(i16)imm} -- imm printed as signed
    Regs_Int,       // 3 words: op + regs + i64 (global index, constant, etc.)
    Regs_Float,     // 3 words: op + regs + f64
    Regs_Ptr,       // 3 words: op + regs + pointer (type, CodeBlock, etc.)
    Jump,           // 2 words: op + Code* target
    Regs_Jump,      // 3 words: op + regs + Code* target
    Regs_Ptr_Ptr,   // 4 words: op + regs + ptr + ptr
    Regs_Int_Ptr,   // 4 words: op + regs + i64 + ptr
    Regs_Ptr_Flags, // 4 words: op + regs + ptr + i64(flags)
    Regs_Ptr3,      // 5 words: op + regs + ptr + ptr + ptr
};

struct OpInfo {
    const char* name;
    u32 size;          // Total instruction size in Code words
    OpFmt fmt;
    u8 nregs;          // Number of meaningful register slots to display (0-4)
};

// Actual word count of the instruction at `pc` (handles the variable-length
// ops the fixed size field cannot express).
static u32 instrWords(Code const* pc, OpInfo const& info) {
    if (pc->op == op_pack_existential) return 5 + (u32)pc[4].i;
    return info.size;
}

static const std::unordered_map<Operation, OpInfo>& opInfoMap() {
    static const std::unordered_map<Operation, OpInfo> table = {
        // --- Load/Store ---
        { op_load_int_const,    { "LOAD_INT",          3, OpFmt::Regs_Int,   1 } },
        { op_load_float_const,  { "LOAD_FLOAT",        3, OpFmt::Regs_Float, 1 } },
        { op_load_bool_true,    { "LOAD_TRUE",         2, OpFmt::Regs,       1 } },
        { op_load_bool_false,   { "LOAD_FALSE",        2, OpFmt::Regs,       1 } },
        { op_load_nil,          { "LOAD_NIL",          2, OpFmt::Regs,       1 } },
        { op_load_obj,          { "LOAD_OBJ",          2, OpFmt::Regs,       2 } },
        { op_mov,               { "MOV",               2, OpFmt::Regs,       2 } },
        { op_move_n,            { "MOV_N",             3, OpFmt::Regs_Int,   2 } },
        { op_load_global,       { "LOAD_GLOBAL",       3, OpFmt::Regs_Int,   1 } },
        { op_store_global,      { "STORE_GLOBAL",      3, OpFmt::Regs_Int,   1 } },
        { op_store_global_obj,  { "STORE_GLOBAL_OBJ",  3, OpFmt::Regs_Int,   1 } },
        { op_init_global_obj,   { "INIT_GLOBAL_OBJ",   3, OpFmt::Regs_Int,   1 } },
        { op_load_global_inline,  { "LOAD_GLOBAL_I",  4, OpFmt::Regs_Int_Ptr, 1 } },
        { op_store_global_inline, { "STORE_GLOBAL_I", 4, OpFmt::Regs_Int_Ptr, 1 } },
        { op_init_global_inline,  { "INIT_GLOBAL_I",  4, OpFmt::Regs_Int_Ptr, 1 } },

        // --- Integer Arithmetic ---
        { op_add_int,           { "ADD_INT",           2, OpFmt::Regs, 3 } },
        { op_sub_int,           { "SUB_INT",           2, OpFmt::Regs, 3 } },
        { op_mul_int,           { "MUL_INT",           2, OpFmt::Regs, 3 } },
        { op_div_int,           { "DIV_INT",           2, OpFmt::Regs, 3 } },
        { op_mod_int,           { "MOD_INT",           2, OpFmt::Regs, 3 } },
        { op_neg_int,           { "NEG_INT",           2, OpFmt::Regs, 2 } },

        // --- Integer arithmetic, i16 immediate ---
        { op_add_int_imm,       { "ADDI_INT",          2, OpFmt::Regs_Imm16, 2 } },
        { op_sub_int_imm,       { "SUBI_INT",          2, OpFmt::Regs_Imm16, 2 } },
        { op_mul_int_imm,       { "MULI_INT",          2, OpFmt::Regs_Imm16, 2 } },
        { op_cmp_eq_int_imm,    { "EQI_INT",           2, OpFmt::Regs_Imm16, 2 } },
        { op_cmp_ne_int_imm,    { "NEI_INT",           2, OpFmt::Regs_Imm16, 2 } },
        { op_cmp_lt_int_imm,    { "LTI_INT",           2, OpFmt::Regs_Imm16, 2 } },
        { op_cmp_le_int_imm,    { "LEI_INT",           2, OpFmt::Regs_Imm16, 2 } },
        { op_cmp_gt_int_imm,    { "GTI_INT",           2, OpFmt::Regs_Imm16, 2 } },
        { op_cmp_ge_int_imm,    { "GEI_INT",           2, OpFmt::Regs_Imm16, 2 } },

        // --- Float Arithmetic ---
        { op_add_float,         { "ADD_FLOAT",         2, OpFmt::Regs, 3 } },
        { op_sub_float,         { "SUB_FLOAT",         2, OpFmt::Regs, 3 } },
        { op_mul_float,         { "MUL_FLOAT",         2, OpFmt::Regs, 3 } },
        { op_div_float,         { "DIV_FLOAT",         2, OpFmt::Regs, 3 } },
        { op_neg_float,         { "NEG_FLOAT",         2, OpFmt::Regs, 2 } },

        // --- Fraction Arithmetic ---
        { op_add_fraction,      { "ADD_FRAC",          2, OpFmt::Regs, 3 } },
        { op_sub_fraction,      { "SUB_FRAC",          2, OpFmt::Regs, 3 } },
        { op_mul_fraction,      { "MUL_FRAC",          2, OpFmt::Regs, 3 } },
        { op_div_fraction,      { "DIV_FRAC",          2, OpFmt::Regs, 3 } },
        { op_div_int_to_fraction, { "DIV_INT_TO_FRAC", 2, OpFmt::Regs, 3 } },
        { op_neg_fraction,      { "NEG_FRAC",          2, OpFmt::Regs, 2 } },

        // --- Fraction Comparison ---
        { op_cmp_eq_fraction,   { "EQ_FRAC",           2, OpFmt::Regs, 3 } },
        { op_cmp_ne_fraction,   { "NE_FRAC",           2, OpFmt::Regs, 3 } },
        { op_cmp_lt_fraction,   { "LT_FRAC",           2, OpFmt::Regs, 3 } },
        { op_cmp_le_fraction,   { "LE_FRAC",           2, OpFmt::Regs, 3 } },
        { op_cmp_gt_fraction,   { "GT_FRAC",           2, OpFmt::Regs, 3 } },
        { op_cmp_ge_fraction,   { "GE_FRAC",           2, OpFmt::Regs, 3 } },

        // --- Complex Arithmetic ---
        { op_add_complex,       { "ADD_CMPLX",         2, OpFmt::Regs, 3 } },
        { op_sub_complex,       { "SUB_CMPLX",         2, OpFmt::Regs, 3 } },
        { op_mul_complex,       { "MUL_CMPLX",         2, OpFmt::Regs, 3 } },
        { op_div_complex,       { "DIV_CMPLX",         2, OpFmt::Regs, 3 } },
        { op_neg_complex,       { "NEG_CMPLX",         2, OpFmt::Regs, 2 } },

        // --- Complex Comparison ---
        { op_cmp_eq_complex,    { "EQ_CMPLX",          2, OpFmt::Regs, 3 } },
        { op_cmp_ne_complex,    { "NE_CMPLX",          2, OpFmt::Regs, 3 } },

        // --- Complex / Fraction Boxing (Phase 4f) ---
        { op_box_complex,           { "BOX_CMPLX",         2, OpFmt::Regs, 2 } },
        { op_unbox_complex,         { "UNBOX_CMPLX",       2, OpFmt::Regs, 2 } },
        { op_box_fraction,          { "BOX_FRAC",          2, OpFmt::Regs, 2 } },
        { op_unbox_fraction,        { "UNBOX_FRAC",        2, OpFmt::Regs, 2 } },
        // --- Generic boxing for inline composites (Phase 4g.2) ---
        { op_box_struct,            { "BOX_STRUCT",        3, OpFmt::Regs_Ptr, 2 } },
        { op_unbox_struct,          { "UNBOX_STRUCT",      3, OpFmt::Regs_Ptr, 2 } },
        { op_box_tuple,             { "BOX_TUPLE",         3, OpFmt::Regs_Ptr, 2 } },
        { op_unbox_tuple,           { "UNBOX_TUPLE",       3, OpFmt::Regs_Ptr, 2 } },

        // --- Complex Inline Arithmetic (Phase 4f scaffolding) ---
        { op_add_complex_inline,    { "ADD_CMPLX_I",       2, OpFmt::Regs, 3 } },
        { op_sub_complex_inline,    { "SUB_CMPLX_I",       2, OpFmt::Regs, 3 } },
        { op_mul_complex_inline,    { "MUL_CMPLX_I",       2, OpFmt::Regs, 3 } },
        { op_div_complex_inline,    { "DIV_CMPLX_I",       2, OpFmt::Regs, 3 } },
        { op_neg_complex_inline,    { "NEG_CMPLX_I",       2, OpFmt::Regs, 2 } },
        { op_cmp_eq_complex_inline, { "EQ_CMPLX_I",        2, OpFmt::Regs, 3 } },
        { op_cmp_ne_complex_inline, { "NE_CMPLX_I",        2, OpFmt::Regs, 3 } },

        // --- Conversion ---
        { op_int_to_float,      { "INT_TO_FLOAT",      2, OpFmt::Regs, 2 } },
        { op_float_to_int,      { "FLOAT_TO_INT",      2, OpFmt::Regs, 2 } },
        { op_int_to_fraction,   { "INT_TO_FRAC",       2, OpFmt::Regs, 2 } },
        { op_int_to_complex,    { "INT_TO_CMPLX",      2, OpFmt::Regs, 2 } },
        { op_fraction_to_float, { "FRAC_TO_FLOAT",     2, OpFmt::Regs, 2 } },
        { op_fraction_to_complex,{ "FRAC_TO_CMPLX",    2, OpFmt::Regs, 2 } },
        { op_float_to_complex,  { "FLOAT_TO_CMPLX",    2, OpFmt::Regs, 2 } },
        { op_fraction_to_int,   { "FRAC_TO_INT",       2, OpFmt::Regs, 2 } },
        { op_complex_to_float,  { "CMPLX_TO_FLOAT",    2, OpFmt::Regs, 2 } },
        { op_complex_to_int,    { "CMPLX_TO_INT",      2, OpFmt::Regs, 2 } },
        { op_complex_to_fraction,{ "CMPLX_TO_FRAC",    2, OpFmt::Regs, 2 } },

        // --- Construction ---
        { op_make_complex,      { "MAKE_CMPLX",        2, OpFmt::Regs, 3 } },
        { op_int_div,           { "INT_DIV",           2, OpFmt::Regs, 3 } },

        // --- Integer Comparison ---
        { op_cmp_eq_int,        { "EQ_INT",            2, OpFmt::Regs, 3 } },
        { op_cmp_ne_int,        { "NE_INT",            2, OpFmt::Regs, 3 } },
        { op_cmp_lt_int,        { "LT_INT",            2, OpFmt::Regs, 3 } },
        { op_cmp_le_int,        { "LE_INT",            2, OpFmt::Regs, 3 } },
        { op_cmp_gt_int,        { "GT_INT",            2, OpFmt::Regs, 3 } },
        { op_cmp_ge_int,        { "GE_INT",            2, OpFmt::Regs, 3 } },

        // --- Float Comparison ---
        { op_cmp_eq_float,      { "EQ_FLOAT",          2, OpFmt::Regs, 3 } },
        { op_cmp_ne_float,      { "NE_FLOAT",          2, OpFmt::Regs, 3 } },
        { op_cmp_lt_float,      { "LT_FLOAT",          2, OpFmt::Regs, 3 } },
        { op_cmp_le_float,      { "LE_FLOAT",          2, OpFmt::Regs, 3 } },
        { op_cmp_gt_float,      { "GT_FLOAT",          2, OpFmt::Regs, 3 } },
        { op_cmp_ge_float,      { "GE_FLOAT",          2, OpFmt::Regs, 3 } },

        // --- Bitwise Integer ---
        { op_bitand_int,        { "BITAND",            2, OpFmt::Regs, 3 } },
        { op_bitor_int,         { "BITOR",             2, OpFmt::Regs, 3 } },
        { op_bitxor_int,        { "BITXOR",            2, OpFmt::Regs, 3 } },
        { op_bitnot_int,        { "BITNOT",            2, OpFmt::Regs, 2 } },
        { op_shl_int,           { "SHL",               2, OpFmt::Regs, 3 } },
        { op_shr_int,           { "SHR",               2, OpFmt::Regs, 3 } },
        { op_ushr_int,          { "USHR",              2, OpFmt::Regs, 3 } },

        // --- Logic ---
        { op_not_bool,          { "NOT",               2, OpFmt::Regs, 2 } },
        { op_and_bool,          { "AND",               2, OpFmt::Regs, 3 } },
        { op_or_bool,           { "OR",                2, OpFmt::Regs, 3 } },

        // --- Control Flow ---
        { op_jump,              { "JUMP",              2, OpFmt::Jump,      0 } },
        { op_jump_if_true,      { "JUMP_IF_TRUE",      3, OpFmt::Regs_Jump, 1 } },
        { op_jump_if_false,     { "JUMP_IF_FALSE",     3, OpFmt::Regs_Jump, 1 } },
        { op_safepoint,         { "SAFEPOINT",         1, OpFmt::None,      0 } },
        { op_call,              { "CALL",              3, OpFmt::Regs_Int,  3 } },
        { op_call_primitive,    { "CALL_PRIM",         3, OpFmt::Regs_Int,  3 } },
        { op_return,            { "RETURN",            2, OpFmt::Regs,      2 } },
        { op_return_void,       { "RETURN_VOID",       1, OpFmt::None,      0 } },
        { op_halt,              { "HALT",              1, OpFmt::None,      0 } },
        { op_no_match,          { "NO_MATCH",          1, OpFmt::None,      0 } },
        { op_undefined_function,{ "UNDEFINED_FN",      1, OpFmt::None,      0 } },

        // --- Async / futures ---
        { op_async_call,        { "ASYNC_CALL",        4, OpFmt::Regs_Int_Ptr, 3 } },
        { op_async_return,      { "ASYNC_RETURN",      2, OpFmt::Regs,      1 } },
        { op_future_await,      { "FUTURE_AWAIT",      2, OpFmt::Regs,      3 } },
        { op_future_block,      { "FUTURE_BLOCK",      2, OpFmt::Regs,      2 } },
        { op_future_ready,      { "FUTURE_READY",      3, OpFmt::Regs_Ptr,  2 } },
        { op_delay,             { "DELAY",             3, OpFmt::Regs_Ptr,  2 } },

        // --- Upvars / closures ---
        { op_capture_upvar_local, { "CAPTURE_UPVAR_L", 4, OpFmt::Regs_Int_Ptr, 3 } },
        { op_load_upvar_n,      { "LOAD_UPVAR_N",      2, OpFmt::Regs,      3 } },
        { op_store_upvar_n,     { "STORE_UPVAR_N",     3, OpFmt::Regs_Int,  3 } },

        // --- Existentials ---
        // PACK_EXISTENTIAL is variable-length: 5 + numMethods (pc[4].i)
        // words; both walkers special-case it via instrWords().
        { op_pack_existential,  { "PACK_EXISTENTIAL",  5, OpFmt::Regs_Ptr_Ptr, 2 } },
        { op_call_witness,      { "CALL_WITNESS",      3, OpFmt::Regs_Int,  3 } },

        // --- Debug/Print ---
        { op_print_int,         { "PRINT_INT",         2, OpFmt::Regs, 1 } },
        { op_print_float,       { "PRINT_FLOAT",       2, OpFmt::Regs, 1 } },
        { op_print_bool,        { "PRINT_BOOL",        2, OpFmt::Regs, 1 } },
        { op_print_obj,         { "PRINT_OBJ",         2, OpFmt::Regs, 1 } },
        { op_println,           { "PRINTLN",           1, OpFmt::None, 0 } },
        { op_print_symbol,      { "PRINT_SYMBOL",      2, OpFmt::Regs, 1 } },

        // --- Object Comparison ---
        { op_cmp_eq_obj,        { "EQ_OBJ",            3, OpFmt::Regs_Ptr, 3 } },
        { op_cmp_ne_obj,        { "NE_OBJ",            3, OpFmt::Regs_Ptr, 3 } },

        // --- String Operations ---
        { op_concat_str,        { "CONCAT_STR",        2, OpFmt::Regs, 3 } },
        { op_cmp_eq_str,        { "EQ_STR",            2, OpFmt::Regs, 3 } },
        { op_cmp_ne_str,        { "NE_STR",            2, OpFmt::Regs, 3 } },
        { op_cmp_lt_str,        { "LT_STR",            2, OpFmt::Regs, 3 } },
        { op_cmp_le_str,        { "LE_STR",            2, OpFmt::Regs, 3 } },
        { op_cmp_gt_str,        { "GT_STR",            2, OpFmt::Regs, 3 } },
        { op_cmp_ge_str,        { "GE_STR",            2, OpFmt::Regs, 3 } },
        { op_string_get_byte,   { "STR_GET_BYTE",      2, OpFmt::Regs, 3 } },

        // --- Composite Arithmetic (5 words: binary) ---
        { op_add_composite,     { "ADD_COMP",          5, OpFmt::Regs_Ptr3,    3 } },
        { op_sub_composite,     { "SUB_COMP",          5, OpFmt::Regs_Ptr3,    3 } },
        { op_mul_composite,     { "MUL_COMP",          5, OpFmt::Regs_Ptr3,    3 } },
        { op_div_composite,     { "DIV_COMP",          5, OpFmt::Regs_Ptr3,    3 } },

        // --- Composite Arithmetic (4 words: unary) ---
        { op_neg_composite,     { "NEG_COMP",          4, OpFmt::Regs_Ptr_Ptr, 2 } },
        { op_not_composite,     { "NOT_COMP",          4, OpFmt::Regs_Ptr_Ptr, 2 } },
        { op_bitnot_composite,  { "BITNOT_COMP",       4, OpFmt::Regs_Ptr_Ptr, 2 } },

        // --- Composite Comparison (5 words) ---
        { op_cmp_eq_composite,  { "EQ_COMP",           5, OpFmt::Regs_Ptr3,    3 } },
        { op_cmp_ne_composite,  { "NE_COMP",           5, OpFmt::Regs_Ptr3,    3 } },
        { op_cmp_lt_composite,  { "LT_COMP",           5, OpFmt::Regs_Ptr3,    3 } },
        { op_cmp_le_composite,  { "LE_COMP",           5, OpFmt::Regs_Ptr3,    3 } },
        { op_cmp_gt_composite,  { "GT_COMP",           5, OpFmt::Regs_Ptr3,    3 } },
        { op_cmp_ge_composite,  { "GE_COMP",           5, OpFmt::Regs_Ptr3,    3 } },

        // --- Inline composite arithmetic (Phase 4g.7) ---
        { op_add_composite_inline,    { "ADD_COMP_I",     5, OpFmt::Regs_Ptr3,    3 } },
        { op_sub_composite_inline,    { "SUB_COMP_I",     5, OpFmt::Regs_Ptr3,    3 } },
        { op_mul_composite_inline,    { "MUL_COMP_I",     5, OpFmt::Regs_Ptr3,    3 } },
        { op_div_composite_inline,    { "DIV_COMP_I",     5, OpFmt::Regs_Ptr3,    3 } },
        { op_neg_composite_inline,    { "NEG_COMP_I",     4, OpFmt::Regs_Ptr_Ptr, 2 } },
        { op_not_composite_inline,    { "NOT_COMP_I",     4, OpFmt::Regs_Ptr_Ptr, 2 } },
        { op_bitnot_composite_inline, { "BITNOT_COMP_I",  4, OpFmt::Regs_Ptr_Ptr, 2 } },
        { op_cmp_eq_composite_inline, { "EQ_COMP_I",      5, OpFmt::Regs_Ptr3,    3 } },
        { op_cmp_ne_composite_inline, { "NE_COMP_I",      5, OpFmt::Regs_Ptr3,    3 } },
        { op_cmp_lt_composite_inline, { "LT_COMP_I",      5, OpFmt::Regs_Ptr3,    3 } },
        { op_cmp_le_composite_inline, { "LE_COMP_I",      5, OpFmt::Regs_Ptr3,    3 } },
        { op_cmp_gt_composite_inline, { "GT_COMP_I",      5, OpFmt::Regs_Ptr3,    3 } },
        { op_cmp_ge_composite_inline, { "GE_COMP_I",      5, OpFmt::Regs_Ptr3,    3 } },

        // --- Array/Tuple/List Concatenation ---
        { op_concat_array,      { "CONCAT_ARRAY",      3, OpFmt::Regs_Ptr, 3 } },
        { op_concat_tuple,      { "CONCAT_TUPLE",      5, OpFmt::Regs_Ptr3, 3 } },
        { op_concat_list,       { "CONCAT_LIST",       3, OpFmt::Regs_Ptr, 3 } },

        // --- Array/Tuple/Struct Access/Construction ---
        { op_make_array,        { "MAKE_ARRAY",        3, OpFmt::Regs_Ptr, 3 } },
        { op_tuple_get,         { "TUPLE_GET",         3, OpFmt::Regs_Ptr, 3 } },
        { op_inline_tuple_get,  { "I_TUPLE_GET",       3, OpFmt::Regs_Ptr, 3 } },
        { op_tuple_slice,       { "TUPLE_SLICE",       3, OpFmt::Regs_Ptr, 3 } },
        { op_make_tuple,        { "MAKE_TUPLE",        3, OpFmt::Regs_Ptr, 3 } },
        { op_make_tuple_heap,   { "MAKE_TUPLE_HEAP",   3, OpFmt::Regs_Ptr, 3 } },
        { op_make_struct,       { "MAKE_STRUCT",       3, OpFmt::Regs_Ptr, 3 } },
        { op_struct_get,        { "STRUCT_GET",        3, OpFmt::Regs_Ptr, 3 } },
        { op_inline_struct_get, { "I_STRUCT_GET",      3, OpFmt::Regs_Ptr, 3 } },

        // --- Array Destructuring ---
        { op_array_get,         { "ARRAY_GET",         3, OpFmt::Regs_Ptr, 3 } },
        { op_array_slice,       { "ARRAY_SLICE",       3, OpFmt::Regs_Ptr, 3 } },
        { op_array_length_int,      { "ARRAY_LENGTH_INT",      3, OpFmt::Regs_Ptr, 2 } },
        { op_array_length_float,    { "ARRAY_LENGTH_FLOAT",    3, OpFmt::Regs_Ptr, 2 } },
        { op_array_length_complex,  { "ARRAY_LENGTH_COMPLEX",  3, OpFmt::Regs_Ptr, 2 } },
        { op_array_length_fraction, { "ARRAY_LENGTH_FRACTION", 3, OpFmt::Regs_Ptr, 2 } },
        { op_array_length_inline,   { "ARRAY_LENGTH_INLINE",   3, OpFmt::Regs_Ptr, 2 } },
        { op_array_length_obj,      { "ARRAY_LENGTH_OBJ",      3, OpFmt::Regs_Ptr, 2 } },

        // --- Enum Construction/Access ---
        { op_make_enum,         { "MAKE_ENUM",         3, OpFmt::Regs_Ptr, 3 } },
        { op_make_enum_nodata,  { "MAKE_ENUM_NODATA",  3, OpFmt::Regs_Ptr, 2 } },
        { op_enum_get_which,    { "ENUM_GET_WHICH",    2, OpFmt::Regs,     2 } },
        { op_enum_get_value,    { "ENUM_GET_VALUE",    3, OpFmt::Regs_Ptr, 2 } },
        { op_make_inline_enum,        { "MAKE_I_ENUM",        3, OpFmt::Regs_Ptr, 3 } },
        { op_make_inline_enum_nodata, { "MAKE_I_ENUM_NODATA", 3, OpFmt::Regs_Ptr, 2 } },
        { op_box_enum,                { "BOX_ENUM",           3, OpFmt::Regs_Ptr, 2 } },
        { op_unbox_enum,              { "UNBOX_ENUM",         3, OpFmt::Regs_Ptr, 2 } },

        // --- Dynamic Array Operations ---
        { op_array_alloc,       { "ARRAY_ALLOC",       3, OpFmt::Regs_Ptr, 2 } },
        { op_array_set_int,         { "ARRAY_SET_INT",         3, OpFmt::Regs_Ptr, 3 } },
        { op_array_set_float,       { "ARRAY_SET_FLOAT",       3, OpFmt::Regs_Ptr, 3 } },
        { op_array_set_complex,     { "ARRAY_SET_COMPLEX",     3, OpFmt::Regs_Ptr, 3 } },
        { op_array_set_fraction,    { "ARRAY_SET_FRACTION",    3, OpFmt::Regs_Ptr, 3 } },
        { op_array_set_inline,      { "ARRAY_SET_INLINE",      3, OpFmt::Regs_Ptr, 3 } },
        { op_array_set_obj,         { "ARRAY_SET_OBJ",         3, OpFmt::Regs_Ptr, 3 } },
        { op_array_get_dyn_int,     { "ARRAY_GET_DYN_INT",     3, OpFmt::Regs_Ptr, 3 } },
        { op_array_get_dyn_float,   { "ARRAY_GET_DYN_FLOAT",   3, OpFmt::Regs_Ptr, 3 } },
        { op_array_get_dyn_complex, { "ARRAY_GET_DYN_COMPLEX", 3, OpFmt::Regs_Ptr, 3 } },
        { op_array_get_dyn_fraction,{ "ARRAY_GET_DYN_FRACTION",3, OpFmt::Regs_Ptr, 3 } },
        { op_array_get_dyn_inline,  { "ARRAY_GET_DYN_INLINE",  3, OpFmt::Regs_Ptr, 3 } },
        { op_array_get_dyn_obj,     { "ARRAY_GET_DYN_OBJ",     3, OpFmt::Regs_Ptr, 3 } },

        // --- List ---
        { op_cons,              { "CONS",              3, OpFmt::Regs_Ptr, 3 } },
        { op_make_list,         { "MAKE_LIST",         3, OpFmt::Regs_Ptr, 3 } },
        { op_list_head,         { "LIST_HEAD",         2, OpFmt::Regs,     2 } },
        { op_list_tail,         { "LIST_TAIL",         2, OpFmt::Regs,     2 } },
        { op_list_is_nil,       { "LIST_IS_NIL",       2, OpFmt::Regs,     2 } },

        // --- List Print Limit ---
        { op_get_list_print_limit, { "GET_LIST_PRINT_LIMIT", 2, OpFmt::Regs, 1 } },
        { op_set_list_print_limit, { "SET_LIST_PRINT_LIMIT", 2, OpFmt::Regs, 2 } },

        // --- Range ---
        { op_make_range,        { "MAKE_RANGE",        4, OpFmt::Regs_Ptr_Flags, 4 } },

        // --- Lambda ---
        { op_make_lambda,       { "MAKE_LAMBDA",       3, OpFmt::Regs_Ptr, 3 } },
        { op_call_lambda,       { "CALL_LAMBDA",       2, OpFmt::Regs,     4 } },
        { op_func_ref,          { "FUNC_REF",          3, OpFmt::Regs_Ptr, 1 } },

        // --- Template Lambda ---
        { op_make_template_lambda,      { "MAKE_TMPL_LAMBDA",      3, OpFmt::Regs_Ptr, 3 } },
        { op_call_template_lambda,      { "CALL_TMPL_LAMBDA",      3, OpFmt::Regs_Ptr, 4 } },
        { op_tail_call_template_lambda, { "TAIL_CALL_TMPL_LAMBDA", 3, OpFmt::Regs_Ptr, 4 } },
        { op_specialize_lambda,         { "SPECIALIZE_LAMBDA",     3, OpFmt::Regs_Ptr, 2 } },

        // --- Tail Calls ---
        { op_tail_call,         { "TAIL_CALL",         3, OpFmt::Regs_Int, 3 } },
        { op_tail_call_lambda,  { "TAIL_CALL_LAMBDA",  2, OpFmt::Regs,    4 } },

        // --- Lazy auto-map ---
        { op_make_lazy_automap, { "MAKE_LAZY_AUTOMAP", 3, OpFmt::Regs_Ptr, 4 } },
        { op_make_lazy_witness_map, { "MAKE_LAZY_WITNESS_MAP", 4, OpFmt::Regs_Ptr_Flags, 2 } },

        // --- Map ---
        { op_make_map,          { "MAKE_MAP",          3, OpFmt::Regs_Ptr, 3 } },
        { op_map_get,           { "MAP_GET",           3, OpFmt::Regs_Ptr, 3 } },
        { op_map_get_option,    { "MAP_GET_OPTION",    3, OpFmt::Regs_Ptr, 3 } },
        { op_map_set,           { "MAP_SET",           3, OpFmt::Regs_Ptr, 3 } },

        // --- Range ---
        { op_range_get,         { "RANGE_GET",         3, OpFmt::Regs_Ptr, 3 } },

        // --- Persistent vector / map ---
        { op_make_pvec,         { "MAKE_PVEC",         3, OpFmt::Regs_Ptr, 3 } },
        { op_pvec_get,          { "PVEC_GET",          3, OpFmt::Regs_Ptr, 3 } },
        { op_pvec_len,          { "PVEC_LEN",          2, OpFmt::Regs,     2 } },
        { op_concat_pvec,       { "CONCAT_PVEC",       3, OpFmt::Regs_Ptr, 3 } },
        { op_pvec_from_array,   { "PVEC_FROM_ARRAY",   3, OpFmt::Regs_Ptr, 3 } },
        { op_pvec_to_array,     { "PVEC_TO_ARRAY",     3, OpFmt::Regs_Ptr, 3 } },
        { op_make_pmap,         { "MAKE_PMAP",         3, OpFmt::Regs_Ptr, 3 } },
        { op_pmap_get_option,   { "PMAP_GET_OPTION",   3, OpFmt::Regs_Ptr, 3 } },
        { op_pmap_len,          { "PMAP_LEN",          2, OpFmt::Regs,     2 } },

        // --- Set ---
        { op_make_set,          { "MAKE_SET",          3, OpFmt::Regs_Ptr, 3 } },

        // --- Ref ---
        { op_make_ref,          { "MAKE_REF",          3, OpFmt::Regs_Ptr, 2 } },
        { op_ref_get,           { "REF_GET",           2, OpFmt::Regs,     2 } },
        { op_ref_set,           { "REF_SET",           3, OpFmt::Regs_Ptr, 3 } },
        { op_make_ref_inline,   { "MAKE_REF_I",        3, OpFmt::Regs_Ptr, 2 } },
        { op_ref_get_inline,    { "REF_GET_I",         3, OpFmt::Regs_Ptr, 2 } },
        { op_ref_set_inline,    { "REF_SET_I",         3, OpFmt::Regs_Ptr, 3 } },

        // --- Coroutines ---
        { op_coro_create,       { "CORO_CREATE",       4, OpFmt::Regs_Int_Ptr,  3 } },
        { op_coro_create_lambda,{ "CORO_CREATE_LAMBDA",3, OpFmt::Regs_Ptr,      4 } },
        { op_coro_resume,       { "CORO_RESUME",       2, OpFmt::Regs,          2 } },
        { op_yield,             { "YIELD",             2, OpFmt::Regs,          2 } },
        { op_coro_done,         { "CORO_DONE",         1, OpFmt::None,          0 } },
        { op_coro_is_done,      { "CORO_IS_DONE",      2, OpFmt::Regs,          2 } },
        { op_coro_wrap_option,  { "CORO_WRAP_OPTION",  3, OpFmt::Regs_Ptr,      3 } },

        // --- Dynamic Scope ---
        { op_load_dynamic,      { "LOAD_DYN",          3, OpFmt::Regs_Int, 1 } },
        { op_store_dynamic,     { "STORE_DYN",         3, OpFmt::Regs_Int, 1 } },
        { op_store_dynamic_obj, { "STORE_DYN_OBJ",     3, OpFmt::Regs_Int, 1 } },
        { op_init_dynamic_obj,  { "INIT_DYN_OBJ",      3, OpFmt::Regs_Int, 1 } },
        { op_dynscope_push,     { "DYNSCOPE_PUSH",     3, OpFmt::Regs_Int, 1 } },
        { op_load_dynamic_inline,  { "LOAD_DYN_I",       4, OpFmt::Regs_Int_Ptr, 1 } },
        { op_store_dynamic_inline, { "STORE_DYN_I",      4, OpFmt::Regs_Int_Ptr, 1 } },
        { op_init_dynamic_inline,  { "INIT_DYN_I",       4, OpFmt::Regs_Int_Ptr, 1 } },
        { op_dynscope_push_inline, { "DYNSCOPE_PUSH_I",  4, OpFmt::Regs_Int_Ptr, 1 } },

        // --- Any ---
        { op_make_any,          { "MAKE_ANY",          3, OpFmt::Regs_Ptr, 3 } },
        { op_any_get_value,     { "ANY_GET_VALUE",     2, OpFmt::Regs,     2 } },
        { op_any_get_type_ptr,  { "ANY_GET_TYPE_PTR",  2, OpFmt::Regs,     2 } },
    };
    return table;
}

// Try to get a Type* name string, safely
static const char* typeName(void* p) {
    if (!p) return "null";
    auto* type = static_cast<Type*>(p);
    static thread_local char buf[128];
    VMString s = type->str();
    size_t len = s.size() < sizeof(buf) - 1 ? s.size() : sizeof(buf) - 1;
    std::memcpy(buf, s.data(), len);
    buf[len] = '\0';
    return buf;
}

// Print N registers from a regs word
static void printRegs(FILE* out, Code regWord, u8 n) {
    for (u8 i = 0; i < n; ++i) {
        if (i > 0) std::fprintf(out, ", ");
        std::fprintf(out, "r%u", regWord.regs[i]);
    }
}

// Try to get a CodeBlock name for a global index
static const char* globalName(VM* vm, u32 idx) {
    if (!vm) return nullptr;
    // The global might be a CodeBlock* (for named functions)
    void* p = vm->global(idx).p;
    if (!p) return nullptr;
    auto* cb = static_cast<CodeBlock*>(p);
    if (cb->name) {
        static thread_local char buf[128];
        auto sv = cb->name->str();
        size_t len = sv.size() < sizeof(buf) - 1 ? sv.size() : sizeof(buf) - 1;
        std::memcpy(buf, sv.data(), len);
        buf[len] = '\0';
        return buf;
    }
    return nullptr;
}

void disassembleCodeBlock(CodeBlock* block, FILE* out) {
    if (!block) {
        std::fprintf(out, "<null CodeBlock>\n");
        return;
    }

    // Get current VM for resolving global names (may be null)
    VM* vm = gCurrentVM;

    // Header
    if (block->name) {
        auto sv = block->name->str();
        std::fprintf(out, "-- %.*s", (int)sv.size(), sv.data());
    } else {
        std::fprintf(out, "-- <anonymous>");
    }
    std::fprintf(out, "  (%u regs, %u args", (unsigned)block->numRegs, (unsigned)block->numArgs);
    if (!block->defaultEntryOffsets.empty()) {
        std::fprintf(out, ", minArity %u", (unsigned)block->minArity);
    }
    if (block->funcType) {
        VMString ts = block->funcType->str();
        std::fprintf(out, ", type %.*s", (int)ts.size(), ts.data());
    }
    std::fprintf(out, ")\n");

    // Default entry offsets
    if (!block->defaultEntryOffsets.empty()) {
        std::fprintf(out, "-- default entry offsets:");
        for (size_t i = 0; i < block->defaultEntryOffsets.size(); ++i) {
            std::fprintf(out, " [%zu]=%u", i, block->defaultEntryOffsets[i]);
        }
        std::fprintf(out, "\n");
    }

    const auto& table = opInfoMap();
    Code* base = block->code.data();
    Code* end = base + block->code.size();
    Code* pc = base;

    while (pc < end) {
        u32 offset = (u32)(pc - base);
        std::fprintf(out, "  %4u  ", offset);

        auto it = table.find(pc->op);
        if (it == table.end()) {
            std::fprintf(out, "??? (unknown op)\n");
            ++pc;
            continue;
        }

        const OpInfo& info = it->second;
        std::fprintf(out, "%-24s", info.name);

        switch (info.fmt) {
        case OpFmt::None:
            break;

        case OpFmt::Regs:
            printRegs(out, pc[1], info.nregs);
            break;

        case OpFmt::Regs_Imm16:
            printRegs(out, pc[1], info.nregs);
            std::fprintf(out, ", %d", (int)(i16)pc[1].regs[2]);
            break;

        case OpFmt::Regs_Int: {
            printRegs(out, pc[1], info.nregs);
            // CALL/CALL_PRIM/TAIL_CALL carry a CODE-image index (ranged from
            // kCodeGlobalBase). Show its local index for readability; other
            // Regs_Int operands (constants, data-global indices) print raw.
            long long operand = (long long)pc[2].i;
            bool codeRef = (pc->op == op_call || pc->op == op_call_primitive
                            || pc->op == op_tail_call);
            if (codeRef && (u32)operand >= kCodeGlobalBase) {
                operand -= (long long)kCodeGlobalBase;
            }
            std::fprintf(out, "  ;  %lld", operand);
            // Try to show function name for CALL/TAIL_CALL (op_call_primitive
            // stores a Primitive* in the global slot, not a CodeBlock*, so the
            // cast in globalName would read garbage).
            if (vm && (pc->op == op_call || pc->op == op_tail_call)) {
                const char* name = globalName(vm, (u32)pc[2].i);
                if (name) std::fprintf(out, "  (%s)", name);
            }
            break;
        }

        case OpFmt::Regs_Float: {
            printRegs(out, pc[1], info.nregs);
            char fbuf[32];
            formatFloat(pc[2].f, fbuf, sizeof(fbuf));
            std::fprintf(out, "  ;  %s", fbuf);
            break;
        }

        case OpFmt::Regs_Ptr:
            printRegs(out, pc[1], info.nregs);
            std::fprintf(out, "  ;  %s", typeName(pc[2].p));
            break;

        case OpFmt::Jump: {
            Code* target = static_cast<Code*>(pc[1].p);
            i64 targetOff = target - base;
            std::fprintf(out, "-> %lld", (long long)targetOff);
            break;
        }

        case OpFmt::Regs_Jump: {
            printRegs(out, pc[1], info.nregs);
            Code* target = static_cast<Code*>(pc[2].p);
            i64 targetOff = target - base;
            std::fprintf(out, "  -> %lld", (long long)targetOff);
            break;
        }

        case OpFmt::Regs_Ptr_Ptr:
            printRegs(out, pc[1], info.nregs);
            std::fprintf(out, "  ;  %s, %s", typeName(pc[2].p), typeName(pc[3].p));
            break;

        case OpFmt::Regs_Int_Ptr:
            printRegs(out, pc[1], info.nregs);
            std::fprintf(out, "  ;  %lld, %s", (long long)pc[2].i, typeName(pc[3].p));
            break;

        case OpFmt::Regs_Ptr_Flags:
            printRegs(out, pc[1], info.nregs);
            std::fprintf(out, "  ;  %s, flags=%lld", typeName(pc[2].p), (long long)pc[3].i);
            break;

        case OpFmt::Regs_Ptr3:
            printRegs(out, pc[1], info.nregs);
            std::fprintf(out, "  ;  %s, %s, %s",
                typeName(pc[2].p), typeName(pc[3].p), typeName(pc[4].p));
            break;
        }

        // Phase 2/5.2 of tracing-GC project: annotate any instruction whose
        // pcOffset matches a stack map. Safepoints emit one inline; CALL
        // sites emit one at the post-call returnPC (which is the offset
        // of the *next* instruction, so its annotation surfaces there).
        for (auto const& sm : block->stackMaps_) {
            if (sm.pcOffset == offset) {
                std::fprintf(out, "  ; refs: [");
                for (size_t i = 0; i < sm.liveRefRegs.size(); ++i) {
                    std::fprintf(out, "%sr%u", i ? ", " : "", (unsigned)sm.liveRefRegs[i]);
                }
                std::fprintf(out, "]");
                break;
            }
        }

        std::fprintf(out, "\n");
        pc += instrWords(pc, info);
    }
}

bool verifyCodeBlock(CodeBlock* block, std::string& err) {
    const auto& table = opInfoMap();
    Code* base = block->code.data();
    size_t n = block->code.size();
    const char* name = block->name ? block->name->cstr() : "<unnamed>";

    // Pass 1: decode the stream, recording instruction boundaries.
    std::vector<bool> isStart(n, false);
    size_t i = 0;
    while (i < n) {
        auto it = table.find(base[i].op);
        if (it == table.end()) {
            err = std::format("{}: unknown op at offset {}", name, i);
            return false;
        }
        if (i + it->second.size > n) {
            err = std::format("{}: truncated {} at offset {} (size {})",
                              name, it->second.name, i, n);
            return false;
        }
        isStart[i] = true;
        i += instrWords(base + i, it->second);
    }
    if (i != n) {
        err = std::format("{}: truncated instruction at offset {} (size {})",
                          name, i, n);
        return false;
    }

    // Pass 2: every jump target must be in-bounds on a boundary.
    i = 0;
    while (i < n) {
        const OpInfo& info = table.at(base[i].op);
        if (info.fmt == OpFmt::Jump || info.fmt == OpFmt::Regs_Jump) {
            size_t word = i + (info.fmt == OpFmt::Jump ? 1 : 2);
            Code* target = static_cast<Code*>(base[word].p);
            if (target < base || target >= base + n) {
                err = std::format(
                    "{}: {} at offset {} targets {:+} words out of block "
                    "[0, {})", name, info.name, i,
                    (long long)(target - base), n);
                return false;
            }
            if (!isStart[(size_t)(target - base)]) {
                err = std::format(
                    "{}: {} at offset {} targets offset {} inside an "
                    "instruction", name, info.name, i, (size_t)(target - base));
                return false;
            }
        }
        i += instrWords(base + i, info);
    }

    for (size_t k = 0; k < block->defaultEntryOffsets.size(); ++k) {
        u32 off = block->defaultEntryOffsets[k];
        if (off >= n || !isStart[off]) {
            err = std::format("{}: defaultEntryOffsets[{}] = {} invalid "
                              "(size {})", name, k, off, n);
            return false;
        }
    }
    return true;
}

} // namespace ts
