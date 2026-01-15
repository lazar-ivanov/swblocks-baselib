@echo off
REM ================================================================================
REM This file is part of the swblocks-baselib library.
REM
REM Licensed under the Apache License, Version 2.0 (the "License");
REM you may not use this file except in compliance with the License.
REM You may obtain a copy of the License at
REM
REM     http://www.apache.org/licenses/LICENSE-2.0
REM
REM Unless required by applicable law or agreed to in writing, software
REM distributed under the License is distributed on an "AS IS" BASIS,
REM WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
REM See the License for the specific language governing permissions and
REM limitations under the License.
REM ================================================================================
REM
REM Windows devenv7 MSVC Toolchain Setup Script
REM ================================================================================
REM
REM This script automates the setup of the Windows development toolchain including:
REM   - Visual Studio Build Tools detection and copying
REM   - Windows SDK detection and copying
REM   - Git installation (portable) for ARM64, x64, x86
REM   - Python installation (embeddable) for ARM64, x64, x86
REM   - MSYS2 installation (for make)
REM   - Strawberry Perl installation (for OpenSSL builds)
REM   - JSON Spirit library installation
REM   - OpenJDK installation (for ARM64, x64)
REM   - Gradle installation
REM   - Boost source installation
REM   - OpenSSL source installation
REM
REM Usage:
REM   build-msvc-toolchain.bat [options]
REM
REM Options:
REM   -dist-root <path>        Distribution root directory
REM                            Default: %USERPROFILE%\swblocks\dist-devenv7-windows-a64
REM
REM   -hostarch <arch>         Host architecture for tools and cross-compilation
REM                            Options: a64, x64, x86 (arm64 accepted as alias for a64)
REM                            Default: auto-detect from environment
REM
REM   -targets <arch-list>     Comma-separated list of target architectures
REM                            Options: a64, x64, x86 (arm64 accepted as alias for a64)
REM                            Default: matches -hostarch value
REM
REM   -git-version <version>   Git version to download
REM                            Default: 2.52.0
REM
REM   -python-version <ver>    Python version to download
REM                            Default: 3.14.2
REM
REM   -msys2-version <ver>     MSYS2 version to download
REM                            Default: 20251213
REM
REM   -perl-version <ver>      Strawberry Perl version to download
REM                            Default: 5.40.0.1
REM
REM   -json-version <ver>      JSON Spirit version to download
REM                            Default: 4.08
REM
REM   -openjdk-version <ver>   OpenJDK version to download
REM                            Default: 25
REM
REM   -gradle-version <ver>    Gradle version to download
REM                            Default: 9.2.1
REM
REM   -boost-version <ver>     Boost version to download
REM                            Default: 1.90.0
REM
REM   -openssl-version <ver>   OpenSSL version to download
REM                            Default: 3.5.4
REM
REM   -vs-version <ver>        Visual Studio version to detect
REM                            Default: 2022
REM
REM   -skip-vs-copy            Skip copying Visual Studio installation
REM
REM   -skip-sdk-copy           Skip copying Windows SDK installation
REM
REM   -skip-downloads          Skip downloading tools (use cached copies)
REM
REM   -update-msys2            Update MSYS2 packages after installation
REM
REM   -cache-dir <path>        Directory for download cache
REM                            Default: %TEMP%\devenv7-downloads
REM
REM   -whatif                  Show what would be done without making changes
REM
REM   -verbose                 Enable verbose logging
REM
REM   -help                    Show this help message
REM
REM Examples:
REM   build-msvc-toolchain.bat
REM   build-msvc-toolchain.bat -hostarch x64 -targets arm64,x64
REM   build-msvc-toolchain.bat -dist-root C:\mydev\toolchain -whatif
REM   build-msvc-toolchain.bat -skip-vs-copy -skip-sdk-copy
REM
REM ================================================================================

REM Capture script directory BEFORE setlocal/shift (shift can corrupt %~dp0)
set "SCRIPT_DIR=%~dp0"

setlocal enabledelayedexpansion

REM Default parameters
set "DIST_ROOT="
set "DIST_ROOT_PROVIDED="
set "HOST_ARCH="
set "TARGET_ARCHS="
set "GIT_VERSION=2.52.0"
set "PYTHON_VERSION=3.14.2"
set "MSYS2_VERSION=20251213"
set "PERL_VERSION=5.40.0.1"
set "JSON_VERSION=4.08"
set "OPENJDK_VERSION=25"
set "GRADLE_VERSION=9.2.1"
set "BOOST_VERSION=1.90.0"
set "OPENSSL_VERSION=3.5.4"
set "VS_VERSION=2022"
set "CACHE_DIR="
set "SKIP_VS_COPY="
set "SKIP_SDK_COPY="
set "SKIP_DOWNLOADS="
set "UPDATE_MSYS2="
set "WHATIF="
set "VERBOSE="
set "HELP_EXIT_CODE="

REM Parse command line arguments
:parse_args
if "%~1"=="" goto args_done
if /i "%~1"=="-dist-root" (
    set "DIST_ROOT=%~2"
    set "DIST_ROOT_PROVIDED=1"
    shift
    shift
    goto parse_args
)
if /i "%~1"=="-hostarch" (
    set "HOST_ARCH=%~2"
    shift
    shift
    goto parse_args
)
if /i "%~1"=="-targets" (
    set "TARGET_ARCHS="
    shift
    goto collect_targets_loop
)
goto skip_collect_targets
:collect_targets_loop
if "%~1"=="" goto args_done
set "FIRST_CHAR=%~1"
set "FIRST_CHAR=!FIRST_CHAR:~0,1!"
if "!FIRST_CHAR!"=="-" goto parse_args
if "!TARGET_ARCHS!"=="" (
    set "TARGET_ARCHS=%~1"
) else (
    set "TARGET_ARCHS=!TARGET_ARCHS!,%~1"
)
shift
goto collect_targets_loop
:skip_collect_targets
if /i "%~1"=="-git-version" (
    set "GIT_VERSION=%~2"
    shift
    shift
    goto parse_args
)
if /i "%~1"=="-python-version" (
    set "PYTHON_VERSION=%~2"
    shift
    shift
    goto parse_args
)
if /i "%~1"=="-msys2-version" (
    set "MSYS2_VERSION=%~2"
    shift
    shift
    goto parse_args
)
if /i "%~1"=="-perl-version" (
    set "PERL_VERSION=%~2"
    shift
    shift
    goto parse_args
)
if /i "%~1"=="-json-version" (
    set "JSON_VERSION=%~2"
    shift
    shift
    goto parse_args
)
if /i "%~1"=="-openjdk-version" (
    set "OPENJDK_VERSION=%~2"
    shift
    shift
    goto parse_args
)
if /i "%~1"=="-gradle-version" (
    set "GRADLE_VERSION=%~2"
    shift
    shift
    goto parse_args
)
if /i "%~1"=="-boost-version" (
    set "BOOST_VERSION=%~2"
    shift
    shift
    goto parse_args
)
if /i "%~1"=="-openssl-version" (
    set "OPENSSL_VERSION=%~2"
    shift
    shift
    goto parse_args
)
if /i "%~1"=="-vs-version" (
    set "VS_VERSION=%~2"
    shift
    shift
    goto parse_args
)
if /i "%~1"=="-cache-dir" (
    set "CACHE_DIR=%~2"
    shift
    shift
    goto parse_args
)
if /i "%~1"=="-skip-vs-copy" (
    set "SKIP_VS_COPY=1"
    shift
    goto parse_args
)
if /i "%~1"=="-skip-sdk-copy" (
    set "SKIP_SDK_COPY=1"
    shift
    goto parse_args
)
if /i "%~1"=="-skip-downloads" (
    set "SKIP_DOWNLOADS=1"
    shift
    goto parse_args
)
if /i "%~1"=="-update-msys2" (
    set "UPDATE_MSYS2=1"
    shift
    goto parse_args
)
if /i "%~1"=="-whatif" (
    set "WHATIF=1"
    shift
    goto parse_args
)
if /i "%~1"=="-verbose" (
    set "VERBOSE=1"
    shift
    goto parse_args
)
if /i "%~1"=="-help" (
    goto show_help
)
echo Unknown option: %~1
set "HELP_EXIT_CODE=1"
goto show_help

:args_done

REM Auto-detect host architecture if not specified
if "%HOST_ARCH%"=="" (
    echo Detecting host architecture...
    if /i "%PROCESSOR_ARCHITECTURE%"=="AMD64" (
        set "HOST_ARCH=x64"
    ) else if /i "%PROCESSOR_ARCHITECTURE%"=="ARM64" (
        set "HOST_ARCH=a64"
    ) else if /i "%PROCESSOR_ARCHITECTURE%"=="x86" (
        set "HOST_ARCH=x86"
    ) else (
        echo WARNING: Unknown processor architecture "%PROCESSOR_ARCHITECTURE%", defaulting to x64
        set "HOST_ARCH=x64"
    )
    echo Detected host architecture: !HOST_ARCH!
)

REM Normalize host architecture value (backward compatibility)
if /i "%HOST_ARCH%"=="arm64" set "HOST_ARCH=a64"
if /i "%HOST_ARCH%"=="amd64" set "HOST_ARCH=x64"
if /i "%HOST_ARCH%"=="x86_64" set "HOST_ARCH=x64"
if /i "%HOST_ARCH%"=="aarch64" set "HOST_ARCH=a64"

REM Validate host architecture
if /i not "%HOST_ARCH%"=="a64" if /i not "%HOST_ARCH%"=="x64" if /i not "%HOST_ARCH%"=="x86" (
    echo ERROR: Invalid host architecture "%HOST_ARCH%"
    echo Valid options: a64, x64, x86 ^(arm64 accepted as alias for a64^)
    exit /b 1
)

REM Set default target architectures to host architecture if not specified
if "%TARGET_ARCHS%"=="" (
    set "TARGET_ARCHS=!HOST_ARCH!"
    echo Using default target architecture: !TARGET_ARCHS!
)

REM Normalize and validate target architectures
set "NORMALIZED_TARGETS="
set "TEMP_ARCHS=!TARGET_ARCHS!"
:parse_arch_loop
for /f "tokens=1* delims=," %%a in ("!TEMP_ARCHS!") do (
    set "CURR_ARCH=%%a"
    set "TEMP_ARCHS=%%b"

    REM Normalize architecture (backward compatibility)
    if /i "!CURR_ARCH!"=="arm64" set "CURR_ARCH=a64"
    if /i "!CURR_ARCH!"=="amd64" set "CURR_ARCH=x64"
    if /i "!CURR_ARCH!"=="x86_64" set "CURR_ARCH=x64"
    if /i "!CURR_ARCH!"=="aarch64" set "CURR_ARCH=a64"

    REM Validate architecture
    if /i not "!CURR_ARCH!"=="a64" if /i not "!CURR_ARCH!"=="x64" if /i not "!CURR_ARCH!"=="x86" (
        echo ERROR: Invalid target architecture "!CURR_ARCH!"
        echo Valid options: a64, x64, x86 ^(arm64 accepted as alias for a64^)
        exit /b 1
    )

    REM Append to normalized list
    if "!NORMALIZED_TARGETS!"=="" (
        set "NORMALIZED_TARGETS=!CURR_ARCH!"
    ) else (
        set "NORMALIZED_TARGETS=!NORMALIZED_TARGETS!-!CURR_ARCH!"
    )

    if not "!TEMP_ARCHS!"=="" goto parse_arch_loop
)

REM Update TARGET_ARCHS with normalized values (comma-separated for passing to scripts)
set "TARGET_ARCHS=!NORMALIZED_TARGETS:-=,!"

REM Construct dist folder name with hostarch and targets (only if not provided via -dist-root)
if not "%DIST_ROOT_PROVIDED%"=="1" (
    set "DIST_ROOT=%USERPROFILE%\swblocks\dist-devenv7-windows-hostarch-!HOST_ARCH!-targets-!NORMALIZED_TARGETS!"
)

REM Verify script directory was captured correctly
REM If the internal directory doesn't exist, we have a problem
REM This can happen if the script is called in unusual ways
if not exist "%SCRIPT_DIR%internal\toolchain-setup.ps1" (
    echo Error: Cannot locate PowerShell modules
    echo Expected location: %SCRIPT_DIR%internal\toolchain-setup.ps1
    echo.
    echo This script must be run from its own directory or via full path.
    echo Please run from command prompt:
    echo   cd %~dp0
    echo   build-msvc-toolchain.bat -targets arm64
    echo.
    echo Or use full path:
    echo   %~dp0build-msvc-toolchain.bat -targets arm64
    exit /b 1
)

set "PS_SCRIPT=%SCRIPT_DIR%internal\toolchain-setup.ps1"

REM Build PowerShell command - use dot-sourcing for better compatibility
set "PS_CMD=. '%PS_SCRIPT%'; New-ToolchainEnvironment"
set "PS_CMD=%PS_CMD% -DistRoot '%DIST_ROOT%'"
set "PS_CMD=%PS_CMD% -HostArchitecture '%HOST_ARCH%'"
set "PS_CMD=%PS_CMD% -TargetArchitectures ('%TARGET_ARCHS%' -split ',')"
set "PS_CMD=%PS_CMD% -GitVersion '%GIT_VERSION%'"
set "PS_CMD=%PS_CMD% -PythonVersion '%PYTHON_VERSION%'"
set "PS_CMD=%PS_CMD% -MSYS2Version '%MSYS2_VERSION%'"
set "PS_CMD=%PS_CMD% -PerlVersion '%PERL_VERSION%'"
set "PS_CMD=%PS_CMD% -JSONSpiritVersion '%JSON_VERSION%'"
set "PS_CMD=%PS_CMD% -OpenJDKVersion '%OPENJDK_VERSION%'"
set "PS_CMD=%PS_CMD% -GradleVersion '%GRADLE_VERSION%'"
set "PS_CMD=%PS_CMD% -BoostVersion '%BOOST_VERSION%'"
set "PS_CMD=%PS_CMD% -OpenSSLVersion '%OPENSSL_VERSION%'"
set "PS_CMD=%PS_CMD% -VSVersion '%VS_VERSION%'"

if defined CACHE_DIR (
    set "PS_CMD=%PS_CMD% -CacheDirectory '%CACHE_DIR%'"
)

if defined SKIP_VS_COPY (
    set "PS_CMD=%PS_CMD% -SkipVSCopy"
)

if defined SKIP_SDK_COPY (
    set "PS_CMD=%PS_CMD% -SkipSDKCopy"
)

if defined SKIP_DOWNLOADS (
    set "PS_CMD=%PS_CMD% -SkipDownloads"
)

if defined UPDATE_MSYS2 (
    set "PS_CMD=%PS_CMD% -UpdateMSYS2"
)

if defined WHATIF (
    set "PS_CMD=%PS_CMD% -WhatIf"
)

if defined VERBOSE (
    set "PS_CMD=%PS_CMD% -Verbose"
)

REM Execute PowerShell script
echo.
echo Executing toolchain setup...
echo.

powershell.exe -ExecutionPolicy Bypass -NoProfile -Command "& { %PS_CMD% }"

if errorlevel 1 (
    echo.
    echo Toolchain setup failed!
    exit /b 1
)

echo.
echo Toolchain setup completed successfully.
exit /b 0

:show_help
type "%~f0" | findstr /B "REM"
if defined HELP_EXIT_CODE exit /b 1
exit /b 0
