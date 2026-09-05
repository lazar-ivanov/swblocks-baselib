################################################################################
# This file is part of the swblocks-baselib library.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
################################################################################
#
# Source code download utilities for Boost and OpenSSL
#

# Import common utilities
$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
Import-Module (Join-Path $scriptRoot "common.ps1") -Force

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Get-BoostDownloadUrl {
    param(
        [Parameter(Mandatory=$true)]
        [string]$Version
    )

    # Boost URL format: https://archives.boost.io/release/1.90.0/source/boost_1_90_0.tar.gz
    $versionUnderscore = $Version -replace '\.', '_'
    return "https://archives.boost.io/release/$Version/source/boost_$versionUnderscore.tar.gz"
}

function Get-OpenSSLDownloadUrl {
    param(
        [Parameter(Mandatory=$true)]
        [string]$Version
    )

    # OpenSSL URL format: https://github.com/openssl/openssl/releases/download/openssl-3.5.4/openssl-3.5.4.tar.gz
    return "https://github.com/openssl/openssl/releases/download/openssl-$Version/openssl-$Version.tar.gz"
}

function Install-BoostSource {
    param(
        [Parameter(Mandatory=$true)]
        [string]$Version,

        [Parameter(Mandatory=$true)]
        [string]$DestinationRoot,

        [string]$CacheDirectory = $null,

        [switch]$SkipDownload
    )

    Write-SubSection "Installing Boost $Version Source"

    $installPath = Join-Path $DestinationRoot "boost\$Version\source-windows"

    # Check if already installed
    if (Test-Path $installPath) {
        $boostHpp = Join-Path $installPath "boost\version.hpp"
        if (Test-Path $boostHpp) {
            Write-Log "Boost source already exists at: $installPath" -Level Info
            Write-Log "Skipping download" -Level Info
            return $installPath
        }
    }

    if ($SkipDownload) {
        throw "Boost source not found and -SkipDownload was specified"
    }

    # Determine cache directory
    if (-not $CacheDirectory) {
        $CacheDirectory = Join-Path $env:TEMP "devenv7-downloads"
    }

    New-DirectoryIfNotExists -Path $CacheDirectory | Out-Null

    # Download
    $downloadUrl = Get-BoostDownloadUrl -Version $Version
    $versionUnderscore = $Version -replace '\.', '_'
    $archivePath = Join-Path $CacheDirectory "boost_$versionUnderscore.tar.gz"

    if (-not (Test-Path $archivePath)) {
        Write-Log "Downloading Boost $Version..." -Level Info
        Write-Log "From: $downloadUrl" -Level Verbose
        Write-Log "To: $archivePath" -Level Verbose

        Invoke-WebDownload -Url $downloadUrl -DestinationPath $archivePath

        Write-Log "Download completed" -Level Success
    } else {
        Write-Log "Using cached archive: $archivePath" -Level Info
    }

    # Extract
    Write-Log "Extracting Boost source to: $installPath" -Level Info

    # Create temp extraction directory
    $tempExtract = Join-Path $env:TEMP "boost-extract-$(Get-Random)"
    New-DirectoryIfNotExists -Path $tempExtract | Out-Null

    try {
        # Extract .tar.gz (need to extract twice: .gz then .tar)
        Write-Log "Decompressing archive..." -Level Verbose

        # First extract .gz to get .tar
        $tarPath = Join-Path $tempExtract "boost.tar"

        # Use PowerShell to decompress gzip
        $gzipStream = New-Object System.IO.FileStream($archivePath, [System.IO.FileMode]::Open)
        $gzipDecompressor = New-Object System.IO.Compression.GZipStream($gzipStream, [System.IO.Compression.CompressionMode]::Decompress)
        $tarStream = New-Object System.IO.FileStream($tarPath, [System.IO.FileMode]::Create)

        $gzipDecompressor.CopyTo($tarStream)

        $tarStream.Close()
        $gzipDecompressor.Close()
        $gzipStream.Close()

        Write-Log "Extracting tar archive..." -Level Verbose

        # Use tar command (available in Windows 10+)
        $extractDir = Join-Path $tempExtract "extracted"
        New-DirectoryIfNotExists -Path $extractDir | Out-Null

        $result = Invoke-NativeCommand -Command "tar" -Arguments @("-xf", $tarPath, "-C", $extractDir)

        if ($result.ExitCode -ne 0) {
            throw "Failed to extract tar archive: $($result.StdErr)"
        }

        # Find extracted directory (should be boost_X_XX_X)
        $extractedDir = Get-ChildItem -Path $extractDir -Directory | Select-Object -First 1

        if (-not $extractedDir) {
            throw "Could not find extracted Boost directory"
        }

        # Copy to final location
        Write-Log "Copying to installation path..." -Level Verbose
        New-DirectoryIfNotExists -Path (Split-Path $installPath -Parent) | Out-Null

        Copy-DirectoryWithProgress -SourcePath $extractedDir.FullName -DestinationPath $installPath

        Write-Log "Boost source installed successfully" -Level Success

        return $installPath

    } finally {
        # Clean up temp directory
        if (Test-Path $tempExtract) {
            Remove-Item -Path $tempExtract -Recurse -Force -ErrorAction SilentlyContinue
        }
    }
}

function Install-OpenSSLSource {
    param(
        [Parameter(Mandatory=$true)]
        [string]$Version,

        [Parameter(Mandatory=$true)]
        [string]$DestinationRoot,

        [string]$CacheDirectory = $null,

        [switch]$SkipDownload
    )

    Write-SubSection "Installing OpenSSL $Version Source"

    $installPath = Join-Path $DestinationRoot "openssl\$Version\source-windows"

    # Check if already installed
    if (Test-Path $installPath) {
        $configureFile = Join-Path $installPath "Configure"
        if (Test-Path $configureFile) {
            Write-Log "OpenSSL source already exists at: $installPath" -Level Info
            Write-Log "Skipping download" -Level Info
            return $installPath
        }
    }

    if ($SkipDownload) {
        throw "OpenSSL source not found and -SkipDownload was specified"
    }

    # Determine cache directory
    if (-not $CacheDirectory) {
        $CacheDirectory = Join-Path $env:TEMP "devenv7-downloads"
    }

    New-DirectoryIfNotExists -Path $CacheDirectory | Out-Null

    # Download
    $downloadUrl = Get-OpenSSLDownloadUrl -Version $Version
    $archivePath = Join-Path $CacheDirectory "openssl-$Version.tar.gz"

    if (-not (Test-Path $archivePath)) {
        Write-Log "Downloading OpenSSL $Version..." -Level Info
        Write-Log "From: $downloadUrl" -Level Verbose
        Write-Log "To: $archivePath" -Level Verbose

        Invoke-WebDownload -Url $downloadUrl -DestinationPath $archivePath

        Write-Log "Download completed" -Level Success
    } else {
        Write-Log "Using cached archive: $archivePath" -Level Info
    }

    # Extract
    Write-Log "Extracting OpenSSL source to: $installPath" -Level Info

    # Create temp extraction directory
    $tempExtract = Join-Path $env:TEMP "openssl-extract-$(Get-Random)"
    New-DirectoryIfNotExists -Path $tempExtract | Out-Null

    try {
        # Extract .tar.gz
        Write-Log "Decompressing archive..." -Level Verbose

        # First extract .gz to get .tar
        $tarPath = Join-Path $tempExtract "openssl.tar"

        # Use PowerShell to decompress gzip
        $gzipStream = New-Object System.IO.FileStream($archivePath, [System.IO.FileMode]::Open)
        $gzipDecompressor = New-Object System.IO.Compression.GZipStream($gzipStream, [System.IO.Compression.CompressionMode]::Decompress)
        $tarStream = New-Object System.IO.FileStream($tarPath, [System.IO.FileMode]::Create)

        $gzipDecompressor.CopyTo($tarStream)

        $tarStream.Close()
        $gzipDecompressor.Close()
        $gzipStream.Close()

        Write-Log "Extracting tar archive..." -Level Verbose

        # Use tar command (available in Windows 10+)
        $extractDir = Join-Path $tempExtract "extracted"
        New-DirectoryIfNotExists -Path $extractDir | Out-Null

        $result = Invoke-NativeCommand -Command "tar" -Arguments @("-xf", $tarPath, "-C", $extractDir)

        if ($result.ExitCode -ne 0) {
            throw "Failed to extract tar archive: $($result.StdErr)"
        }

        # Find extracted directory (should be openssl-X.X.X)
        $extractedDir = Get-ChildItem -Path $extractDir -Directory | Select-Object -First 1

        if (-not $extractedDir) {
            throw "Could not find extracted OpenSSL directory"
        }

        # Copy to final location
        Write-Log "Copying to installation path..." -Level Verbose
        New-DirectoryIfNotExists -Path (Split-Path $installPath -Parent) | Out-Null

        Copy-DirectoryWithProgress -SourcePath $extractedDir.FullName -DestinationPath $installPath

        Write-Log "OpenSSL source installed successfully" -Level Success

        return $installPath

    } finally {
        # Clean up temp directory
        if (Test-Path $tempExtract) {
            Remove-Item -Path $tempExtract -Recurse -Force -ErrorAction SilentlyContinue
        }
    }
}

# Export functions
# Export-ModuleMember -Function @(
#     'Get-BoostDownloadUrl',
#     'Get-OpenSSLDownloadUrl',
#     'Install-BoostSource',
#     'Install-OpenSSLSource'
# )
