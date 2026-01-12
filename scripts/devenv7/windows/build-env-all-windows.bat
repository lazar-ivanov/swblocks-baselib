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
REM Complete Windows devenv7 Environment Build Script
REM ================================================================================
REM
REM This script automates the complete setup of the Windows development environment:
REM   1. Toolchain setup (Git, Python, MSYS2, Perl, JSON Spirit, VS detection)
REM   2. Boost library build (debug and release)
REM   3. OpenSSL library build (debug and release)
REM
REM This is equivalent to the Linux/macOS build-env-all scripts
REM
REM Usage:
REM   build-env-all-windows.bat [options]
REM
REM Options:
REM   -dist-root <path>        Distribution root directory
REM                            Default: %USERPROFILE%\swblocks\dist-devenv7-windows-a64
REM
REM   -targets <arch-list>     Comma-separated list of target architectures
REM                            Options: arm64, x64, x86
REM                            Default: arm64,x64,x86
REM
REM   -git-version <version>   Git version to download
REM                            Default: 2.52.0
REM
REM   -python-version <ver>    Python version to download
REM                            Default: 3.14.2
REM
REM   -boost-version <ver>     Boost version to build
REM                            Default: 1.90.0
REM
REM   -openssl-version <ver>   OpenSSL version to build
REM                            Default: 3.5.4
REM
REM   -toolchain <name>        Toolchain name (vc143, etc.)
REM                            Default: vc143
REM
REM   -vs-version <ver>        Visual Studio version (2022, etc.)
REM                            Default: 2022
REM
REM   -threads <n>             Number of Boost.Build threads
REM                            Default: 8
REM
REM   -skip-toolchain          Skip toolchain setup (assume already done)
REM
REM   -skip-boost              Skip Boost build
REM
REM   -skip-openssl            Skip OpenSSL build
REM
REM   -skip-tests              Skip OpenSSL tests
REM
REM   -help                    Show this help message
REM
REM Examples:
REM   build-env-all-windows.bat
REM   build-env-all-windows.bat -targets arm64,x64
REM   build-env-all-windows.bat -dist-root C:\mydev\toolchain
REM   build-env-all-windows.bat -skip-toolchain
REM
REM ================================================================================

setlocal enabledelayedexpansion

REM Default parameters
set "DIST_ROOT=%USERPROFILE%\swblocks\dist-devenv7-windows-a64"
set "TARGET_ARCHS=arm64,x64,x86"
set "GIT_VERSION=2.52.0"
set "PYTHON_VERSION=3.14.2"
set "BOOST_VERSION=1.90.0"
set "OPENSSL_VERSION=3.5.4"
set "TOOLCHAIN_NAME=vc143"
set "VS_VERSION=2022"
set "BOOST_THREADS=8"
set "SKIP_TOOLCHAIN="
set "SKIP_BOOST="
set "SKIP_OPENSSL="
set "SKIP_TESTS="
set "HELP_EXIT_CODE="

REM Get script directory
set "SCRIPT_DIR=%~dp0"

REM Parse command line arguments
:parse_args
if "%~1"=="" goto args_done
if /i "%~1"=="-dist-root" (
    set "DIST_ROOT=%~2"
    shift
    shift
    goto parse_args
)
if /i "%~1"=="-targets" (
    set "TARGET_ARCHS=%~2"
    shift
    shift
    goto parse_args
)
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
if /i "%~1"=="-threads" (
    set "BOOST_THREADS=%~2"
    shift
    shift
    goto parse_args
)
if /i "%~1"=="-skip-toolchain" (
    set "SKIP_TOOLCHAIN=1"
    shift
    goto parse_args
)
if /i "%~1"=="-skip-boost" (
    set "SKIP_BOOST=1"
    shift
    goto parse_args
)
if /i "%~1"=="-skip-openssl" (
    set "SKIP_OPENSSL=1"
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

REM Display overall configuration
echo.
echo ================================================================================
echo Windows devenv7 Complete Environment Build
echo ================================================================================
echo.
echo Configuration:
echo   Distribution Root:    %DIST_ROOT%
echo   Target Architectures: %TARGET_ARCHS%
echo   Git Version:          %GIT_VERSION%
echo   Python Version:       %PYTHON_VERSION%
echo   Boost Version:        %BOOST_VERSION%
echo   OpenSSL Version:      %OPENSSL_VERSION%
echo   Toolchain:            %TOOLCHAIN_NAME%
echo   VS Version:           %VS_VERSION%
echo   Boost Build Threads:  %BOOST_THREADS%
echo.
echo Build Steps:
echo   1. Toolchain Setup:   %SKIP_TOOLCHAIN:~0,1%
if "%SKIP_TOOLCHAIN%"=="1" (
    echo      [SKIPPED]
) else (
    echo      [ENABLED]
)
echo   2. Boost Build:       %SKIP_BOOST:~0,1%
if "%SKIP_BOOST%"=="1" (
    echo      [SKIPPED]
) else (
    echo      [ENABLED]
)
echo   3. OpenSSL Build:     %SKIP_OPENSSL:~0,1%
if "%SKIP_OPENSSL%"=="1" (
    echo      [SKIPPED]
) else (
    echo      [ENABLED]
)
echo.
echo ================================================================================
echo.

set "START_TIME=%TIME%"

REM Step 1: Toolchain Setup
if not "!SKIP_TOOLCHAIN!"=="1" (
    echo.
    echo ################################################################################
    echo # STEP 1: Toolchain Setup
    echo ################################################################################
    echo.

    call "%SCRIPT_DIR%build-msvc-toolchain.bat" ^
        -dist-root "%DIST_ROOT%" ^
        -targets "%TARGET_ARCHS%" ^
        -git-version "%GIT_VERSION%" ^
        -python-version "%PYTHON_VERSION%" ^
        -vs-version "%VS_VERSION%"

    if errorlevel 1 (
        echo.
        echo ERROR: Toolchain setup failed!
        goto error
    )

    echo.
    echo Toolchain setup completed successfully
) else (
    echo.
    echo ################################################################################
    echo # STEP 1: Toolchain Setup [SKIPPED]
    echo ################################################################################
    echo.
)

REM Step 2: Build Boost for each architecture
if not "!SKIP_BOOST!"=="1" (
    echo.
    echo ################################################################################
    echo # STEP 2: Boost Library Build
    echo ################################################################################
    echo.

    REM Parse architectures and build for each
    set "ARCH_LIST=!TARGET_ARCHS!"
    set "ARCH_LIST=!ARCH_LIST:,= !"
    echo Target architectures ^(raw^): !TARGET_ARCHS!
    echo Target architectures ^(parsed^): !ARCH_LIST!

    for %%A in (!ARCH_LIST!) do (
        echo.
        echo ================================================================================
        echo Building Boost for architecture: %%A
        echo ================================================================================
        echo.

        call "%SCRIPT_DIR%build-boost-windows.bat" ^
            -arch "%%A" ^
            -version "%BOOST_VERSION%" ^
            -toolchain-name "%TOOLCHAIN_NAME%" ^
            -vs-version "%VS_VERSION%" ^
            -dist-root "%DIST_ROOT%" ^
            -threads "%BOOST_THREADS%"

        if errorlevel 1 (
            echo.
            echo ERROR: Boost build failed for architecture %%A!
            goto error
        )

        echo.
        echo Boost build for %%A completed successfully
    )

    echo.
    echo All Boost builds completed successfully
) else (
    echo.
    echo ################################################################################
    echo # STEP 2: Boost Library Build [SKIPPED]
    echo ################################################################################
    echo.
)

REM Step 3: Build OpenSSL for each architecture
if not "!SKIP_OPENSSL!"=="1" (
    echo.
    echo ################################################################################
    echo # STEP 3: OpenSSL Library Build
    echo ################################################################################
    echo.

    REM Parse architectures and build for each
    set "ARCH_LIST=!TARGET_ARCHS!"
    set "ARCH_LIST=!ARCH_LIST:,= !"
    echo Target architectures ^(raw^): !TARGET_ARCHS!
    echo Target architectures ^(parsed^): !ARCH_LIST!

    for %%A in (!ARCH_LIST!) do (
        echo.
        echo ================================================================================
        echo Building OpenSSL for architecture: %%A
        echo ================================================================================
        echo.

        set "TEST_ARGS="
        if "%SKIP_TESTS%"=="1" set "TEST_ARGS=-skip-tests"

        call "%SCRIPT_DIR%build-openssl-windows.bat" ^
            -arch "%%A" ^
            -version "%OPENSSL_VERSION%" ^
            -toolchain "%TOOLCHAIN_NAME%" ^
            -vs-version "%VS_VERSION%" ^
            -dist-root "%DIST_ROOT%" ^
            !TEST_ARGS!

        if errorlevel 1 (
            echo.
            echo ERROR: OpenSSL build failed for architecture %%A!
            goto error
        )

        echo.
        echo OpenSSL build for %%A completed successfully
    )

    echo.
    echo All OpenSSL builds completed successfully
) else (
    echo.
    echo ################################################################################
    echo # STEP 3: OpenSSL Library Build [SKIPPED]
    echo ################################################################################
    echo.
)

REM Calculate elapsed time
set "END_TIME=%TIME%"

echo.
echo ================================================================================
echo ================================================================================
echo.
echo            WINDOWS DEVENV7 ENVIRONMENT BUILD COMPLETE!
echo.
echo ================================================================================
echo ================================================================================
echo.
echo Build Summary:
echo   Distribution Root: %DIST_ROOT%
echo   Start Time:        %START_TIME%
echo   End Time:          %END_TIME%
echo.
echo Installed Components:
if not "!SKIP_TOOLCHAIN!"=="1" (
    echo   - Toolchain ^(Git, Python, MSYS2, Perl, JSON Spirit^)
)
if not "!SKIP_BOOST!"=="1" (
    echo   - Boost %BOOST_VERSION% libraries
)
if not "!SKIP_OPENSSL!"=="1" (
    echo   - OpenSSL %OPENSSL_VERSION% libraries
)
echo.
echo Target Architectures: %TARGET_ARCHS%
echo.
echo Next Steps:
echo   1. Review the environment initialization script:
echo      %DIST_ROOT%\scripts\ci\ci-init-env.bat
echo.
echo   2. Set up your build environment by sourcing ci-init-env.bat
echo.
echo   3. Build your project using the configured toolchain
echo.
echo For more information, see logs in:
echo   %DIST_ROOT%\..\logs\
echo.
echo ================================================================================

exit /b 0

:show_help
type "%~f0" | findstr /B "REM"
if defined HELP_EXIT_CODE exit /b 1
exit /b 0

:error
echo.
echo ================================================================================
echo BUILD FAILED!
echo ================================================================================
echo.
echo Please review the error messages above and check the log files in:
echo   %DIST_ROOT%\..\logs\
echo.
exit /b 1
