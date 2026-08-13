<#
.SYNOPSIS
    Downloads and unpacks the foobar2000 SDK into external/foobar2000_sdk.

.DESCRIPTION
    Thin wrapper around cmake/fb2k_download_sdk.cmake, which is also what the
    top level CMakeLists.txt calls on its own when the SDK is missing - so you
    normally never need to run this by hand.

    Nothing but CMake is required: its bundled libarchive reads .7z, so no
    7-Zip installation is involved.

.PARAMETER Destination
    Where to unpack. Default: external/foobar2000_sdk next to this repository.

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
if (-not $Destination) { $Destination = Join-Path $root 'external\foobar2000_sdk' }

if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    throw "cmake was not found on PATH. Install CMake 3.16 or newer."
}

$args = @("-DFB2K_SDK_DEST=$Destination")
if ($Force) { $args += '-DFB2K_SDK_FORCE=ON' }
$args += @('-P', (Join-Path $root 'cmake\fb2k_download_sdk.cmake'))

& cmake @args
if ($LASTEXITCODE -ne 0) { throw "SDK download failed with exit code $LASTEXITCODE" }
