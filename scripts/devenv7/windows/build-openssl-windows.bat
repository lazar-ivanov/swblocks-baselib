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
REM   build-openssl-windows.bat [options]
REM
REM Options:
REM   -arch <architecture>     Target architecture: arm64, x64, or x86
REM                            Default: arm64
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
REM   -help                    Show this help message
REM
REM Examples:
REM   build-openssl-windows.bat
REM   build-openssl-windows.bat -arch x64
REM   build-openssl-windows.bat -arch arm64 -version 3.5.4
REM   build-openssl-windows.bat -dist-root C:\mydev\toolchain -skip-tests
REM
REM ================================================================================

setlocal

REM Default parameters
set "ARCH=arm64"
set "OPENSSL_VERSION=3.5.4"
set "TOOLCHAIN_NAME=vc143"
set "VS_VERSION=2022"
set "DIST_ROOT=%USERPROFILE%\swblocks\dist-devenv7-windows-a64"
set "DEVENV_TAG=devenv7"
set "SKIP_TESTS="
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
if /i "%~1"=="-skip-tests" (
    set "SKIP_TESTS=1"
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

REM Normalize architecture to lowercase for comparison
set "ARCH_LOWER=%ARCH%"
if /i "%ARCH%"=="ARM64" set "ARCH_LOWER=arm64"
if /i "%ARCH%"=="Arm64" set "ARCH_LOWER=arm64"
if /i "%ARCH%"=="X64" set "ARCH_LOWER=x64"
if /i "%ARCH%"=="AMD64" set "ARCH_LOWER=x64"
if /i "%ARCH%"=="X86" set "ARCH_LOWER=x86"

if "%ARCH_LOWER%"=="arm64" (
    set "ARCH=arm64"
    set "ARCH_TAG=a64"
    set "BUILD_CONFIG_NAME=VC-WIN64-ARM"
) else if "%ARCH_LOWER%"=="x64" (
    set "ARCH=x64"
    set "ARCH_TAG=x64"
    set "BUILD_CONFIG_NAME=VC-WIN64A"
) else if "%ARCH_LOWER%"=="amd64" (
    set "ARCH=x64"
    set "ARCH_TAG=x64"
    set "BUILD_CONFIG_NAME=VC-WIN64A"
) else if "%ARCH_LOWER%"=="x86" (
    set "ARCH=x86"
    set "ARCH_TAG=x86"
    set "BUILD_CONFIG_NAME=VC-WIN32"
) else (
    echo ERROR: Invalid architecture '%ARCH%'. Must be arm64, x64, or x86
    goto error
)

REM Display configuration
echo ================================================================================
echo OpenSSL Build Configuration
echo ================================================================================
echo Architecture:       %ARCH%
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

REM Add Strawberry Perl to PATH - detect version automatically
set "PERL_PATH="
for /d %%P in ("%DIST_ROOT_DEPS1%\strawberry-perl\*") do (
    if exist "%%P\default\perl\bin\perl.exe" (
        set "PERL_PATH=%%P\default\perl\bin"
        goto :found_perl
    )
)
:found_perl

if "%PERL_PATH%"=="" (
    echo ERROR: Strawberry Perl not found in %DIST_ROOT_DEPS1%\strawberry-perl
    echo Please run build-msvc-toolchain.bat first
    goto error
)

set "Path=%PERL_PATH%;%Path%"
echo Perl found:
where perl

REM Verify assembler is available (optional; ARM64 uses armasm64, x86/x64 use NASM)
if /i "%ARCH%"=="arm64" (
    where armasm64 >nul 2>&1
    if errorlevel 1 (
        echo Warning: armasm64 not found, building with no-asm option
        set "ASM_OPTION=no-asm"
    ) else (
        echo armasm64 found:
        where armasm64
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
        echo   https://www.openssl.org/source/openssl-%OPENSSL_VERSION%.tar.gz
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

REM Get repository root (3 levels up from scripts\devenv7\windows)
set "SCRIPT_DIR=%~dp0"
pushd "%SCRIPT_DIR%..\..\..\"
set "REPO_ROOT=%CD%"
popd

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

REM Build directory (in repo bld folder)
set "OPENSSL_ROOT_PATH=%REPO_ROOT%\bld\swblocks\openssl\%OPENSSL_VERSION%\win-%ARCH_TAG%-%TOOLCHAIN_NAME%-%BUILD_TYPE%"

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

if "%BUILD_TYPE%"=="debug" (
    REM Debug build: no optimizations, debug symbols, no runtime debug CRT
    REM Use /Z7 instead of /Zi - embeds debug info in .obj files, no PDB conflicts
    perl Configure %BUILD_CONFIG_NAME% %ASM_OPTION% no-shared --debug ^
        --prefix=%OPENSSL_ROOT_PATH%\out ^
        --openssldir=%OPENSSL_ROOT_PATH%\out\ssl ^
        -Od -Ob0 -Oy- -EHs -Z7 -GS -bigobj > log_bootstrap.log 2>&1

    if errorlevel 1 (
        echo ERROR: Configure failed, see log: %OPENSSL_ROOT_PATH%\log_bootstrap.log
        type log_bootstrap.log
        exit /b 1
    )

    REM Modify makefile to use release CRT (/MD instead of /MDd)
    REM This avoids runtime debug dependencies
    echo Patching makefile to use release CRT...
    powershell -NoProfile -ExecutionPolicy Bypass -Command "$c = Get-Content -Raw -Encoding ASCII makefile; $c = $c -replace '/MDd','/MD' -replace '-DDEBUG','' -replace '-D_DEBUG',''; Set-Content -Encoding ASCII makefile $c"

    REM Patch makefile to use /Z7 instead of /Zi in LIB_CFLAGS (embeds debug info, no PDB files)
    echo Patching makefile to use /Z7 instead of /Zi...
    powershell -NoProfile -ExecutionPolicy Bypass -Command "$lines = Get-Content makefile; $lines = $lines -replace '/Zi /Fdossl_static\.pdb','/Z7'; Set-Content -Encoding ASCII makefile $lines"

    REM Remove PDB copy commands from install target (no PDB files generated with /Z7)
    echo Patching makefile to remove PDB install steps...
    powershell -NoProfile -ExecutionPolicy Bypass -Command "$lines = Get-Content makefile; $lines = $lines -replace '.*copy\.pl.*ossl_static\.pdb.*','# Removed: PDB copy (using /Z7)'; Set-Content -Encoding ASCII makefile $lines"
) else (
    REM Release build: optimizations, debug symbols for debugging
    REM Use /Z7 instead of /Zi - embeds debug info in .obj files, no PDB conflicts
    perl Configure %BUILD_CONFIG_NAME% %ASM_OPTION% no-shared --release ^
        --prefix=%OPENSSL_ROOT_PATH%\out ^
        --openssldir=%OPENSSL_ROOT_PATH%\out\ssl ^
        -O2 -Ob1 -Ot -Oi -Oy- -EHs -Z7 -GS -bigobj -Zo -DNDEBUG > log_bootstrap.log 2>&1

    if errorlevel 1 (
        echo ERROR: Configure failed, see log: %OPENSSL_ROOT_PATH%\log_bootstrap.log
        type log_bootstrap.log
        exit /b 1
    )

    REM Patch makefile to use /Z7 instead of /Zi in LIB_CFLAGS (embeds debug info, no PDB files)
    echo Patching makefile to use /Z7 instead of /Zi...
    powershell -NoProfile -ExecutionPolicy Bypass -Command "$lines = Get-Content makefile; $lines = $lines -replace '/Zi /Fdossl_static\.pdb','/Z7'; Set-Content -Encoding ASCII makefile $lines"

    REM Remove PDB copy commands from install target (no PDB files generated with /Z7)
    echo Patching makefile to remove PDB install steps...
    powershell -NoProfile -ExecutionPolicy Bypass -Command "$lines = Get-Content makefile; $lines = $lines -replace '.*copy\.pl.*ossl_static\.pdb.*','# Removed: PDB copy (using /Z7)'; Set-Content -Encoding ASCII makefile $lines"
)

echo Configuration completed successfully

REM Calculate number of parallel jobs (CPU count * 5 for both build and tests)
for /f "tokens=*" %%i in ('wmic cpu get NumberOfLogicalProcessors /value ^| find "="') do set "%%i"
set /a "PARALLEL_JOBS=%NumberOfLogicalProcessors% * 5"
set /a "HARNESS_JOBS=%NumberOfLogicalProcessors% * 5"
echo Detected %NumberOfLogicalProcessors% logical processors, using %PARALLEL_JOBS% parallel jobs
echo TAP::Harness will use %HARNESS_JOBS% parallel test jobs

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
echo Cleaning up build directory...
rd /s /q "%OPENSSL_ROOT_PATH%"
if errorlevel 1 (
    echo WARNING: Failed to delete build directory
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
