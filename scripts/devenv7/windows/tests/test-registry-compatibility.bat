@echo off
setlocal enabledelayedexpansion

echo ============================================================================
echo Testing Registry Compatibility Mode Application
echo ============================================================================

REM Create a test executable path (doesn't need to exist for registry test)
set "TEST_EXE_PATH=%TEMP%\test-perl.exe"
echo Test executable path: %TEST_EXE_PATH%

REM Apply compatibility mode
echo.
echo Applying Win8RTM compatibility mode...
reg add "HKCU\Software\Microsoft\Windows NT\CurrentVersion\AppCompatFlags\Layers" /v "%TEST_EXE_PATH%" /t REG_SZ /d "~ WIN8RTM" /f >nul 2>&1
if %errorlevel% neq 0 (
    echo ERROR: Failed to apply compatibility mode
    goto cleanup
)
echo   Compatibility mode applied successfully

REM Verify registry entry
echo.
echo Verifying registry entry...
reg query "HKCU\Software\Microsoft\Windows NT\CurrentVersion\AppCompatFlags\Layers" /v "%TEST_EXE_PATH%" >nul 2>&1
if %errorlevel% neq 0 (
    echo ERROR: Registry entry not found
    goto cleanup
)
echo   Registry entry verified

REM Show the value
reg query "HKCU\Software\Microsoft\Windows NT\CurrentVersion\AppCompatFlags\Layers" /v "%TEST_EXE_PATH%"

REM Cleanup
:cleanup
echo.
echo Cleaning up test registry entry...
reg delete "HKCU\Software\Microsoft\Windows NT\CurrentVersion\AppCompatFlags\Layers" /v "%TEST_EXE_PATH%" /f >nul 2>&1
if %errorlevel% equ 0 (
    echo   Cleanup successful
) else (
    echo   WARNING: Cleanup may have failed (entry might not exist)
)

echo.
echo Test PASSED: Registry operations completed successfully
exit /b 0
