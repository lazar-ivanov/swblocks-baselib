<#
.SYNOPSIS
    Validates all paths and environment variables configured in setup-env-*.bat scripts

.DESCRIPTION
    This script performs comprehensive validation of the development environment setup
    by testing all PATH components, INCLUDE paths, LIB paths, and critical executables
    configured in the generated setup-env-*.bat scripts.

    This allows quick validation of toolchain setup without running full builds.

.PARAMETER Architecture
    Architecture to test (a64, x64, x86). If not specified, tests all found architectures.

.PARAMETER DistRoot
    Distribution root directory. Defaults to detecting from environment or using standard location.

.PARAMETER SkipToolChecks
    Skip checking for executable availability (only validate paths exist)

.EXAMPLE
    .\test-setup-env-paths.ps1
    Tests all available architectures

.EXAMPLE
    .\test-setup-env-paths.ps1 -Architecture a64
    Tests only ARM64 environment

.EXAMPLE
    .\test-setup-env-paths.ps1 -DistRoot C:\custom\dist -SkipToolChecks
    Tests with custom dist root, skipping executable checks
#>

param(
    [Parameter(Mandatory=$false)]
    [ValidateSet("a64", "x64", "x86", "All")]
    [string]$Architecture = "All",

    [Parameter(Mandatory=$false)]
    [string]$DistRoot = $null,

    [Parameter(Mandatory=$false)]
    [switch]$SkipToolChecks
)

$ErrorActionPreference = "Stop"

# Test counters
$script:TestsPassed = 0
$script:TestsFailed = 0
$script:TestsSkipped = 0

# ============================================================================
# Helper Functions
# ============================================================================

function Write-TestHeader {
    param([string]$Title)
    Write-Host "`n================================================================" -ForegroundColor Cyan
    Write-Host "  $Title" -ForegroundColor Cyan
    Write-Host "================================================================" -ForegroundColor Cyan
}

function Write-TestResult {
    param(
        [string]$TestName,
        [bool]$Passed,
        [string]$Message = "",
        [bool]$Skipped = $false
    )

    if ($Skipped) {
        $script:TestsSkipped++
        Write-Host "[SKIP] " -ForegroundColor Yellow -NoNewline
        Write-Host "$TestName" -ForegroundColor Gray
        if ($Message) {
            Write-Host "       $Message" -ForegroundColor Gray
        }
    } elseif ($Passed) {
        $script:TestsPassed++
        Write-Host "[PASS] " -ForegroundColor Green -NoNewline
        Write-Host $TestName
        if ($Message) {
            Write-Host "       $Message" -ForegroundColor Gray
        }
    } else {
        $script:TestsFailed++
        Write-Host "[FAIL] " -ForegroundColor Red -NoNewline
        Write-Host $TestName
        if ($Message) {
            Write-Host "       $Message" -ForegroundColor Yellow
        }
    }
}

function Test-PathExists {
    param(
        [string]$Path,
        [string]$Description
    )

    # Expand environment variables
    $expandedPath = [System.Environment]::ExpandEnvironmentVariables($Path)

    if (Test-Path $expandedPath) {
        Write-TestResult -TestName $Description -Passed $true -Message $expandedPath
        return $true
    } else {
        Write-TestResult -TestName $Description -Passed $false -Message "Path not found: $expandedPath"
        return $false
    }
}

function Test-ExecutableExists {
    param(
        [string]$ExecutableName,
        [string]$Description
    )

    if ($SkipToolChecks) {
        Write-TestResult -TestName $Description -Skipped $true -Message "Tool checks disabled"
        return $true
    }

    $found = Get-Command $ExecutableName -ErrorAction SilentlyContinue
    if ($found) {
        Write-TestResult -TestName $Description -Passed $true -Message $found.Source
        return $true
    } else {
        Write-TestResult -TestName $Description -Passed $false -Message "$ExecutableName not found in PATH"
        return $false
    }
}

function Get-SetupEnvScript {
    param([string]$Arch)

    $scriptPath = Join-Path $DistRoot "scripts\ci\setup-env-$Arch.bat"
    if (-not (Test-Path $scriptPath)) {
        return $null
    }
    return $scriptPath
}

# ============================================================================
# Main Test Functions
# ============================================================================

function Test-EnvironmentVariables {
    param([string]$Arch)

    Write-TestHeader "Testing Environment Variables for $Arch"

    # Required variables
    $requiredVars = @(
        "DIST_ROOT_DEPS1",
        "DIST_ROOT_DEPS2",
        "DIST_ROOT_DEPS3",
        "MSVC_ROOT",
        "WINSDK_ROOT",
        "LLVM_ROOT",
        "MSVC_VERSION",
        "WINSDK_VERSION",
        "TOOLCHAIN_NAME"
    )

    foreach ($var in $requiredVars) {
        $value = [System.Environment]::GetEnvironmentVariable($var)
        if ($value) {
            Write-TestResult -TestName "$var is set" -Passed $true -Message $value
        } else {
            Write-TestResult -TestName "$var is set" -Passed $false -Message "Variable not defined"
        }
    }

    # Architecture-specific variables
    $platform = [System.Environment]::GetEnvironmentVariable("Platform")
    $tgtArch = [System.Environment]::GetEnvironmentVariable("VSCMD_ARG_TGT_ARCH")
    $hostArch = [System.Environment]::GetEnvironmentVariable("VSCMD_ARG_HOST_ARCH")

    if ($platform) {
        Write-TestResult -TestName "Platform is set" -Passed $true -Message $platform
    } else {
        Write-TestResult -TestName "Platform is set" -Passed $false
    }

    if ($tgtArch) {
        Write-TestResult -TestName "VSCMD_ARG_TGT_ARCH is set" -Passed $true -Message $tgtArch
    } else {
        Write-TestResult -TestName "VSCMD_ARG_TGT_ARCH is set" -Passed $false
    }

    if ($hostArch) {
        Write-TestResult -TestName "VSCMD_ARG_HOST_ARCH is set" -Passed $true -Message $hostArch
    } else {
        Write-TestResult -TestName "VSCMD_ARG_HOST_ARCH is set" -Passed $false
    }
}

function Test-PathComponents {
    param([string]$Arch)

    Write-TestHeader "Testing PATH Components for $Arch"

    $msvcRoot = [System.Environment]::GetEnvironmentVariable("MSVC_ROOT")
    $winsdkRoot = [System.Environment]::GetEnvironmentVariable("WINSDK_ROOT")
    $llvmRoot = [System.Environment]::GetEnvironmentVariable("LLVM_ROOT")
    $winsdkVersion = [System.Environment]::GetEnvironmentVariable("WINSDK_VERSION")
    $hostArch = [System.Environment]::GetEnvironmentVariable("VSCMD_ARG_HOST_ARCH")

    if (-not $msvcRoot -or -not $winsdkRoot -or -not $llvmRoot) {
        Write-Host "Cannot test paths - environment variables not set" -ForegroundColor Yellow
        return
    }

    # Determine host path component
    $hostPathComponent = switch ($hostArch) {
        "arm64" { "Hostarm64" }
        "x64"   { "Hostx64" }
        "x86"   { "Hostx86" }
        default { "Hostarm64" }  # Default to ARM64 if not set
    }

    # Test architecture-specific paths
    if ($Arch -eq "a64") {
        # ARM64 paths
        Test-PathExists "$msvcRoot\bin\$hostPathComponent\arm64" "MSVC ARM64 compiler bin"
        Test-PathExists "$winsdkRoot\bin\$winsdkVersion\arm64" "Windows SDK ARM64 bin"
        Test-PathExists "$winsdkRoot\bin\$winsdkVersion\x64" "Windows SDK x64 bin (fallback)"
        Test-PathExists "$llvmRoot\ARM64\bin" "Clang-CL ARM64 bin"

        # Debugger path - should use arm64 (VS naming), not a64
        Test-PathExists "$winsdkRoot\Debuggers\arm64" "Windows SDK Debuggers (ARM64 host)"

    } elseif ($Arch -eq "x64") {
        # x64 paths
        Test-PathExists "$msvcRoot\bin\$hostPathComponent\x64" "MSVC x64 compiler bin"
        Test-PathExists "$winsdkRoot\bin\$winsdkVersion\x64" "Windows SDK x64 bin"
        Test-PathExists "$llvmRoot\x64\bin" "Clang-CL x64 bin"

        # Debugger path
        $debuggerArch = if ($hostArch -eq "arm64") { "arm64" } elseif ($hostArch -eq "x64") { "x64" } else { "x86" }
        Test-PathExists "$winsdkRoot\Debuggers\$debuggerArch" "Windows SDK Debuggers ($hostArch host)"

    } else {
        # x86 paths
        Test-PathExists "$msvcRoot\bin\$hostPathComponent\x86" "MSVC x86 compiler bin"
        Test-PathExists "$winsdkRoot\bin\$winsdkVersion\x86" "Windows SDK x86 bin"
        Test-PathExists "$winsdkRoot\bin\$winsdkVersion\x64" "Windows SDK x64 bin (fallback)"
        Test-PathExists "$llvmRoot\bin" "Clang-CL bin"

        # Debugger path
        $debuggerArch = if ($hostArch -eq "arm64") { "arm64" } elseif ($hostArch -eq "x64") { "x64" } else { "x86" }
        Test-PathExists "$winsdkRoot\Debuggers\$debuggerArch" "Windows SDK Debuggers ($hostArch host)"
    }

    # Common tool paths
    $distRootDeps1 = [System.Environment]::GetEnvironmentVariable("DIST_ROOT_DEPS1")
    if ($distRootDeps1) {
        Test-PathExists "$distRootDeps1\jom\1.1.5" "Jom"
        Test-PathExists "$distRootDeps1\git\2.48.1\cmd" "Git"
        Test-PathExists "$distRootDeps1\python\3.14.2" "Python"
        Test-PathExists "$distRootDeps1\msys64\usr\bin" "MSYS2"
        Test-PathExists "$distRootDeps1\perl\5.40.0.1\perl\bin" "Strawberry Perl"

        # NASM only for x64 and x86
        if ($Arch -ne "a64") {
            Test-PathExists "$distRootDeps1\nasm\3.01" "NASM"
        }
    }
}

function Test-IncludePaths {
    param([string]$Arch)

    Write-TestHeader "Testing INCLUDE Paths for $Arch"

    $msvcRoot = [System.Environment]::GetEnvironmentVariable("MSVC_ROOT")
    $winsdkRoot = [System.Environment]::GetEnvironmentVariable("WINSDK_ROOT")
    $winsdkVersion = [System.Environment]::GetEnvironmentVariable("WINSDK_VERSION")

    if (-not $msvcRoot -or -not $winsdkRoot) {
        Write-Host "Cannot test INCLUDE paths - environment variables not set" -ForegroundColor Yellow
        return
    }

    Test-PathExists "$msvcRoot\include" "MSVC include"
    Test-PathExists "$msvcRoot\atlmfc\include" "MSVC ATL/MFC include"
    Test-PathExists "$winsdkRoot\Include\$winsdkVersion\ucrt" "Windows SDK UCRT include"
    Test-PathExists "$winsdkRoot\Include\$winsdkVersion\um" "Windows SDK UM include"
    Test-PathExists "$winsdkRoot\Include\$winsdkVersion\shared" "Windows SDK shared include"
    Test-PathExists "$winsdkRoot\Include\$winsdkVersion\winrt" "Windows SDK WinRT include"
    Test-PathExists "$winsdkRoot\Include\$winsdkVersion\cppwinrt" "Windows SDK C++/WinRT include"
}

function Test-LibPaths {
    param([string]$Arch)

    Write-TestHeader "Testing LIB Paths for $Arch"

    $msvcRoot = [System.Environment]::GetEnvironmentVariable("MSVC_ROOT")
    $winsdkRoot = [System.Environment]::GetEnvironmentVariable("WINSDK_ROOT")
    $winsdkVersion = [System.Environment]::GetEnvironmentVariable("WINSDK_VERSION")

    if (-not $msvcRoot -or -not $winsdkRoot) {
        Write-Host "Cannot test LIB paths - environment variables not set" -ForegroundColor Yellow
        return
    }

    # Convert architecture to VS naming for lib paths
    $libArch = switch ($Arch) {
        "a64" { "arm64" }
        "x64" { "x64" }
        "x86" { "x86" }
    }

    Test-PathExists "$msvcRoot\lib\$libArch" "MSVC lib ($libArch)"
    Test-PathExists "$msvcRoot\atlmfc\lib\$libArch" "MSVC ATL/MFC lib ($libArch)"
    Test-PathExists "$winsdkRoot\Lib\$winsdkVersion\ucrt\$libArch" "Windows SDK UCRT lib ($libArch)"
    Test-PathExists "$winsdkRoot\Lib\$winsdkVersion\um\$libArch" "Windows SDK UM lib ($libArch)"
}

function Test-CriticalTools {
    param([string]$Arch)

    Write-TestHeader "Testing Critical Tool Availability for $Arch"

    # MSVC tools
    Test-ExecutableExists "cl.exe" "MSVC C/C++ Compiler (cl.exe)"
    Test-ExecutableExists "link.exe" "MSVC Linker (link.exe)"
    Test-ExecutableExists "lib.exe" "MSVC Librarian (lib.exe)"
    Test-ExecutableExists "nmake.exe" "NMAKE (nmake.exe)"

    # Clang-CL for ARM assembly preprocessing
    Test-ExecutableExists "clang-cl.exe" "Clang-CL (clang-cl.exe)"

    # Build tools
    Test-ExecutableExists "jom.exe" "Jom parallel build (jom.exe)"

    # NASM for x64/x86 only
    if ($Arch -ne "a64") {
        Test-ExecutableExists "nasm.exe" "NASM Assembler (nasm.exe)"
    }

    # Version control and scripting
    Test-ExecutableExists "git.exe" "Git (git.exe)"
    Test-ExecutableExists "python.exe" "Python (python.exe)"
    Test-ExecutableExists "perl.exe" "Perl (perl.exe)"

    # Debuggers (optional, may not be in PATH)
    if (-not $SkipToolChecks) {
        $cdb = Get-Command "cdb.exe" -ErrorAction SilentlyContinue
        if ($cdb) {
            Write-TestResult -TestName "Windows Debugger (cdb.exe)" -Passed $true -Message $cdb.Source
        } else {
            Write-TestResult -TestName "Windows Debugger (cdb.exe)" -Skipped $true -Message "Not in PATH (debugger path may not be activated)"
        }
    }

    # MSYS2 tools (should be last in PATH)
    Test-ExecutableExists "bash.exe" "MSYS2 Bash (bash.exe)"
    Test-ExecutableExists "make.exe" "MSYS2 Make (make.exe)"
}

function Test-ArchitectureEnvironment {
    param([string]$Arch)

    Write-Host "`n" -NoNewline
    Write-Host "================================================================================" -ForegroundColor Magenta
    Write-Host "  Testing $Arch Environment" -ForegroundColor Magenta
    Write-Host "================================================================================" -ForegroundColor Magenta

    # Load the setup environment script
    $setupScript = Get-SetupEnvScript -Arch $Arch
    if (-not $setupScript) {
        Write-Host "Setup script not found for $Arch architecture" -ForegroundColor Yellow
        Write-Host "Expected: $(Join-Path $DistRoot "scripts\ci\setup-env-$Arch.bat")" -ForegroundColor Yellow
        Write-Host "Skipping $Arch tests" -ForegroundColor Yellow
        return
    }

    Write-Host "Loading environment from: $setupScript" -ForegroundColor Gray
    Write-Host ""

    # Execute the setup script in a new cmd process and capture environment
    $envOutput = & cmd /c "`"$setupScript`" > nul 2>&1 && set"

    # Parse environment variables
    foreach ($line in $envOutput) {
        if ($line -match '^([^=]+)=(.*)$') {
            $varName = $matches[1]
            $varValue = $matches[2]
            [System.Environment]::SetEnvironmentVariable($varName, $varValue, "Process")
        }
    }

    # Run all tests
    Test-EnvironmentVariables -Arch $Arch
    Test-PathComponents -Arch $Arch
    Test-IncludePaths -Arch $Arch
    Test-LibPaths -Arch $Arch
    Test-CriticalTools -Arch $Arch
}

# ============================================================================
# Main Execution
# ============================================================================

Write-Host "`n================================================================" -ForegroundColor Magenta
Write-Host "  Setup Environment Path Validation Tests" -ForegroundColor Magenta
Write-Host "================================================================" -ForegroundColor Magenta

# Determine DistRoot
if (-not $DistRoot) {
    # Try to detect from environment
    $DistRoot = $env:DIST_ROOT_DEPS1
    if (-not $DistRoot) {
        # Try to detect from CI_ENV_ROOT
        $DistRoot = $env:CI_ENV_ROOT
    }
    if (-not $DistRoot) {
        # Default location based on current directory
        $scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
        $repoRoot = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $scriptDir))
        $DistRoot = Join-Path (Split-Path -Parent $repoRoot) "dist-devenv7-windows-hostarch-a64-targets-a64-x64-x86"

        if (-not (Test-Path $DistRoot)) {
            # Try user profile default
            $DistRoot = Join-Path $env:USERPROFILE "swblocks\dist-devenv7-windows-a64"
        }
    }
}

Write-Host "Distribution Root: $DistRoot" -ForegroundColor Cyan
Write-Host ""

if (-not (Test-Path $DistRoot)) {
    Write-Host "ERROR: Distribution root not found: $DistRoot" -ForegroundColor Red
    Write-Host "Please specify -DistRoot parameter or ensure toolchain is installed" -ForegroundColor Yellow
    exit 1
}

# Determine which architectures to test
$archsToTest = @()
if ($Architecture -eq "All") {
    # Check which setup scripts exist
    foreach ($arch in @("a64", "x64", "x86")) {
        if (Get-SetupEnvScript -Arch $arch) {
            $archsToTest += $arch
        }
    }

    if ($archsToTest.Count -eq 0) {
        Write-Host "ERROR: No setup-env-*.bat scripts found in $DistRoot\scripts\ci" -ForegroundColor Red
        exit 1
    }
} else {
    $archsToTest = @($Architecture)
}

Write-Host "Testing architectures: $($archsToTest -join ', ')" -ForegroundColor Cyan
Write-Host ""

# Test each architecture
foreach ($arch in $archsToTest) {
    Test-ArchitectureEnvironment -Arch $arch
}

# Print summary
Write-Host "`n================================================================" -ForegroundColor Magenta
Write-Host "  Test Summary" -ForegroundColor Magenta
Write-Host "================================================================" -ForegroundColor Magenta
Write-Host ""
Write-Host "Tests Passed:  " -NoNewline
Write-Host $script:TestsPassed -ForegroundColor Green
Write-Host "Tests Failed:  " -NoNewline
Write-Host $script:TestsFailed -ForegroundColor $(if ($script:TestsFailed -eq 0) { "Green" } else { "Red" })
Write-Host "Tests Skipped: " -NoNewline
Write-Host $script:TestsSkipped -ForegroundColor Yellow
Write-Host ""

# Exit with appropriate code
if ($script:TestsFailed -eq 0) {
    Write-Host "[SUCCESS] All tests passed!" -ForegroundColor Green
    exit 0
} else {
    Write-Host "[FAILURE] Some tests failed" -ForegroundColor Red
    exit 1
}
