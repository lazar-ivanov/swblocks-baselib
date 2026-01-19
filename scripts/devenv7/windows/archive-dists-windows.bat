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
REM This script creates compressed archives of the distribution directory and
REM downloads cache using 7-zip with proper relative paths for easy extraction.
REM
REM Usage:
REM   archive-dists-windows.bat -dist-root <folder-name>
REM
REM Parameters:
REM   -dist-root <name>        Distribution folder name (required)
REM                            Expected location: %USERPROFILE%\swblocks\<name>
REM
REM   -delete-target-if-exists Delete existing archives if they exist
REM
REM   -help                    Show this help message
REM
REM The script will:
REM   1. Archive %USERPROFILE%\swblocks\<dist-root> to zip\<dist-root>.zip
REM   2. Archive %USERPROFILE%\swblocks\<dist-root>-downloads-cache to zip\<dist-root>-downloads-cache.zip
REM   3. Use 7-zip from <dist-root>\7zip\<version>\7za.exe
REM   4. Create archives with relative paths for extraction back to %USERPROFILE%\swblocks
REM   5. Fail if target archives exist unless -delete-target-if-exists is specified
REM
REM Examples:
REM   archive-dists-windows.bat -dist-root dist-devenv7-windows-hostarch-a64-targets-a64-x64-x86
REM   archive-dists-windows.bat -dist-root dist-devenv7-windows-hostarch-a64-targets-a64-x64-x86 -delete-target-if-exists
REM
REM Extraction:
REM   cd %USERPROFILE%\swblocks
REM   7za.exe x zip\<dist-root>.zip
REM   7za.exe x zip\<dist-root>-downloads-cache.zip
REM
REM ================================================================================

setlocal enabledelayedexpansion

REM Default parameters
set "DIST_ROOT="
set "DELETE_TARGET="
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
if /i "%~1"=="-delete-target-if-exists" (
    set "DELETE_TARGET=1"
    shift
    goto parse_args
)
if /i "%~1"=="-help" (
    goto show_help
)
echo ERROR: Unknown option: %~1
set "HELP_EXIT_CODE=1"
goto show_help

:args_done

REM Validate required parameter
if "%DIST_ROOT%"=="" (
    echo ERROR: -dist-root parameter is required
    echo.
    set "HELP_EXIT_CODE=1"
    goto show_help
)

REM Set up paths
set "SWBLOCKS_ROOT=%USERPROFILE%\swblocks"
set "DIST_FOLDER=%SWBLOCKS_ROOT%\%DIST_ROOT%"
set "CACHE_FOLDER=%SWBLOCKS_ROOT%\%DIST_ROOT%-downloads-cache"
set "OUTPUT_FOLDER=%SWBLOCKS_ROOT%\zip"

REM Verify distribution folder exists
if not exist "%DIST_FOLDER%" (
    echo ERROR: Distribution folder does not exist: %DIST_FOLDER%
    goto error
)

REM Verify downloads cache folder exists
if not exist "%CACHE_FOLDER%" (
    echo ERROR: Downloads cache folder does not exist: %CACHE_FOLDER%
    goto error
)

REM Auto-detect 7-zip version
set "SEVEN_ZIP_EXE="
for /d %%D in ("%DIST_FOLDER%\7zip\*") do (
    set "SEVEN_ZIP_VERSION=%%~nxD"
    set "SEVEN_ZIP_EXE=%%D\7za.exe"
    goto found_7zip
)

:found_7zip
if not defined SEVEN_ZIP_EXE (
    echo ERROR: 7-zip not found in %DIST_FOLDER%\7zip
    goto error
)

if not exist "%SEVEN_ZIP_EXE%" (
    echo ERROR: 7-zip executable not found: %SEVEN_ZIP_EXE%
    goto error
)

REM Create output directory if needed
if not exist "%OUTPUT_FOLDER%" (
    echo Creating output directory: %OUTPUT_FOLDER%
    mkdir "%OUTPUT_FOLDER%"
    if errorlevel 1 (
        echo ERROR: Failed to create output directory
        goto error
    )
)

REM Set archive names (using exact folder names, no timestamp)
set "DIST_ARCHIVE=%OUTPUT_FOLDER%\%DIST_ROOT%.zip"
set "CACHE_ARCHIVE=%OUTPUT_FOLDER%\%DIST_ROOT%-downloads-cache.zip"

REM Check if archives already exist
set "ARCHIVE_EXISTS="
if exist "%DIST_ARCHIVE%" set "ARCHIVE_EXISTS=1"
if exist "%CACHE_ARCHIVE%" set "ARCHIVE_EXISTS=1"

if defined ARCHIVE_EXISTS (
    if not defined DELETE_TARGET (
        echo ERROR: Target archive^(s^) already exist:
        if exist "%DIST_ARCHIVE%" echo   %DIST_ARCHIVE%
        if exist "%CACHE_ARCHIVE%" echo   %CACHE_ARCHIVE%
        echo.
        echo Use -delete-target-if-exists to overwrite existing archives
        goto error
    ) else (
        echo Deleting existing archives...
        if exist "%DIST_ARCHIVE%" (
            echo   Deleting: %DIST_ARCHIVE%
            del /f /q "%DIST_ARCHIVE%"
            if errorlevel 1 (
                echo ERROR: Failed to delete existing distribution archive
                goto error
            )
        )
        if exist "%CACHE_ARCHIVE%" (
            echo   Deleting: %CACHE_ARCHIVE%
            del /f /q "%CACHE_ARCHIVE%"
            if errorlevel 1 (
                echo ERROR: Failed to delete existing cache archive
                goto error
            )
        )
        echo.
    )
)

REM Display configuration
echo.
echo ================================================================================
echo Archive Distribution Configuration
echo ================================================================================
echo Distribution Folder:  %DIST_FOLDER%
echo Downloads Cache:      %CACHE_FOLDER%
echo Output Directory:     %OUTPUT_FOLDER%
echo 7-Zip Version:        %SEVEN_ZIP_VERSION%
echo 7-Zip Executable:     %SEVEN_ZIP_EXE%
echo Delete If Exists:     %DELETE_TARGET%
echo ================================================================================
echo.

REM Change to swblocks root to ensure proper relative paths in archives
pushd "%SWBLOCKS_ROOT%"
if errorlevel 1 (
    echo ERROR: Failed to change directory to %SWBLOCKS_ROOT%
    goto error
)

echo.
echo Creating archives with normal compression...
echo.

REM Archive 1: Distribution folder
echo ================================================================================
echo Archiving distribution folder...
echo ================================================================================
echo Source:  %DIST_ROOT%
echo Archive: %DIST_ARCHIVE%
echo.

"%SEVEN_ZIP_EXE%" a -mx=5 "%DIST_ARCHIVE%" "%DIST_ROOT%\*" -r

if errorlevel 1 (
    echo ERROR: Failed to create distribution archive
    popd
    goto error
)

echo.
echo Distribution archive created successfully
echo.

REM Archive 2: Downloads cache
echo ================================================================================
echo Archiving downloads cache...
echo ================================================================================
echo Source:  %DIST_ROOT%-downloads-cache
echo Archive: %CACHE_ARCHIVE%
echo.

"%SEVEN_ZIP_EXE%" a -mx=5 "%CACHE_ARCHIVE%" "%DIST_ROOT%-downloads-cache\*" -r

if errorlevel 1 (
    echo ERROR: Failed to create downloads cache archive
    popd
    goto error
)

echo.
echo Downloads cache archive created successfully
echo.

REM Return to original directory
popd

REM Display results
echo.
echo ================================================================================
echo Archive Creation Complete!
echo ================================================================================
echo.
echo Archive files created in: %OUTPUT_FOLDER%
echo.

if exist "%DIST_ARCHIVE%" (
    echo Distribution archive:
    echo   %DIST_ARCHIVE%
    for %%F in ("%DIST_ARCHIVE%") do (
        set /a "SIZE_MB=%%~zF / 1048576"
        echo   Size: !SIZE_MB! MB ^(%%~zF bytes^)
    )
    echo.
)

if exist "%CACHE_ARCHIVE%" (
    echo Downloads cache archive:
    echo   %CACHE_ARCHIVE%
    for %%F in ("%CACHE_ARCHIVE%") do (
        set /a "SIZE_MB=%%~zF / 1048576"
        echo   Size: !SIZE_MB! MB ^(%%~zF bytes^)
    )
    echo.
)

echo ================================================================================
echo.
echo To restore from archives:
echo   cd %USERPROFILE%\swblocks
echo   "%SEVEN_ZIP_EXE%" x "zip\%DIST_ROOT%.zip"
echo   "%SEVEN_ZIP_EXE%" x "zip\%DIST_ROOT%-downloads-cache.zip"
echo.
echo Archives contain relative paths and will extract to the correct locations.
echo.

exit /b 0

:show_help
type "%~f0" | findstr /B "REM"
if defined HELP_EXIT_CODE exit /b 1
exit /b 0

:error
exit /b 1
