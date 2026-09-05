@echo off
echo Running all unit tests...
echo.

call test-processor-detection.bat
if %errorlevel% neq 0 (
    echo FAILED: test-processor-detection.bat
    exit /b 1
)
echo.

call test-registry-compatibility.bat
if %errorlevel% neq 0 (
    echo FAILED: test-registry-compatibility.bat
    exit /b 1
)
echo.

call test-perl-version-detection.bat
if %errorlevel% neq 0 (
    echo FAILED: test-perl-version-detection.bat
    exit /b 1
)
echo.

echo ============================================================================
echo ALL TESTS PASSED
echo ============================================================================
exit /b 0
