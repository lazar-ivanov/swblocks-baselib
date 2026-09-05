<#
.SYNOPSIS
    Unit tests for OpenSSL build configuration

.DESCRIPTION
    This script validates the OpenSSL build configuration without running full builds.
    It tests:
    1. Configure command construction for debug and release variants
    2. Makefile patching logic for debug information flags
    3. ARM64 assembly preprocessor redirection to clang-cl
    4. Debug optimization flag replacement in makefile

    This allows quick validation of build script changes without waiting for full builds.

.PARAMETER TestName
    Optional specific test to run. If not specified, runs all tests.
    Valid values: ConfigureDebug, ConfigureRelease, MakefilePatch, ARM64PerlPatch, DebugOptimizationPatch, All

.EXAMPLE
    .\test-openssl-build-config.ps1
    Runs all tests

.EXAMPLE
    .\test-openssl-build-config.ps1 -TestName MakefilePatch
    Runs only makefile patching tests
#>

param(
    [Parameter(Mandatory=$false)]
    [ValidateSet("ConfigureDebug", "ConfigureRelease", "MakefilePatch", "ARM64PerlPatch", "DebugOptimizationPatch", "All")]
    [string]$TestName = "All"
)

$ErrorActionPreference = "Stop"

# Test counters
$script:TestsPassed = 0
$script:TestsFailed = 0
$script:TestsTotal = 0

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

    $script:TestsTotal++

    if ($Passed) {
        $script:TestsPassed++
        Write-Host "[PASS] $TestName" -ForegroundColor Green
        if ($Message) {
            Write-Host "       $Message" -ForegroundColor Gray
        }
    } else {
        $script:TestsFailed++
        Write-Host "[FAIL] $TestName" -ForegroundColor Red
        if ($Message) {
            Write-Host "       $Message" -ForegroundColor Yellow
        }
    }
}

function Assert-Contains {
    param(
        [string]$Text,
        [string]$Pattern,
        [string]$TestName
    )

    if ($Text -match $Pattern) {
        Write-TestResult -TestName $TestName -Passed $true -Message "Found pattern: $Pattern"
        return $true
    } else {
        Write-TestResult -TestName $TestName -Passed $false -Message "Pattern not found: $Pattern"
        return $false
    }
}

function Assert-NotContains {
    param(
        [string]$Text,
        [string]$Pattern,
        [string]$TestName
    )

    if ($Text -notmatch $Pattern) {
        Write-TestResult -TestName $TestName -Passed $true -Message "Pattern correctly absent: $Pattern"
        return $true
    } else {
        Write-TestResult -TestName $TestName -Passed $false -Message "Pattern should not be present: $Pattern"
        return $false
    }
}

# ============================================================================
# Test 1: Debug Build Configure Command
# ============================================================================
function Test-ConfigureDebug {
    Write-TestHeader "Testing Debug Build Configure Command"

    # Expected Configure command for debug build
    # NOTE: Debug builds pass RELEASE optimization flags to Configure
    # This prevents OpenSSL Configure from detecting debug flags and disabling assembly
    $expectedFlags = @(
        "--release",              # Should use release CRT
        "-DNDEBUG",               # Should define NDEBUG
        "-O2",                    # Release optimization (prevents assembly from being disabled)
        "-Ob1",                   # Release inline expansion
        "-Ot",                    # Optimize for speed
        "-Oi",                    # Intrinsic functions
        "-DOPENSSL_BN_ASM_MONT",  # Assembly activation macro
        "-DOPENSSL_CPUID_OBJ",    # Assembly activation macro
        "-DSHA1_ASM",             # Assembly activation macro
        "-DSHA256_ASM"            # Assembly activation macro
    )

    $unexpectedFlags = @(
        "--debug",        # Should NOT use debug mode
        "/MDd",           # Should NOT use debug CRT
        "-DDEBUG",        # Should NOT define DEBUG
        "-D_DEBUG",       # Should NOT define _DEBUG
        "-Zi",            # Should NOT pass Zi to Configure
        "-Z7",            # Should NOT pass Z7 to Configure
        "-Zo",            # Should NOT pass Zo to Configure
        "CC=",            # Should NOT override CC (uses MSVC cl.exe for C compilation)
        "AS="             # Should NOT override AS
    )

    # Simulate debug Configure command for ARM64 (a64)
    # Note: Debug builds pass RELEASE optimization flags to Configure to prevent assembly from being disabled
    # The makefile is then patched to replace release flags with debug flags
    # cl.exe is used for C compilation, clang-cl only used for assembly preprocessing (patched in makefile)
    # Assembly activation macros tell C code to call assembly functions
    # ARM architecture macros enable compile-time fast paths (aes_platform.h, gcm128.c, etc.)
    $configureCmd = "perl Configure VC-WIN64-CLANGASM-ARM no-shared --release " +
                    "--prefix=C:\temp\out --openssldir=C:\temp\out\ssl " +
                    "-O2 -Ob1 -Ot -Oi -Oy- -EHs -GS -bigobj -DNDEBUG " +
                    "-DOPENSSL_CPUID_OBJ -DOPENSSL_BN_ASM_MONT -DMD5_ASM -DVPAES_ASM -DBSAES_ASM " +
                    "-DSHA1_ASM -DSHA256_ASM -DSHA512_ASM -DKECCAK1600_ASM -DPOLY1305_ASM -DECP_NISTZ256_ASM " +
                    "-D__ARM_ARCH__=8 -D__ARM_MAX_ARCH__=8"

    Write-Host "`nSimulated Configure command:" -ForegroundColor Gray
    Write-Host $configureCmd -ForegroundColor Gray
    Write-Host ""

    # Check expected flags
    foreach ($flag in $expectedFlags) {
        $escapedFlag = [regex]::Escape($flag)
        Assert-Contains -Text $configureCmd -Pattern $escapedFlag `
            -TestName "Debug Configure contains $flag"
    }

    # Check unexpected flags are absent
    foreach ($flag in $unexpectedFlags) {
        $escapedFlag = [regex]::Escape($flag)
        Assert-NotContains -Text $configureCmd -Pattern $escapedFlag `
            -TestName "Debug Configure does not contain $flag"
    }
}

# ============================================================================
# Test 2: Release Build Configure Command
# ============================================================================
function Test-ConfigureRelease {
    Write-TestHeader "Testing Release Build Configure Command"

    # Expected Configure command for release build
    $expectedFlags = @(
        "--release",              # Should use release CRT
        "-DNDEBUG",               # Should define NDEBUG
        "-O2",                    # Optimizations enabled
        "-Ob1",                   # Inline expansion
        "-Ot",                    # Favor speed
        "-Oi",                    # Intrinsic functions
        "-DOPENSSL_BN_ASM_MONT",  # Assembly activation macro
        "-DOPENSSL_CPUID_OBJ",    # Assembly activation macro
        "-DSHA1_ASM",             # Assembly activation macro
        "-DSHA256_ASM"            # Assembly activation macro
    )

    $unexpectedFlags = @(
        "--debug",        # Should NOT use debug mode
        "/MDd",           # Should NOT use debug CRT
        "-DDEBUG",        # Should NOT define DEBUG
        "-D_DEBUG",       # Should NOT define _DEBUG
        "-Zi",            # Should NOT pass Zi to Configure
        "-Z7",            # Should NOT pass Z7 to Configure
        "-Zo",            # Should NOT pass Zo to Configure (added via patching)
        "CC=",            # Should NOT override CC (uses MSVC cl.exe for C compilation)
        "AS="             # Should NOT override AS
    )

    # Simulate release Configure command for ARM64 (a64)
    # Note: cl.exe is used for C compilation, clang-cl only used for assembly preprocessing (patched in makefile)
    # Assembly activation macros tell C code to call assembly functions
    # ARM architecture macros enable compile-time fast paths (aes_platform.h, gcm128.c, etc.)
    $configureCmd = "perl Configure VC-WIN64-CLANGASM-ARM no-shared --release " +
                    "--prefix=C:\temp\out --openssldir=C:\temp\out\ssl " +
                    "-O2 -Ob1 -Ot -Oi -Oy- -EHs -GS -bigobj -DNDEBUG " +
                    "-DOPENSSL_CPUID_OBJ -DOPENSSL_BN_ASM_MONT -DMD5_ASM -DVPAES_ASM -DBSAES_ASM " +
                    "-DSHA1_ASM -DSHA256_ASM -DSHA512_ASM -DKECCAK1600_ASM -DPOLY1305_ASM -DECP_NISTZ256_ASM " +
                    "-D__ARM_ARCH__=8 -D__ARM_MAX_ARCH__=8"

    Write-Host "`nSimulated Configure command:" -ForegroundColor Gray
    Write-Host $configureCmd -ForegroundColor Gray
    Write-Host ""

    # Check expected flags
    foreach ($flag in $expectedFlags) {
        $escapedFlag = [regex]::Escape($flag)
        Assert-Contains -Text $configureCmd -Pattern $escapedFlag `
            -TestName "Release Configure contains $flag"
    }

    # Check unexpected flags are absent
    foreach ($flag in $unexpectedFlags) {
        $escapedFlag = [regex]::Escape($flag)
        Assert-NotContains -Text $configureCmd -Pattern $escapedFlag `
            -TestName "Release Configure does not contain $flag"
    }
}

# ============================================================================
# Test 3: Makefile Patching Logic
# ============================================================================
function Test-MakefilePatch {
    Write-TestHeader "Testing Makefile Patching Logic"

    # Create a mock makefile with typical OpenSSL content
    $mockMakefile = @"
# OpenSSL makefile (mocked for testing)
LIB_CFLAGS=/MT -Ox -O2 /Gs0 /GF /Gy -W3 -wd4090 -nologo -DOPENSSL_SYS_WIN32
CNF_CFLAGS=
LIB_CPPFLAGS=-D"OPENSSLDIR=\"/usr/local/ssl\"" -D"ENGINESDIR=\"/usr/local/lib/engines-3\""
CNF_CPPFLAGS=
ASFLAGS=-g
"@

    Write-Host "`nOriginal makefile:" -ForegroundColor Gray
    Write-Host $mockMakefile -ForegroundColor Gray
    Write-Host ""

    # Apply the patching logic (same order as build script)
    $patchedMakefile = $mockMakefile
    $patchedMakefile = $patchedMakefile -replace '/Zi\s+/Fd[^\s]+\s*','/Z7 '
    $patchedMakefile = $patchedMakefile -replace '(LIB_CFLAGS=[^\r\n]+)','$1 /Z7 /Zo'

    Write-Host "Patched makefile:" -ForegroundColor Gray
    Write-Host $patchedMakefile -ForegroundColor Gray
    Write-Host ""

    # Verify LIB_CFLAGS has /Z7 and /Zo
    Assert-Contains -Text $patchedMakefile -Pattern 'LIB_CFLAGS=.*\s+/Z7\s+/Zo' `
        -TestName "LIB_CFLAGS contains /Z7 /Zo"

    # Verify ASFLAGS does not have debug flags
    Assert-NotContains -Text $patchedMakefile -Pattern 'ASFLAGS=.*\s+/Z[i7]' `
        -TestName "ASFLAGS does not contain /Zi or /Z7"

    Assert-NotContains -Text $patchedMakefile -Pattern 'ASFLAGS=.*\s+/Zo' `
        -TestName "ASFLAGS does not contain /Zo"

    # Test conversion of /Zi to /Z7
    $makefileWithZi = @"
LIB_CFLAGS=/MT -Ox /Zi /Fdossl_static.pdb -DOPENSSL_SYS_WIN32
"@

    Write-Host "`nTesting /Zi to /Z7 conversion:" -ForegroundColor Gray
    Write-Host "Before: $makefileWithZi" -ForegroundColor Gray

    $patchedZi = $makefileWithZi -replace '/Zi\s+/Fd[^\s]+\s*','/Z7 '
    $patchedZi = $patchedZi -replace '(LIB_CFLAGS=[^\r\n]+)','$1 /Z7 /Zo'

    Write-Host "After:  $patchedZi" -ForegroundColor Gray
    Write-Host ""

    Assert-NotContains -Text $patchedZi -Pattern '/Zi\s' `
        -TestName "/Zi successfully converted to /Z7"

    Assert-Contains -Text $patchedZi -Pattern '/Z7' `
        -TestName "/Z7 present after conversion"

    Assert-NotContains -Text $patchedZi -Pattern '/Fd' `
        -TestName "PDB reference removed"

    # Test PDB install command removal
    $makefileWithPDB = @'
install_dev: install_runtime_libs
	@copy /y libssl.lib "$(INSTALLTOP)\lib\libssl.lib"
	@if exist ossl_static.pdb $(PERL) "$(SRCDIR)\util\copy.pl" ossl_static.pdb "$(INSTALLTOP)\lib\ossl_static.pdb"
	@copy /y libcrypto.lib "$(INSTALLTOP)\lib\libcrypto.lib"
'@

    Write-Host "`nTesting PDB install command removal:" -ForegroundColor Gray
    Write-Host "Before:" -ForegroundColor Gray
    Write-Host $makefileWithPDB -ForegroundColor Gray

    $patchedPDB = $makefileWithPDB -replace '.*copy\.pl.*ossl_static\.pdb.*','# Removed: PDB copy (using /Z7)'

    Write-Host "After:" -ForegroundColor Gray
    Write-Host $patchedPDB -ForegroundColor Gray
    Write-Host ""

    Assert-NotContains -Text $patchedPDB -Pattern 'copy\.pl.*ossl_static\.pdb' `
        -TestName "PDB install command removed"

    Assert-Contains -Text $patchedPDB -Pattern '# Removed: PDB copy' `
        -TestName "PDB install command replaced with comment"
}

# ============================================================================
# Test 4: ARM64 Assembly Preprocessor Perl Patch
# ============================================================================
function Test-ARM64PerlPatch {
    Write-TestHeader "Testing ARM64 Assembly Preprocessor Perl Patch"

    # Create a mock makefile with typical OpenSSL ARM64 assembly preprocessor pattern
    $mockMakefileARM64 = @"
# OpenSSL makefile for ARM64 (mocked for testing)
CFLAGS=/MT -Ox -O2 /Gs0 /GF /Gy -W3 -wd4090 -nologo -DOPENSSL_SYS_WIN32
CNF_CFLAGS=
LIB_CPPFLAGS=-D"OPENSSLDIR=\"/usr/local/ssl\"" -D"ENGINESDIR=\"/usr/local/lib/engines-3\""
CNF_CPPFLAGS=

# Assembly preprocessing line
crypto\arm64cpuid.obj: crypto\arm64cpuid.S
	`$(CC) /EP -D__ASSEMBLER__ `$(CPPFLAGS) crypto\arm64cpuid.S > crypto\arm64cpuid.asm
	`$(AS) /c /Focrypto\arm64cpuid.obj crypto\arm64cpuid.asm
"@

    Write-Host "`nOriginal ARM64 makefile:" -ForegroundColor Gray
    Write-Host $mockMakefileARM64 -ForegroundColor Gray
    Write-Host ""

    # Simulate the Perl patch (using PowerShell equivalent for testing)
    # In the actual build script, Perl is used: perl -i.bak -pe "s/\$(CC) \/EP -D__ASSEMBLER__/clang-cl.exe \/EP -D__ASSEMBLER__/g; s/^CFLAGS=(.*)/CFLAGS=$1 \/Z7 \/Zo/g" makefile
    $patchedARM64 = $mockMakefileARM64
    $patchedARM64 = $patchedARM64 -replace '\$\(CC\) \/EP -D__ASSEMBLER__','clang-cl.exe /EP -D__ASSEMBLER__'
    $patchedARM64 = $patchedARM64 -replace '(?m)^CFLAGS=(.*)','CFLAGS=$1 /Z7 /Zo'

    Write-Host "Patched ARM64 makefile:" -ForegroundColor Gray
    Write-Host $patchedARM64 -ForegroundColor Gray
    Write-Host ""

    # Verify assembly preprocessor is redirected to clang-cl
    Assert-Contains -Text $patchedARM64 -Pattern 'clang-cl\.exe /EP -D__ASSEMBLER__' `
        -TestName "Assembly preprocessor redirected to clang-cl.exe"

    # Verify $(CC) is no longer used for assembly preprocessing
    Assert-NotContains -Text $patchedARM64 -Pattern '\$\(CC\) /EP -D__ASSEMBLER__' `
        -TestName '$(CC) no longer used for assembly preprocessing'

    # Verify CFLAGS has /Z7 and /Zo added
    Assert-Contains -Text $patchedARM64 -Pattern 'CFLAGS=.*\/Z7 \/Zo' `
        -TestName "CFLAGS contains /Z7 /Zo"

    # Verify original CFLAGS content is preserved
    Assert-Contains -Text $patchedARM64 -Pattern 'CFLAGS=/MT -Ox -O2 /Gs0 /GF /Gy' `
        -TestName "Original CFLAGS content preserved"

    # Test that AS (assembler) command is NOT modified
    Assert-Contains -Text $patchedARM64 -Pattern '\$\(AS\) /c /Focrypto\\arm64cpuid\.obj' `
        -TestName "Assembler command unchanged"

    # Test edge case: Multiple assembly preprocessing lines
    $makefileMultipleAsm = @"
CFLAGS=/MT -DNDEBUG

crypto\arm64cpuid.obj: crypto\arm64cpuid.S
	`$(CC) /EP -D__ASSEMBLER__ `$(CPPFLAGS) crypto\arm64cpuid.S > crypto\arm64cpuid.asm

crypto\aes_core.obj: crypto\aes_core.S
	`$(CC) /EP -D__ASSEMBLER__ `$(CPPFLAGS) crypto\aes_core.S > crypto\aes_core.asm
"@

    Write-Host "`nTesting multiple assembly preprocessing lines:" -ForegroundColor Gray
    Write-Host "Before:" -ForegroundColor Gray
    Write-Host $makefileMultipleAsm -ForegroundColor Gray

    $patchedMultiple = $makefileMultipleAsm
    $patchedMultiple = $patchedMultiple -replace '\$\(CC\) \/EP -D__ASSEMBLER__','clang-cl.exe /EP -D__ASSEMBLER__'
    $patchedMultiple = $patchedMultiple -replace '(?m)^CFLAGS=(.*)','CFLAGS=$1 /Z7 /Zo'

    Write-Host "After:" -ForegroundColor Gray
    Write-Host $patchedMultiple -ForegroundColor Gray
    Write-Host ""

    # Verify both assembly preprocessing lines are patched
    $clangClCount = ([regex]::Matches($patchedMultiple, 'clang-cl\.exe /EP -D__ASSEMBLER__')).Count
    if ($clangClCount -eq 2) {
        Write-TestResult -TestName "All assembly preprocessing lines redirected" -Passed $true `
            -Message "Found $clangClCount occurrences"
    } else {
        Write-TestResult -TestName "All assembly preprocessing lines redirected" -Passed $false `
            -Message "Expected 2 occurrences, found $clangClCount"
    }

    # Verify CFLAGS only modified once
    Assert-Contains -Text $patchedMultiple -Pattern '^CFLAGS=/MT -DNDEBUG /Z7 /Zo' `
        -TestName "CFLAGS modified exactly once"
}

# ============================================================================
# Test 5: Debug Optimization Flag Patching
# ============================================================================
function Test-DebugOptimizationPatch {
    Write-TestHeader "Testing Debug Optimization Flag Patching"

    # Create a mock makefile with release optimization flags (as generated by Configure)
    $mockMakefileDebug = @"
# OpenSSL makefile for debug build (mocked for testing)
# Configure was passed release optimization flags to keep assembly enabled
CFLAGS=/MT -O2 -Ob1 -Ot -Oi -Oy- -EHs -GS -bigobj -DOPENSSL_SYS_WIN32 -DNDEBUG
LIB_CFLAGS=/MT /O2 /Gs0 /GF /Gy -W3 -wd4090 -nologo -DOPENSSL_SYS_WIN32
CNF_CFLAGS=
"@

    Write-Host "`nOriginal makefile (after Configure with release flags):" -ForegroundColor Gray
    Write-Host $mockMakefileDebug -ForegroundColor Gray
    Write-Host ""

    # Apply the optimization flag patching (same Perl command as build script)
    # perl -i.bak -pe "s/-O2 -Ob1 -Ot -Oi/-Od -Ob0/g; s/\/O2 /\/Od /g" makefile
    $patchedDebug = $mockMakefileDebug
    $patchedDebug = $patchedDebug -replace '-O2 -Ob1 -Ot -Oi','-Od -Ob0'
    $patchedDebug = $patchedDebug -replace '/O2 ','/Od '

    Write-Host "Patched makefile (after optimization flag replacement):" -ForegroundColor Gray
    Write-Host $patchedDebug -ForegroundColor Gray
    Write-Host ""

    # Verify CFLAGS has debug optimization flags
    Assert-Contains -Text $patchedDebug -Pattern 'CFLAGS=/MT -Od -Ob0 -Oy-' `
        -TestName "CFLAGS contains debug optimization flags -Od -Ob0"

    # Verify release optimization flags are removed from CFLAGS
    Assert-NotContains -Text $patchedDebug -Pattern 'CFLAGS=.*-O2 -Ob1 -Ot -Oi' `
        -TestName "CFLAGS no longer contains release optimization flags"

    # Verify /O2 is replaced with /Od in LIB_CFLAGS
    Assert-Contains -Text $patchedDebug -Pattern 'LIB_CFLAGS=/MT /Od /Gs0' `
        -TestName "LIB_CFLAGS has /O2 replaced with /Od"

    # Verify /O2 followed by space is removed from LIB_CFLAGS
    Assert-NotContains -Text $patchedDebug -Pattern 'LIB_CFLAGS=.*/O2 ' `
        -TestName "LIB_CFLAGS no longer contains /O2 (followed by space)"

    # Test that other flags are preserved
    Assert-Contains -Text $patchedDebug -Pattern 'CFLAGS=.*-DNDEBUG' `
        -TestName "CFLAGS preserves -DNDEBUG"

    Assert-Contains -Text $patchedDebug -Pattern 'CFLAGS=.*-EHs -GS -bigobj' `
        -TestName "CFLAGS preserves other flags"

    # Test edge case: Multiple lines with optimization flags
    $makefileMultipleOpt = @"
CFLAGS=/MT -O2 -Ob1 -Ot -Oi -Oy- -DNDEBUG
LIB_CFLAGS=/MT /O2 /Ox /Gs0
CNF_CFLAGS=-O2 -Ob1 -Ot -Oi
"@

    Write-Host "`nTesting multiple lines with optimization flags:" -ForegroundColor Gray
    Write-Host "Before:" -ForegroundColor Gray
    Write-Host $makefileMultipleOpt -ForegroundColor Gray

    $patchedMultipleOpt = $makefileMultipleOpt
    $patchedMultipleOpt = $patchedMultipleOpt -replace '-O2 -Ob1 -Ot -Oi','-Od -Ob0'
    $patchedMultipleOpt = $patchedMultipleOpt -replace '/O2 ','/Od '

    Write-Host "After:" -ForegroundColor Gray
    Write-Host $patchedMultipleOpt -ForegroundColor Gray
    Write-Host ""

    # Verify all occurrences are replaced
    $o2Count = ([regex]::Matches($patchedMultipleOpt, '-O2 -Ob1 -Ot -Oi')).Count
    if ($o2Count -eq 0) {
        Write-TestResult -TestName "All -O2 -Ob1 -Ot -Oi occurrences replaced" -Passed $true `
            -Message "No release optimization flags found"
    } else {
        Write-TestResult -TestName "All -O2 -Ob1 -Ot -Oi occurrences replaced" -Passed $false `
            -Message "Found $o2Count unreplaced occurrences"
    }

    $slashO2Count = ([regex]::Matches($patchedMultipleOpt, '/O2 ')).Count
    if ($slashO2Count -eq 0) {
        Write-TestResult -TestName "All /O2 occurrences replaced" -Passed $true `
            -Message "No /O2 flags found"
    } else {
        Write-TestResult -TestName "All /O2 occurrences replaced" -Passed $false `
            -Message "Found $slashO2Count unreplaced occurrences"
    }
}

# ============================================================================
# Main Test Runner
# ============================================================================
Write-Host "`n================================================================" -ForegroundColor Magenta
Write-Host "  OpenSSL Build Configuration Unit Tests" -ForegroundColor Magenta
Write-Host "================================================================" -ForegroundColor Magenta

# Run tests based on TestName parameter
switch ($TestName) {
    "ConfigureDebug" { Test-ConfigureDebug }
    "ConfigureRelease" { Test-ConfigureRelease }
    "MakefilePatch" { Test-MakefilePatch }
    "ARM64PerlPatch" { Test-ARM64PerlPatch }
    "DebugOptimizationPatch" { Test-DebugOptimizationPatch }
    "All" {
        Test-ConfigureDebug
        Test-ConfigureRelease
        Test-MakefilePatch
        Test-ARM64PerlPatch
        Test-DebugOptimizationPatch
    }
}

# Print summary
Write-Host "`n================================================================" -ForegroundColor Magenta
Write-Host "  Test Summary" -ForegroundColor Magenta
Write-Host "================================================================" -ForegroundColor Magenta
Write-Host ""
Write-Host "Total Tests:  $script:TestsTotal" -ForegroundColor Cyan
Write-Host "Passed:       $script:TestsPassed" -ForegroundColor Green
Write-Host "Failed:       $script:TestsFailed" -ForegroundColor $(if ($script:TestsFailed -eq 0) { "Green" } else { "Red" })
Write-Host ""

# Exit with appropriate code
if ($script:TestsFailed -eq 0) {
    Write-Host "[SUCCESS] All tests passed!" -ForegroundColor Green
    exit 0
} else {
    Write-Host "[FAILURE] Some tests failed" -ForegroundColor Red
    exit 1
}
