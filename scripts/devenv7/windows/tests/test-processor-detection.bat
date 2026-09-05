@echo off
setlocal enabledelayedexpansion

echo ============================================================================
echo Testing Processor Architecture Detection
echo ============================================================================

REM Priority 1: PROCESSOR_IDENTIFIER (ARMv8 check)
set "REAL_PROCESSOR_ARCH="
echo Checking PROCESSOR_IDENTIFIER: %PROCESSOR_IDENTIFIER%

echo %PROCESSOR_IDENTIFIER% | findstr /i "ARMv8" >nul
if %errorlevel% equ 0 (
    set "REAL_PROCESSOR_ARCH=ARM64"
    echo   Result: Detected ARM64 via ARMv8 in PROCESSOR_IDENTIFIER
    goto detected_processor_arch
)

REM Priority 1b: PROCESSOR_IDENTIFIER (AArch64 check)
echo %PROCESSOR_IDENTIFIER% | findstr /i "AArch64" >nul
if %errorlevel% equ 0 (
    set "REAL_PROCESSOR_ARCH=ARM64"
    echo   Result: Detected ARM64 via AArch64 in PROCESSOR_IDENTIFIER
    goto detected_processor_arch
)

REM Priority 2: PROCESSOR_ARCHITECTURE
echo Checking PROCESSOR_ARCHITECTURE: %PROCESSOR_ARCHITECTURE%
if /i "%PROCESSOR_ARCHITECTURE%"=="ARM64" (
    set "REAL_PROCESSOR_ARCH=ARM64"
    echo   Result: Detected ARM64 via PROCESSOR_ARCHITECTURE
    goto detected_processor_arch
)

REM Priority 3: PROCESSOR_ARCHITEW6432
echo Checking PROCESSOR_ARCHITEW6432: %PROCESSOR_ARCHITEW6432%
if /i "%PROCESSOR_ARCHITEW6432%"=="ARM64" (
    set "REAL_PROCESSOR_ARCH=ARM64"
    echo   Result: Detected ARM64 via PROCESSOR_ARCHITEW6432 (emulation mode)
    goto detected_processor_arch
)

REM Priority 4: Fallback
if /i "%PROCESSOR_ARCHITECTURE%"=="AMD64" (
    set "REAL_PROCESSOR_ARCH=x64"
    echo   Result: Detected x64 via PROCESSOR_ARCHITECTURE (fallback)
) else if /i "%PROCESSOR_ARCHITECTURE%"=="x86" (
    set "REAL_PROCESSOR_ARCH=x86"
    echo   Result: Detected x86 via PROCESSOR_ARCHITECTURE (fallback)
) else (
    set "REAL_PROCESSOR_ARCH=x64"
    echo   Result: Unknown PROCESSOR_ARCHITECTURE, defaulting to x64
)

:detected_processor_arch
echo.
echo Final Detection Result: %REAL_PROCESSOR_ARCH%
echo.
echo Test PASSED: Processor architecture detected successfully
exit /b 0
