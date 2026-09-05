@echo off
setlocal enabledelayedexpansion

echo ============================================================================
echo Testing Perl Version Detection
echo ============================================================================

REM Set dist root (user should adjust this to their environment)
if "%DIST_ROOT_DEPS1%"=="" (
    echo ERROR: DIST_ROOT_DEPS1 environment variable not set
    echo Please run this from a build environment or set DIST_ROOT_DEPS1 manually
    exit /b 1
)

echo DIST_ROOT_DEPS1: %DIST_ROOT_DEPS1%

REM Detect Perl version
REM Trim trailing whitespace from DIST_ROOT_DEPS1 (common issue when variable is set manually)
set "DIST_ROOT=%DIST_ROOT_DEPS1%"
:trim_dist_root
if "%DIST_ROOT:~-1%"==" " (
    set "DIST_ROOT=%DIST_ROOT:~0,-1%"
    goto trim_dist_root
)

echo.
echo Detecting Perl version...
for /d %%P in ("%DIST_ROOT%\strawberry-perl\*") do (
    if exist "%%P\default\perl\bin\perl.exe" (
        set "PERL_VERSION=%%~nxP"
        echo   Found Perl version: !PERL_VERSION!
        goto found_perl
    )
)

echo ERROR: Perl not found
exit /b 1

:found_perl
echo.
echo Detecting versioned Perl executables (perl5.*.exe)...
set "FOUND_COUNT=0"
for %%F in ("%DIST_ROOT%\strawberry-perl\%PERL_VERSION%\default\perl\bin\perl5.*.exe") do (
    echo   Found: %%~nxF at %%F
    set /a FOUND_COUNT+=1
)

if %FOUND_COUNT% equ 0 (
    echo WARNING: No versioned Perl executables found (perl5.*.exe pattern)
    echo This is unexpected but may be OK for newer Perl versions
) else (
    echo   Total versioned executables found: %FOUND_COUNT%
)

echo.
echo Test PASSED: Perl version detection completed
exit /b 0
