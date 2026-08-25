<#
.SYNOPSIS
    Downloads and unpacks the VirtualDJ 8 plugin SDK into external/virtualdj_sdk.

.DESCRIPTION
    Thin wrapper around cmake/vdj_download_sdk.cmake, which the top level
    CMakeLists.txt also calls on its own when the SDK is missing - so you
    normally never need to run this by hand. The counterpart of
    ../foobar2000_dsp/scripts/get_sdk.ps1, deliberately the same shape.

    Nothing but CMake is required: its bundled libarchive reads the .zip.

    The archive is three header files and 7 kB. There is no library to link and
    no runtime to find: a VirtualDJ plug-in is a DLL that exports one C function
    and implements one abstract class, and the headers are the whole of the ABI.

.PARAMETER Destination
    Where to unpack. Default: external/virtualdj_sdk next to this project.

.PARAMETER Force
    Re-unpack even if the SDK is already there.
#>

[CmdletBinding()]
param(
    [string] $Destination = '',
    [switch] $Force
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$root = Split-Path -Parent $PSScriptRoot
if (-not $Destination) { $Destination = Join-Path $root 'external\virtualdj_sdk' }

if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    throw "cmake was not found on PATH. Install CMake 3.16 or newer."
}

$cmakeArgs = @("-DVDJ_SDK_DEST=$Destination")
if ($Force) { $cmakeArgs += '-DVDJ_SDK_FORCE=ON' }
$cmakeArgs += @('-P', (Join-Path $root 'cmake\vdj_download_sdk.cmake'))

& cmake @cmakeArgs
if ($LASTEXITCODE -ne 0) { throw "SDK download failed with exit code $LASTEXITCODE" }
