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
REM Boost Build Script for Windows devenv7
REM ================================================================================
REM
REM This script builds Boost libraries for Windows using the MSVC toolchain from
REM the devenv7 distribution.
REM
REM Usage:
REM   build-boost-windows.bat [options]
REM
REM Options:
REM   -arch <architecture>     Target architecture: a64, x64, or x86
REM                            Note: arm64 accepted as alias for a64
REM                            Default: a64
REM
REM   -version <version>       Boost version to build
REM                            Default: 1.90.0
REM
REM   -toolchain-name <name>   Toolchain identifier (e.g., vc143)
REM                            Default: vc143
REM
REM   -vs-version <version>    Visual Studio version (e.g., 2022)
REM                            Default: 2022
REM
REM   -dist-root <path>        Distribution root directory
REM                            Default: %USERPROFILE%\swblocks\dist-devenv7-windows-a64
REM
REM   -threads <count>         Number of parallel build threads
REM                            Default: 8
REM
REM   -help                    Show this help message
REM
REM Examples:
REM   build-boost-windows.bat
REM   build-boost-windows.bat -arch x64
REM   build-boost-windows.bat -arch arm64 -version 1.90.0
REM   build-boost-windows.bat -dist-root C:\mydev\toolchain
REM
REM ================================================================================

setlocal enabledelayedexpansion

REM Default parameters
set "ARCH=a64"
set "BOOST_VERSION=1.90.0"
set "TOOLCHAIN_NAME=vc143"
set "VS_VERSION=2022"
set "DIST_ROOT=%USERPROFILE%\swblocks\dist-devenv7-windows-a64"
set "BOOST_BUILD_THREADS=8"
set "DEVENV_TAG=devenv7"
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
    set "BOOST_VERSION=%~2"
    shift
    shift
    goto parse_args
)
if /i "%~1"=="-toolchain-name" (
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
if /i "%~1"=="-threads" (
    set "BOOST_BUILD_THREADS=%~2"
    shift
    shift
    goto parse_args
)
if /i "%~1"=="-help" (
    goto show_help
)
echo ERROR: Unknown option: %~1
echo.
echo NOTE: This script builds Boost for ONE architecture at a time.
echo       To build multiple architectures, use build-env-all-windows.bat
echo       or call this script multiple times.
echo.
echo       Example: build-boost-windows.bat -arch a64
echo       NOT:     build-boost-windows.bat -arch a64,x64,x86
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
if /i "%ARCH%"=="X86" set "ARCH_LOWER=x86"

if "%ARCH_LOWER%"=="a64" (
    set "ARCH=a64"
    set "ADDRESS_MODEL=64"
    set "ARCH_TAG=a64"
    set "BOOST_ARCHITECTURE=arm"
) else if "%ARCH_LOWER%"=="x64" (
    set "ARCH=x64"
    set "ADDRESS_MODEL=64"
    set "ARCH_TAG=x64"
    set "BOOST_ARCHITECTURE=x86"
) else if "%ARCH_LOWER%"=="x86" (
    set "ARCH=x86"
    set "ADDRESS_MODEL=32"
    set "ARCH_TAG=x86"
    set "BOOST_ARCHITECTURE=x86"
) else (
    echo ERROR: Unsupported architecture: %ARCH%
    echo Supported architectures: a64, x64, x86 ^(arm64 accepted as alias for a64^)
    goto error
)

REM Display configuration
echo ================================================================================
echo Boost Build Configuration
echo ================================================================================
echo Architecture:       %ARCH%
echo Boost Version:      %BOOST_VERSION%
echo Toolchain:          %TOOLCHAIN_NAME%
echo VS Version:         %VS_VERSION%
echo Distribution Root:  %DIST_ROOT%
echo Build Threads:      %BOOST_BUILD_THREADS%
echo devenv Tag:         %DEVENV_TAG%
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

REM Set paths
set "BOOST_SOURCE_PATH=%DIST_ROOT_DEPS1%\boost\%BOOST_VERSION%\source-windows"

echo Boost source path: %BOOST_SOURCE_PATH%

REM Download Boost source if it doesn't exist
if not exist "%BOOST_SOURCE_PATH%\boost\version.hpp" (
    echo.
    echo Boost source not found at %BOOST_SOURCE_PATH%
    echo Downloading Boost %BOOST_VERSION% automatically...
    echo.

    REM Get script directory for PowerShell modules
    set "SCRIPT_DIR=%~dp0"

    powershell -NoProfile -ExecutionPolicy Bypass -Command "& { Import-Module '%SCRIPT_DIR%internal\common.ps1' -Force; Import-Module '%SCRIPT_DIR%internal\download-sources.ps1' -Force; Install-BoostSource -Version '%BOOST_VERSION%' -DestinationRoot '%DIST_ROOT_DEPS1%' }"

    if errorlevel 1 (
        echo ERROR: Failed to download Boost source
        goto error
    )

    echo.
    echo Boost source downloaded successfully
    echo.
)

REM Verify Boost source exists
if not exist "%BOOST_SOURCE_PATH%\boost\version.hpp" (
    echo ERROR: Boost source verification failed
    echo Expected file not found: %BOOST_SOURCE_PATH%\boost\version.hpp
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
echo Boost Build Successful!
echo ================================================================================
echo.
echo Debug installation location:
echo   %DIST_ROOT_DEPS1%\boost\%BOOST_VERSION%\win-%ARCH_TAG%-%TOOLCHAIN_NAME%-debug
echo.
echo Release installation location:
echo   %DIST_ROOT_DEPS1%\boost\%BOOST_VERSION%\win-%ARCH_TAG%-%TOOLCHAIN_NAME%-release
echo.
echo ================================================================================

exit /b 0

REM =============================================================================
REM Build a specific variant (debug or release)
REM =============================================================================
:build_variant
setlocal
set "BUILD_TYPE=%~1"

echo.
echo ================================================================================
echo Building Boost %BOOST_VERSION% - %BUILD_TYPE% variant
echo ================================================================================
echo.

REM Build directory (in swblocks bld folder, parallel to dist)
set "BOOST_ROOT_PATH=%BLD_ROOT%\boost\%BOOST_VERSION%\win-%ARCH_TAG%-%TOOLCHAIN_NAME%-%BUILD_TYPE%"

REM Clean existing build
if exist "%BOOST_ROOT_PATH%" (
    echo Deleting existing build directory: %BOOST_ROOT_PATH%
    rd /s /q "%BOOST_ROOT_PATH%"
    if errorlevel 1 (
        echo ERROR: Failed to delete existing build directory
        exit /b 1
    )
)

REM Create build directory
echo Creating build directory: %BOOST_ROOT_PATH%
mkdir "%BOOST_ROOT_PATH%"
if errorlevel 1 (
    echo ERROR: Failed to create build directory
    exit /b 1
)

REM Copy source to build directory
echo Copying Boost source from %BOOST_SOURCE_PATH%...
echo To %BOOST_ROOT_PATH%...
robocopy /E "%BOOST_SOURCE_PATH%" "%BOOST_ROOT_PATH%" /NFL /NDL /NJH /NJS > nul 2>&1
if errorlevel 8 (
    echo ERROR: Failed to copy Boost source
    exit /b 1
)

REM Change to build directory
pushd "%BOOST_ROOT_PATH%"

REM Bootstrap b2 if needed
if not exist ".\b2.exe" (
    echo.
    echo Bootstrapping Boost.Build...
    call bootstrap.bat msvc
    if errorlevel 1 (
        echo ERROR: Bootstrap failed
        popd
        exit /b 1
    )
    echo Bootstrap completed successfully
)

echo.
echo ================================================================================
echo Building Boost Libraries (%BUILD_TYPE%)
echo ================================================================================
echo.
echo This may take 30-60 minutes depending on your system...
echo.

REM Set variant-specific build flags
if "%BUILD_TYPE%"=="debug" (
    set "VARIANT_FLAG=debug"
) else (
    set "VARIANT_FLAG=release"
)

REM Build with specific variant only - building only the libraries needed by swblocks-baselib
REM This matches the libraries built in the Linux script
REM
REM Important build settings:
REM   link=static              - Build only static libraries (.lib files)
REM   runtime-link=static      - Link statically to the C/C++ runtime (no debug runtime)
REM   runtime-debugging=off    - Do not use debug runtime even for debug builds
REM   debug-symbols=on         - Generate PDB files for both debug and release
REM   --layout=tagged          - Use tagged library names (includes -mt, -s, etc.)
REM                             This helps identify library variants by their filename
REM   architecture=arm         - Specify ARM architecture for library naming (-a64 suffix)
REM                             Must be set explicitly for ARM64 builds to generate proper tags
REM   --no-cmake-config        - Disable CMake config file generation (Windows-specific issue)
REM                             CMake variant files fail to generate on Windows due to temp file
REM                             access issues in Boost.Build's print.jam module
REM
.\b2 ^
    --abbreviate-paths ^
    --stagedir=.\bld\stage ^
    --build-dir=.\bld\int ^
    -j%BOOST_BUILD_THREADS% ^
    --layout=tagged ^
    --no-cmake-config ^
    toolset=msvc ^
    variant=%VARIANT_FLAG% ^
    link=static ^
    runtime-link=static ^
    threading=multi ^
    exception-handling=on ^
    asynch-exceptions=off ^
    extern-c-nothrow=off ^
    debug-symbols=on ^
    runtime-debugging=off ^
    embed-manifest=on ^
    cflags="-bigobj -GS -Oy- -D_SECURE_SCL=0 -D_WIN32_WINNT=0x0601" ^
    linkflags="-subsystem:console -incremental:no -opt:icf -opt:ref" ^
    address-model=%ADDRESS_MODEL% ^
    architecture=%BOOST_ARCHITECTURE% ^
    --with-date_time ^
    --with-system ^
    --with-thread ^
    --with-filesystem ^
    --with-program_options ^
    --with-regex ^
    --with-random ^
    --with-test ^
    --with-locale ^
    --with-json ^
    stage > log_boost_build.log 2>&1

if errorlevel 1 (
    echo ERROR: Boost build failed, see log: %BOOST_ROOT_PATH%\log_boost_build.log
    echo.
    echo Last 50 lines of log:
    powershell -Command "Get-Content log_boost_build.log -Tail 50"
    exit /b 1
)

echo Boost build completed successfully

REM Organize installation
echo.
echo Organizing Boost installation...

REM Create installation structure
set "BOOST_TARGET_PATH=%DIST_ROOT_DEPS1%\boost\%BOOST_VERSION%\win-%ARCH_TAG%-%TOOLCHAIN_NAME%-%BUILD_TYPE%"

if exist "%BOOST_TARGET_PATH%" (
    echo Deleting existing installation: %BOOST_TARGET_PATH%
    rd /s /q "%BOOST_TARGET_PATH%"
    if errorlevel 1 (
        echo ERROR: Failed to delete existing installation
        exit /b 1
    )
)

echo Creating installation directory structure...
mkdir "%BOOST_TARGET_PATH%\include"
if errorlevel 1 exit /b 1
mkdir "%BOOST_TARGET_PATH%\lib"
if errorlevel 1 exit /b 1

REM Copy headers (boost directory from source)
echo Copying Boost headers...
robocopy /E "boost" "%BOOST_TARGET_PATH%\include\boost" > log_headers_copy.log 2>&1
if errorlevel 8 (
    echo ERROR: Failed to copy headers
    exit /b 1
)

REM Copy libraries from stage
echo Copying Boost libraries...
robocopy ".\bld\stage\lib" "%BOOST_TARGET_PATH%\lib" *.lib > log_libs_copy.log 2>&1
if errorlevel 8 (
    echo ERROR: Failed to copy libraries
    exit /b 1
)

REM Copy PDB files from intermediate build directory
echo Copying PDB files...
if exist ".\bld\int" (
    for /r ".\bld\int" %%i in (*.pdb) do (
        copy "%%i" "%BOOST_TARGET_PATH%\lib\" >nul 2>&1
    )
)

REM Clean intermediate directory
echo Cleaning intermediate build files...
rd /s /q ".\bld\int" 2>nul

REM Move log files to destination
move log_* "%BOOST_TARGET_PATH%" >nul 2>&1

REM Clean up build directory with retry logic
popd
echo Cleaning up build directory...

REM Try to delete with retries (files may be locked by antivirus/indexer)
set "DELETE_RETRIES=3"
set "DELETE_SUCCESS=0"

:retry_delete
rd /s /q "%BOOST_ROOT_PATH%" 2>nul
if not exist "%BOOST_ROOT_PATH%" (
    set "DELETE_SUCCESS=1"
    goto delete_done
)

set /a DELETE_RETRIES-=1
if %DELETE_RETRIES% GTR 0 (
    echo WARNING: Build directory still in use, retrying in 2 seconds... ^(%DELETE_RETRIES% attempts remaining^)
    timeout /t 2 /nobreak >nul 2>&1
    goto retry_delete
)

:delete_done
if "%DELETE_SUCCESS%"=="1" (
    echo Build directory cleaned up successfully
) else (
    echo WARNING: Could not fully delete build directory ^(files may be locked by antivirus/indexer^)
    echo You may need to manually delete: %BOOST_ROOT_PATH%
)

echo.
echo %BUILD_TYPE% build completed successfully
echo Installation: %BOOST_TARGET_PATH%
echo.

endlocal
exit /b 0

:show_help
type "%~f0" | findstr /B "REM"
if defined HELP_EXIT_CODE exit /b 1
exit /b 0

:error
exit /b 1
