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
# Visual Studio Build Tools and Windows SDK detection and validation
#

# Import common utilities
$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
Import-Module (Join-Path $scriptRoot "common.ps1") -Force

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Get-VSInstallPath {
    param(
        [string]$Version = "2022"
    )

    Write-Log "Searching for Visual Studio $Version Build Tools..." -Level Info

    # Common installation paths
    $possiblePaths = @(
        "C:\Program Files\Microsoft Visual Studio\$Version\BuildTools",
        "C:\Program Files\Microsoft Visual Studio\$Version\Professional",
        "C:\Program Files\Microsoft Visual Studio\$Version\Enterprise",
        "C:\Program Files\Microsoft Visual Studio\$Version\Community",
        "C:\Program Files (x86)\Microsoft Visual Studio\$Version\BuildTools",
        "C:\Program Files (x86)\Microsoft Visual Studio\$Version\Professional",
        "C:\Program Files (x86)\Microsoft Visual Studio\$Version\Enterprise",
        "C:\Program Files (x86)\Microsoft Visual Studio\$Version\Community"
    )

    foreach ($path in $possiblePaths) {
        if (Test-Path $path) {
            Write-Log "Found VS installation at: $path" -Level Success
            return $path
        }
    }

    # Try using vswhere if available
    $vswherePath = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswherePath) {
        Write-Log "Using vswhere to locate Visual Studio..." -Level Verbose

        $vsPath = & $vswherePath -version "[$Version,$([int]$Version + 1))" -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath -latest

        if ($vsPath -and (Test-Path $vsPath)) {
            Write-Log "Found VS installation via vswhere: $vsPath" -Level Success
            return $vsPath
        }
    }

    return $null
}

function Get-VSVersion {
    param(
        [Parameter(Mandatory=$true)]
        [string]$VSPath
    )

    # Try to read the version from catalog file
    $catalogPath = Join-Path $VSPath "VC\Auxiliary\Build\Microsoft.VCToolsVersion.default.txt"

    if (Test-Path $catalogPath) {
        $version = Get-Content $catalogPath -Raw
        $version = $version.Trim()
        Write-Log "VC Tools Version: $version" -Level Verbose
        return $version
    }

    return "Unknown"
}

function Test-VSComponentInstalled {
    param(
        [Parameter(Mandatory=$true)]
        [string]$VSPath,

        [Parameter(Mandatory=$true)]
        [string]$Architecture
    )

    $arch = ConvertTo-ArchitectureName -Architecture $Architecture

    # Check for required compiler binaries
    $compilerPaths = @(
        "VC\Tools\MSVC\*\bin\Hostx64\$arch\cl.exe",
        "VC\Tools\MSVC\*\bin\Hostarm64\$arch\cl.exe"
    )

    foreach ($pattern in $compilerPaths) {
        $fullPattern = Join-Path $VSPath $pattern
        $matches = Get-ChildItem -Path $fullPattern -ErrorAction SilentlyContinue

        if ($matches) {
            Write-Log "Found compiler for $arch at: $($matches[0].FullName)" -Level Verbose
            return $true
        }
    }

    return $false
}

function Get-WindowsSDKPath {
    Write-Log "Searching for Windows SDK..." -Level Info

    # Common installation paths
    $possiblePaths = @(
        "C:\Program Files (x86)\Windows Kits\10",
        "C:\Program Files\Windows Kits\10"
    )

    foreach ($path in $possiblePaths) {
        if (Test-Path $path) {
            Write-Log "Found Windows SDK at: $path" -Level Success
            return $path
        }
    }

    # Check registry
    $regPaths = @(
        "HKLM:\SOFTWARE\Microsoft\Windows Kits\Installed Roots",
        "HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows Kits\Installed Roots"
    )

    foreach ($regPath in $regPaths) {
        if (Test-Path $regPath) {
            $sdkRoot = (Get-ItemProperty -Path $regPath -Name "KitsRoot10" -ErrorAction SilentlyContinue).KitsRoot10
            if ($sdkRoot -and (Test-Path $sdkRoot)) {
                Write-Log "Found Windows SDK via registry: $sdkRoot" -Level Success
                return $sdkRoot
            }
        }
    }

    return $null
}

function Get-WindowsSDKVersion {
    param(
        [Parameter(Mandatory=$true)]
        [string]$SDKPath
    )

    $includePath = Join-Path $SDKPath "Include"

    if (-not (Test-Path $includePath)) {
        return $null
    }

    # Get all SDK versions (directories that look like version numbers)
    $versions = Get-ChildItem -Path $includePath -Directory |
        Where-Object { $_.Name -match '^\d+\.\d+\.\d+\.\d+$' } |
        Sort-Object Name -Descending

    if ($versions) {
        $latestVersion = $versions[0].Name
        Write-Log "Latest Windows SDK version: $latestVersion" -Level Verbose
        return $latestVersion
    }

    return $null
}

function Test-WindowsSDKComponentInstalled {
    param(
        [Parameter(Mandatory=$true)]
        [string]$SDKPath,

        [string]$Component = "Debuggers"
    )

    $componentPath = Join-Path $SDKPath $Component

    if (Test-Path $componentPath) {
        Write-Log "Windows SDK component '$Component' is installed" -Level Verbose
        return $true
    }

    return $false
}

function Show-VSInstallationInstructions {
    param(
        [string]$Version = "2022"
    )

    Write-Section "Visual Studio Build Tools Installation Required"

    Write-Host "Visual Studio $Version Build Tools is not installed or could not be found." -ForegroundColor Yellow
    Write-Host ""
    Write-Host "Please install it manually using the following steps:" -ForegroundColor White
    Write-Host ""
    Write-Host "1. Download Visual Studio $Version Build Tools from:" -ForegroundColor White
    Write-Host "   https://visualstudio.microsoft.com/downloads/" -ForegroundColor Cyan
    Write-Host ""
    Write-Host "2. Select 'Fixed version bootstrappers' under Release History for $Version" -ForegroundColor White
    Write-Host ""
    Write-Host "3. Search for 'Build Tools $Version version 17.12 LTSC' (or latest)" -ForegroundColor White
    Write-Host ""
    Write-Host "4. During installation, select the following workloads:" -ForegroundColor White
    Write-Host "   - Desktop development with C++" -ForegroundColor Cyan
    Write-Host "   - MSVC v143 - VS $Version C++ build tools (Latest)" -ForegroundColor Cyan
    Write-Host "   - C++ CMake tools for Windows" -ForegroundColor Cyan
    Write-Host ""
    Write-Host "5. For ARM64 development, also install:" -ForegroundColor White
    Write-Host "   - MSVC v143 - VS $Version C++ ARM64/ARM64EC build tools (Latest)" -ForegroundColor Cyan
    Write-Host ""
    Write-Host "After installation, re-run this script." -ForegroundColor Green
    Write-Host ""
}

function Show-WindowsSDKInstructions {
    Write-Section "Windows SDK Installation (Optional)"

    Write-Host "Windows SDK is not installed or could not be found." -ForegroundColor Yellow
    Write-Host ""
    Write-Host "The Windows SDK is optional but recommended for debugging tools." -ForegroundColor White
    Write-Host ""
    Write-Host "To install Windows SDK:" -ForegroundColor White
    Write-Host ""
    Write-Host "1. Download from:" -ForegroundColor White
    Write-Host "   https://developer.microsoft.com/en-us/windows/downloads/windows-sdk/" -ForegroundColor Cyan
    Write-Host ""
    Write-Host "2. During installation, make sure to select:" -ForegroundColor White
    Write-Host "   - Debugging Tools for Windows" -ForegroundColor Cyan
    Write-Host "   - Windows Performance Toolkit" -ForegroundColor Cyan
    Write-Host ""
    Write-Host "You can continue without the SDK, or install it and re-run this script." -ForegroundColor Green
    Write-Host ""
}

function Get-VSInstallationInfo {
    param(
        [string]$VSVersion = "2022",
        [string[]]$RequiredArchitectures = @("arm64", "x64", "x86"),
        [switch]$ShowInstructionsOnFailure
    )

    $info = @{
        VSPath = $null
        VSVersion = $null
        VSToolsVersion = $null
        SDKPath = $null
        SDKVersion = $null
        HasDebuggers = $false
        MissingComponents = @()
        IsValid = $false
    }

    # Find Visual Studio
    $vsPath = Get-VSInstallPath -Version $VSVersion

    if (-not $vsPath) {
        Write-Log "Visual Studio $VSVersion Build Tools not found" -Level Error
        if ($ShowInstructionsOnFailure) {
            Show-VSInstallationInstructions -Version $VSVersion
        }
        return $info
    }

    $info.VSPath = $vsPath
    $info.VSVersion = $VSVersion
    $info.VSToolsVersion = Get-VSVersion -VSPath $vsPath

    # Check for required architecture components
    foreach ($arch in $RequiredArchitectures) {
        if (-not (Test-VSComponentInstalled -VSPath $vsPath -Architecture $arch)) {
            $info.MissingComponents += "MSVC $arch compiler"
            Write-Log "Missing MSVC compiler for architecture: $arch" -Level Warning
        }
    }

    # Find Windows SDK (optional)
    $sdkPath = Get-WindowsSDKPath

    if ($sdkPath) {
        $info.SDKPath = $sdkPath
        $info.SDKVersion = Get-WindowsSDKVersion -SDKPath $sdkPath
        $info.HasDebuggers = Test-WindowsSDKComponentInstalled -SDKPath $sdkPath -Component "Debuggers"
    } else {
        Write-Log "Windows SDK not found (optional)" -Level Warning
        if ($ShowInstructionsOnFailure) {
            Show-WindowsSDKInstructions
        }
    }

    # Determine if installation is valid
    $info.IsValid = ($info.VSPath -ne $null) -and ($info.MissingComponents.Count -eq 0)

    return $info
}

function Copy-VSInstallation {
    param(
        [Parameter(Mandatory=$true)]
        [string]$SourcePath,

        [Parameter(Mandatory=$true)]
        [string]$DestinationPath,

        [string]$ToolchainName = "vc143"
    )

    $destRoot = Join-Path $DestinationPath "toolchain-msvc\$ToolchainName"
    $destPath = Join-Path $destRoot "BuildTools"

    Write-Log "Copying Visual Studio installation to distribution directory..." -Level Info
    Write-Log "Source: $SourcePath" -Level Verbose
    Write-Log "Destination: $destPath" -Level Verbose

    Copy-DirectoryWithProgress -Source $SourcePath -Destination $destPath -Description "Copying VS Build Tools"

    return $destPath
}

function Copy-WindowsSDK {
    param(
        [Parameter(Mandatory=$true)]
        [string]$SourcePath,

        [Parameter(Mandatory=$true)]
        [string]$DestinationPath
    )

    $destPath = Join-Path $DestinationPath "winsdk\10\default"

    Write-Log "Copying Windows SDK to distribution directory..." -Level Info
    Write-Log "Source: $SourcePath" -Level Verbose
    Write-Log "Destination: $destPath" -Level Verbose

    Copy-DirectoryWithProgress -Source $SourcePath -Destination $destPath -Description "Copying Windows SDK"

    return $destPath
}

# Export functions
# Export-ModuleMember -Function @(
#     'Get-VSInstallPath',
#     'Get-VSVersion',
#     'Test-VSComponentInstalled',
#     'Get-WindowsSDKPath',
#     'Get-WindowsSDKVersion',
#     'Test-WindowsSDKComponentInstalled',
#     'Show-VSInstallationInstructions',
#     'Show-WindowsSDKInstructions',
#     'Get-VSInstallationInfo',
#     'Copy-VSInstallation',
#     'Copy-WindowsSDK'
# )
