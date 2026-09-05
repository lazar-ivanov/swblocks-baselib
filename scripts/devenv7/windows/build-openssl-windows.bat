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
REM OpenSSL Build Script for Windows devenv7
REM ================================================================================
REM
REM This script builds OpenSSL libraries for Windows with MSVC toolchain
REM Both debug and release variants are built automatically
REM
REM Usage:
REM   build-openssl-windows.bat -hostarch <host-arch> [options]
REM
REM Options:
REM   -hostarch <architecture> Host architecture where build runs (REQUIRED)
REM                            Must be: a64, x64, or x86
REM                            Tests are skipped for x64/a64 targets when host is x86
REM
REM   -arch <architecture>     Target architecture: a64, x64, or x86
REM                            Note: arm64 accepted as alias for a64
REM                            Default: a64
REM
REM   -version <version>       OpenSSL version to build
REM                            Default: 3.5.4
REM
REM   -toolchain <name>        Toolchain name (vc143, etc.)
REM                            Default: vc143
REM
REM   -vs-version <ver>        Visual Studio version (2022, etc.)
REM                            Default: 2022
REM
REM   -dist-root <path>        Distribution root directory
REM                            Default: %USERPROFILE%\swblocks\dist-devenv7-windows-a64
REM
REM   -devenv-tag <tag>        devenv tag
REM                            Default: devenv7
REM
REM   -skip-tests              Skip running tests
REM
REM   -skip-verification       Skip build verification (hardware accel, compiler flags)
REM
REM   -no-cleanup              Skip cleanup of build directories (for debugging)
REM
REM   -help                    Show this help message
REM
REM Examples:
REM   build-openssl-windows.bat -hostarch a64
REM   build-openssl-windows.bat -hostarch a64 -arch x64
REM   build-openssl-windows.bat -hostarch x86 -arch x64 -version 3.5.4
REM   build-openssl-windows.bat -hostarch a64 -dist-root C:\mydev\toolchain -skip-tests
REM
REM ================================================================================

setlocal enabledelayedexpansion

REM Default parameters
set "ARCH=a64"
set "OPENSSL_VERSION=3.5.4"
set "TOOLCHAIN_NAME=vc143"
set "VS_VERSION=2022"
set "DIST_ROOT=%USERPROFILE%\swblocks\dist-devenv7-windows-a64"
set "DEVENV_TAG=devenv7"
set "HOST_ARCH="
set "SKIP_TESTS="
set "SKIP_VERIFICATION="
set "NO_CLEANUP="
set "HELP_EXIT_CODE="

REM Parse command line arguments
:parse_args
if "%~1"=="" goto args_done
if /i "%~1"=="-arch" (
    set "ARCH=%~2"
    shift
    shift
    goto parse_args
)
if /i "%~1"=="-version" (
    set "OPENSSL_VERSION=%~2"
    shift
    shift
    goto parse_args
)
if /i "%~1"=="-toolchain" (
    set "TOOLCHAIN_NAME=%~2"
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
if /i "%~1"=="-dist-root" (
    set "DIST_ROOT=%~2"
    shift
    shift
    goto parse_args
)
if /i "%~1"=="-devenv-tag" (
    set "DEVENV_TAG=%~2"
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
if /i "%~1"=="-skip-tests" (
    set "SKIP_TESTS=1"
    shift
    goto parse_args
)
if /i "%~1"=="-skip-verification" (
    set "SKIP_VERIFICATION=1"
    shift
    goto parse_args
)
if /i "%~1"=="-no-cleanup" (
    set "NO_CLEANUP=1"
    shift
    goto parse_args
)
if /i "%~1"=="-help" (
    goto show_help
)
echo ERROR: Unknown option: %~1
echo.
echo NOTE: This script builds OpenSSL for ONE architecture at a time.
echo       To build multiple architectures, use build-env-all-windows.bat
echo       or call this script multiple times.
echo.
echo       Example: build-openssl-windows.bat -arch a64
echo       NOT:     build-openssl-windows.bat -arch a64,x64,x86
echo.
set "HELP_EXIT_CODE=1"
goto show_help

:args_done

REM Normalize architecture to lowercase for comparison
set "ARCH_LOWER=%ARCH%"
REM Handle aliases - arm64 is an alias for a64
if /i "%ARCH%"=="ARM64" set "ARCH_LOWER=a64"
if /i "%ARCH%"=="arm64" set "ARCH_LOWER=a64"
if /i "%ARCH%"=="A64" set "ARCH_LOWER=a64"
if /i "%ARCH%"=="X64" set "ARCH_LOWER=x64"
if /i "%ARCH%"=="AMD64" set "ARCH_LOWER=x64"
if /i "%ARCH%"=="amd64" set "ARCH_LOWER=x64"
if /i "%ARCH%"=="X86" set "ARCH_LOWER=x86"

if "%ARCH_LOWER%"=="a64" (
    set "ARCH=a64"
    set "ARCH_TAG=a64"
    set "BUILD_CONFIG_NAME=VC-WIN64-CLANGASM-ARM"
) else if "%ARCH_LOWER%"=="x64" (
    set "ARCH=x64"
    set "ARCH_TAG=x64"
    set "BUILD_CONFIG_NAME=VC-WIN64A"
) else if "%ARCH_LOWER%"=="x86" (
    set "ARCH=x86"
    set "ARCH_TAG=x86"
    set "BUILD_CONFIG_NAME=VC-WIN32"
) else (
    echo ERROR: Invalid architecture '%ARCH%'. Must be a64, x64, or x86 ^(arm64 accepted as alias for a64^)
    goto error
)

REM Validate required -hostarch parameter
if "%HOST_ARCH%"=="" (
    echo ERROR: Required parameter -hostarch not specified
    echo Usage: %~n0 -hostarch ^<a64^|x64^|x86^> -arch ^<target-arch^> [other options]
    echo.
    echo The -hostarch parameter specifies the host architecture where the build runs.
    echo Tests are automatically skipped for x64/a64 targets when host is x86.
    goto error
)

REM Normalize and validate host architecture
set "HOST_ARCH_LOWER=%HOST_ARCH%"
if /i "%HOST_ARCH%"=="ARM64" set "HOST_ARCH_LOWER=a64"
if /i "%HOST_ARCH%"=="arm64" set "HOST_ARCH_LOWER=a64"
if /i "%HOST_ARCH%"=="A64" set "HOST_ARCH_LOWER=a64"
if /i "%HOST_ARCH%"=="X64" set "HOST_ARCH_LOWER=x64"
if /i "%HOST_ARCH%"=="AMD64" set "HOST_ARCH_LOWER=x64"
if /i "%HOST_ARCH%"=="amd64" set "HOST_ARCH_LOWER=x64"
if /i "%HOST_ARCH%"=="X86" set "HOST_ARCH_LOWER=x86"
if /i "%HOST_ARCH%"=="x86" set "HOST_ARCH_LOWER=x86"

if /i not "%HOST_ARCH_LOWER%"=="a64" if /i not "%HOST_ARCH_LOWER%"=="x64" if /i not "%HOST_ARCH_LOWER%"=="x86" (
    echo ERROR: Invalid host architecture '%HOST_ARCH%'. Must be a64, x64, or x86
    goto error
)
set "HOST_ARCH=%HOST_ARCH_LOWER%"

REM Display configuration
echo ================================================================================
echo OpenSSL Build Configuration
echo ================================================================================
echo Host Architecture:  %HOST_ARCH%
echo Target Architecture: %ARCH%
echo OpenSSL Version:    %OPENSSL_VERSION%
echo Toolchain:          %TOOLCHAIN_NAME%
echo VS Version:         %VS_VERSION%
echo Distribution Root:  %DIST_ROOT%
echo devenv Tag:         %DEVENV_TAG%
echo Build Config:       %BUILD_CONFIG_NAME%
echo Skip Tests:         %SKIP_TESTS%
echo ================================================================================
echo.

REM Check for CI_ENV_ROOT or load from DIST_ROOT
if "%CI_ENV_ROOT%" == "" (
    echo CI_ENV_ROOT not set, using DIST_ROOT
    set "CI_ENV_ROOT=%DIST_ROOT%"
)

REM Load environment
if exist "%CI_ENV_ROOT%\scripts\ci\ci-init-env.bat" (
    echo Loading environment from %CI_ENV_ROOT%\scripts\ci\ci-init-env.bat
    call "%CI_ENV_ROOT%\scripts\ci\ci-init-env.bat"
    if errorlevel 1 (
        echo ERROR: Failed to load environment
        goto error
    )
) else (
    echo Warning: ci-init-env.bat not found, using provided DIST_ROOT
    set "DIST_ROOT_DEPS1=%DIST_ROOT%"
    set "DIST_ROOT_DEPS2=%DIST_ROOT%"
    set "DIST_ROOT_DEPS3=%DIST_ROOT%"
)

REM Verify DIST_ROOT_DEPS1 is set
if "%DIST_ROOT_DEPS1%" == "" (
    echo ERROR: DIST_ROOT_DEPS1 not defined
    goto error
)

REM Verify MSVC_ROOT and WINSDK_ROOT are set by ci-init-env.bat
if not defined MSVC_ROOT (
    echo ERROR: MSVC_ROOT not defined. ci-init-env.bat may have failed.
    goto error
)

if not defined WINSDK_ROOT (
    echo ERROR: WINSDK_ROOT not defined. ci-init-env.bat may have failed.
    goto error
)

if not defined MSVC_VERSION (
    echo ERROR: MSVC_VERSION not defined. ci-init-env.bat may have failed.
    goto error
)

if not defined WINSDK_VERSION (
    echo ERROR: WINSDK_VERSION not defined. ci-init-env.bat may have failed.
    goto error
)

REM Load toolchain environment - REQUIRED
set "SETUP_ENV_SCRIPT=%DIST_ROOT_DEPS1%\scripts\ci\setup-env-%ARCH%.bat"

if not exist "%SETUP_ENV_SCRIPT%" (
    echo ERROR: Setup environment script not found: %SETUP_ENV_SCRIPT%
    echo.
    echo Please run the toolchain setup first:
    echo   scripts\devenv7\windows\build-env-all-windows.bat -targets %ARCH%
    echo.
    goto error
)

echo Loading toolchain environment from %SETUP_ENV_SCRIPT%
call "%SETUP_ENV_SCRIPT%"
if errorlevel 1 (
    echo ERROR: Failed to load toolchain environment from %SETUP_ENV_SCRIPT%
    goto error
)

echo Environment configured successfully
echo Using MSVC %MSVC_VERSION%
echo Using Windows SDK %WINSDK_VERSION%
echo.

REM Verify compiler is available
echo Checking for compiler...
where cl 1>nul 2>nul
if errorlevel 1 (
    echo ERROR: MSVC compiler ^(cl.exe^) not found in PATH
    goto error
)

echo Compiler found:
where cl

REM Trim trailing whitespace from DIST_ROOT_DEPS1 (common issue when variable is set manually)
set "DIST_ROOT=%DIST_ROOT_DEPS1%"
:trim_dist_root_perl
if "%DIST_ROOT:~-1%"==" " (
    set "DIST_ROOT=%DIST_ROOT:~0,-1%"
    goto trim_dist_root_perl
)

REM Add Strawberry Perl to PATH - detect version automatically
set "PERL_PATH="
set "PERL_VERSION="
for /d %%P in ("%DIST_ROOT%\strawberry-perl\*") do (
    if exist "%%P\default\perl\bin\perl.exe" (
        set "PERL_PATH=%%P\default\perl\bin"
        set "PERL_VERSION=%%~nxP"
        goto :found_perl
    )
)
:found_perl

if "%PERL_PATH%"=="" (
    echo ERROR: Strawberry Perl not found in %DIST_ROOT%\strawberry-perl
    echo Please run build-msvc-toolchain.bat first
    goto error
)

set "Path=%PERL_PATH%;%Path%"
echo Perl version: %PERL_VERSION%
echo Perl found:
where perl

REM ============================================================================
REM Detect Real Processor Architecture (for ARM64 Perl compatibility fix)
REM ============================================================================
REM Priority 1: PROCESSOR_IDENTIFIER (most reliable, works in all environments)
REM Check for ARMv8 and AArch64 separately, not as a combined string
set "REAL_PROCESSOR_ARCH="

echo %PROCESSOR_IDENTIFIER% | findstr /i "ARMv8" >nul
if %errorlevel% equ 0 (
    set "REAL_PROCESSOR_ARCH=ARM64"
    goto detected_processor_arch
)

echo %PROCESSOR_IDENTIFIER% | findstr /i "AArch64" >nul
if %errorlevel% equ 0 (
    set "REAL_PROCESSOR_ARCH=ARM64"
    goto detected_processor_arch
)

REM Priority 2: PROCESSOR_ARCHITECTURE direct check
if /i "%PROCESSOR_ARCHITECTURE%"=="ARM64" (
    set "REAL_PROCESSOR_ARCH=ARM64"
    goto detected_processor_arch
)

REM Priority 3: PROCESSOR_ARCHITEW6432 (emulation detection - 32-bit process on 64-bit OS)
if /i "%PROCESSOR_ARCHITEW6432%"=="ARM64" (
    set "REAL_PROCESSOR_ARCH=ARM64"
    goto detected_processor_arch
)

REM Priority 4: Default to x64 or x86
if /i "%PROCESSOR_ARCHITECTURE%"=="AMD64" (
    set "REAL_PROCESSOR_ARCH=x64"
) else if /i "%PROCESSOR_ARCHITECTURE%"=="x86" (
    set "REAL_PROCESSOR_ARCH=x86"
) else (
    set "REAL_PROCESSOR_ARCH=x64"
)

:detected_processor_arch
echo Detected processor architecture: %REAL_PROCESSOR_ARCH%

REM ============================================================================
REM Apply Windows 8 Compatibility Mode to Perl on ARM64
REM ============================================================================
REM This fixes fork() emulation deadlocks when x86/x64 Perl runs under ARM64 emulation
REM Strawberry Perl is x86 or x64 only (no native ARM64 version), so always runs under emulation on ARM64
if /i "%REAL_PROCESSOR_ARCH%"=="ARM64" (
    echo.
    echo ARM64 processor detected - applying Windows 8 compatibility mode to Perl
    echo This prevents fork^(^) emulation deadlocks when Perl runs under ARM64 emulation

    REM Construct full path to perl.exe
    set "PERL_EXE_PATH=%DIST_ROOT%\strawberry-perl\%PERL_VERSION%\default\perl\bin\perl.exe"

    REM Apply Win8RTM compatibility shim via registry for perl.exe
    REM CRITICAL: If this fails, script must abort - cannot proceed without compatibility mode
    echo   Setting compatibility mode for: !PERL_EXE_PATH!
    reg add "HKCU\Software\Microsoft\Windows NT\CurrentVersion\AppCompatFlags\Layers" /v "!PERL_EXE_PATH!" /t REG_SZ /d "~ WIN8RTM" /f >nul 2>&1
    if !errorlevel! neq 0 (
        echo ERROR: Failed to set compatibility mode for perl.exe
        echo Registry command failed - tests may hang without this fix
        goto error
    )

    REM Dynamically detect and apply compatibility mode to versioned Perl executable (e.g., perl5.32.1.exe)
    REM Fail immediately if registry command fails
    for %%F in ("%DIST_ROOT%\strawberry-perl\%PERL_VERSION%\default\perl\bin\perl5.*.exe") do (
        echo   Setting compatibility mode for: %%F
        reg add "HKCU\Software\Microsoft\Windows NT\CurrentVersion\AppCompatFlags\Layers" /v "%%F" /t REG_SZ /d "~ WIN8RTM" /f >nul 2>&1
        if !errorlevel! neq 0 (
            echo ERROR: Failed to set compatibility mode for %%~nxF
            echo Registry command failed - tests may hang without this fix
            goto error
        )
    )

    echo   Compatibility mode applied successfully
)

REM Verify assembler is available (optional; ARM64 uses clang-cl, x86/x64 use NASM)
if /i "%ARCH%"=="a64" (
    echo.
    echo NOTE: ARM64 build uses VC-WIN64-CLANGASM-ARM configuration
    echo       This uses clang-cl ONLY for assembler, MSVC cl.exe is used for C/C++ compilation
    echo.
    where clang-cl >nul 2>&1
    if errorlevel 1 (
        echo Warning: clang-cl not found, building with no-asm option
        set "ASM_OPTION=no-asm"
    ) else (
        echo clang-cl found ^(for assembler^):
        where clang-cl
        set "ASM_OPTION="
    )
) else (
    where nasm >nul 2>&1
    if errorlevel 1 (
        echo Warning: NASM not found, building with no-asm option
        set "ASM_OPTION=no-asm"
    ) else (
        echo NASM found:
        where nasm
        set "ASM_OPTION="
    )
)

REM Set paths
set "OPENSSL_SOURCE_PATH=%DIST_ROOT_DEPS1%\openssl\%OPENSSL_VERSION%\source-windows"

REM Download OpenSSL source if it doesn't exist
if not exist "%OPENSSL_SOURCE_PATH%\Configure" (
    echo.
    echo OpenSSL source not found at %OPENSSL_SOURCE_PATH%
    echo Downloading OpenSSL %OPENSSL_VERSION% automatically...
    echo.

    REM Get script directory for PowerShell modules
    set "PS_SCRIPT_DIR=%~dp0internal"

    powershell -ExecutionPolicy Bypass -Command "& { ^
        Import-Module '%PS_SCRIPT_DIR%\download-sources.ps1' -Force; ^
        Install-OpenSSLSource -Version '%OPENSSL_VERSION%' -DestinationRoot '%DIST_ROOT_DEPS1%' ^
    }"

    if errorlevel 1 (
        echo.
        echo ERROR: Failed to download OpenSSL source
        echo.
        echo You can manually download from:
        echo   https://github.com/openssl/openssl/releases/download/openssl-%OPENSSL_VERSION%/openssl-%OPENSSL_VERSION%.tar.gz
        echo And extract to:
        echo   %OPENSSL_SOURCE_PATH%
        goto error
    )

    echo.
    echo OpenSSL source downloaded successfully
    echo.
)

REM Verify OpenSSL source exists
if not exist "%OPENSSL_SOURCE_PATH%\Configure" (
    echo ERROR: OpenSSL source verification failed
    echo Expected file not found: %OPENSSL_SOURCE_PATH%\Configure
    goto error
)

REM Determine swblocks build root (parallel to dist folder)
REM Extract parent directory from DIST_ROOT_DEPS1 to get swblocks folder
for %%I in ("%DIST_ROOT_DEPS1%") do set "SWBLOCKS_ROOT=%%~dpI"
set "SWBLOCKS_ROOT=%SWBLOCKS_ROOT:~0,-1%"
set "BLD_ROOT=%SWBLOCKS_ROOT%\bld"

REM Build both debug and release
for %%B in (debug release) do (
    call :build_variant %%B
    if errorlevel 1 goto error
)

REM Success
echo.
echo ================================================================================
echo OpenSSL Build Successful!
echo ================================================================================
echo.
echo Debug installation location:
echo   %DIST_ROOT_DEPS1%\openssl\%OPENSSL_VERSION%\win-%ARCH_TAG%-%TOOLCHAIN_NAME%-debug
echo.
echo Release installation location:
echo   %DIST_ROOT_DEPS1%\openssl\%OPENSSL_VERSION%\win-%ARCH_TAG%-%TOOLCHAIN_NAME%-release
echo.
echo ================================================================================

exit /b 0

REM =============================================================================
REM Build a specific variant (debug or release)
REM =============================================================================
:build_variant
setlocal

set "BUILD_TYPE=%~1"
set "TESTS_FAILED="

echo.
echo ================================================================================
echo Building OpenSSL %OPENSSL_VERSION% - %BUILD_TYPE% variant
echo ================================================================================
echo.

REM Build directory (in swblocks bld folder, parallel to dist)
set "OPENSSL_ROOT_PATH=%BLD_ROOT%\openssl\%OPENSSL_VERSION%\win-%ARCH_TAG%-%TOOLCHAIN_NAME%-%BUILD_TYPE%"

REM Clean existing build
if exist "%OPENSSL_ROOT_PATH%" (
    echo Deleting existing build directory: %OPENSSL_ROOT_PATH%
    rd /s /q "%OPENSSL_ROOT_PATH%"
    if errorlevel 1 (
        echo ERROR: Failed to delete existing build directory
        exit /b 1
    )
)

REM Create build directory
echo Creating build directory: %OPENSSL_ROOT_PATH%
mkdir "%OPENSSL_ROOT_PATH%"
if errorlevel 1 exit /b 1

pushd "%OPENSSL_ROOT_PATH%"
if errorlevel 1 exit /b 1

REM Copy source files
echo Copying OpenSSL source files...
robocopy /E "%OPENSSL_SOURCE_PATH%" . > ..\log_src_filecopy.log 2>&1
if errorlevel 8 (
    echo ERROR: Failed to copy source files, see %OPENSSL_ROOT_PATH%\..\log_src_filecopy.log
    exit /b 1
)
move ..\log_src_filecopy.log .

REM Configure based on build type
echo Configuring OpenSSL for %BUILD_TYPE%...

REM Set assembly activation and architecture macros for ARM64
REM Two categories of macros are required for ARM64:
REM
REM 1. Assembly activation macros (ASM_MACROS):
REM    Due to quirks in VC-WIN64-CLANGASM-ARM, OpenSSL builds assembly but doesn't activate it in C code
REM    These macros tell the C "glue" code to call the assembly functions
REM
REM 2. ARM architecture macros (ARCH_MACROS):
REM    MSVC cl.exe does not define __ARM_ARCH__ or __ARM_MAX_ARCH__ when compiling C code
REM    Without these macros, OpenSSL's compile-time guards prevent AES/GCM fast paths from being compiled
REM    Example: aes_platform.h (line 98) and gcm128.c (line 372) check __ARM_MAX_ARCH__>=8
REM    Result: Even with assembly objects and runtime detection, code falls back to generic C implementation
REM    Fix: Explicitly define __ARM_ARCH__=8 and __ARM_MAX_ARCH__=8 for ARMv8 targets
if /i "%ARCH%"=="a64" (
    set "ASM_MACROS=-DOPENSSL_CPUID_OBJ -DOPENSSL_BN_ASM_MONT -DMD5_ASM -DVPAES_ASM -DBSAES_ASM -DSHA1_ASM -DSHA256_ASM -DSHA512_ASM -DKECCAK1600_ASM -DPOLY1305_ASM -DECP_NISTZ256_ASM"
    set "ARCH_MACROS=-D__ARM_ARCH__=8 -D__ARM_MAX_ARCH__=8"
) else (
    set "ASM_MACROS="
    set "ARCH_MACROS="
)

if "%BUILD_TYPE%"=="debug" (
    REM Debug build: Pass release optimization flags to Configure to keep assembly enabled
    REM Then patch makefile to replace with debug optimization flags
    REM Both debug and release use --release to avoid debug CRT (/MDd)
    REM Debug info flags will be added via makefile patching
    perl Configure %BUILD_CONFIG_NAME% %ASM_OPTION% no-shared --release ^
        --prefix=%OPENSSL_ROOT_PATH%\out ^
        --openssldir=%OPENSSL_ROOT_PATH%\out\ssl ^
        -O2 -Ob1 -Ot -Oi -Oy- -EHs -GS -bigobj -DNDEBUG %ASM_MACROS% %ARCH_MACROS% > log_bootstrap.log 2>&1

    if errorlevel 1 (
        echo ERROR: Configure failed, see log: %OPENSSL_ROOT_PATH%\log_bootstrap.log
        type log_bootstrap.log
        exit /b 1
    )

    REM Patch makefile to replace release optimization flags with debug flags
    REM This prevents Configure from disabling assembly due to -Od detection
    echo Patching makefile to replace optimization flags for debug build...
    perl -i.bak -pe "s/-O2 -Ob1 -Ot -Oi/-Od -Ob0/g; s/\/O2 /\/Od /g" makefile

    if errorlevel 1 (
        echo ERROR: Failed to patch optimization flags in makefile
        exit /b 1
    )
) else (
    REM Release build: optimizations enabled, release CRT, NDEBUG defined
    REM Debug info flags will be added via makefile patching
    perl Configure %BUILD_CONFIG_NAME% %ASM_OPTION% no-shared --release ^
        --prefix=%OPENSSL_ROOT_PATH%\out ^
        --openssldir=%OPENSSL_ROOT_PATH%\out\ssl ^
        -O2 -Ob1 -Ot -Oi -Oy- -EHs -GS -bigobj -DNDEBUG %ASM_MACROS% %ARCH_MACROS% > log_bootstrap.log 2>&1

    if errorlevel 1 (
        echo ERROR: Configure failed, see log: %OPENSSL_ROOT_PATH%\log_bootstrap.log
        type log_bootstrap.log
        exit /b 1
    )
)

REM Patch makefile based on architecture
if /i "%ARCH%"=="a64" (
    REM ARM64: Use Perl to redirect assembly preprocessor to clang-cl and add debug symbols
    REM - Redirects $(CC) /EP to clang-cl.exe /EP for assembly preprocessing
    REM - This fixes Microsoft C preprocessor mangling GNU-style ARM assembly (# character)
    REM - Adds /Z7 and /Zo to CFLAGS for debug information
    REM - Note: cl.exe is still used for C compilation (needed for MSVC intrinsics like _InterlockedAdd64)
    echo Patching makefile for ARM64: redirecting assembly preprocessor to clang-cl and adding debug flags...
    perl -i.bak -pe "s/\$\(CC\) \/EP -D__ASSEMBLER__/clang-cl.exe \/EP -D__ASSEMBLER__/g; s/^CFLAGS=(.*)/CFLAGS=$1 \/Z7 \/Zo/g" makefile

    if errorlevel 1 (
        echo ERROR: Failed to patch makefile with Perl
        exit /b 1
    )
) else (
    REM x64/x86: Use PowerShell to add debug information flags
    REM - Convert any /Zi to /Z7 (embeds debug info in .obj files, avoids PDB conflicts with jom)
    REM - Add /Z7 and /Zo to LIB_CFLAGS only (not ASFLAGS)
    REM - /Zo enables optimized debugging for release builds
    echo Patching makefile to add debug information flags...
    powershell -NoProfile -ExecutionPolicy Bypass -Command "$content = Get-Content -Raw -Encoding ASCII makefile; $content = $content -replace '/Zi\s+/Fd[^\s]+\s*','/Z7 ' -replace '(LIB_CFLAGS=[^\r\n]+)',('$1 /Z7 /Zo'); Set-Content -Encoding ASCII makefile $content"

    if errorlevel 1 (
        echo ERROR: Failed to patch makefile
        exit /b 1
    )
)

REM Remove PDB copy commands from install target (no PDB files generated with /Z7)
REM This applies to all architectures
echo Patching makefile to remove PDB install steps...
powershell -NoProfile -ExecutionPolicy Bypass -Command "$lines = Get-Content makefile; $lines = $lines -replace '.*copy\.pl.*ossl_static\.pdb.*','# Removed: PDB copy (using /Z7)'; Set-Content -Encoding ASCII makefile $lines"

if errorlevel 1 (
    echo ERROR: Failed to patch makefile for PDB removal
    exit /b 1
)

echo Configuration completed successfully

REM Calculate number of parallel jobs
REM Build jobs: 4*CPUs for fast compilation across all builds
REM Test jobs: 2*CPUs for stability (reduced from 4*CPUs)
for /f "tokens=*" %%i in ('wmic cpu get NumberOfLogicalProcessors /value ^| find "="') do set "%%i"
set /a "PARALLEL_JOBS=%NumberOfLogicalProcessors% * 4"
set /a "HARNESS_JOBS=%NumberOfLogicalProcessors% * 2"
echo Detected %NumberOfLogicalProcessors% logical processors, using %PARALLEL_JOBS% parallel build jobs
echo TAP::Harness will use %HARNESS_JOBS% parallel test jobs ^(reduced for stability^)

REM Build OpenSSL
echo.
echo Building OpenSSL (%BUILD_TYPE%) with parallel build (jom -j %PARALLEL_JOBS%)...
echo This may take 1-2 minutes...
echo.

jom -j %PARALLEL_JOBS% > log_openssl_build.log 2>&1
if errorlevel 1 (
    echo ERROR: Build failed, see log: %OPENSSL_ROOT_PATH%\log_openssl_build.log
    echo.
    echo Last 50 lines of log:
    powershell -Command "Get-Content log_openssl_build.log -Tail 50"
    exit /b 1
)

echo Build completed successfully

REM Skip tests when target binary cannot execute on the processor
REM On ARM64 processor: Win8 compatibility mode allows all tests to run (ARM64 can emulate x86/x64 and run native a64)
REM On x86 processor: Cannot execute x64 or a64 binaries (no emulation layer)
REM On x64 processor: Cannot execute a64 binaries (x64 cannot run ARM64)
if NOT "%REAL_PROCESSOR_ARCH%"=="ARM64" (
    REM Skip tests for architectures the processor cannot execute
    if "%REAL_PROCESSOR_ARCH%"=="x86" (
        if "%ARCH%"=="a64" (
            echo.
            echo Skipping tests: x86 processor cannot execute ARM64 binaries
            set "SKIP_TESTS=1"
        )
        if "%ARCH%"=="x64" (
            echo.
            echo Skipping tests: x86 processor cannot execute x64 binaries
            set "SKIP_TESTS=1"
        )
    )
    if "%REAL_PROCESSOR_ARCH%"=="x64" (
        if "%ARCH%"=="a64" (
            echo.
            echo Skipping tests: x64 processor cannot execute ARM64 binaries
            set "SKIP_TESTS=1"
        )
    )
) else (
    REM On ARM64 processor, all tests can run (Win8 compat mode prevents Perl deadlock)
    echo Tests enabled: ARM64 processor can execute all architectures
    echo (Win8 compatibility mode prevents Perl fork emulation deadlocks)
)

REM Skip tests for x86 hostarch building a64 target (known issue with emulation)
if /i "%HOST_ARCH%"=="x86" (
    if "%ARCH%"=="a64" (
        echo.
        echo Skipping tests: x86 hostarch building a64 target has known test failures
        set "SKIP_TESTS=1"
    )
)

REM Test OpenSSL (unless skipped)
if "%SKIP_TESTS%"=="1" (
    echo Skipping tests
) else (
    echo.
    echo Testing OpenSSL ^(%BUILD_TYPE%^)...

    jom -j %PARALLEL_JOBS% test > log_openssl_test.log 2>&1
    if errorlevel 1 (
        set "TESTS_FAILED=1"
        echo WARNING: Tests failed; log will be moved to installation directory.
    ) else (
        echo Tests passed
    )
)

REM Install OpenSSL
echo.
echo Installing OpenSSL ^(%BUILD_TYPE%^)...

REM Ensure all build targets are marked complete before parallel install
REM This prevents multiple jom workers from triggering depend during install
jom build_sw > nul 2>&1

REM Now install in parallel - all dependencies are satisfied
jom -j %PARALLEL_JOBS% install > log_openssl_install.log 2>&1
if errorlevel 1 (
    echo ERROR: Install failed, see log: %OPENSSL_ROOT_PATH%\log_openssl_install.log
    exit /b 1
)

echo Install completed successfully

REM Verify build configuration before copying to dist
if not defined SKIP_VERIFICATION (
    echo.
    echo ================================================================================
    echo Verifying OpenSSL Build Configuration
    echo ================================================================================
    echo.

    powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0verify-openssl-build.ps1" ^
        -OpensslExe "%OPENSSL_ROOT_PATH%\out\bin\openssl.exe" ^
        -BuildType "%BUILD_TYPE%" ^
        -Architecture "%ARCH_LOWER%"

    if errorlevel 1 (
        echo.
        echo ================================================================================
        echo ERROR: OpenSSL build verification failed
        echo ================================================================================
        echo.
        echo The build completed successfully but verification checks failed.
        echo Review the verification errors above.
        echo.
        echo You can skip verification with -skip-verification flag for debugging.
        exit /b 1
    )

    echo.
    echo ================================================================================
    echo Verification Passed Successfully
    echo ================================================================================
)

REM Copy to destination
set "OPENSSL_TARGET_PATH=%DIST_ROOT_DEPS1%\openssl\%OPENSSL_VERSION%\win-%ARCH_TAG%-%TOOLCHAIN_NAME%-%BUILD_TYPE%"

if exist "%OPENSSL_TARGET_PATH%" (
    echo Deleting existing installation: %OPENSSL_TARGET_PATH%
    rd /s /q "%OPENSSL_TARGET_PATH%"
    if errorlevel 1 (
        echo ERROR: Failed to delete existing installation
        exit /b 1
    )
)

echo Creating installation directory: %OPENSSL_TARGET_PATH%
mkdir "%OPENSSL_TARGET_PATH%"
if errorlevel 1 exit /b 1

echo Copying build output to installation directory...
robocopy /E "%OPENSSL_ROOT_PATH%\out" "%OPENSSL_TARGET_PATH%" > ..\log_dst_filecopy.log 2>&1
if errorlevel 8 (
    echo ERROR: Failed to copy to destination, see %OPENSSL_ROOT_PATH%\log_dst_filecopy.log
    exit /b 1
)
move ..\log_dst_filecopy.log .

REM Move log files and makefile to destination
move log_* "%OPENSSL_TARGET_PATH%" >nul 2>&1
if defined TESTS_FAILED (
    echo WARNING: OpenSSL tests failed, see log: %OPENSSL_TARGET_PATH%\log_openssl_test.log
)
move makefile "%OPENSSL_TARGET_PATH%" >nul 2>&1

REM Clean up build directory
popd
if "%NO_CLEANUP%"=="1" (
    echo Skipping cleanup of build directory: %OPENSSL_ROOT_PATH%
) else (
    echo Cleaning up build directory...
    rd /s /q "%OPENSSL_ROOT_PATH%"
    if errorlevel 1 (
        echo WARNING: Failed to delete build directory
    )
)

echo.
echo %BUILD_TYPE% build completed successfully
echo Installation: %OPENSSL_TARGET_PATH%
echo.

endlocal
exit /b 0

:show_help
type "%~f0" | findstr /B "REM"
if defined HELP_EXIT_CODE exit /b 1
exit /b 0

:error
exit /b 1
