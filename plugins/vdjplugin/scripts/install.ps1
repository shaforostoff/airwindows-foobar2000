<#
.SYNOPSIS
    Copies the built plug-ins into VirtualDJ's plug-in folder.

.DESCRIPTION
    VirtualDJ loads plug-ins from a per-user folder, and which one depends on
    the version:

        %LOCALAPPDATA%\VirtualDJ              2023 and later
        %USERPROFILE%\Documents\VirtualDJ     8 through 2021

    Under it, plug-ins go in an architecture folder and then a category
    sub-folder:

        Plugins64\SoundEffect      64 bit VirtualDJ, which is all of it since 8.2
        Plugins\SoundEffect        32 bit

    This copies to every VirtualDJ home it finds, so it does the right thing
    without being told which version is installed, and says what it did.

    All four plug-ins go in SoundEffect, the buffer ones included. That is the
    documented home for a DSP plug-in and the SDK gives no separate folder for a
    buffer one - VirtualDJ decides what a plug-in is by asking it for an
    interface by IID, not by where it sits (see common/vdj_entry.h). If a buffer
    plug-in does not appear in the effects list on your build, that is the first
    thing to try moving.

    Restart VirtualDJ afterwards: the folder is scanned at startup.

.PARAMETER Arch
    Which architectures to install. Default: whatever has been built.

.PARAMETER VdjHome
    Install into this VirtualDJ home instead of searching for one.

.PARAMETER Uninstall
    Remove the plug-ins instead of copying them.

.EXAMPLE
    .\scripts\install.ps1

.EXAMPLE
    .\scripts\install.ps1 -Uninstall
#>

[CmdletBinding()]
param(
    [ValidateSet('x86', 'x64')]
    [string[]] $Arch = @(),
    [string]   $VdjHome = '',
    [switch]   $Uninstall
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$root    = Split-Path -Parent $PSScriptRoot
$plugins = Split-Path -Parent $root
$dist    = Join-Path $plugins 'dist\vdj'

$plugNames = @('Declick', 'Dehum')

# --- where does VirtualDJ keep its plug-ins ---------------------------------
$homes = @()
if ($VdjHome) {
    $homes += $VdjHome
} else {
    $candidates = @(
        (Join-Path $env:LOCALAPPDATA 'VirtualDJ'),
        (Join-Path $env:USERPROFILE 'Documents\VirtualDJ')
    )
    foreach ($c in $candidates) {
        if (Test-Path $c) { $homes += $c }
    }
}

if ($homes.Count -eq 0) {
    throw ("No VirtualDJ home found. Looked in {0}\VirtualDJ and " +
           "{1}\Documents\VirtualDJ. Run VirtualDJ once so it creates its " +
           "folder, or pass -VdjHome." -f $env:LOCALAPPDATA, $env:USERPROFILE)
}

# --- which architectures ----------------------------------------------------
if ($Arch.Count -eq 0) {
    foreach ($a in @('x64', 'x86')) {
        if (Test-Path (Join-Path $dist $a)) { $Arch += $a }
    }
}
if ($Arch.Count -eq 0) {
    throw "Nothing built. Run scripts\build.ps1 first."
}

$copied = 0
foreach ($vdjHome in $homes) {
    foreach ($a in $Arch) {
        $src = Join-Path $dist $a
        $sub = if ($a -eq 'x86') { 'Plugins' } else { 'Plugins64' }
        $target = Join-Path $vdjHome "$sub\SoundEffect"

        if ($Uninstall) {
            foreach ($name in $plugNames) {
                $dst = Join-Path $target "$name.dll"
                if (Test-Path $dst) {
                    Remove-Item $dst -Force
                    Write-Host "  removed  $dst" -ForegroundColor Yellow
                    $copied++
                }
            }
            continue
        }

        New-Item -ItemType Directory -Force $target | Out-Null
        foreach ($name in $plugNames) {
            $dll = Join-Path $src "$name.dll"
            if (-not (Test-Path $dll)) { continue }
            Copy-Item $dll $target -Force
            Write-Host "  installed  $target\$name.dll" -ForegroundColor Green
            $copied++
        }
    }
}

if ($copied -eq 0) {
    Write-Warning "nothing to do"
} elseif (-not $Uninstall) {
    Write-Host "`nRestart VirtualDJ; the plug-in folder is scanned at startup." -ForegroundColor Cyan
    Write-Host "They appear under Settings > Extensions > Effects as " -NoNewline
    Write-Host "Declick and Dehum."
}
