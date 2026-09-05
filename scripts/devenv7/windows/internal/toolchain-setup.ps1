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
# Main toolchain setup automation for Windows devenv7
#

# Import required modules
$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
Import-Module (Join-Path $scriptRoot "common.ps1") -Force
Import-Module (Join-Path $scriptRoot "vs-detector.ps1") -Force
Import-Module (Join-Path $scriptRoot "download-tools.ps1") -Force

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function New-ToolchainEnvironment {
    param(
        [string]$DistRoot = "C:\swblocks\dist-devenv7-windows-arm",
        [string]$HostArchitecture = $null,
        [string[]]$TargetArchitectures = @(),
        [string]$GitVersion = "2.48.1",
        [string]$PythonVersion = "3.14.2",
        [string]$MSYS2Version = "20251213",
        [string]$PerlVersion = "5.32.1.1",
        [string]$JSONSpiritVersion = "4.08",
        [string]$OpenJDKVersion = "25",
        [string]$GradleVersion = "9.2.1",
        [string]$JomVersion = "1.1.5",
        [string]$NASMVersion = "3.01",
        [string]$BoostVersion = "1.90.0",
        [string]$OpenSSLVersion = "3.5.4",
        [string]$VSVersion = "2022",
        [string]$CacheDirectory = $null,
        [switch]$SkipVSCopy,
        [switch]$SkipSDKCopy,
        [switch]$SkipDownloads,
        [switch]$UpdateMSYS2,
        [switch]$WhatIf,
        [switch]$Verbose
    )

    # Initialize logging
    # Place logs in parent swblocks directory, not in dist
    $swblocksRoot = Split-Path -Parent $DistRoot
    $logPath = Join-Path $swblocksRoot "logs\toolchain-setup-$(Get-Date -Format 'yyyyMMdd-HHmmss').log"
    Initialize-Logging -LogPath $logPath -Verbose:$Verbose

    Write-Section "Windows devenv7 Toolchain Setup"

    # Display configuration
    Write-Host "Configuration:" -ForegroundColor Cyan
    Write-Host "  Distribution Root: $DistRoot" -ForegroundColor White
    Write-Host "  Host Architecture: $(if ($HostArchitecture) { $HostArchitecture } else { 'auto-detect' })" -ForegroundColor White
    Write-Host "  Target Architectures: $(if ($TargetArchitectures) { $TargetArchitectures -join ', ' } else { 'auto-detect' })" -ForegroundColor White
    Write-Host "  Git Version: $GitVersion" -ForegroundColor White
    Write-Host "  Python Version: $PythonVersion" -ForegroundColor White
    Write-Host "  MSYS2 Version: $MSYS2Version" -ForegroundColor White
    Write-Host "  Strawberry Perl Version: $PerlVersion" -ForegroundColor White
    Write-Host "  JSON Spirit Version: $JSONSpiritVersion" -ForegroundColor White
    Write-Host "  OpenJDK Version: $OpenJDKVersion" -ForegroundColor White
    Write-Host "  Gradle Version: $GradleVersion" -ForegroundColor White
    Write-Host "  Jom Version: $JomVersion" -ForegroundColor White
    Write-Host "  NASM Version: $NASMVersion" -ForegroundColor White
    Write-Host "  Boost Version: $BoostVersion" -ForegroundColor White
    Write-Host "  OpenSSL Version: $OpenSSLVersion" -ForegroundColor White
    Write-Host "  Visual Studio Version: $VSVersion" -ForegroundColor White
    Write-Host ""

    if ($WhatIf) {
        Write-Host "Running in WhatIf mode - no changes will be made" -ForegroundColor Yellow
        Write-Host ""
    }

    # Detect or use specified host architecture
    if (-not $HostArchitecture) {
        $HostArchitecture = Get-NativeArchitecture
        Write-Log "Detected host architecture: $HostArchitecture" -Level Info
    } else {
        $HostArchitecture = ConvertTo-ArchitectureName $HostArchitecture
        Write-Log "Using specified host architecture: $HostArchitecture" -Level Info
    }

    # Set default target architectures to host architecture if not specified
    if (-not $TargetArchitectures -or $TargetArchitectures.Count -eq 0) {
        $TargetArchitectures = @($HostArchitecture)
        Write-Log "Using default target architecture: $HostArchitecture" -Level Info
    }

    # Normalize architecture names
    $TargetArchitectures = $TargetArchitectures | ForEach-Object { ConvertTo-ArchitectureName $_ }

    if ($WhatIf) {
        Write-Log "Would create toolchain for architectures: $($TargetArchitectures -join ', ')" -Level Info
        return
    }

    # Step 1: Validate Visual Studio installation
    Write-Section "Step 1: Validating Visual Studio Installation"

    $vsInfo = Get-VSInstallationInfo -VSVersion $VSVersion -RequiredArchitectures $TargetArchitectures `
        -HostArchitecture $HostArchitecture -ShowInstructionsOnFailure

    if (-not $vsInfo.IsValid) {
        throw "Visual Studio installation is invalid or incomplete. Please install required components."
    }

    Write-Log "Visual Studio $VSVersion Build Tools found at: $($vsInfo.VSPath)" -Level Success
    Write-Log "VC Tools Version: $($vsInfo.VSToolsVersion)" -Level Info

    if ($vsInfo.SDKPath) {
        Write-Log "Windows SDK found at: $($vsInfo.SDKPath)" -Level Success
        Write-Log "SDK Version: $($vsInfo.SDKVersion)" -Level Info
    }

    # Step 2: Create distribution directory structure
    Write-Section "Step 2: Creating Distribution Directory Structure"

    New-DirectoryIfNotExists -Path $DistRoot

    Write-Log "Distribution root created: $DistRoot" -Level Success

    # Step 3: Copy Visual Studio and Windows SDK
    if (-not $SkipVSCopy) {
        Write-Section "Step 3: Copying Visual Studio Build Tools"

        $toolchainName = "vc143"  # VS 2022 = vc143 (MSVC 14.3x)
        $vsCopyPath = Copy-VSInstallation -SourcePath $vsInfo.VSPath -DestinationPath $DistRoot -ToolchainName $toolchainName

        Write-Log "Visual Studio copied to: $vsCopyPath" -Level Success
    } else {
        Write-Section "Step 3: Skipping Visual Studio Copy"
        Write-Log "Visual Studio copy skipped (using installed version)" -Level Info
    }

    if ($vsInfo.SDKPath -and -not $SkipSDKCopy) {
        Write-SubSection "Copying Windows SDK"

        $sdkCopyPath = Copy-WindowsSDK -SourcePath $vsInfo.SDKPath -DestinationPath $DistRoot

        Write-Log "Windows SDK copied to: $sdkCopyPath" -Level Success
    } elseif ($SkipSDKCopy) {
        Write-Log "Windows SDK copy skipped" -Level Info
    }

    # Step 4: Download and install development tools
    Write-Section "Step 4: Installing Development Tools"

    # Determine cache directory
    if (-not $CacheDirectory) {
        # Use dist folder name for cache directory
        $distFolderName = Split-Path $DistRoot -Leaf
        $CacheDirectory = Join-Path $env:USERPROFILE "swblocks\$distFolderName-downloads-cache"
    }

    New-DirectoryIfNotExists -Path $CacheDirectory

    # Install Git for host architecture
    Install-GitPortable -Version $GitVersion -Architecture $HostArchitecture `
        -DestinationRoot $DistRoot -CacheDirectory $CacheDirectory `
        -SkipDownload:$SkipDownloads

    # Install Python for host architecture
    Install-PythonEmbeddable -Version $PythonVersion -Architecture $HostArchitecture `
        -DestinationRoot $DistRoot -CacheDirectory $CacheDirectory `
        -SkipDownload:$SkipDownloads

    # Install MSYS2 (host architecture, works for all targets)
    Install-MSYS2 -Version $MSYS2Version -Architecture $HostArchitecture `
        -DestinationRoot $DistRoot -CacheDirectory $CacheDirectory `
        -SkipDownload:$SkipDownloads -UpdatePackages:$UpdateMSYS2 -InstallMake

    # Install Strawberry Perl (portable version for host architecture)
    Install-StrawberryPerl -Version $PerlVersion -Architecture $HostArchitecture `
        -DestinationRoot $DistRoot -CacheDirectory $CacheDirectory `
        -SkipDownload:$SkipDownloads

    # Install JSON Spirit
    Install-JSONSpirit -Version $JSONSpiritVersion `
        -DestinationRoot $DistRoot -CacheDirectory $CacheDirectory `
        -SkipDownload:$SkipDownloads

    # Install OpenJDK for all target architectures (except x86 - not available from Microsoft)
    Write-SubSection "Installing OpenJDK for Target Architectures"
    Write-Log "Target architectures for OpenJDK installation: $($TargetArchitectures -join ', ')" -Level Info
    Write-Log "Target architectures array count: $($TargetArchitectures.Count)" -Level Verbose

    foreach ($arch in $TargetArchitectures) {
        if ($arch -ne "x86") {
            try {
                Write-Log "Starting OpenJDK $OpenJDKVersion installation for architecture: $arch" -Level Info

                $installPath = Install-OpenJDK -Version $OpenJDKVersion -Architecture $arch `
                    -DestinationRoot $DistRoot -CacheDirectory $CacheDirectory `
                    -SkipDownload:$SkipDownloads

                Write-Log "Successfully installed OpenJDK $OpenJDKVersion for $arch at: $installPath" -Level Success

                # Verify installation
                $jdkBinPath = Join-Path $installPath "bin\java.exe"
                if (-not (Test-Path $jdkBinPath)) {
                    Write-Log "WARNING: OpenJDK installed but java.exe not found at: $jdkBinPath" -Level Warning
                }
            }
            catch {
                Write-Log "FAILED to install OpenJDK $OpenJDKVersion for $arch" -Level Error
                Write-Log "Error details: $($_.Exception.Message)" -Level Error
                Write-Log "Stack trace: $($_.ScriptStackTrace)" -Level Error

                # Re-throw to fail the entire toolchain setup
                $errorMsg = "OpenJDK installation failed for architecture ${arch}: $($_.Exception.Message)"
                throw $errorMsg
            }
        } else {
            Write-Log "Skipping OpenJDK installation for $arch (not available from Microsoft)" -Level Info
        }
    }

    # Verify all OpenJDK installations
    Write-SubSection "Verifying OpenJDK Installations"
    $missingInstalls = @()
    foreach ($arch in $TargetArchitectures) {
        if ($arch -ne "x86") {
            $expectedPath = Join-Path $DistRoot "openjdk\$OpenJDKVersion\$arch"
            $javaExe = Join-Path $expectedPath "bin\java.exe"

            if (-not (Test-Path $expectedPath)) {
                $missingInstalls += "$arch (directory missing: $expectedPath)"
                Write-Log "ERROR: OpenJDK installation missing for $arch at: $expectedPath" -Level Error
            }
            elseif (-not (Test-Path $javaExe)) {
                $missingInstalls += "$arch (java.exe missing: $javaExe)"
                Write-Log "ERROR: OpenJDK installation incomplete for $arch - java.exe not found" -Level Error
            }
            else {
                Write-Log "OpenJDK for $arch verified at: $expectedPath" -Level Success
            }
        }
    }

    if ($missingInstalls.Count -gt 0) {
        $errorMsg = "OpenJDK installation verification failed for: $($missingInstalls -join ', ')"
        throw $errorMsg
    }

    Write-Log "All OpenJDK installations verified successfully" -Level Success

    # Install Gradle (architecture independent)
    Install-Gradle -Version $GradleVersion `
        -DestinationRoot $DistRoot -CacheDirectory $CacheDirectory `
        -SkipDownload:$SkipDownloads

    # Install Jom (architecture independent)
    Install-Jom -Version $JomVersion `
        -DestinationRoot $DistRoot -CacheDirectory $CacheDirectory `
        -SkipDownload:$SkipDownloads

    # Install 7-Zip (extra edition - architecture independent)
    $sevenZipVersion = "25.01"
    Install-SevenZip -Version $sevenZipVersion `
        -DestinationRoot $DistRoot -CacheDirectory $CacheDirectory `
        -SkipDownload:$SkipDownloads

    # Install NASM (assembler for x64 and x86 builds)
    Install-NASM -Version $NASMVersion -Architecture $HostArchitecture `
        -DestinationRoot $DistRoot -CacheDirectory $CacheDirectory `
        -SkipDownload:$SkipDownloads

    # Install Boost source
    Install-Boost -Version $BoostVersion `
        -DestinationRoot $DistRoot -CacheDirectory $CacheDirectory `
        -SkipDownload:$SkipDownloads

    # Install OpenSSL source
    Install-OpenSSL -Version $OpenSSLVersion `
        -DestinationRoot $DistRoot -CacheDirectory $CacheDirectory `
        -SkipDownload:$SkipDownloads

    # Step 5: Create environment initialization scripts
    Write-Section "Step 5: Creating Environment Initialization Scripts"

    New-EnvironmentInitScripts -DistRoot $DistRoot -HostArchitecture $HostArchitecture -Architectures $TargetArchitectures `
        -GitVersion $GitVersion -PythonVersion $PythonVersion -MSYS2Version $MSYS2Version -JomVersion $JomVersion -NASMVersion $NASMVersion

    # Step 6: Summary
    Write-Section "Toolchain Setup Complete!"

    Write-Host "The Windows devenv7 toolchain has been successfully set up." -ForegroundColor Green
    Write-Host ""
    Write-Host "Distribution root: $DistRoot" -ForegroundColor White
    Write-Host ""
    Write-Host "Next steps:" -ForegroundColor Cyan
    Write-Host "  1. Review the environment initialization script at:" -ForegroundColor White
    Write-Host "     $DistRoot\scripts\ci\ci-init-env.bat" -ForegroundColor Yellow
    Write-Host ""
    Write-Host "  2. Build Boost libraries using:" -ForegroundColor White
    Write-Host "     scripts\devenv7\windows\build-boost-windows.bat" -ForegroundColor Yellow
    Write-Host ""
    Write-Host "  3. Build OpenSSL libraries using:" -ForegroundColor White
    Write-Host "     scripts\devenv7\windows\build-openssl-windows.bat" -ForegroundColor Yellow
    Write-Host ""
    Write-Host "  Or run the full environment build:" -ForegroundColor White
    Write-Host "     scripts\devenv7\windows\build-env-all-windows.bat" -ForegroundColor Yellow
    Write-Host ""

    Write-Log "Setup completed successfully. Log file: $logPath" -Level Success
}

function New-EnvironmentInitScripts {
    param(
        [Parameter(Mandatory=$true)]
        [string]$DistRoot,

        [Parameter(Mandatory=$true)]
        [string]$HostArchitecture,

        [Parameter(Mandatory=$true)]
        [string[]]$Architectures,

        [Parameter(Mandatory=$false)]
        [string]$GitVersion = "",

        [Parameter(Mandatory=$false)]
        [string]$PythonVersion = "",

        [Parameter(Mandatory=$false)]
        [string]$MSYS2Version = "",

        [Parameter(Mandatory=$false)]
        [string]$JomVersion = "",

        [Parameter(Mandatory=$false)]
        [string]$NASMVersion = ""
    )

    Write-SubSection "Generating Environment Initialization Scripts"

    # Create ci-init-env.bat
    $batPath = Join-Path $DistRoot "scripts\ci\ci-init-env.bat"

    # Calculate relative path from %USERPROFILE% to $DistRoot
    $userProfile = $env:USERPROFILE
    if ($DistRoot.StartsWith($userProfile, [StringComparison]::OrdinalIgnoreCase)) {
        $relativePath = $DistRoot.Substring($userProfile.Length).TrimStart('\')
        $distRootVar = "%USERPROFILE%\$relativePath"
    } else {
        # Fallback to absolute path if not under user profile
        $distRootVar = $DistRoot
    }

    $batContent = @"
@echo off

REM Initialize the important environment roots

set "DIST_ROOT_DEPS1=$distRootVar"
set "DIST_ROOT_DEPS2=$distRootVar"
set "DIST_ROOT_DEPS3=$distRootVar"

REM Detect MSVC toolchain version (find the first vc* directory)
for /d %%D in ("%DIST_ROOT_DEPS1%\toolchain-msvc\vc*") do (
    set "TOOLCHAIN_NAME=%%~nxD"
    goto :found_toolchain
)
:found_toolchain

REM Detect MSVC compiler version (find the first version directory)
for /d %%D in ("%DIST_ROOT_DEPS1%\toolchain-msvc\%TOOLCHAIN_NAME%\BuildTools\VC\Tools\MSVC\*") do (
    set "MSVC_VERSION=%%~nxD"
    goto :found_msvc_version
)
:found_msvc_version

REM Detect Windows SDK version (find the first version directory)
for /d %%D in ("%DIST_ROOT_DEPS1%\winsdk\10\default\Include\*") do (
    set "WINSDK_VERSION=%%~nxD"
    goto :found_winsdk_version
)
:found_winsdk_version

REM Set MSVC, Windows SDK, and LLVM (Clang) root paths
set "MSVC_ROOT=%DIST_ROOT_DEPS1%\toolchain-msvc\%TOOLCHAIN_NAME%\BuildTools\VC\Tools\MSVC\%MSVC_VERSION%"
set "WINSDK_ROOT=%DIST_ROOT_DEPS1%\winsdk\10\default"
set "LLVM_ROOT=%DIST_ROOT_DEPS1%\toolchain-msvc\%TOOLCHAIN_NAME%\BuildTools\VC\Tools\Llvm"

REM For reference:
REM Path setup examples (uncomment and adjust as needed):

REM Using host architecture tools (installed based on -hostarch parameter):
REM set PATH=$distRootVar\msys2\$MSYS2Version\msys64\usr\bin;$distRootVar\git\$GitVersion\default\bin;%PATH%
REM OR with Python:
REM set PATH=$distRootVar\msys2\$MSYS2Version\msys64\usr\bin;$distRootVar\git\$GitVersion\default\bin;$distRootVar\python\$PythonVersion\default;%PATH%

REM Note: Tool paths use default (arm64), default-x64, or default-x86 based on host architecture
REM set PATH=$distRootVar\msys2\$MSYS2Version\msys64\usr\bin;$distRootVar\git\$GitVersion\default-x64\bin;%PATH%
REM OR with Python:
REM set PATH=$distRootVar\msys2\$MSYS2Version\msys64\usr\bin;$distRootVar\git\$GitVersion\default-x64\bin;$distRootVar\python\$PythonVersion\default-x64;%PATH%

REM for x86 (emulation):
REM set PATH=$distRootVar\msys2\$MSYS2Version\msys64\usr\bin;$distRootVar\git\$GitVersion\default-x86\bin;%PATH%
REM OR with Python:
REM set PATH=$distRootVar\msys2\$MSYS2Version\msys64\usr\bin;$distRootVar\git\$GitVersion\default-x86\bin;$distRootVar\python\$PythonVersion\default-x86;%PATH%

REM Optional: Windows debuggers
REM for arm64:
REM set PATH=$distRootVar\winsdk\10\default\Debuggers\arm64;%PATH%
REM for x64:
REM set PATH=$distRootVar\winsdk\10\default\Debuggers\x64;%PATH%
REM for x86:
REM set PATH=$distRootVar\winsdk\10\default\Debuggers\x86;%PATH%

REM ================================================================================
REM MSVC Compiler Environment Setup
REM ================================================================================
REM To use the MSVC compiler from the dist location, uncomment ONE of the sections
REM below based on your target architecture. This sets up PATH, INCLUDE, and LIB
REM environment variables without relying on vcvarsall.bat.

REM ================================================================================
REM ARM64 Native Development (Hostarm64\arm64)
REM ================================================================================
REM set "PATH=%MSVC_ROOT%\bin\Hostarm64\arm64;%WINSDK_ROOT%\bin\%WINSDK_VERSION%\arm64;%WINSDK_ROOT%\bin\%WINSDK_VERSION%\x64;%PATH%"
REM set "INCLUDE=%MSVC_ROOT%\include;%MSVC_ROOT%\atlmfc\include;%WINSDK_ROOT%\Include\%WINSDK_VERSION%\ucrt;%WINSDK_ROOT%\Include\%WINSDK_VERSION%\um;%WINSDK_ROOT%\Include\%WINSDK_VERSION%\shared;%WINSDK_ROOT%\Include\%WINSDK_VERSION%\winrt;%WINSDK_ROOT%\Include\%WINSDK_VERSION%\cppwinrt"
REM set "LIB=%MSVC_ROOT%\lib\arm64;%MSVC_ROOT%\atlmfc\lib\arm64;%WINSDK_ROOT%\Lib\%WINSDK_VERSION%\ucrt\arm64;%WINSDK_ROOT%\Lib\%WINSDK_VERSION%\um\arm64"
REM set "LIBPATH=%MSVC_ROOT%\lib\arm64;%MSVC_ROOT%\atlmfc\lib\arm64"

REM ================================================================================
REM x64 Development on ARM64 Host (Hostarm64\x64 - cross-compilation)
REM ================================================================================
REM set "PATH=%MSVC_ROOT%\bin\Hostarm64\x64;%WINSDK_ROOT%\bin\%WINSDK_VERSION%\x64;%PATH%"
REM set "INCLUDE=%MSVC_ROOT%\include;%MSVC_ROOT%\atlmfc\include;%WINSDK_ROOT%\Include\%WINSDK_VERSION%\ucrt;%WINSDK_ROOT%\Include\%WINSDK_VERSION%\um;%WINSDK_ROOT%\Include\%WINSDK_VERSION%\shared;%WINSDK_ROOT%\Include\%WINSDK_VERSION%\winrt;%WINSDK_ROOT%\Include\%WINSDK_VERSION%\cppwinrt"
REM set "LIB=%MSVC_ROOT%\lib\x64;%MSVC_ROOT%\atlmfc\lib\x64;%WINSDK_ROOT%\Lib\%WINSDK_VERSION%\ucrt\x64;%WINSDK_ROOT%\Lib\%WINSDK_VERSION%\um\x64"
REM set "LIBPATH=%MSVC_ROOT%\lib\x64;%MSVC_ROOT%\atlmfc\lib\x64"

REM ================================================================================
REM x64 Native Development (Hostx64\x64 - emulation or native x64 host)
REM ================================================================================
REM set "PATH=%MSVC_ROOT%\bin\Hostx64\x64;%WINSDK_ROOT%\bin\%WINSDK_VERSION%\x64;%PATH%"
REM set "INCLUDE=%MSVC_ROOT%\include;%MSVC_ROOT%\atlmfc\include;%WINSDK_ROOT%\Include\%WINSDK_VERSION%\ucrt;%WINSDK_ROOT%\Include\%WINSDK_VERSION%\um;%WINSDK_ROOT%\Include\%WINSDK_VERSION%\shared;%WINSDK_ROOT%\Include\%WINSDK_VERSION%\winrt;%WINSDK_ROOT%\Include\%WINSDK_VERSION%\cppwinrt"
REM set "LIB=%MSVC_ROOT%\lib\x64;%MSVC_ROOT%\atlmfc\lib\x64;%WINSDK_ROOT%\Lib\%WINSDK_VERSION%\ucrt\x64;%WINSDK_ROOT%\Lib\%WINSDK_VERSION%\um\x64"
REM set "LIBPATH=%MSVC_ROOT%\lib\x64;%MSVC_ROOT%\atlmfc\lib\x64"

REM ================================================================================
REM x86 Development on ARM64 Host (Hostarm64\x86 - cross-compilation)
REM ================================================================================
REM set "PATH=%MSVC_ROOT%\bin\Hostarm64\x86;%WINSDK_ROOT%\bin\%WINSDK_VERSION%\x86;%WINSDK_ROOT%\bin\%WINSDK_VERSION%\x64;%PATH%"
REM set "INCLUDE=%MSVC_ROOT%\include;%MSVC_ROOT%\atlmfc\include;%WINSDK_ROOT%\Include\%WINSDK_VERSION%\ucrt;%WINSDK_ROOT%\Include\%WINSDK_VERSION%\um;%WINSDK_ROOT%\Include\%WINSDK_VERSION%\shared;%WINSDK_ROOT%\Include\%WINSDK_VERSION%\winrt;%WINSDK_ROOT%\Include\%WINSDK_VERSION%\cppwinrt"
REM set "LIB=%MSVC_ROOT%\lib\x86;%MSVC_ROOT%\atlmfc\lib\x86;%WINSDK_ROOT%\Lib\%WINSDK_VERSION%\ucrt\x86;%WINSDK_ROOT%\Lib\%WINSDK_VERSION%\um\x86"
REM set "LIBPATH=%MSVC_ROOT%\lib\x86;%MSVC_ROOT%\atlmfc\lib\x86"

REM ================================================================================
REM x86 Native Development (Hostx86\x86 - emulation)
REM ================================================================================
REM set "PATH=%MSVC_ROOT%\bin\Hostx86\x86;%WINSDK_ROOT%\bin\%WINSDK_VERSION%\x86;%WINSDK_ROOT%\bin\%WINSDK_VERSION%\x64;%PATH%"
REM set "INCLUDE=%MSVC_ROOT%\include;%MSVC_ROOT%\atlmfc\include;%WINSDK_ROOT%\Include\%WINSDK_VERSION%\ucrt;%WINSDK_ROOT%\Include\%WINSDK_VERSION%\um;%WINSDK_ROOT%\Include\%WINSDK_VERSION%\shared;%WINSDK_ROOT%\Include\%WINSDK_VERSION%\winrt;%WINSDK_ROOT%\Include\%WINSDK_VERSION%\cppwinrt"
REM set "LIB=%MSVC_ROOT%\lib\x86;%MSVC_ROOT%\atlmfc\lib\x86;%WINSDK_ROOT%\Lib\%WINSDK_VERSION%\ucrt\x86;%WINSDK_ROOT%\Lib\%WINSDK_VERSION%\um\x86"
REM set "LIBPATH=%MSVC_ROOT%\lib\x86;%MSVC_ROOT%\atlmfc\lib\x86"
"@

    New-DirectoryIfNotExists -Path (Split-Path $batPath -Parent) | Out-Null
    Set-Content -Path $batPath -Value $batContent -Encoding ASCII
    Write-Log "Created: $batPath" -Level Success

    # Create ci-init-env.mk for make
    $mkPath = Join-Path $DistRoot "projects\make\ci-init-env.mk"
    New-DirectoryIfNotExists -Path (Split-Path $mkPath -Parent) | Out-Null

    # Use $(USERNAME) environment variable for portable paths
    # Extract dist directory name (e.g., "dist-devenv7-windows-a64") from full path
    $distDirName = Split-Path $DistRoot -Leaf

    $mkContent = @"
# Initialize the important env roots
# Note: Paths use Unix-style format for MSYS2 make compatibility

DIST_ROOT_DEPS1 = /c/Users/`$(USERNAME)/swblocks/$distDirName
DIST_ROOT_DEPS2 = /c/Users/`$(USERNAME)/swblocks/$distDirName
DIST_ROOT_DEPS3 = /c/Users/`$(USERNAME)/swblocks/$distDirName
"@

    Set-Content -Path $mkPath -Value $mkContent -Encoding ASCII
    Write-Log "Created: $mkPath" -Level Success

    # Create architecture-specific helper scripts for all architectures
    # Always generate all three scripts regardless of target architectures
    $allArchitectures = @("a64", "x64", "x86")
    foreach ($arch in $allArchitectures) {
        # Git and Python use host architecture (not target)
        # Both install to 'default' folder for all host architectures
        $helperPath = Join-Path $DistRoot "scripts\ci\setup-env-$arch.bat"

        $gitPath = "$distRootVar\git\$GitVersion\default\bin"
        $pythonPath = "$distRootVar\python\$PythonVersion\default"
        # Use msys32 for x86 hosts, msys64 for x64/ARM64 hosts
        $msysFolderName = if ($HostArchitecture -eq "x86") { "msys32" } else { "msys64" }
        $msysPath = "$distRootVar\msys2\$MSYS2Version\$msysFolderName\usr\bin"
        $jomPath = "$distRootVar\jom\$JomVersion\default"
        $nasmPath = "$distRootVar\nasm\$NASMVersion\default"

        # Determine architecture descriptions
        $hostDesc = if ($HostArchitecture -eq "a64") { "ARM64" } elseif ($HostArchitecture -eq "x64") { "x64" } else { "x86" }
        $targetDesc = if ($arch -eq $HostArchitecture) { "Native" } else { "Cross-Compilation" }
        $archDesc = "$($arch.ToUpper()) $targetDesc"
        $targetArch = $arch.ToUpper()
        $hostArch = $hostDesc.ToUpper()

        # Determine the Host{arch} path component for MSVC
        $hostPathComponent = if ($HostArchitecture -eq "a64") { "Hostarm64" } elseif ($HostArchitecture -eq "x64") { "Hostx64" } else { "Hostx86" }

        # Set up MSVC paths based on host and target architecture
        # NASM is only needed for x64 and x86 builds (ARM64 uses armasm64)
        # Clang-CL is available for all architectures

        # Convert host architecture to VS naming for debugger path (a64 -> arm64)
        $hostArchForDebugger = ConvertTo-VSArchitectureName $HostArchitecture

        # Determine Clang-CL path based on HOST architecture with compatible fallbacks
        # Clang-CL tools run on host, so must match host architecture
        # a64 hosts can execute: arm64, x64 (via emulation), x86 (via emulation)
        # x64 hosts can execute: x64, x86 (via WoW64)
        # x86 hosts can execute: x86 only
        if ($HostArchitecture -eq "a64") {
            $clangPath = "%LLVM_ROOT%\ARM64\bin;%LLVM_ROOT%\x64\bin;%LLVM_ROOT%\bin"
        } elseif ($HostArchitecture -eq "x64") {
            $clangPath = "%LLVM_ROOT%\x64\bin;%LLVM_ROOT%\bin"
        } else {
            $clangPath = "%LLVM_ROOT%\bin"
        }

        # Determine Windows SDK bin path based on HOST architecture with compatible fallbacks
        # Windows SDK tools run on host, so must match host architecture
        # a64 hosts can execute: arm64, x64 (via emulation), x86 (via emulation)
        # x64 hosts can execute: x64, x86 (via WoW64)
        # x86 hosts can execute: x86 only
        if ($HostArchitecture -eq "a64") {
            $sdkBinPaths = "%WINSDK_ROOT%\bin\%WINSDK_VERSION%\arm64;%WINSDK_ROOT%\bin\%WINSDK_VERSION%\x64;%WINSDK_ROOT%\bin\%WINSDK_VERSION%\x86"
        } elseif ($HostArchitecture -eq "x64") {
            $sdkBinPaths = "%WINSDK_ROOT%\bin\%WINSDK_VERSION%\x64;%WINSDK_ROOT%\bin\%WINSDK_VERSION%\x86"
        } else {
            $sdkBinPaths = "%WINSDK_ROOT%\bin\%WINSDK_VERSION%\x86"
        }

        if ($arch -eq "a64") {
            $msvcBinPath = "%MSVC_ROOT%\bin\$hostPathComponent\arm64;$sdkBinPaths"
            $msvcLib = "%MSVC_ROOT%\lib\arm64;%MSVC_ROOT%\atlmfc\lib\arm64;%WINSDK_ROOT%\Lib\%WINSDK_VERSION%\ucrt\arm64;%WINSDK_ROOT%\Lib\%WINSDK_VERSION%\um\arm64"
            $msvcLibPath = "%MSVC_ROOT%\lib\arm64;%MSVC_ROOT%\atlmfc\lib\arm64"
            $pathWithTools = "$msvcBinPath;$clangPath;%WINSDK_ROOT%\Debuggers\$hostArchForDebugger;$pythonPath;%PATH%;$jomPath;$gitPath;$msysPath"
        } elseif ($arch -eq "x64") {
            $msvcBinPath = "%MSVC_ROOT%\bin\$hostPathComponent\x64;$sdkBinPaths"
            $msvcLib = "%MSVC_ROOT%\lib\x64;%MSVC_ROOT%\atlmfc\lib\x64;%WINSDK_ROOT%\Lib\%WINSDK_VERSION%\ucrt\x64;%WINSDK_ROOT%\Lib\%WINSDK_VERSION%\um\x64"
            $msvcLibPath = "%MSVC_ROOT%\lib\x64;%MSVC_ROOT%\atlmfc\lib\x64"
            $pathWithTools = "$msvcBinPath;$clangPath;%WINSDK_ROOT%\Debuggers\$hostArchForDebugger;$pythonPath;%PATH%;$jomPath;$nasmPath;$gitPath;$msysPath"
        } else {
            $msvcBinPath = "%MSVC_ROOT%\bin\$hostPathComponent\x86;$sdkBinPaths"
            $msvcLib = "%MSVC_ROOT%\lib\x86;%MSVC_ROOT%\atlmfc\lib\x86;%WINSDK_ROOT%\Lib\%WINSDK_VERSION%\ucrt\x86;%WINSDK_ROOT%\Lib\%WINSDK_VERSION%\um\x86"
            $msvcLibPath = "%MSVC_ROOT%\lib\x86;%MSVC_ROOT%\atlmfc\lib\x86"
            $pathWithTools = "$msvcBinPath;$clangPath;%WINSDK_ROOT%\Debuggers\$hostArchForDebugger;$pythonPath;%PATH%;$jomPath;$nasmPath;$gitPath;$msysPath"
        }

        $helperContent = @"
@echo off
REM ================================================================================
REM Setup Development Environment for $archDesc
REM ================================================================================
REM This script sets up the complete development environment for $arch
REM compilation using the devenv7 toolchain from the dist location.
REM
REM Usage:
REM   call setup-env-$arch.bat
REM
REM After running this script, you can use cl.exe, link.exe, and other MSVC tools
REM directly without needing to run vcvarsall.bat
REM ================================================================================

REM Initialize base environment
call "%~dp0ci-init-env.bat"

REM Set up MSVC compiler environment for $arch development
REM IMPORTANT: PATH order matters! MSVC/Windows SDK tools must come before MSYS2 to avoid conflicts
REM (MSYS2 contains 'link' and 'find' commands that conflict with Windows native tools)
REM Python comes before existing PATH to override Windows fake python stubs
REM Order: MSVC -> Clang-CL -> Windows SDK -> Python -> existing PATH -> Jom -> NASM (x64/x86 only) -> Git -> MSYS2 (lowest priority)
set "PATH=$pathWithTools"
set "INCLUDE=%MSVC_ROOT%\include;%MSVC_ROOT%\atlmfc\include;%WINSDK_ROOT%\Include\%WINSDK_VERSION%\ucrt;%WINSDK_ROOT%\Include\%WINSDK_VERSION%\um;%WINSDK_ROOT%\Include\%WINSDK_VERSION%\shared;%WINSDK_ROOT%\Include\%WINSDK_VERSION%\winrt;%WINSDK_ROOT%\Include\%WINSDK_VERSION%\cppwinrt"
set "LIB=$msvcLib"
set "LIBPATH=$msvcLibPath"

REM Set platform and architecture variables
set "Platform=$targetArch"
set "VSCMD_ARG_TGT_ARCH=$($arch.ToLower())"
set "VSCMD_ARG_HOST_ARCH=$($HostArchitecture.ToLower())"

REM Configure debugger symbol path
set "_NT_SYMBOL_PATH=srv*%USERPROFILE%\windbg\sym*https://msdl.microsoft.com/download/symbols"

echo.
echo ================================================================================
echo $archDesc Development Environment Configured
echo ================================================================================
echo Toolchain:     %TOOLCHAIN_NAME%
echo MSVC Version:  %MSVC_VERSION%
echo SDK Version:   %WINSDK_VERSION%
echo Target Arch:   $targetArch
echo Host Arch:     $hostArch
echo ================================================================================
echo.
echo Compiler available: cl.exe
where cl 2>nul
echo.
"@

        Set-Content -Path $helperPath -Value $helperContent -Encoding ASCII
        Write-Log "Created: $helperPath" -Level Verbose
    }

    # Create setup-env-nomsvc-{arch}.bat scripts (no MSVC compiler paths)
    # These scripts include debuggers and build tools but exclude MSVC/SDK/LLVM compiler paths
    foreach ($arch in $allArchitectures) {
        $helperPath = Join-Path $DistRoot "scripts\ci\setup-env-nomsvc-$arch.bat"

        # Reuse path variables from above
        $gitPath = "$distRootVar\git\$GitVersion\default\bin"
        $pythonPath = "$distRootVar\python\$PythonVersion\default"
        $msysFolderName = if ($HostArchitecture -eq "x86") { "msys32" } else { "msys64" }
        $msysPath = "$distRootVar\msys2\$MSYS2Version\$msysFolderName\usr\bin"
        $jomPath = "$distRootVar\jom\$JomVersion\default"
        $nasmPath = "$distRootVar\nasm\$NASMVersion\default"

        # Architecture descriptions
        $hostDesc = if ($HostArchitecture -eq "a64") { "ARM64" } elseif ($HostArchitecture -eq "x64") { "x64" } else { "x86" }
        $targetDesc = if ($arch -eq $HostArchitecture) { "Native" } else { "Cross-Compilation" }
        $archDesc = "$($arch.ToUpper()) $targetDesc"
        $targetArch = $arch.ToUpper()
        $hostArch = $hostDesc.ToUpper()

        # Convert host architecture to VS naming for debugger path
        $hostArchForDebugger = ConvertTo-VSArchitectureName $HostArchitecture

        # Construct PATH without MSVC/SDK/LLVM compiler paths
        # ARM64: No NASM (uses clang-cl as assembler, not included in this variant)
        # x64/x86: Include NASM
        if ($arch -eq "a64") {
            $pathWithTools = "%WINSDK_ROOT%\Debuggers\$hostArchForDebugger;$pythonPath;%PATH%;$jomPath;$gitPath;$msysPath"
        } else {
            $pathWithTools = "%WINSDK_ROOT%\Debuggers\$hostArchForDebugger;$pythonPath;%PATH%;$jomPath;$nasmPath;$gitPath;$msysPath"
        }

        $helperContent = @"
@echo off
REM ================================================================================
REM Setup Development Environment for $archDesc (No MSVC)
REM ================================================================================
REM This script sets up a development environment WITHOUT MSVC compiler tools.
REM
REM Includes:
REM   - Windows Debuggers
REM   - Jom (parallel build tool)
REM   - NASM (assembler, x64/x86 only)
REM   - Git (version control)
REM   - Python (scripting)
REM   - MSYS2 (Unix utilities)
REM
REM Excludes:
REM   - MSVC compiler (cl.exe, link.exe, lib.exe)
REM   - Windows SDK bins
REM   - Clang-CL
REM   - INCLUDE/LIB/LIBPATH environment variables
REM
REM Useful for: Debugging, non-C/C++ builds, scripting tasks
REM ================================================================================

REM Initialize base environment
call "%~dp0ci-init-env.bat"

REM Set up PATH with debuggers and build tools (no compiler)
REM Order: Debuggers -> existing PATH -> Jom -> NASM (x64/x86 only) -> Git -> Python -> MSYS2 (lowest priority)
set "PATH=$pathWithTools"

REM Set platform and architecture variables
set "Platform=$targetArch"
set "VSCMD_ARG_TGT_ARCH=$($arch.ToLower())"
set "VSCMD_ARG_HOST_ARCH=$($HostArchitecture.ToLower())"

REM Configure debugger symbol path
set "_NT_SYMBOL_PATH=srv*%USERPROFILE%\windbg\sym*https://msdl.microsoft.com/download/symbols"

echo.
echo ================================================================================
echo $archDesc Development Environment Configured (No MSVC)
echo ================================================================================
echo Toolchain:     %TOOLCHAIN_NAME%
echo MSVC Version:  %MSVC_VERSION%
echo SDK Version:   %WINSDK_VERSION%
echo Target Arch:   $targetArch
echo Host Arch:     $hostArch
echo Configuration: No MSVC compiler paths
echo ================================================================================
echo.
echo Debugger available: cdb.exe
where cdb 2>nul
echo.
"@

        Set-Content -Path $helperPath -Value $helperContent -Encoding ASCII
        Write-Log "Created: $helperPath" -Level Verbose
    }

    # Create setup-env-minimal-{arch}.bat scripts (minimal PATH with Git + MSYS2 only)
    # These scripts provide only version control and Unix utilities
    foreach ($arch in $allArchitectures) {
        $helperPath = Join-Path $DistRoot "scripts\ci\setup-env-minimal-$arch.bat"

        # Reuse path variables
        $gitPath = "$distRootVar\git\$GitVersion\default\bin"
        $msysFolderName = if ($HostArchitecture -eq "x86") { "msys32" } else { "msys64" }
        $msysPath = "$distRootVar\msys2\$MSYS2Version\$msysFolderName\usr\bin"

        # Architecture descriptions
        $hostDesc = if ($HostArchitecture -eq "a64") { "ARM64" } elseif ($HostArchitecture -eq "x64") { "x64" } else { "x86" }
        $targetDesc = if ($arch -eq $HostArchitecture) { "Native" } else { "Cross-Compilation" }
        $archDesc = "$($arch.ToUpper()) $targetDesc"
        $targetArch = $arch.ToUpper()
        $hostArch = $hostDesc.ToUpper()

        # Minimal PATH: only Git and MSYS2
        $pathWithTools = "$gitPath;$msysPath;%PATH%"

        $helperContent = @"
@echo off
REM ================================================================================
REM Minimal Development Environment for $archDesc
REM ================================================================================
REM This script sets up a minimal development environment.
REM
REM Includes:
REM   - Git (version control)
REM   - MSYS2 (Unix utilities: bash, make, grep, sed, awk, etc.)
REM
REM Excludes:
REM   - All compiler tools (MSVC, Clang-CL)
REM   - Debuggers
REM   - Build utilities (Jom, NASM)
REM   - Python
REM   - INCLUDE/LIB/LIBPATH environment variables
REM
REM Useful for: Version control operations, shell scripting, text processing
REM ================================================================================

REM Initialize base environment
call "%~dp0ci-init-env.bat"

REM Set up minimal PATH with Git and MSYS2 only
REM Order: Git -> MSYS2 -> existing PATH
set "PATH=$pathWithTools"

REM Set platform and architecture variables
set "Platform=$targetArch"
set "VSCMD_ARG_TGT_ARCH=$($arch.ToLower())"
set "VSCMD_ARG_HOST_ARCH=$($HostArchitecture.ToLower())"

REM Configure debugger symbol path (for reference)
set "_NT_SYMBOL_PATH=srv*%USERPROFILE%\windbg\sym*https://msdl.microsoft.com/download/symbols"

echo.
echo ================================================================================
echo $archDesc Minimal Development Environment Configured
echo ================================================================================
echo Toolchain:     %TOOLCHAIN_NAME%
echo Target Arch:   $targetArch
echo Host Arch:     $hostArch
echo Configuration: Minimal (Git + MSYS2 only)
echo ================================================================================
echo.
echo Git available: git.exe
where git 2>nul
echo.
"@

        Set-Content -Path $helperPath -Value $helperContent -Encoding ASCII
        Write-Log "Created: $helperPath" -Level Verbose
    }

    Write-Log "Environment initialization scripts created" -Level Success
}

# Export functions
# Export-ModuleMember -Function @(
#     'New-ToolchainEnvironment',
#     'New-EnvironmentInitScripts'
# )
