<#
.SYNOPSIS
    Builds the VirtualDJ plug-ins into release DLLs.

.DESCRIPTION
    Produces, per architecture:

        plugins\dist\vdj\x64\Declick.dll   reads ahead, no delay
        plugins\dist\vdj\x64\Dehum.dll     no delay, scouts the record
        plugins\dist\vdj\x64\symbols\*.pdb

    and the same under x86 when that architecture is asked for.

    x64 by default and only. VirtualDJ has shipped a 64 bit build since 8.2 and
    a 32 bit plug-in cannot be loaded into it, so the 32 bit output is there for
    completeness rather than because anybody is likely to want it - pass
    -Arch x86,x64 if you do.

    Unless -SkipTests is given, declick_vdj_verify and dehum_vdj_verify are
    built and run for each architecture. What they check is the part of this
    project that is not shared with any other port - the slider mappings and
    BufferPipeline - and in particular that the audio the buffer plug-ins hand
    back is what the core produces running straight through the song, to the
    bit, at any block size. See tests/vdj_test_support.h.

    The VirtualDJ SDK is fetched on the first configure; nothing needs
    installing but CMake and Visual Studio.

.PARAMETER Arch
    Which architectures to build. Default: x64.

.PARAMETER Generator
    CMake generator. Default: whatever CMake picks, which on a machine with
    Visual Studio installed is the newest one it finds.

.PARAMETER SkipTests
    Build but do not verify. Not recommended.

.PARAMETER Clean
    Wipe the build trees first.

.PARAMETER Trace
    Build diagnostic plug-ins that log every load and lifecycle call to
    %TEMP%\shellacfilters_vdj_trace.log. For working out why a host does not
    list a plug-in; see common/vdj_trace.h for how to read it. Not for shipping.

.PARAMETER Install
    Copy the finished plug-ins into the VirtualDJ plug-in folder afterwards, by
    handing off to install.ps1.

.EXAMPLE
    .\scripts\build.ps1

.EXAMPLE
    .\scripts\build.ps1 -Arch x86,x64 -Clean

.EXAMPLE
    # build, verify, and drop them where VirtualDJ will find them
    .\scripts\build.ps1 -Install
#>

[CmdletBinding()]
param(
    [ValidateSet('x86', 'x64')]
    [string[]] $Arch = @('x64'),
    [string]   $Generator = '',
    [switch]   $SkipTests,
    [switch]   $Clean,
    [switch]   $Install,
    [switch]   $Trace
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$root    = Split-Path -Parent $PSScriptRoot          # plugins/vdjplugin
$plugins = Split-Path -Parent $root                  # plugins
# Alongside the .fb2k-components and the VST2 DLLs, because a VirtualDJ plug-in
# is neither of those either.
$dist    = Join-Path $plugins 'dist\vdj'

if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    throw "cmake was not found on PATH. Install CMake 3.16 or newer."
}

$plugNames = @('Declick', 'Dehum')
$failed    = @()

foreach ($a in $Arch) {
    $platform = if ($a -eq 'x86') { 'Win32' } else { 'x64' }
    $build    = Join-Path $root "build\$a"

    if ($Clean -and (Test-Path $build)) {
        Write-Host "cleaning $build" -ForegroundColor DarkGray
        Remove-Item -Recurse -Force $build
    }

    Write-Host "`n=== configure $a ===" -ForegroundColor Cyan
    $cfg = @('-S', $root, '-B', $build, '-A', $platform)
    if ($Generator) { $cfg += @('-G', $Generator) }
    if ($SkipTests) { $cfg += '-DVDJ_BUILD_TESTS=OFF' }
    $cfg += ('-DVDJ_TRACE=' + $(if ($Trace) { 'ON' } else { 'OFF' }))
    & cmake @cfg
    if ($LASTEXITCODE -ne 0) { throw "configure failed for $a" }

    Write-Host "`n=== build $a ===" -ForegroundColor Cyan
    & cmake --build $build --config Release
    if ($LASTEXITCODE -ne 0) { throw "build failed for $a" }

    if (-not $SkipTests) {
        Write-Host "`n=== verify $a ===" -ForegroundColor Cyan
        & ctest --test-dir $build -C Release --output-on-failure
        if ($LASTEXITCODE -ne 0) { $failed += $a }
    }

    # --- package ----------------------------------------------------------
    $outDir = Join-Path $dist $a
    $symDir = Join-Path $outDir 'symbols'
    New-Item -ItemType Directory -Force $outDir | Out-Null
    New-Item -ItemType Directory -Force $symDir | Out-Null

    foreach ($name in $plugNames) {
        $dll = Join-Path $build "plugins\$name.dll"
        if (-not (Test-Path $dll)) { throw "expected $dll" }
        Copy-Item $dll $outDir -Force
        Write-Host ("  {0,-20} {1:n0} bytes" -f "$name.dll", (Get-Item $dll).Length)

        # Release builds carry a PDB on purpose - a crash report from a DJ
        # booth is only useful with symbols, and /DEBUG does not slow the
        # generated code down. They are packaged separately so that what gets
        # copied into the plug-in folder is only the DLLs.
        $pdb = Join-Path $build "symbols\Release\$name.pdb"
        if (-not (Test-Path $pdb)) { $pdb = Join-Path $build "symbols\$name.pdb" }
        if (Test-Path $pdb) { Copy-Item $pdb $symDir -Force }
    }
}

if ($failed.Count -gt 0) {
    throw ("verification FAILED for: {0}. The DLLs were still written to " +
           "{1}, but do not ship them." -f ($failed -join ', '), $dist)
}

Write-Host "`nplug-ins in $dist" -ForegroundColor Green

if ($Install) {
    & (Join-Path $PSScriptRoot 'install.ps1') -Arch $Arch
}
