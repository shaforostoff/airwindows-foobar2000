<#
.SYNOPSIS
    Builds the VirtualDJ plug-ins and wraps them in a Windows installer.

.DESCRIPTION
    Produces one file:

        plugins\dist\vdj\ShellacFilters-VirtualDJ-<version>-x64-Setup.exe

    x64 only. VirtualDJ has been 64 bit since 8.2 and a 32 bit plug-in cannot
    be loaded into it, so there is nothing for a 32 bit installer to install.

    Unless -SkipBuild is given this calls build.ps1 first, which includes the
    verification harnesses - packaging an unverified DLL into something that
    looks installable is exactly the mistake worth making impossible. Use
    -SkipBuild only to re-wrap binaries you have just built yourself.

    The installer is NSIS, and non-elevated on purpose. VirtualDJ keeps its
    plug-ins under a per-user root, so an installer that asked for
    administrator rights would resolve %LOCALAPPDATA% to the administrator's
    profile and put the DLLs where the user's VirtualDJ never looks. See the
    header of installer\vdj_plugins.nsi for that and for the two other things
    the script has to get right.

.PARAMETER Version
    Version to stamp the installer and the Programs and Features entry with.
    Default: the project() version in CMakeLists.txt, which is what package.sh
    reads too - one place to change it, and the file name then says the same
    thing the DLLs do.

.PARAMETER SkipBuild
    Package whatever is already in plugins\dist\vdj\x64 instead of rebuilding.

.PARAMETER MakeNsis
    Path to makensis.exe, if it is somewhere this script would not look.

.PARAMETER Clean
    Passed through to build.ps1: wipe the build tree first.

.EXAMPLE
    .\scripts\package.ps1

.EXAMPLE
    # re-wrap the current binaries without a rebuild
    .\scripts\package.ps1 -SkipBuild -Version 1.1
#>

[CmdletBinding()]
param(
    [string] $Version = '',
    [switch] $SkipBuild,
    [string] $MakeNsis = '',
    [switch] $Clean
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$root      = Split-Path -Parent $PSScriptRoot          # plugins/vdjplugin
$plugins   = Split-Path -Parent $root                  # plugins
$dist      = Join-Path $plugins 'dist\vdj'
$binDir    = Join-Path $dist 'x64'
$script    = Join-Path $root 'installer\vdj_plugins.nsi'
$license   = Join-Path (Split-Path -Parent $plugins) 'LICENSE'

$plugNames = @('Declick', 'Dehum')

# --- makensis ---------------------------------------------------------------
# NSIS does not put itself on PATH, so PATH is only the first guess. The
# registry key is written by both the 32 and 64 bit installers and is the
# authoritative answer when it is there.
function Find-MakeNsis {
    if ($MakeNsis) {
        if (-not (Test-Path $MakeNsis)) { throw "makensis not found at $MakeNsis" }
        return (Resolve-Path $MakeNsis).Path
    }

    $onPath = Get-Command makensis -ErrorAction SilentlyContinue
    if ($onPath) { return $onPath.Source }

    $candidates = @()
    foreach ($key in @('HKLM:\SOFTWARE\NSIS', 'HKLM:\SOFTWARE\WOW6432Node\NSIS')) {
        try {
            $dir = (Get-ItemProperty -Path $key -ErrorAction Stop).'(default)'
            if ($dir) { $candidates += (Join-Path $dir 'makensis.exe') }
        } catch { }
    }
    $candidates += @(
        (Join-Path ${env:ProgramFiles(x86)} 'NSIS\makensis.exe'),
        (Join-Path $env:ProgramFiles 'NSIS\makensis.exe')
    )
    foreach ($c in $candidates) {
        if ($c -and (Test-Path $c)) { return $c }
    }

    throw ("makensis.exe was not found. Install NSIS 3 from " +
           "https://nsis.sourceforge.io/ or pass -MakeNsis <path>.")
}

$nsis = Find-MakeNsis
Write-Host "makensis  $nsis" -ForegroundColor DarkGray

# --- version ----------------------------------------------------------------
# From project() rather than a literal here, so this and package.sh cannot
# disagree about what they just built. Matched on the project() block
# specifically: the first VERSION in the file belongs to
# cmake_minimum_required.
if (-not $Version) {
    $cml = Get-Content (Join-Path $root 'CMakeLists.txt') -Raw
    $m = [regex]::Match($cml, 'project\(shellacfilters_virtualdj[^)]*?VERSION\s+([0-9][0-9.]*)')
    if (-not $m.Success) {
        throw "could not find the project() version in CMakeLists.txt; pass -Version"
    }
    $Version = $m.Groups[1].Value
}

# VIProductVersion insists on four fields; the friendly string does not.
if ($Version -notmatch '^\d+(\.\d+){0,3}$') {
    throw "-Version must be numeric and dotted, e.g. 1.0 or 1.2.3"
}
$parts = @($Version -split '\.')
while ($parts.Count -lt 4) { $parts += '0' }
$version4 = ($parts[0..3] -join '.')

# --- build ------------------------------------------------------------------
if ($SkipBuild) {
    Write-Host "`n=== skipping build ===" -ForegroundColor Yellow
} else {
    # A hashtable, not an array: splatting an array binds positionally, so
    # @('-Arch','x64') would hand build.ps1 the literal string "-Arch" as its
    # first positional value.
    $buildArgs = @{ Arch = @('x64') }
    if ($Clean) { $buildArgs['Clean'] = $true }
    & (Join-Path $PSScriptRoot 'build.ps1') @buildArgs
}

foreach ($name in $plugNames) {
    $dll = Join-Path $binDir "$name.dll"
    if (-not (Test-Path $dll)) {
        throw ("$dll is missing. Run scripts\build.ps1 first, or drop " +
               "-SkipBuild.")
    }
}

# --- compile the installer --------------------------------------------------
$outFile = Join-Path $dist "ShellacFilters-VirtualDJ-$Version-x64-Setup.exe"
if (Test-Path $outFile) { Remove-Item $outFile -Force }

Write-Host "`n=== installer ===" -ForegroundColor Cyan
& $nsis `
    '/V2' `
    "/DDIST=$binDir" `
    "/DVERSION=$Version" `
    "/DVERSION4=$version4" `
    "/DLICENSEFILE=$license" `
    "/DOUTFILE=$outFile" `
    $script
if ($LASTEXITCODE -ne 0) { throw "makensis failed with exit code $LASTEXITCODE" }
if (-not (Test-Path $outFile)) { throw "makensis reported success but wrote nothing" }

# --- report -----------------------------------------------------------------
$payload = 0
foreach ($name in $plugNames) {
    $payload += (Get-Item (Join-Path $binDir "$name.dll")).Length
}
$size = (Get-Item $outFile).Length

Write-Host ""
Write-Host ("  {0,-24} {1,10:n0} bytes" -f 'payload (2 DLLs)', $payload)
Write-Host ("  {0,-24} {1,10:n0} bytes" -f (Split-Path -Leaf $outFile), $size) `
    -ForegroundColor Green
Write-Host ("  {0,-24} {1,10:p0}" -f 'compressed to', ($size / $payload)) `
    -ForegroundColor DarkGray
Write-Host "`ninstaller in $dist" -ForegroundColor Green
