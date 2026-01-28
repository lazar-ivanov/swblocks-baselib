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
# OpenSSL Build Verification Script
# ================================================================================
#
# This script verifies that an OpenSSL build has been configured correctly:
#   1. Hardware acceleration performance (AES-128-GCM > 1 GB/sec)
#   2. Optimization flags (debug: /Od -Od -Ob0, release: /O2 -O2 -Ob1 -Ot -Oi)
#   3. Debug information flags (/Z7 /Zo for both variants)
#
# Usage:
#   verify-openssl-build.ps1 -OpensslExe <path> -BuildType <debug|release> -Architecture <a64|x64|x86>
#
# Exit codes:
#   0 - All verifications passed
#   1 - One or more verifications failed
#
# ================================================================================

param(
    [Parameter(Mandatory=$true)]
    [string]$OpensslExe,

    [Parameter(Mandatory=$true)]
    [ValidateSet("debug", "release")]
    [string]$BuildType,

    [Parameter(Mandatory=$true)]
    [ValidateSet("a64", "x64", "x86")]
    [string]$Architecture
)

# Verification state tracking
$script:VerificationsPassed = 0
$script:VerificationsFailed = 0
$script:AllPassed = $true

# ================================================================================
# Helper Functions
# ================================================================================

function Write-VerificationStep {
    param(
        [string]$StepNumber,
        [string]$Message
    )
    Write-Host "`n[Step $StepNumber] $Message" -ForegroundColor Cyan
}

function Write-VerificationDetail {
    param([string]$Message)
    Write-Host "  $Message" -ForegroundColor Gray
}

function Write-VerificationPass {
    param([string]$Message)
    Write-Host "  [PASS] $Message" -ForegroundColor Green
    $script:VerificationsPassed++
}

function Write-VerificationFail {
    param([string]$Message)
    Write-Host "  [FAIL] $Message" -ForegroundColor Red
    $script:VerificationsFailed++
    $script:AllPassed = $false
}

# ================================================================================
# Verification 1: Hardware Acceleration Performance
# ================================================================================

function Test-HardwareAcceleration {
    param(
        [string]$OpensslExe,
        [string]$Architecture
    )

    Write-VerificationStep "1/3" "Checking hardware acceleration performance..."
    Write-VerificationDetail "Running: openssl.exe speed -evp aes-128-gcm"

    try {
        # Run OpenSSL speed test
        $output = & $OpensslExe speed -evp aes-128-gcm 2>&1 | Out-String

        # Find the performance line for aes-128-gcm
        # Expected format: "AES-128-GCM     123.45k   234.56k   345.67k   456.78k   567.89k   7982270.93k"
        $perfLine = $output -split "`n" | Where-Object { $_ -match '^\s*AES-128-GCM\s+' }

        if (-not $perfLine) {
            Write-VerificationFail "Could not find AES-128-GCM performance line in output"
            return $false
        }

        # Extract the last column value (16384 bytes performance)
        # Split by whitespace and get the last token
        $tokens = $perfLine -split '\s+' | Where-Object { $_ -ne '' }
        $lastValue = $tokens[-1]

        # Parse value like "7982270.93k" -> 7982270.93
        if ($lastValue -match '^([\d.]+)k$') {
            $perfKBps = [double]$Matches[1]

            # Convert to GB/sec: KB/s -> GB/s = (KB/s) / 1024 / 1024
            $perfGBps = $perfKBps / 1024.0 / 1024.0

            Write-VerificationDetail ("AES-128-GCM Speed (16384 bytes): {0:F2} GB/sec" -f $perfGBps)

            # Check threshold: x86 has lower hardware acceleration performance
            # x86: 0.5 GB/sec (32-bit architecture, limited hardware acceleration)
            # x64/a64: 1.0 GB/sec (64-bit architectures, full hardware acceleration)
            $thresholdGBps = if ($Architecture -eq "x86") { 0.5 } else { 1.0 }

            if ($perfGBps -gt $thresholdGBps) {
                Write-VerificationPass ("Performance exceeds {0:F2} GB/sec threshold" -f $thresholdGBps)
                return $true
            } else {
                Write-VerificationFail ("Performance {0:F2} GB/sec is below {1:F2} GB/sec threshold" -f $perfGBps, $thresholdGBps)
                Write-VerificationDetail "This indicates hardware acceleration is not working correctly"
                return $false
            }
        } else {
            Write-VerificationFail "Could not parse performance value: $lastValue"
            return $false
        }
    }
    catch {
        Write-VerificationFail "Failed to run openssl speed test: $_"
        return $false
    }
}

# ================================================================================
# Verification 2: Optimization Flags
# ================================================================================

function Test-OptimizationFlags {
    param(
        [string]$OpensslExe,
        [string]$BuildType,
        [string]$Architecture
    )

    Write-VerificationStep "2/3" "Checking optimization flags..."
    Write-VerificationDetail "Running: openssl.exe version -a"

    try {
        # Run OpenSSL version command
        $output = & $OpensslExe version -a 2>&1 | Out-String

        # Find the compiler line
        $compilerLine = $output -split "`n" | Where-Object { $_ -match '^compiler:' }

        if (-not $compilerLine) {
            Write-VerificationFail "Could not find compiler line in version output"
            return $false
        }

        # Define expected flags based on build type
        if ($BuildType -eq "debug") {
            # Debug variant should have: /Od -Od -Ob0 (or -Od -Od -Ob0 for some architectures)
            $expectedFlags = "/Od -Od -Ob0"
            $alternatePattern = "-Od -Od -Ob0"  # Some configs might use - instead of /

            Write-VerificationDetail "Expected flags (debug): $expectedFlags"

            # Check if compiler line contains the expected flags pattern
            if ($compilerLine -match [regex]::Escape($expectedFlags) -or
                $compilerLine -match [regex]::Escape($alternatePattern)) {
                Write-VerificationPass "Optimization flags correct for debug variant"
                return $true
            } else {
                Write-VerificationDetail "Compiler line: $compilerLine"
                Write-VerificationFail "Missing or incorrect debug optimization flags"
                Write-VerificationDetail "Expected: $expectedFlags"
                return $false
            }
        }
        else {
            # Release variant should have: /O2 -O2 -Ob1 -Ot -Oi (or -O2 -Ob1 -Ot -Oi)
            $expectedFlags = "/O2 -O2 -Ob1 -Ot -Oi"
            $alternatePattern = "-O2 -O2 -Ob1 -Ot -Oi"

            Write-VerificationDetail "Expected flags (release): $expectedFlags"

            # Check if compiler line contains the expected flags pattern
            if ($compilerLine -match [regex]::Escape($expectedFlags) -or
                $compilerLine -match [regex]::Escape($alternatePattern)) {
                Write-VerificationPass "Optimization flags correct for release variant"
                return $true
            } else {
                Write-VerificationDetail "Compiler line: $compilerLine"
                Write-VerificationFail "Missing or incorrect release optimization flags"
                Write-VerificationDetail "Expected: $expectedFlags"
                return $false
            }
        }
    }
    catch {
        Write-VerificationFail "Failed to run openssl version command: $_"
        return $false
    }
}

# ================================================================================
# Verification 3: Debug Information Flags
# ================================================================================

function Test-DebugFlags {
    param(
        [string]$OpensslExe
    )

    Write-VerificationStep "3/3" "Checking debug information flags..."
    Write-VerificationDetail "Expected flags: /Z7 /Zo"

    try {
        # Run OpenSSL version command
        $output = & $OpensslExe version -a 2>&1 | Out-String

        # Find the compiler line
        $compilerLine = $output -split "`n" | Where-Object { $_ -match '^compiler:' }

        if (-not $compilerLine) {
            Write-VerificationFail "Could not find compiler line in version output"
            return $false
        }

        # Check for both /Z7 and /Zo flags
        $hasZ7 = $compilerLine -match '/Z7\s'
        $hasZo = $compilerLine -match '/Zo\s'

        if ($hasZ7 -and $hasZo) {
            Write-VerificationPass "Debug flags present"
            return $true
        } else {
            Write-VerificationDetail "Compiler line: $compilerLine"

            if (-not $hasZ7) {
                Write-VerificationFail "Missing /Z7 flag (embedded debug info)"
            }
            if (-not $hasZo) {
                Write-VerificationFail "Missing /Zo flag (optimized debugging)"
            }

            return $false
        }
    }
    catch {
        Write-VerificationFail "Failed to run openssl version command: $_"
        return $false
    }
}

# ================================================================================
# Main Execution
# ================================================================================

# Verify openssl.exe exists
if (-not (Test-Path $OpensslExe)) {
    Write-Host "`n[ERROR] OpenSSL executable not found: $OpensslExe" -ForegroundColor Red
    exit 1
}

# Run all verifications
Test-HardwareAcceleration -OpensslExe $OpensslExe -Architecture $Architecture | Out-Null
Test-OptimizationFlags -OpensslExe $OpensslExe -BuildType $BuildType -Architecture $Architecture | Out-Null
Test-DebugFlags -OpensslExe $OpensslExe | Out-Null

# Print summary
Write-Host ""
Write-Host ("Total Verifications: {0}, Passed: {1}, Failed: {2}" -f ($script:VerificationsPassed + $script:VerificationsFailed), $script:VerificationsPassed, $script:VerificationsFailed)

# Exit with appropriate code
if ($script:AllPassed) {
    exit 0
} else {
    exit 1
}
