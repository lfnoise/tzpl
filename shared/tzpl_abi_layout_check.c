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
 *  tzpl_abi_layout_check.c
 *
 *  Prints the size and alignment of every struct in the plugin ABI. On
 *  Windows the host (clang-cl, MSVC ABI) and runtime-compiled plugins
 *  (llvm-mingw) are built by different toolchains that must agree on this
 *  layout; compile this file with both and diff the output:
 *
 *      clang-cl /nologo /Ishared shared\tzpl_abi_layout_check.c /Fe:host.exe
 *      <llvm-mingw>\bin\clang -Ishared shared\tzpl_abi_layout_check.c -o plugin.exe
 *      host.exe > host.txt && plugin.exe > plugin.txt && fc host.txt plugin.txt
 *
 *  Any difference in the struct rows means the two sides would read each
 *  other's structs at the wrong offsets. Rows prefixed "info:" are context
 *  (long double, for one, is 16 bytes under mingw and 8 under MSVC) and are
 *  legitimately allowed to differ: nothing in the ABI uses those types. Also
 *  useful on one toolchain as a record of the layout a given
 *  TZPL_PLUGIN_ABI_VERSION stands for.
 */

#include "tzpl_plugin_abi.h"
#include <stdalign.h>
#include <stdio.h>

#define SHOW(T) printf("%-32s size %4zu align %2zu\n", #T, sizeof(T), (size_t)alignof(T))

int main(void) {
    printf("TZPL_PLUGIN_ABI_VERSION %d\n", TZPL_PLUGIN_ABI_VERSION);
    printf("info: %-26s size %4zu\n", "void*", sizeof(void*));
    printf("info: %-26s size %4zu\n", "long", sizeof(long));
    printf("info: %-26s size %4zu\n", "long double", sizeof(long double));
    SHOW(tzpl_SErr);
    SHOW(tzpl_Rate);
    SHOW(tzpl_ElemType);
    SHOW(tzpl_SignalType);
    SHOW(tzpl_Slice);
    SHOW(tzpl_ParamPair);
    SHOW(tzpl_ControlKind);
    SHOW(tzpl_ControlWarp);
    SHOW(tzpl_ControlSpec);
    SHOW(tzpl_PortDef);
    SHOW(tzpl_ControlDef);
    SHOW(tzpl_ControlDispatchTableEntry);
    SHOW(tzpl_ControlDispatchTable);
    SHOW(tzpl_SynthFuns);
    SHOW(tzpl_SynthDef);
    SHOW(tzpl_SynthData);
    SHOW(tzpl_Buffer);
    SHOW(tzpl_BufferDef);
    SHOW(tzpl_BufferDefList);
    SHOW(tzpl_SampleBankEntry);
    SHOW(tzpl_SampleBank);
    SHOW(tzpl_SampleBankDef);
    SHOW(tzpl_SampleBankDefList);
    SHOW(tzpl_TagList);
    SHOW(tzpl_SharedInput);
    return 0;
}
