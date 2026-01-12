################################################################################
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
################################################################################
#
# Common utilities for Windows devenv7 build scripts
#

# Strict mode
Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

# Script-level variables
$script:VerboseLogging = $false
$script:LogFile = $null

function Write-Log {
    param(
        [Parameter(Mandatory=$true)]
        [string]$Message,

        [ValidateSet('Info', 'Warning', 'Error', 'Success', 'Verbose')]
        [string]$Level = 'Info'
    )

    $timestamp = Get-Date -Format "yyyy-MM-dd HH:mm:ss"
    $prefix = switch ($Level) {
        'Info'    { "[INFO]   " }
        'Warning' { "[WARN]   " }
        'Error'   { "[ERROR]  " }
        'Success' { "[OK]     " }
        'Verbose' { "[VERBOSE]" }
    }

    $logLine = "$timestamp $prefix $Message"

    # Write to console with color
    $color = switch ($Level) {
        'Info'    { 'White' }
        'Warning' { 'Yellow' }
        'Error'   { 'Red' }
        'Success' { 'Green' }
        'Verbose' { 'Gray' }
    }

    if ($Level -ne 'Verbose' -or $script:VerboseLogging) {
        Write-Host $logLine -ForegroundColor $color
    }

    # Write to log file if configured
    if ($script:LogFile) {
        Add-Content -Path $script:LogFile -Value $logLine
    }
}

function Initialize-Logging {
    param(
        [string]$LogPath,
        [bool]$Verbose = $false
    )

    $script:VerboseLogging = $Verbose

    if ($LogPath) {
        $script:LogFile = $LogPath
        $logDir = Split-Path -Parent $LogPath
        if (-not (Test-Path $logDir)) {
            New-Item -Path $logDir -ItemType Directory -Force | Out-Null
        }
        Write-Log "Logging initialized: $LogPath" -Level Info
    }
}

function Test-Administrator {
    $currentUser = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object Security.Principal.WindowsPrincipal($currentUser)
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Get-NativeArchitecture {
    $arch = $env:PROCESSOR_ARCHITECTURE

    switch ($arch) {
        "AMD64" { return "x64" }
        "ARM64" { return "arm64" }
        "x86"   { return "x86" }
        default {
            Write-Log "Unknown architecture: $arch, defaulting to x64" -Level Warning
            return "x64"
        }
    }
}

function ConvertTo-ArchitectureName {
    param(
        [Parameter(Mandatory=$true)]
        [string]$Architecture
    )

    switch ($Architecture.ToLower()) {
        "arm64"  { return "arm64" }
        "x64"    { return "x64" }
        "amd64"  { return "x64" }
        "x86"    { return "x86" }
        "win32"  { return "x86" }
        default  {
            throw "Unknown architecture: $Architecture"
        }
    }
}

function Test-PathExists {
    param(
        [Parameter(Mandatory=$true)]
        [string]$Path,

        [string]$Description = "Path"
    )

    if (Test-Path $Path) {
        Write-Log "$Description exists: $Path" -Level Verbose
        return $true
    } else {
        Write-Log "$Description does not exist: $Path" -Level Verbose
        return $false
    }
}

function New-DirectoryIfNotExists {
    param(
        [Parameter(Mandatory=$true)]
        [string]$Path
    )

    if (-not (Test-Path $Path)) {
        Write-Log "Creating directory: $Path" -Level Verbose
        New-Item -Path $Path -ItemType Directory -Force | Out-Null
        return $true
    }
    return $false
}

function Remove-DirectoryIfExists {
    param(
        [Parameter(Mandatory=$true)]
        [string]$Path,

        [switch]$Force
    )

    if (Test-Path $Path) {
        Write-Log "Removing directory: $Path" -Level Verbose

        # Convert to absolute path if relative
        $absolutePath = (Resolve-Path -Path $Path -ErrorAction SilentlyContinue).Path
        if (-not $absolutePath) {
            $absolutePath = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Path)
        }

        # Use UNC path prefix to handle long paths (> 260 characters)
        # This tells Windows to disable path parsing and send the string directly to the file system driver
        if (-not $absolutePath.StartsWith("\\?\")) {
            $absolutePath = "\\?\$absolutePath"
        }

        Remove-Item -LiteralPath $absolutePath -Recurse -Force:$Force
        return $true
    }
    return $false
}

function Copy-DirectoryWithProgress {
    param(
        [Parameter(Mandatory=$true)]
        [string]$Source,

        [Parameter(Mandatory=$true)]
        [string]$Destination,

        [string]$Description = "Copying"
    )

    if (-not (Test-Path $Source)) {
        throw "Source directory does not exist: $Source"
    }

    Write-Log "$Description from $Source to $Destination" -Level Info

    # Create destination if it doesn't exist
    New-DirectoryIfNotExists -Path $Destination | Out-Null

    # Use robocopy for efficient copying with progress
    $robocopyArgs = @(
        $Source,
        $Destination,
        "/E",           # Copy subdirectories, including empty ones
        "/NP",          # No progress (we'll show our own)
        "/NDL",         # No directory list
        "/NFL",         # No file list
        "/NC",          # No class
        "/NS",          # No size
        "/NJS",         # No job summary
        "/MT:8"         # Multi-threaded (8 threads)
    )

    $result = & robocopy @robocopyArgs
    $exitCode = $LASTEXITCODE

    # Robocopy exit codes: 0-7 are success, 8+ are errors
    if ($exitCode -ge 8) {
        throw "Failed to copy directory. Robocopy exit code: $exitCode"
    }

    Write-Log "Successfully copied directory" -Level Success
}

function Get-FileHash256 {
    param(
        [Parameter(Mandatory=$true)]
        [string]$Path
    )

    if (-not (Test-Path $Path)) {
        throw "File does not exist: $Path"
    }

    $hash = Get-FileHash -Path $Path -Algorithm SHA256
    return $hash.Hash
}

function Invoke-WebDownload {
    param(
        [Parameter(Mandatory=$true)]
        [string]$Url,

        [Parameter(Mandatory=$true)]
        [string]$OutputPath,

        [string]$Description = "Downloading file",

        [string]$ExpectedHash = $null,

        [switch]$SkipIfExists
    )

    # Check if file already exists
    if ($SkipIfExists -and (Test-Path $OutputPath)) {
        Write-Log "File already exists, skipping download: $OutputPath" -Level Info

        # Verify hash if provided
        if ($ExpectedHash) {
            $actualHash = Get-FileHash256 -Path $OutputPath
            if ($actualHash -eq $ExpectedHash) {
                Write-Log "Hash verification passed" -Level Success
                return $true
            } else {
                Write-Log "Hash mismatch, re-downloading file" -Level Warning
                Remove-Item -Path $OutputPath -Force
            }
        } else {
            return $true
        }
    }

    # Create output directory if needed
    $outputDir = Split-Path -Parent $OutputPath
    New-DirectoryIfNotExists -Path $outputDir | Out-Null

    Write-Log "$Description from $Url" -Level Info

    # Retry logic for transient network failures
    $maxRetries = 3
    $retryDelay = 2  # seconds
    $attempt = 0
    $lastError = $null

    while ($attempt -lt $maxRetries) {
        $attempt++

        try {
            if ($attempt -gt 1) {
                Write-Log "Retry attempt $attempt of $maxRetries..." -Level Info
                Start-Sleep -Seconds $retryDelay
            }

            # Try using BITS for better reliability with large files
            try {
                Write-Log "Using BITS transfer for large file..." -Level Verbose
                Start-BitsTransfer -Source $Url -Destination $OutputPath -Description $Description -ErrorAction Stop
                Write-Log "Download completed: $OutputPath" -Level Success
            }
            catch {
                # Fall back to Invoke-WebRequest if BITS fails
                Write-Log "BITS transfer failed, falling back to Invoke-WebRequest..." -Level Verbose
                $ProgressPreference = 'Continue'  # Show progress for large downloads
                Invoke-WebRequest -Uri $Url -OutFile $OutputPath -UseBasicParsing -TimeoutSec 600
                Write-Log "Download completed: $OutputPath" -Level Success
            }

            # Verify hash if provided
            if ($ExpectedHash) {
                Write-Log "Verifying file hash..." -Level Info
                $actualHash = Get-FileHash256 -Path $OutputPath

                if ($actualHash -eq $ExpectedHash) {
                    Write-Log "Hash verification passed" -Level Success
                } else {
                    throw "Hash mismatch! Expected: $ExpectedHash, Got: $actualHash"
                }
            }

            return $true
        }
        catch {
            $lastError = $_
            Write-Log "Download attempt $attempt failed: $_" -Level Warning

            if (Test-Path $OutputPath) {
                Remove-Item -Path $OutputPath -Force -ErrorAction SilentlyContinue
            }

            # Don't retry on hash mismatch or other non-network errors
            if ($_.Exception.Message -like "*Hash mismatch*") {
                throw
            }

            # If this was the last attempt, throw the error
            if ($attempt -ge $maxRetries) {
                Write-Log "Download failed after $maxRetries attempts" -Level Error
                throw $lastError
            }
        }
    }

    # Should not reach here, but just in case
    throw $lastError
}

function Expand-ArchiveWithProgress {
    param(
        [Parameter(Mandatory=$true)]
        [string]$Path,

        [Parameter(Mandatory=$true)]
        [string]$DestinationPath,

        [string]$Description = "Extracting archive"
    )

    if (-not (Test-Path $Path)) {
        throw "Archive does not exist: $Path"
    }

    Write-Log "$Description from $Path to $DestinationPath" -Level Info

    # Create destination directory
    New-DirectoryIfNotExists -Path $DestinationPath | Out-Null

    # Expand archive
    Expand-Archive -Path $Path -DestinationPath $DestinationPath -Force

    Write-Log "Extraction completed" -Level Success
}

function Invoke-NativeCommand {
    param(
        [Parameter(Mandatory=$true)]
        [string]$Command,

        [string[]]$Arguments = @(),

        [string]$Description = "Running command",

        [string]$WorkingDirectory = $null
    )

    $argString = $Arguments -join " "
    Write-Log "$Description : $Command $argString" -Level Info

    $startInfo = New-Object System.Diagnostics.ProcessStartInfo
    $startInfo.FileName = $Command
    $startInfo.Arguments = $argString
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true

    if ($WorkingDirectory) {
        $startInfo.WorkingDirectory = $WorkingDirectory
    }

    $process = New-Object System.Diagnostics.Process
    $process.StartInfo = $startInfo

    $process.Start() | Out-Null

    $stdout = $process.StandardOutput.ReadToEnd()
    $stderr = $process.StandardError.ReadToEnd()

    $process.WaitForExit()

    $exitCode = $process.ExitCode

    if ($stdout) {
        Write-Log "STDOUT: $stdout" -Level Verbose
    }

    if ($stderr) {
        Write-Log "STDERR: $stderr" -Level Verbose
    }

    if ($exitCode -ne 0) {
        Write-Log "Command failed with exit code: $exitCode" -Level Error
        if ($stderr) {
            Write-Log "Error output: $stderr" -Level Error
        }
        throw "Command failed: $Command $argString"
    }

    Write-Log "Command completed successfully" -Level Success

    return @{
        ExitCode = $exitCode
        StdOut = $stdout
        StdErr = $stderr
    }
}

function Format-Bytes {
    param(
        [Parameter(Mandatory=$true)]
        [long]$Bytes
    )

    if ($Bytes -ge 1GB) {
        return "{0:N2} GB" -f ($Bytes / 1GB)
    }
    elseif ($Bytes -ge 1MB) {
        return "{0:N2} MB" -f ($Bytes / 1MB)
    }
    elseif ($Bytes -ge 1KB) {
        return "{0:N2} KB" -f ($Bytes / 1KB)
    }
    else {
        return "$Bytes bytes"
    }
}

function Write-Section {
    param(
        [Parameter(Mandatory=$true)]
        [string]$Title
    )

    $separator = "=" * 80
    Write-Host ""
    Write-Host $separator -ForegroundColor Cyan
    Write-Host $Title -ForegroundColor Cyan
    Write-Host $separator -ForegroundColor Cyan
    Write-Host ""
}

function Write-SubSection {
    param(
        [Parameter(Mandatory=$true)]
        [string]$Title
    )

    $separator = "-" * 60
    Write-Host ""
    Write-Host $separator -ForegroundColor DarkCyan
    Write-Host $Title -ForegroundColor DarkCyan
    Write-Host $separator -ForegroundColor DarkCyan
}

function Expand-ToolArchive {
    param(
        [Parameter(Mandatory=$true)]
        [string]$ArchivePath,

        [Parameter(Mandatory=$true)]
        [string]$DestinationPath,

        [string]$Description = "Extracting archive"
    )

    if (-not (Test-Path $ArchivePath)) {
        throw "Archive file not found: $ArchivePath"
    }

    Write-Log "$Description to: $DestinationPath" -Level Info
    New-DirectoryIfNotExists -Path $DestinationPath | Out-Null

    $extension = [System.IO.Path]::GetExtension($ArchivePath).ToLower()
    $fileName = [System.IO.Path]::GetFileName($ArchivePath)

    try {
        if ($fileName -match '\.7z\.exe$') {
            Write-Log "Extracting self-extracting 7z archive..." -Level Verbose
            $arguments = "-o`"$DestinationPath`" -y"
            $processInfo = Start-Process -FilePath $ArchivePath -ArgumentList $arguments -Wait -NoNewWindow -PassThru

            if ($processInfo.ExitCode -ne 0) {
                throw "Self-extraction failed with exit code $($processInfo.ExitCode)"
            }
        }
        elseif ($extension -eq '.zip' -or $fileName -match '\.(tar|tar\.gz|tar\.xz|tar\.zst|tar\.bz2|tgz)$') {
            Write-Log "Extracting using Windows tar command..." -Level Verbose
            $tarArgs = @("-xf", $ArchivePath, "-C", $DestinationPath)
            $processInfo = Start-Process -FilePath "tar" -ArgumentList $tarArgs -Wait -NoNewWindow -PassThru

            if ($processInfo.ExitCode -ne 0) {
                throw "tar extraction failed with exit code $($processInfo.ExitCode)"
            }
        }
        else {
            throw "Unsupported archive format: $extension (file: $fileName)"
        }

        Write-Log "Extraction completed successfully" -Level Success
        return $true
    }
    catch {
        Write-Log "Extraction failed: $_" -Level Error
        throw
    }
}

# Export functions - commented out for dot-sourcing compatibility
# When using Import-Module, this would be needed
# When using dot-sourcing (. script.ps1), all functions are automatically available
# Export-ModuleMember -Function @(
#     'Write-Log',
#     'Initialize-Logging',
#     'Test-Administrator',
#     'Get-NativeArchitecture',
#     'ConvertTo-ArchitectureName',
#     'Test-PathExists',
#     'New-DirectoryIfNotExists',
#     'Remove-DirectoryIfExists',
#     'Copy-DirectoryWithProgress',
#     'Get-FileHash256',
#     'Invoke-WebDownload',
#     'Expand-ArchiveWithProgress',
#     'Invoke-NativeCommand',
#     'Format-Bytes',
#     'Write-Section',
#     'Write-SubSection'
# )
