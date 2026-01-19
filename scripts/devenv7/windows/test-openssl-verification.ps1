# ================================================================================
# This file is part of the swblocks-baselib library.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
# ================================================================================
#
# Unit Tests for OpenSSL Build Verification
# ================================================================================
#
# This script tests the verification logic used in verify-openssl-build.ps1
# without requiring a full OpenSSL build. It uses mock data to test all
# scenarios including pass/fail cases and edge conditions.
#
# Usage:
#   test-openssl-verification.ps1 [-TestName <specific-test>]
#
# ================================================================================

param(
    [string]$TestName = ""
)

# Test counters
$script:TestsPassed = 0
$script:TestsFailed = 0

# ================================================================================
# Test Helper Functions
# ================================================================================

function Write-TestHeader {
    param([string]$Message)
    Write-Host "`n========================================" -ForegroundColor Cyan
    Write-Host $Message -ForegroundColor Cyan
    Write-Host "========================================" -ForegroundColor Cyan
}

function Write-TestResult {
    param(
        [string]$TestName,
        [bool]$Passed,
        [string]$Message = ""
    )

    if ($Passed) {
        Write-Host "[PASS] $TestName" -ForegroundColor Green
        $script:TestsPassed++
    } else {
        Write-Host "[FAIL] $TestName" -ForegroundColor Red
        if ($Message) {
            Write-Host "       $Message" -ForegroundColor Yellow
        }
        $script:TestsFailed++
    }
}

function Assert-True {
    param(
        [bool]$Condition,
        [string]$TestName,
        [string]$Message = ""
    )
    Write-TestResult -TestName $TestName -Passed $Condition -Message $Message
}

function Assert-False {
    param(
        [bool]$Condition,
        [string]$TestName,
        [string]$Message = ""
    )
    Write-TestResult -TestName $TestName -Passed (-not $Condition) -Message $Message
}

function Assert-Match {
    param(
        [string]$Text,
        [string]$Pattern,
        [string]$TestName
    )
    $matched = $Text -match $Pattern
    Write-TestResult -TestName $TestName -Passed $matched -Message "Pattern: $Pattern"
}

function Assert-NotMatch {
    param(
        [string]$Text,
        [string]$Pattern,
        [string]$TestName
    )
    $matched = $Text -match $Pattern
    Write-TestResult -TestName $TestName -Passed (-not $matched) -Message "Pattern: $Pattern"
}

# ================================================================================
# Mock Data
# ================================================================================

$script:MockSpeedOutput_FastAccelerated = @"
Doing AES-128-GCM ops for 3s on 16 size blocks: 25761850 AES-128-GCM ops in 2.92s
Doing AES-128-GCM ops for 3s on 64 size blocks: 28883574 AES-128-GCM ops in 2.94s
Doing AES-128-GCM ops for 3s on 256 size blocks: 25020306 AES-128-GCM ops in 2.98s
Doing AES-128-GCM ops for 3s on 1024 size blocks: 14457145 AES-128-GCM ops in 2.95s
Doing AES-128-GCM ops for 3s on 8192 size blocks: 2814263 AES-128-GCM ops in 2.95s
Doing AES-128-GCM ops for 3s on 16384 size blocks: 1465538 AES-128-GCM ops in 3.00s
version: 3.5.4
built on: Sat Jan 17 21:41:51 2026 UTC
options: bn(64,64)
The 'numbers' are in 1000s of bytes per second processed.
type             16 bytes     64 bytes    256 bytes   1024 bytes   8192 bytes  16384 bytes
AES-128-GCM     141070.24k   629293.19k  2146244.47k  5013034.15k  7806795.34k  8003791.53k
"@

$script:MockSpeedOutput_SlowNoAccel = @"
Doing AES-128-GCM ops for 3s on 16 size blocks: 5761850 AES-128-GCM ops in 2.92s
Doing AES-128-GCM ops for 3s on 64 size blocks: 5883574 AES-128-GCM ops in 2.94s
Doing AES-128-GCM ops for 3s on 256 size blocks: 5020306 AES-128-GCM ops in 2.98s
Doing AES-128-GCM ops for 3s on 1024 size blocks: 4457145 AES-128-GCM ops in 2.95s
Doing AES-128-GCM ops for 3s on 8192 size blocks: 814263 AES-128-GCM ops in 2.95s
Doing AES-128-GCM ops for 3s on 16384 size blocks: 465538 AES-128-GCM ops in 3.00s
version: 3.5.4
built on: Sat Jan 17 21:41:51 2026 UTC
options: bn(64,64)
The 'numbers' are in 1000s of bytes per second processed.
type             16 bytes     64 bytes    256 bytes   1024 bytes   8192 bytes  16384 bytes
AES-128-GCM      41070.24k   129293.19k   246244.47k   313034.15k   206795.34k   203791.53k
"@

$script:MockVersionOutput_DebugCorrect = @"
version: 3.5.4
built on: Sat Jan 17 20:25:16 2026 UTC
options: bn(64,64)
compiler: cl /Zi /Fdossl_static.pdb /MT /Zl /Gs0 /GF /Gy /W3 /wd4090 /nologo /Od -Od -Ob0 -Oy- -EHs -GS -bigobj /Z7 /Zo -DL_ENDIAN -DOPENSSL_PIC -D"OPENSSL_BUILDING_OPENSSL" -DNDEBUG
CPUINFO: N/A
"@

$script:MockVersionOutput_ReleaseCorrect = @"
version: 3.5.4
built on: Sat Jan 17 21:41:51 2026 UTC
options: bn(64,64)
compiler: cl /Zi /Fdossl_static.pdb /MT /Zl /Gs0 /GF /Gy /W3 /wd4090 /nologo /O2 -O2 -Ob1 -Ot -Oi -Oy- -EHs -GS -bigobj /Z7 /Zo -DL_ENDIAN -DOPENSSL_PIC -D"OPENSSL_BUILDING_OPENSSL" -DNDEBUG
CPUINFO: N/A
"@

$script:MockVersionOutput_MissingZ7 = @"
version: 3.5.4
built on: Sat Jan 17 21:41:51 2026 UTC
options: bn(64,64)
compiler: cl /Zi /Fdossl_static.pdb /MT /Zl /Gs0 /GF /Gy /W3 /wd4090 /nologo /O2 -O2 -Ob1 -Ot -Oi -Oy- -EHs -GS -bigobj /Zo -DL_ENDIAN -DOPENSSL_PIC -D"OPENSSL_BUILDING_OPENSSL" -DNDEBUG
CPUINFO: N/A
"@

$script:MockVersionOutput_MissingZo = @"
version: 3.5.4
built on: Sat Jan 17 21:41:51 2026 UTC
options: bn(64,64)
compiler: cl /Zi /Fdossl_static.pdb /MT /Zl /Gs0 /GF /Gy /W3 /wd4090 /nologo /O2 -O2 -Ob1 -Ot -Oi -Oy- -EHs -GS -bigobj /Z7 -DL_ENDIAN -DOPENSSL_PIC -D"OPENSSL_BUILDING_OPENSSL" -DNDEBUG
CPUINFO: N/A
"@

$script:MockVersionOutput_WrongDebugOpt = @"
version: 3.5.4
built on: Sat Jan 17 20:25:16 2026 UTC
options: bn(64,64)
compiler: cl /Zi /Fdossl_static.pdb /MT /Zl /Gs0 /GF /Gy /W3 /wd4090 /nologo /O2 -Oy- -EHs -GS -bigobj /Z7 /Zo -DL_ENDIAN -DOPENSSL_PIC -D"OPENSSL_BUILDING_OPENSSL" -DNDEBUG
CPUINFO: N/A
"@

$script:MockVersionOutput_WrongReleaseOpt = @"
version: 3.5.4
built on: Sat Jan 17 21:41:51 2026 UTC
options: bn(64,64)
compiler: cl /Zi /Fdossl_static.pdb /MT /Zl /Gs0 /GF /Gy /W3 /wd4090 /nologo /Od -Ob0 -Oy- -EHs -GS -bigobj /Z7 /Zo -DL_ENDIAN -DOPENSSL_PIC -D"OPENSSL_BUILDING_OPENSSL" -DNDEBUG
CPUINFO: N/A
"@

# ================================================================================
# Test Functions
# ================================================================================

function Test-PerformanceParsing {
    Write-TestHeader "Testing Performance Parsing"

    # Test 1: Parse fast accelerated output
    $perfLine = $script:MockSpeedOutput_FastAccelerated -split "`n" | Where-Object { $_ -match '^\s*AES-128-GCM\s+' }
    $tokens = $perfLine -split '\s+' | Where-Object { $_ -ne '' }
    $lastValue = $tokens[-1]

    Assert-True ($lastValue -eq "8003791.53k") "Parse fast performance value"

    # Test 2: Extract numeric value
    if ($lastValue -match '^([\d.]+)k$') {
        $perfKBps = [double]$Matches[1]
        Assert-True ($perfKBps -eq 8003791.53) "Extract numeric KB/s value"

        # Test 3: Convert to GB/sec
        $perfGBps = $perfKBps / 1024.0 / 1024.0
        $expectedGBps = 7.63  # Approximately
        Assert-True ($perfGBps -gt 7.0 -and $perfGBps -lt 8.0) "Convert to GB/sec (~7.63)"
    }

    # Test 4: Parse slow non-accelerated output
    $perfLine = $script:MockSpeedOutput_SlowNoAccel -split "`n" | Where-Object { $_ -match '^\s*AES-128-GCM\s+' }
    $tokens = $perfLine -split '\s+' | Where-Object { $_ -ne '' }
    $lastValue = $tokens[-1]

    Assert-True ($lastValue -eq "203791.53k") "Parse slow performance value"

    if ($lastValue -match '^([\d.]+)k$') {
        $perfKBps = [double]$Matches[1]
        $perfGBps = $perfKBps / 1024.0 / 1024.0
        Assert-True ($perfGBps -lt 1.0) "Slow performance is < 1 GB/sec (~0.19)"
    }

    # Test 5: Threshold validation (fast should pass)
    $perfLine = $script:MockSpeedOutput_FastAccelerated -split "`n" | Where-Object { $_ -match '^\s*AES-128-GCM\s+' }
    $tokens = $perfLine -split '\s+' | Where-Object { $_ -ne '' }
    $lastValue = $tokens[-1]
    if ($lastValue -match '^([\d.]+)k$') {
        $perfKBps = [double]$Matches[1]
        $perfGBps = $perfKBps / 1024.0 / 1024.0
        Assert-True ($perfGBps -gt 1.0) "Fast performance exceeds 1 GB/sec threshold"
    }

    # Test 6: Threshold validation (slow should fail)
    $perfLine = $script:MockSpeedOutput_SlowNoAccel -split "`n" | Where-Object { $_ -match '^\s*AES-128-GCM\s+' }
    $tokens = $perfLine -split '\s+' | Where-Object { $_ -ne '' }
    $lastValue = $tokens[-1]
    if ($lastValue -match '^([\d.]+)k$') {
        $perfKBps = [double]$Matches[1]
        $perfGBps = $perfKBps / 1024.0 / 1024.0
        Assert-False ($perfGBps -gt 1.0) "Slow performance fails 1 GB/sec threshold"
    }
}

function Test-DebugOptimizationFlags {
    Write-TestHeader "Testing Debug Optimization Flags"

    # Test 1: Correct debug flags present
    $compilerLine = $script:MockVersionOutput_DebugCorrect -split "`n" | Where-Object { $_ -match '^compiler:' }
    Assert-Match $compilerLine "/Od -Od -Ob0" "Debug flags present in correct output"

    # Test 2: Wrong flags (release flags in debug build)
    $compilerLine = $script:MockVersionOutput_WrongDebugOpt -split "`n" | Where-Object { $_ -match '^compiler:' }
    Assert-NotMatch $compilerLine "/Od -Od -Ob0" "Debug flags absent when wrong optimization used"

    # Test 3: Release flags should NOT match debug pattern
    $compilerLine = $script:MockVersionOutput_ReleaseCorrect -split "`n" | Where-Object { $_ -match '^compiler:' }
    Assert-NotMatch $compilerLine "/Od -Od -Ob0" "Debug flags not present in release build"
}

function Test-ReleaseOptimizationFlags {
    Write-TestHeader "Testing Release Optimization Flags"

    # Test 1: Correct release flags present
    $compilerLine = $script:MockVersionOutput_ReleaseCorrect -split "`n" | Where-Object { $_ -match '^compiler:' }
    Assert-Match $compilerLine "/O2 -O2 -Ob1 -Ot -Oi" "Release flags present in correct output"

    # Test 2: Wrong flags (debug flags in release build)
    $compilerLine = $script:MockVersionOutput_WrongReleaseOpt -split "`n" | Where-Object { $_ -match '^compiler:' }
    Assert-NotMatch $compilerLine "/O2 -O2 -Ob1 -Ot -Oi" "Release flags absent when wrong optimization used"

    # Test 3: Debug flags should NOT match release pattern
    $compilerLine = $script:MockVersionOutput_DebugCorrect -split "`n" | Where-Object { $_ -match '^compiler:' }
    Assert-NotMatch $compilerLine "/O2 -O2 -Ob1 -Ot -Oi" "Release flags not present in debug build"
}

function Test-DebugInformationFlags {
    Write-TestHeader "Testing Debug Information Flags"

    # Test 1: Both /Z7 and /Zo present in debug build
    $compilerLine = $script:MockVersionOutput_DebugCorrect -split "`n" | Where-Object { $_ -match '^compiler:' }
    $hasZ7 = $compilerLine -match '/Z7\s'
    $hasZo = $compilerLine -match '/Zo\s'
    Assert-True ($hasZ7 -and $hasZo) "Both /Z7 and /Zo present in debug build"

    # Test 2: Both /Z7 and /Zo present in release build
    $compilerLine = $script:MockVersionOutput_ReleaseCorrect -split "`n" | Where-Object { $_ -match '^compiler:' }
    $hasZ7 = $compilerLine -match '/Z7\s'
    $hasZo = $compilerLine -match '/Zo\s'
    Assert-True ($hasZ7 -and $hasZo) "Both /Z7 and /Zo present in release build"

    # Test 3: Missing /Z7
    $compilerLine = $script:MockVersionOutput_MissingZ7 -split "`n" | Where-Object { $_ -match '^compiler:' }
    $hasZ7 = $compilerLine -match '/Z7\s'
    Assert-False $hasZ7 "Detect missing /Z7 flag"

    # Test 4: Missing /Zo
    $compilerLine = $script:MockVersionOutput_MissingZo -split "`n" | Where-Object { $_ -match '^compiler:' }
    $hasZo = $compilerLine -match '/Zo\s'
    Assert-False $hasZo "Detect missing /Zo flag"

    # Test 5: Missing both
    $compilerLine = $script:MockVersionOutput_MissingZ7 -replace '/Zo\s', '  '
    $hasZ7 = $compilerLine -match '/Z7\s'
    $hasZo = $compilerLine -match '/Zo\s'
    Assert-False ($hasZ7 -or $hasZo) "Detect missing both /Z7 and /Zo"
}

function Test-CompilerLineExtraction {
    Write-TestHeader "Testing Compiler Line Extraction"

    # Test 1: Extract from debug output
    $compilerLine = $script:MockVersionOutput_DebugCorrect -split "`n" | Where-Object { $_ -match '^compiler:' }
    Assert-True ($compilerLine -ne $null) "Extract compiler line from debug output"
    Assert-Match $compilerLine "^compiler:" "Compiler line starts with 'compiler:'"

    # Test 2: Extract from release output
    $compilerLine = $script:MockVersionOutput_ReleaseCorrect -split "`n" | Where-Object { $_ -match '^compiler:' }
    Assert-True ($compilerLine -ne $null) "Extract compiler line from release output"

    # Test 3: Verify compiler line contains expected content
    Assert-Match $compilerLine "cl\s" "Compiler line contains 'cl'"
    Assert-Match $compilerLine "-DL_ENDIAN" "Compiler line contains defines"
}

function Test-EdgeCases {
    Write-TestHeader "Testing Edge Cases"

    # Test 1: Empty speed output
    $emptyOutput = ""
    $perfLine = $emptyOutput -split "`n" | Where-Object { $_ -match '^\s*AES-128-GCM\s+' }
    Assert-True ($perfLine -eq $null) "Handle empty speed output"

    # Test 2: Malformed performance line
    $malformedOutput = "AES-128-GCM     invalid data here"
    $tokens = $malformedOutput -split '\s+' | Where-Object { $_ -ne '' }
    $lastValue = $tokens[-1]
    $matched = $lastValue -match '^([\d.]+)k$'
    Assert-False $matched "Detect malformed performance value"

    # Test 3: Missing compiler line
    $noCompilerOutput = @"
version: 3.5.4
built on: Sat Jan 17 21:41:51 2026 UTC
options: bn(64,64)
CPUINFO: N/A
"@
    $compilerLine = $noCompilerOutput -split "`n" | Where-Object { $_ -match '^compiler:' }
    Assert-True ($compilerLine -eq $null) "Handle missing compiler line"

    # Test 4: Performance value at boundary (exactly 1 GB/sec = 1048576k)
    $boundaryValue = "1048576.00k"
    if ($boundaryValue -match '^([\d.]+)k$') {
        $perfKBps = [double]$Matches[1]
        $perfGBps = $perfKBps / 1024.0 / 1024.0
        Assert-True ($perfGBps -eq 1.0) "Handle boundary performance value (1.0 GB/sec)"
        Assert-False ($perfGBps -gt 1.0) "Boundary value should not exceed threshold"
    }

    # Test 5: Very high performance value
    $highValue = "10000000.00k"
    if ($highValue -match '^([\d.]+)k$') {
        $perfKBps = [double]$Matches[1]
        $perfGBps = $perfKBps / 1024.0 / 1024.0
        Assert-True ($perfGBps -gt 1.0) "Handle very high performance value (~9.5 GB/sec)"
    }
}

function Test-GBSecFormatting {
    Write-TestHeader "Testing GB/sec Formatting"

    # Test various performance values and their GB/sec formatting
    $testCases = @(
        @{ Value = "8003791.53k"; Expected = 7.63 },
        @{ Value = "203791.53k"; Expected = 0.19 },
        @{ Value = "1048576.00k"; Expected = 1.00 },
        @{ Value = "2097152.00k"; Expected = 2.00 },
        @{ Value = "524288.00k"; Expected = 0.50 }
    )

    foreach ($testCase in $testCases) {
        if ($testCase.Value -match '^([\d.]+)k$') {
            $perfKBps = [double]$Matches[1]
            $perfGBps = $perfKBps / 1024.0 / 1024.0
            $formatted = "{0:F2}" -f $perfGBps
            $expectedFormatted = "{0:F2}" -f $testCase.Expected

            Assert-True ($formatted -eq $expectedFormatted) "Format $($testCase.Value) as $expectedFormatted GB/sec"
        }
    }
}

# ================================================================================
# Main Test Execution
# ================================================================================

Write-Host "`n========================================" -ForegroundColor Magenta
Write-Host "OpenSSL Verification Unit Tests" -ForegroundColor Magenta
Write-Host "========================================" -ForegroundColor Magenta

# Run tests based on test name filter
if ($TestName -eq "" -or $TestName -eq "PerformanceParsing") {
    Test-PerformanceParsing
}

if ($TestName -eq "" -or $TestName -eq "DebugOptimizationFlags") {
    Test-DebugOptimizationFlags
}

if ($TestName -eq "" -or $TestName -eq "ReleaseOptimizationFlags") {
    Test-ReleaseOptimizationFlags
}

if ($TestName -eq "" -or $TestName -eq "DebugInformationFlags") {
    Test-DebugInformationFlags
}

if ($TestName -eq "" -or $TestName -eq "CompilerLineExtraction") {
    Test-CompilerLineExtraction
}

if ($TestName -eq "" -or $TestName -eq "EdgeCases") {
    Test-EdgeCases
}

if ($TestName -eq "" -or $TestName -eq "GBSecFormatting") {
    Test-GBSecFormatting
}

# Print summary
Write-Host "`n========================================" -ForegroundColor Magenta
Write-Host "Test Summary" -ForegroundColor Magenta
Write-Host "========================================" -ForegroundColor Magenta
Write-Host "Total: $($script:TestsPassed + $script:TestsFailed)" -ForegroundColor White
Write-Host "Passed: $script:TestsPassed" -ForegroundColor Green
Write-Host "Failed: $script:TestsFailed" -ForegroundColor $(if ($script:TestsFailed -gt 0) { "Red" } else { "Green" })
Write-Host ""

# Exit with appropriate code
if ($script:TestsFailed -eq 0) {
    Write-Host "All tests passed!" -ForegroundColor Green
    exit 0
} else {
    Write-Host "Some tests failed!" -ForegroundColor Red
    exit 1
}
