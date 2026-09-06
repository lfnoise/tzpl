<#
.SYNOPSIS
    Assemble the Windows distribution zip from the staged dist component.
    Invoked by `cmake --build build --target dist` (root CMakeLists.txt).

.DESCRIPTION
    The stage holds Tzopilotl\ as installed by the COMPONENT dist rules:
    Tzopilotl.exe, tzpl.exe, modules\, examples\, docs\, editors\, include\
    (plugin headers), lib\libsleef.a, README.txt. This script adds the
    bundled plugin toolchain (toolchain\, from -ToolchainDir, see
    make_toolchain_subset.ps1), the VC++ runtime DLLs app-local, checks that
    the executables import nothing outside Windows and that runtime, and
    zips the folder. Binaries are not signed (no certificate; see
    docs/WINDOWS.md for the SmartScreen note).
#>
param(
    [Parameter(Mandatory)][string]$Stage,
    [Parameter(Mandatory)][string]$Zip,
    [string]$ToolchainDir = ''
)
$ErrorActionPreference = 'Stop'
$root = Join-Path $Stage 'Tzopilotl'

foreach ($required in 'Tzopilotl.exe', 'tzpl.exe', 'modules', 'include\tzpl_plugin_abi.h',
                      'include\sleef.h', 'lib\libsleef.a') {
    if (-not (Test-Path (Join-Path $root $required))) {
        throw "dist: $root is missing $required (configure with TZPL_PLUGIN_TOOLCHAIN_DIR for the Sleef pieces)"
    }
}

# Bundled plugin toolchain.
if ($ToolchainDir -and (Test-Path $ToolchainDir)) {
    if (-not (Test-Path (Join-Path $ToolchainDir 'bin\clang++.exe'))) {
        throw "dist: $ToolchainDir has no bin\clang++.exe (run make_toolchain_subset.ps1 first)"
    }
    Copy-Item -Recurse -Force $ToolchainDir (Join-Path $root 'toolchain')
    Write-Host "dist: bundled plugin toolchain from $ToolchainDir"
} else {
    Write-Warning "dist: no TZPL_BUNDLE_TOOLCHAIN_DIR; defSynth will need `$TZPL_CC or clang++ on PATH"
}

# VC++ runtime, app-local (permitted redistribution). VCToolsRedistDir is
# set by the Visual Studio developer shell.
$redist = $env:VCToolsRedistDir
if (-not $redist -and $env:VCINSTALLDIR) {
    $redist = Get-ChildItem (Join-Path $env:VCINSTALLDIR 'Redist\MSVC') -Directory |
              Sort-Object Name -Descending | Select-Object -First 1 -ExpandProperty FullName
}
if (-not $redist) { throw "dist: cannot locate the VC++ redistributable (run from a VS developer shell)" }
$crt = Get-ChildItem (Join-Path $redist 'x64') -Directory -Filter 'Microsoft.VC14*.CRT' | Select-Object -First 1
foreach ($dll in 'vcruntime140.dll', 'vcruntime140_1.dll', 'msvcp140.dll') {
    Copy-Item -Force (Join-Path $crt.FullName $dll) $root
}

# A distributable binary must import only Windows DLLs and what ships in the
# folder (mirrors make_dist_dmg.sh's otool check).
$objdump = Get-Command llvm-objdump -ErrorAction SilentlyContinue
if ($objdump) {
    foreach ($exe in 'Tzopilotl.exe', 'tzpl.exe') {
        $imports = & $objdump.Source -p (Join-Path $root $exe) |
                   Select-String 'DLL Name: (.+)' | ForEach-Object { $_.Matches[0].Groups[1].Value.Trim() }
        foreach ($dll in $imports) {
            $ok = (Test-Path (Join-Path $root $dll)) -or
                  (Test-Path (Join-Path $env:SystemRoot "System32\$dll")) -or
                  ($dll -like 'api-ms-win-*') -or ($dll -like 'ext-ms-win-*')
            if (-not $ok) { throw "dist: $exe imports $dll, which is neither a system DLL nor in the folder" }
        }
    }
} else {
    Write-Warning "dist: llvm-objdump not on PATH; skipping the import check"
}

# Code signing would go here (signtool sign /fd SHA256 /tr <RFC 3161 URL>
# /td SHA256 on Tzopilotl.exe and tzpl.exe). There is no certificate today;
# unsigned binaries trigger SmartScreen's "More info / Run anyway".

if (Test-Path $Zip) { Remove-Item -Force $Zip }
Compress-Archive -Path $root -DestinationPath $Zip -CompressionLevel Optimal
Write-Host "dist: $Zip"
