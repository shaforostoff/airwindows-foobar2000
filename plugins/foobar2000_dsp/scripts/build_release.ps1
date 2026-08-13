<#
.SYNOPSIS
    Builds the optimised release binaries and packages each component as an
    installable .fb2k-component.

.DESCRIPTION
    Configures and builds every requested architecture, runs the test suites,
    then assembles one archive per component containing all of them:

        foo_dsp_decrackle.fb2k-component
          foo_dsp_decrackle.dll        <- 32 bit, used by foobar2000 1.5 - 2.x (x86)
          x64/foo_dsp_decrackle.dll    <- 64 bit, used by foobar2000 2.x (x64)

    foobar2000 ignores subfolders it does not understand, so one file installs
    everywhere. Debug symbols go into a separate archive that is NOT part of
    the component - keep it around so foobar2000 crash reports can be resolved.

.PARAMETER Component
    Which components to build. Default: all of them.

.PARAMETER Arch
    Which architectures to build. Default: x86 and x64.

.PARAMETER Toolset
    MSVC platform toolset. Leave unset for the newest installed one. Use v142
    (Visual Studio 2019, or the "MSVC v142 build tools" component of Visual
    Studio 2022) if the binaries have to run on Windows 7 - v143 from Visual
    Studio 2022 17.10 onwards no longer supports it.

.PARAMETER Generator
    CMake generator. Default: whatever CMake picks for the installed VS.

.PARAMETER InstructionSet
    SSE2 (default, runs anywhere Windows 7 does), AVX or AVX2. Only pick a
    higher one if the machine that will run it definitely supports it - an
    unsupported instruction is an instant crash, not a graceful failure.

.PARAMETER SkipTests
    Do not run the verification harnesses. Not recommended.

.PARAMETER Clean
    Wipe the build directories first.

.EXAMPLE
    .\scripts\build_release.ps1

.EXAMPLE
    # Supported Windows 7 build
    .\scripts\build_release.ps1 -Toolset v142
#>

[CmdletBinding()]
param(
    [ValidateSet('x86', 'x64')]
    [string[]] $Arch = @('x86', 'x64'),
    [ValidateSet('foo_dsp_decrackle', 'foo_dsp_declick')]
    [string[]] $Component = @('foo_dsp_decrackle', 'foo_dsp_declick'),
    [string]   $Toolset = '',
    [string]   $Generator = '',
    [ValidateSet('SSE2', 'AVX', 'AVX2')]
    [string]   $InstructionSet = 'SSE2',
    [switch]   $SkipTests,
    [switch]   $Clean
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$root    = Split-Path -Parent $PSScriptRoot
$distDir = Join-Path $root 'dist'
$stage   = Join-Path $root 'build\_package'
$symbols = Join-Path $root 'build\_symbols'

function Invoke-Checked([string] $what, [scriptblock] $action) {
    & $action
    if ($LASTEXITCODE -ne 0) { throw "$what failed with exit code $LASTEXITCODE" }
}

# --- read each version out of version.h so archive names match the DLLs -----
$versions = @{}
foreach ($c in $Component) {
    $h = Join-Path $root "$c\version.h"
    $versions[$c] = (Select-String -Path $h -Pattern 'VERSION_STRING\s+"([^"]+)"').Matches[0].Groups[1].Value
    Write-Host ("{0} {1}" -f $c, $versions[$c]) -ForegroundColor Cyan
}

foreach ($dir in @($stage, $symbols)) {
    if (Test-Path $dir) { Remove-Item -Recurse -Force $dir }
}
New-Item -ItemType Directory -Force $stage, $symbols, $distDir | Out-Null

foreach ($a in $Arch) {
    $platform  = if ($a -eq 'x64') { 'x64' } else { 'Win32' }
    $buildDir  = Join-Path $root "build\$a"

    if ($Clean -and (Test-Path $buildDir)) { Remove-Item -Recurse -Force $buildDir }

    Write-Host "`n=== Configuring $a ===" -ForegroundColor Cyan
    $cfg = @('-S', $root, '-B', $buildDir, '-A', $platform,
             "-DFOO_DSP_ARCH=$InstructionSet",
             '-DFOO_DSP_STATIC_CRT=ON',
             '-DFOO_DSP_LTO=ON',
             '-DCMAKE_BUILD_TYPE=Release')
    if ($Generator) { $cfg += @('-G', $Generator) }
    if ($Toolset)   { $cfg += @('-T', $Toolset) }
    Invoke-Checked "cmake configure ($a)" { & cmake @cfg }

    Write-Host "`n=== Building $a ===" -ForegroundColor Cyan
    Invoke-Checked "cmake build ($a)" {
        & cmake --build $buildDir --config Release --parallel
    }

    if (-not $SkipTests) {
        Write-Host "`n=== Testing $a ===" -ForegroundColor Cyan
        Invoke-Checked "ctest ($a)" {
            & ctest --test-dir $buildDir -C Release --output-on-failure
        }
    }

    # 32 bit goes at the archive root, 64 bit in x64\ - that is the layout
    # foobar2000 2.x expects, and 1.5 simply ignores the subfolder.
    foreach ($c in $Component) {
        $subdir = if ($a -eq 'x64') { Join-Path $stage "$c\x64" } else { Join-Path $stage $c }
        New-Item -ItemType Directory -Force $subdir | Out-Null

        $built = Join-Path $buildDir "$c\Release\$c.dll"
        if (-not (Test-Path $built)) { throw "Expected output missing: $built" }
        Copy-Item $built $subdir -Force

        $pdb = [System.IO.Path]::ChangeExtension($built, '.pdb')
        if (Test-Path $pdb) {
            $symDir = Join-Path $symbols "$c\$a"
            New-Item -ItemType Directory -Force $symDir | Out-Null
            Copy-Item $pdb $symDir -Force
        }

        $info = Get-Item $built
        Write-Host ("  {0,-18} {1,-4} {2,9:N0} bytes" -f $c, $a, $info.Length) -ForegroundColor Green
    }
}

# --- package ---------------------------------------------------------------
# Compress-Archive would work, but cmake -E tar produces the same zip on every
# PowerShell version and is already a hard dependency here.
Write-Host "`n=== Package ===" -ForegroundColor Cyan
foreach ($c in $Component) {
    $v = $versions[$c]
    $componentPath = Join-Path $distDir "$c-$v.fb2k-component"
    $symbolsPath   = Join-Path $distDir "$c-$v-symbols.zip"
    foreach ($p in @($componentPath, $symbolsPath)) {
        if (Test-Path $p) { Remove-Item -Force $p }
    }
    $src = Join-Path $stage $c
    Invoke-Checked "packaging $c" {
        & cmake -E chdir $src cmake -E tar cf $componentPath --format=zip .
    }
    $symSrc = Join-Path $symbols $c
    if (Test-Path $symSrc) {
        Invoke-Checked "packaging $c symbols" {
            & cmake -E chdir $symSrc cmake -E tar cf $symbolsPath --format=zip .
        }
    }
    Write-Host ("  {0}  ({1:N0} bytes)" -f $componentPath, (Get-Item $componentPath).Length) -ForegroundColor Green
    & cmake -E tar tf $componentPath | ForEach-Object { Write-Host "      $_" }
    if (Test-Path $symbolsPath) {
        Write-Host ("  {0}  ({1:N0} bytes)" -f $symbolsPath, (Get-Item $symbolsPath).Length) -ForegroundColor DarkGray
    }
}

Write-Host @"

To install: drag the .fb2k-component file onto foobar2000, or use
File > Preferences > Components > Install...
"@ -ForegroundColor Yellow
