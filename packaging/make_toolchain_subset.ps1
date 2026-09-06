<#
.SYNOPSIS
    Trim a full llvm-mingw install down to what runtime synthdef compilation
    needs, then prove the result works with nothing else on PATH.

.DESCRIPTION
    The Windows distribution folder bundles this subset as toolchain/ so
    `defSynth` compiles plugins on a machine with no developer tools. From
    the llvm-mingw UCRT x86_64 release (pinned in docs/WINDOWS.md) it keeps:

      bin\        clang.exe, clang++.exe, the x86_64-w64-mingw32-clang*
                  wrappers, ld.lld.exe / lld.exe, and the DLLs those import
      lib\clang\<ver>\include   compiler builtin headers
      lib\clang\<ver>\lib\windows   compiler-rt builtins
      include\    libc++ headers (shared across targets)
      x86_64-w64-mingw32\include, \lib   mingw-w64 CRT/Win32 headers and libs

    and drops the other target sysroots, lldb, clangd, clang-tidy, python,
    and the rest. The result is a few hundred MB unpacked, ~60 MB zipped.

.EXAMPLE
    packaging\make_toolchain_subset.ps1 -Source C:\llvm-mingw-20260826-ucrt-x86_64 -Dest C:\tzpl-toolchain
    cmake -B build ... -DTZPL_BUNDLE_TOOLCHAIN_DIR=C:\tzpl-toolchain
#>
param(
    [Parameter(Mandatory)][string]$Source,
    [Parameter(Mandatory)][string]$Dest
)
$ErrorActionPreference = 'Stop'

if (-not (Test-Path (Join-Path $Source 'bin\clang.exe'))) {
    throw "$Source does not look like an llvm-mingw install (no bin\clang.exe)"
}
if (Test-Path $Dest) { Remove-Item -Recurse -Force $Dest }
New-Item -ItemType Directory -Path $Dest | Out-Null

function Copy-Tree([string]$rel) {
    $from = Join-Path $Source $rel
    if (-not (Test-Path $from)) { throw "missing in source: $rel" }
    $to = Join-Path $Dest $rel
    New-Item -ItemType Directory -Path (Split-Path $to) -Force | Out-Null
    Copy-Item -Recurse -Force $from $to
}
function Copy-File([string]$rel) {
    $from = Join-Path $Source $rel
    if (-not (Test-Path $from)) { throw "missing in source: $rel" }
    $to = Join-Path $Dest $rel
    New-Item -ItemType Directory -Path (Split-Path $to) -Force | Out-Null
    Copy-Item -Force $from $to
}

# Tools. clang++.exe and the target-prefixed wrappers are what
# synthdef_compile_link.cpp runs; lld is the linker clang invokes.
foreach ($f in 'clang.exe', 'clang++.exe', 'lld.exe', 'ld.lld.exe',
               'x86_64-w64-mingw32-clang.exe', 'x86_64-w64-mingw32-clang++.exe') {
    Copy-File "bin\$f"
}
# Every DLL in bin\: the tools themselves are dynamically linked against
# libc++.dll / libunwind.dll / libwinpthread-1.dll (and zlib/zstd when
# present). A few MB; cheaper than resolving the import graph by hand.
Get-ChildItem (Join-Path $Source 'bin') -Filter '*.dll' | ForEach-Object { Copy-File "bin\$($_.Name)" }

# Compiler resource directory (builtin headers + compiler-rt for the target).
$clangLib = Get-ChildItem (Join-Path $Source 'lib\clang') | Select-Object -First 1
Copy-Tree "lib\clang\$($clangLib.Name)\include"
Copy-Tree "lib\clang\$($clangLib.Name)\lib\windows"

# Headers and libraries for the x86_64 target.
if (Test-Path (Join-Path $Source 'include')) { Copy-Tree 'include' }
Copy-Tree 'x86_64-w64-mingw32\include'
Copy-Tree 'x86_64-w64-mingw32\lib'
foreach ($doc in 'LICENSE.TXT', 'README.md') {
    if (Test-Path (Join-Path $Source $doc)) { Copy-File $doc }
}

# Self-check: compile and link a DLL using only the subset. PATH is reduced
# to the system directories so nothing leaks in from the full install.
$probe = Join-Path $env:TEMP 'tzpl-toolchain-probe'
if (Test-Path $probe) { Remove-Item -Recurse -Force $probe }
New-Item -ItemType Directory -Path $probe | Out-Null
@'
#include <cmath>
#include <cstdio>
#include <vector>
extern "C" __declspec(dllexport) double probe(double x) {
    std::vector<double> v{x, std::sin(x)};
    return v[0] + v[1];
}
'@ | Set-Content (Join-Path $probe 'probe.cpp')
$oldPath = $env:PATH
$env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"
try {
    $cxx = Join-Path $Dest 'bin\x86_64-w64-mingw32-clang++.exe'
    & $cxx -std=c++23 -O2 -c (Join-Path $probe 'probe.cpp') -o (Join-Path $probe 'probe.o')
    if ($LASTEXITCODE -ne 0) { throw "subset cannot compile the probe" }
    # (quoted: a bare comma is PowerShell's array separator)
    & $cxx -shared -static '-Wl,--exclude-all-symbols' -o (Join-Path $probe 'probe.dll') (Join-Path $probe 'probe.o')
    if ($LASTEXITCODE -ne 0) { throw "subset cannot link the probe DLL" }
} finally {
    $env:PATH = $oldPath
}
$size = [math]::Round((Get-ChildItem -Recurse $Dest | Measure-Object -Property Length -Sum).Sum / 1MB)
Write-Host "toolchain subset OK: $Dest ($size MB)"
