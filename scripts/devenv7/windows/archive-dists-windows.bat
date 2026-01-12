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
REM Archive Distribution Script for Windows devenv7
REM ================================================================================
REM
REM This script creates compressed archives of the built distribution directories
REM for easy distribution and backup
REM
REM Usage:
REM   archive-dists-windows.bat [options]
REM
REM Options:
REM   -dist-root <path>        Distribution root directory
REM                            Default: %USERPROFILE%\swblocks\dist-devenv7-windows-a64
REM
REM   -output-dir <path>       Output directory for archives
REM                            Default: %USERPROFILE%\swblocks\archives
REM
REM   -archive-name <name>     Base name for archive files
REM                            Default: dist-devenv7-windows-a64
REM
REM   -include-toolchain       Include toolchain (VS, SDK) in archive
REM
REM   -include-sources         Include source files in archive
REM
REM   -help                    Show this help message
REM
REM Examples:
REM   archive-dists-windows.bat
REM   archive-dists-windows.bat -include-toolchain
REM   archive-dists-windows.bat -dist-root C:\mydev\toolchain -output-dir D:\backups
REM
REM ================================================================================

setlocal enabledelayedexpansion

REM Default parameters
set "DIST_ROOT=%USERPROFILE%\swblocks\dist-devenv7-windows-a64"
set "OUTPUT_DIR=%USERPROFILE%\swblocks\archives"
set "ARCHIVE_NAME=dist-devenv7-windows-a64"
set "INCLUDE_TOOLCHAIN="
set "INCLUDE_SOURCES="
set "HELP_EXIT_CODE="

REM Parse command line arguments
:parse_args
if "%~1"=="" goto args_done
if /i "%~1"=="-dist-root" (
    set "DIST_ROOT=%~2"
    shift
    shift
    goto parse_args
)
if /i "%~1"=="-output-dir" (
    set "OUTPUT_DIR=%~2"
    shift
    shift
    goto parse_args
)
if /i "%~1"=="-archive-name" (
    set "ARCHIVE_NAME=%~2"
    shift
    shift
    goto parse_args
)
if /i "%~1"=="-include-toolchain" (
    set "INCLUDE_TOOLCHAIN=1"
    shift
    goto parse_args
)
if /i "%~1"=="-include-sources" (
    set "INCLUDE_SOURCES=1"
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

REM Verify distribution root exists
if not exist "%DIST_ROOT%" (
    echo ERROR: Distribution root does not exist: %DIST_ROOT%
    goto error
)

REM Create output directory
if not exist "%OUTPUT_DIR%" (
    echo Creating output directory: %OUTPUT_DIR%
    mkdir "%OUTPUT_DIR%"
    if errorlevel 1 (
        echo ERROR: Failed to create output directory
        goto error
    )
)

REM Display configuration
echo.
echo ================================================================================
echo Archive Distribution Configuration
echo ================================================================================
echo Distribution Root:  %DIST_ROOT%
echo Output Directory:   %OUTPUT_DIR%
echo Archive Base Name:  %ARCHIVE_NAME%
echo Include Toolchain:  %INCLUDE_TOOLCHAIN%
echo Include Sources:    %INCLUDE_SOURCES%
echo ================================================================================
echo.

REM Generate timestamp for archive name
for /f "tokens=1-4 delims=/-. " %%a in ('date /t') do (
    set "DATE_STAMP=%%c%%a%%b"
)
for /f "tokens=1-2 delims=:. " %%a in ('time /t') do (
    set "TIME_STAMP=%%a%%b"
)
set "TIMESTAMP=%DATE_STAMP%-%TIME_STAMP%"

REM Create archive using PowerShell (built-in ZIP support)
echo.
echo Creating archives...
echo.

REM Archive 1: Essential runtime libraries (Boost, OpenSSL)
set "ARCHIVE_LIBS=%OUTPUT_DIR%\%ARCHIVE_NAME%-libs-%TIMESTAMP%.zip"
echo Creating libraries archive: %ARCHIVE_LIBS%

powershell -Command "& { ^
    $ProgressPreference = 'SilentlyContinue'; ^
    $items = @(); ^
    if (Test-Path '%DIST_ROOT%\boost') { $items += Get-Item '%DIST_ROOT%\boost' }; ^
    if (Test-Path '%DIST_ROOT%\openssl') { $items += Get-Item '%DIST_ROOT%\openssl' }; ^
    if (Test-Path '%DIST_ROOT%\json-spirit') { $items += Get-Item '%DIST_ROOT%\json-spirit' }; ^
    if ($items.Count -gt 0) { ^
        Compress-Archive -Path $items -DestinationPath '%ARCHIVE_LIBS%' -CompressionLevel Optimal -Force; ^
        Write-Host 'Libraries archive created successfully'; ^
    } else { ^
        Write-Host 'Warning: No library directories found to archive' -ForegroundColor Yellow; ^
    } ^
}"

if errorlevel 1 (
    echo WARNING: Failed to create libraries archive
)

REM Archive 2: Development tools (Git, Python, MSYS2, Perl)
set "ARCHIVE_TOOLS=%OUTPUT_DIR%\%ARCHIVE_NAME%-tools-%TIMESTAMP%.zip"
echo.
echo Creating tools archive: %ARCHIVE_TOOLS%

powershell -Command "& { ^
    $ProgressPreference = 'SilentlyContinue'; ^
    $items = @(); ^
    if (Test-Path '%DIST_ROOT%\git') { $items += Get-Item '%DIST_ROOT%\git' }; ^
    if (Test-Path '%DIST_ROOT%\python') { $items += Get-Item '%DIST_ROOT%\python' }; ^
    if (Test-Path '%DIST_ROOT%\msys2') { $items += Get-Item '%DIST_ROOT%\msys2' }; ^
    if (Test-Path '%DIST_ROOT%\strawberry-perl') { $items += Get-Item '%DIST_ROOT%\strawberry-perl' }; ^
    if ($items.Count -gt 0) { ^
        Compress-Archive -Path $items -DestinationPath '%ARCHIVE_TOOLS%' -CompressionLevel Optimal -Force; ^
        Write-Host 'Tools archive created successfully'; ^
    } else { ^
        Write-Host 'Warning: No tool directories found to archive' -ForegroundColor Yellow; ^
    } ^
}"

if errorlevel 1 (
    echo WARNING: Failed to create tools archive
)

REM Archive 3: Configuration and scripts
set "ARCHIVE_CONFIG=%OUTPUT_DIR%\%ARCHIVE_NAME%-config-%TIMESTAMP%.zip"
echo.
echo Creating configuration archive: %ARCHIVE_CONFIG%

powershell -Command "& { ^
    $ProgressPreference = 'SilentlyContinue'; ^
    $items = @(); ^
    if (Test-Path '%DIST_ROOT%\scripts') { $items += Get-Item '%DIST_ROOT%\scripts' }; ^
    if (Test-Path '%DIST_ROOT%\projects') { $items += Get-Item '%DIST_ROOT%\projects' }; ^
    if ($items.Count -gt 0) { ^
        Compress-Archive -Path $items -DestinationPath '%ARCHIVE_CONFIG%' -CompressionLevel Optimal -Force; ^
        Write-Host 'Configuration archive created successfully'; ^
    } else { ^
        Write-Host 'Warning: No configuration directories found to archive' -ForegroundColor Yellow; ^
    } ^
}"

if errorlevel 1 (
    echo WARNING: Failed to create configuration archive
)

REM Archive 4: Toolchain (optional - VS and SDK, usually large)
if "%INCLUDE_TOOLCHAIN%"=="1" (
    set "ARCHIVE_TOOLCHAIN=%OUTPUT_DIR%\%ARCHIVE_NAME%-toolchain-%TIMESTAMP%.zip"
    echo.
    echo Creating toolchain archive: !ARCHIVE_TOOLCHAIN!
    echo Warning: This may be very large and take significant time...

    powershell -Command "& { ^
        $ProgressPreference = 'SilentlyContinue'; ^
        $items = @(); ^
        if (Test-Path '%DIST_ROOT%\toolchain-msvc') { $items += Get-Item '%DIST_ROOT%\toolchain-msvc' }; ^
        if (Test-Path '%DIST_ROOT%\winsdk') { $items += Get-Item '%DIST_ROOT%\winsdk' }; ^
        if ($items.Count -gt 0) { ^
            Compress-Archive -Path $items -DestinationPath '!ARCHIVE_TOOLCHAIN!' -CompressionLevel Optimal -Force; ^
            Write-Host 'Toolchain archive created successfully'; ^
        } else { ^
            Write-Host 'Warning: No toolchain directories found to archive' -ForegroundColor Yellow; ^
        } ^
    }"

    if errorlevel 1 (
        echo WARNING: Failed to create toolchain archive
    )
)

REM Archive 5: Source files (optional)
if "%INCLUDE_SOURCES%"=="1" (
    set "ARCHIVE_SOURCES=%OUTPUT_DIR%\%ARCHIVE_NAME%-sources-%TIMESTAMP%.zip"
    echo.
    echo Creating sources archive: !ARCHIVE_SOURCES!

    powershell -Command "& { ^
        $ProgressPreference = 'SilentlyContinue'; ^
        $items = @(); ^
        if (Test-Path '%DIST_ROOT%\boost\*\source-windows') { $items += Get-Item '%DIST_ROOT%\boost\*\source-windows' }; ^
        if (Test-Path '%DIST_ROOT%\openssl\*\source-windows') { $items += Get-Item '%DIST_ROOT%\openssl\*\source-windows' }; ^
        if ($items.Count -gt 0) { ^
            Compress-Archive -Path $items -DestinationPath '!ARCHIVE_SOURCES!' -CompressionLevel Optimal -Force; ^
            Write-Host 'Sources archive created successfully'; ^
        } else { ^
            Write-Host 'Warning: No source directories found to archive' -ForegroundColor Yellow; ^
        } ^
    }"

    if errorlevel 1 (
        echo WARNING: Failed to create sources archive
    )
)

REM Display archive information
echo.
echo ================================================================================
echo Archive Creation Complete!
echo ================================================================================
echo.
echo Archive files created in: %OUTPUT_DIR%
echo.

if exist "%ARCHIVE_LIBS%" (
    echo Libraries archive:
    echo   %ARCHIVE_LIBS%
    for %%F in ("%ARCHIVE_LIBS%") do echo   Size: %%~zF bytes
    echo.
)

if exist "%ARCHIVE_TOOLS%" (
    echo Tools archive:
    echo   %ARCHIVE_TOOLS%
    for %%F in ("%ARCHIVE_TOOLS%") do echo   Size: %%~zF bytes
    echo.
)

if exist "%ARCHIVE_CONFIG%" (
    echo Configuration archive:
    echo   %ARCHIVE_CONFIG%
    for %%F in ("%ARCHIVE_CONFIG%") do echo   Size: %%~zF bytes
    echo.
)

if "%INCLUDE_TOOLCHAIN%"=="1" (
    if exist "%ARCHIVE_TOOLCHAIN%" (
        echo Toolchain archive:
        echo   %ARCHIVE_TOOLCHAIN%
        for %%F in ("%ARCHIVE_TOOLCHAIN%") do echo   Size: %%~zF bytes
        echo.
    )
)

if "%INCLUDE_SOURCES%"=="1" (
    if exist "%ARCHIVE_SOURCES%" (
        echo Sources archive:
        echo   %ARCHIVE_SOURCES%
        for %%F in ("%ARCHIVE_SOURCES%") do echo   Size: %%~zF bytes
        echo.
    )
)

echo ================================================================================
echo.
echo To restore from archives:
echo   1. Extract archives to desired location
echo   2. Update paths in scripts\ci\ci-init-env.bat
echo   3. Update paths in projects\make\ci-init-env.mk
echo.

exit /b 0

:show_help
type "%~f0" | findstr /B "REM"
if defined HELP_EXIT_CODE exit /b 1
exit /b 0

:error
exit /b 1
