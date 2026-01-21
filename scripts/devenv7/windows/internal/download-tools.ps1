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
# Download and installation utilities for development tools
#

# Import common utilities
$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
Import-Module (Join-Path $scriptRoot "common.ps1") -Force

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

# Tool download URLs and configurations
$script:ToolConfigs = @{
    Git = @{
        BaseUrl = "https://github.com/git-for-windows/git/releases/download"
        VersionPattern = "v{VERSION}.windows.1"
        FilePatterns = @{
            arm64 = "PortableGit-{VERSION}-arm64.7z.exe"
            x64   = "PortableGit-{VERSION}-64-bit.7z.exe"
            x86   = "PortableGit-{VERSION}-32-bit.7z.exe"
        }
    }
    Python = @{
        BaseUrl = "https://www.python.org/ftp/python"
        VersionPattern = "{VERSION}"
        FilePatterns = @{
            arm64 = "python-{VERSION}-embed-arm64.zip"
            x64   = "python-{VERSION}-embed-amd64.zip"
            x86   = "python-{VERSION}-embed-win32.zip"
        }
    }
    MSYS2 = @{
        BaseUrl = "https://repo.msys2.org/distrib/x86_64"
        VersionPattern = ""
        FilePatterns = @{
            # MSYS2 doesn't have ARM64 native support yet, use x64 for all architectures
            # Note: As of 2024-05-07, MSYS2 uses .tar.zst format (was .tar.xz)
            # Download directly from repo.msys2.org instead of GitHub releases
            arm64 = "msys2-base-x86_64-{VERSION}.tar.zst"
            x64   = "msys2-base-x86_64-{VERSION}.tar.zst"
        }
    }
    StrawberryPerl = @{
        BaseUrl = "https://strawberryperl.com/download"
        VersionPattern = "{VERSION}"
        FilePatterns = @{
            x64 = "strawberry-perl-{VERSION}-64bit-portable.zip"
            x86 = "strawberry-perl-no64-{VERSION}-32bit-portable.zip"
        }
    }
    JSONSpirit = @{
        BaseUrl = "https://storage.googleapis.com/swblocks-dist/devenv/deps"
        VersionPattern = ""
        FilePatterns = @{
            source = "json-spirit.tar.gz"
        }
    }
    OpenJDK = @{
        BaseUrl = "https://aka.ms/download-jdk"
        VersionPattern = ""
        FilePatterns = @{
            arm64 = "microsoft-jdk-{VERSION}.0.1-windows-aarch64.zip"
            x64   = "microsoft-jdk-{VERSION}.0.1-windows-x64.zip"
        }
    }
    Gradle = @{
        BaseUrl = "https://services.gradle.org/distributions"
        VersionPattern = ""
        FilePatterns = @{
            all = "gradle-{VERSION}-bin.zip"
        }
    }
    Boost = @{
        BaseUrl = "https://archives.boost.io/release"
        VersionPattern = "{VERSION}/source"
        FilePatterns = @{
            source = "boost_{VERSION_UNDERSCORE}.tar.gz"
        }
    }
    OpenSSL = @{
        BaseUrl = "https://www.openssl.org/source"
        VersionPattern = ""
        FilePatterns = @{
            source = "openssl-{VERSION}.tar.gz"
        }
    }
    Jom = @{
        BaseUrl = "https://download.qt.io/official_releases/jom"
        VersionPattern = ""
        FilePatterns = @{
            all = "jom_{VERSION_UNDERSCORE}.zip"
        }
    }
    SevenZip = @{
        BaseUrl = "https://www.7-zip.org/a"
        VersionPattern = ""
        FilePatterns = @{
            all = "7z{VERSION_NODOT}-extra.7z"
        }
    }
    NASM = @{
        BaseUrl = "https://www.nasm.us/pub/nasm/releasebuilds"
        VersionPattern = "{VERSION}/win{ARCH_BITS}"
        FilePatterns = @{
            # NASM has separate packages for x64 (win64) and x86 (win32)
            x64 = "nasm-{VERSION}-win64.zip"
            x86 = "nasm-{VERSION}-win32.zip"
        }
    }
}

function Get-ToolDownloadUrl {
    param(
        [Parameter(Mandatory=$true)]
        [string]$Tool,

        [Parameter(Mandatory=$true)]
        [string]$Version,

        [Parameter(Mandatory=$true)]
        [string]$Architecture
    )

    $config = $script:ToolConfigs[$Tool]

    if (-not $config) {
        throw "Unknown tool: $Tool"
    }

    # Handle version tag conversions
    $versionTag = $Version
    $versionUnderscore = $Version -replace '\.', '_'
    $arch = $Architecture

    if ($Tool -eq "StrawberryPerl") {
        # Strawberry Perl: 5.40.0.1 -> 54001
        $versionTag = $Version -replace '\.', ''
    }
    elseif ($Tool -eq "Gradle") {
        # Gradle: use 'all' architecture key
        $arch = "all"
    }
    elseif ($Tool -eq "Boost" -or $Tool -eq "OpenSSL") {
        # Boost/OpenSSL: use 'source' architecture key
        $arch = "source"
    }

    # Calculate architecture bits for URL construction (used by NASM)
    $archBits = if ($Architecture -in @("x64", "a64")) { "64" } else { "32" }

    $versionPattern = $config.VersionPattern -replace '\{VERSION\}', $Version -replace '\{VERSION_TAG\}', $versionTag -replace '\{ARCH_BITS\}', $archBits
    $filePattern = $config.FilePatterns[$arch]

    if (-not $filePattern) {
        throw "No file pattern found for tool '$Tool' architecture '$arch'"
    }

    # Handle VERSION_NODOT for 7-Zip
    $versionNoDot = $Version -replace '\.', ''

    $fileName = $filePattern -replace '\{VERSION\}', $Version `
                             -replace '\{VERSION_UNDERSCORE\}', $versionUnderscore `
                             -replace '\{VERSION_NODOT\}', $versionNoDot

    # Build URL - if VersionPattern is empty, don't add extra slash
    if ($versionPattern) {
        $url = "$($config.BaseUrl)/$versionPattern/$fileName"
    } else {
        $url = "$($config.BaseUrl)/$fileName"
    }

    return @{
        Url = $url
        FileName = $fileName
    }
}

function Install-GitPortable {
    param(
        [Parameter(Mandatory=$true)]
        [string]$Version,

        [Parameter(Mandatory=$true)]
        [string]$Architecture,

        [Parameter(Mandatory=$true)]
        [string]$DestinationRoot,

        [string]$CacheDirectory,

        [switch]$SkipDownload
    )

    # Normalize architecture for display and folder naming
    $archNormalized = ConvertTo-ArchitectureName -Architecture $Architecture
    # Convert to VS naming for file pattern lookup
    $archVS = ConvertTo-VSArchitectureName -Architecture $Architecture

    Write-SubSection "Installing Git $Version for $archNormalized"

    # Determine paths - always install to 'default' for host architecture tools
    $installPath = Join-Path $DestinationRoot "git\$Version\default"

    # Set default cache directory if not provided
    if (-not $CacheDirectory) {
        # Use default cache directory based on dist folder name
        $distFolderName = Split-Path $DestinationRoot -Leaf
        $CacheDirectory = Join-Path $env:USERPROFILE "swblocks\$distFolderName-downloads-cache"
    }

    New-DirectoryIfNotExists -Path $installPath | Out-Null
    New-DirectoryIfNotExists -Path $CacheDirectory | Out-Null

    # Download portable archive using VS architecture naming for file patterns
    $downloadInfo = Get-ToolDownloadUrl -Tool "Git" -Version $Version -Architecture $archVS
    $archivePath = Join-Path $CacheDirectory $downloadInfo.FileName

    if (-not $SkipDownload) {
        Invoke-WebDownload -Url $downloadInfo.Url -OutputPath $archivePath `
            -Description "Downloading Git $Version for $archNormalized" -SkipIfExists
    }

    if (-not (Test-Path $archivePath)) {
        throw "Archive file not found: $archivePath"
    }

    # Extract portable Git using self-extracting 7z archive
    Expand-ToolArchive -ArchivePath $archivePath -DestinationPath $installPath `
        -Description "Extracting Git $Version for $archNormalized"

    Write-Log "Git $Version for $archNormalized installed successfully" -Level Success

    return $installPath
}

function Install-PythonEmbeddable {
    param(
        [Parameter(Mandatory=$true)]
        [string]$Version,

        [Parameter(Mandatory=$true)]
        [string]$Architecture,

        [Parameter(Mandatory=$true)]
        [string]$DestinationRoot,

        [string]$CacheDirectory,

        [switch]$SkipDownload
    )

    # Normalize architecture for display and folder naming
    $archNormalized = ConvertTo-ArchitectureName -Architecture $Architecture
    # Convert to VS naming for file pattern lookup
    $archVS = ConvertTo-VSArchitectureName -Architecture $Architecture

    Write-SubSection "Installing Python $Version for $archNormalized"

    # Determine paths - always install to 'default' for host architecture tools
    $installPath = Join-Path $DestinationRoot "python\$Version\default"

    # Set default cache directory if not provided
    if (-not $CacheDirectory) {
        # Use default cache directory based on dist folder name
        $distFolderName = Split-Path $DestinationRoot -Leaf
        $CacheDirectory = Join-Path $env:USERPROFILE "swblocks\$distFolderName-downloads-cache"
    }

    New-DirectoryIfNotExists -Path $installPath | Out-Null
    New-DirectoryIfNotExists -Path $CacheDirectory | Out-Null

    # Download embeddable package using VS architecture naming for file patterns
    $downloadInfo = Get-ToolDownloadUrl -Tool "Python" -Version $Version -Architecture $archVS
    $archivePath = Join-Path $CacheDirectory $downloadInfo.FileName

    if (-not $SkipDownload) {
        Invoke-WebDownload -Url $downloadInfo.Url -OutputPath $archivePath `
            -Description "Downloading Python $Version embeddable for $archNormalized" -SkipIfExists
    }

    if (-not (Test-Path $archivePath)) {
        throw "Archive file not found: $archivePath"
    }

    # Extract embeddable package
    Expand-ToolArchive -ArchivePath $archivePath -DestinationPath $installPath `
        -Description "Extracting Python $Version for $archNormalized"

    Write-Log "Python $Version for $archNormalized installed successfully" -Level Success

    return $installPath
}

function Install-MSYS2 {
    param(
        [Parameter(Mandatory=$true)]
        [string]$Version,

        [Parameter(Mandatory=$true)]
        [string]$Architecture,

        [Parameter(Mandatory=$true)]
        [string]$DestinationRoot,

        [string]$CacheDirectory,

        [switch]$SkipDownload,

        [switch]$UpdatePackages,

        [switch]$InstallMake
    )

    # Normalize architecture for display and folder naming
    $archNormalized = ConvertTo-ArchitectureName -Architecture $Architecture
    # Convert to VS naming for file pattern lookup
    $archVS = ConvertTo-VSArchitectureName -Architecture $Architecture

    Write-SubSection "Installing MSYS2 $Version for $archNormalized"

    $installBase = Join-Path $DestinationRoot "msys2\$Version"

    # Use msys32 for x86, msys64 for x64/ARM64
    $msysFolderName = if ($archNormalized -eq "x86") { "msys32" } else { "msys64" }
    $installPath = Join-Path $installBase $msysFolderName

    # Set default cache directory if not provided
    if (-not $CacheDirectory) {
        # Use default cache directory based on dist folder name
        $distFolderName = Split-Path $DestinationRoot -Leaf
        $CacheDirectory = Join-Path $env:USERPROFILE "swblocks\$distFolderName-downloads-cache"
    }

    New-DirectoryIfNotExists -Path $installBase | Out-Null
    New-DirectoryIfNotExists -Path $CacheDirectory | Out-Null

    # Special case for x86: Use older version and i686 distribution
    # (MSYS2 dropped x86 support after version 20250209)
    if ($archNormalized -eq "x86") {
        Write-Log "x86 host: Using MSYS2 version 20250209 (last i686 build) instead of $Version" -Level Info
        $downloadUrl = "https://repo.msys2.org/distrib/i686/msys2-base-i686-20250209.tar.zst"
        $fileName = "msys2-base-i686-20250209.tar.zst"
    } else {
        # Standard x64/ARM64 path
        $downloadInfo = Get-ToolDownloadUrl -Tool "MSYS2" -Version $Version -Architecture $archVS
        $downloadUrl = $downloadInfo.Url
        $fileName = $downloadInfo.FileName
    }

    $archivePath = Join-Path $CacheDirectory $fileName

    if (-not $SkipDownload) {
        Invoke-WebDownload -Url $downloadUrl -OutputPath $archivePath `
            -Description "Downloading MSYS2 $Version for $archNormalized" -SkipIfExists
    }

    if (-not (Test-Path $archivePath)) {
        throw "Archive file not found: $archivePath"
    }

    # Remove existing installation if present
    if (Test-Path $installPath) {
        Write-Log "Removing existing MSYS2 installation..." -Level Verbose
        Remove-DirectoryIfExists -Path $installPath -Force
    }

    # Extract MSYS2 base tarball
    Expand-ToolArchive -ArchivePath $archivePath -DestinationPath $installBase `
        -Description "Extracting MSYS2 $Version for $archNormalized"

    if (-not (Test-Path $installPath)) {
        throw "MSYS2 extraction failed - $msysFolderName directory not found"
    }

    Write-Log "MSYS2 $Version for $archNormalized installed successfully" -Level Success

    # Update packages if requested
    if ($UpdatePackages) {
        Write-Log "Updating MSYS2 packages..." -Level Info

        $bash = Join-Path $installPath "usr\bin\bash.exe"

        if (-not (Test-Path $bash)) {
            Write-Log "MSYS2 bash not found, skipping package updates" -Level Warning
        } else {
            # First-time MSYS2 initialization - run a simple command to trigger setup
            Write-Log "Initializing MSYS2 (first-time setup)..." -Level Verbose
            $prevErrorAction = $ErrorActionPreference
            $ErrorActionPreference = "Continue"
            try {
                $null = & $bash -lc "echo MSYS2 initialized" 2>&1
            } finally {
                $ErrorActionPreference = $prevErrorAction
            }

            # Update package database and core packages
            Write-Log "Updating package database..." -Level Verbose
            $ErrorActionPreference = "Continue"
            try {
                $output = & $bash -lc "pacman -Syu --noconfirm" 2>&1
            } finally {
                $ErrorActionPreference = $prevErrorAction
            }

            # Check for errors
            $errors = $output | Where-Object { $_ -match "error:" }
            if ($errors) {
                Write-Log "Package update errors: $errors" -Level Warning
            } else {
                Write-Log "MSYS2 packages updated" -Level Success
            }
        }
    }

    # Install make if requested
    if ($InstallMake) {
        Write-Log "Installing make..." -Level Info

        $bash = Join-Path $installPath "usr\bin\bash.exe"

        if (-not (Test-Path $bash)) {
            Write-Log "MSYS2 bash not found, cannot install make" -Level Warning
        } else {
            # First-time MSYS2 initialization - run a simple command to trigger setup
            Write-Log "Initializing MSYS2 (first-time setup)..." -Level Verbose
            $prevErrorAction = $ErrorActionPreference
            $ErrorActionPreference = "Continue"
            try {
                $null = & $bash -lc "echo MSYS2 initialized" 2>&1
            } finally {
                $ErrorActionPreference = $prevErrorAction
            }

            # Install make package
            Write-Log "Installing make package..." -Level Verbose
            $ErrorActionPreference = "Continue"
            try {
                $output = & $bash -lc "pacman -S --noconfirm make" 2>&1
            } finally {
                $ErrorActionPreference = $prevErrorAction
            }

            # Check if there were any errors (ignore informational messages)
            $errors = $output | Where-Object { $_ -match "error:" }
            if ($errors) {
                Write-Log "make installation errors: $errors" -Level Warning
            }

            # Verify make was installed
            $makeExe = Join-Path $installPath "usr\bin\make.exe"
            if (Test-Path $makeExe) {
                Write-Log "make installed successfully" -Level Success
            } else {
                Write-Log "make installation may have failed" -Level Warning
            }
        }
    }

    return $installPath
}

function Install-StrawberryPerl {
    param(
        [Parameter(Mandatory=$true)]
        [string]$Version,

        [Parameter(Mandatory=$true)]
        [string]$Architecture,

        [Parameter(Mandatory=$true)]
        [string]$DestinationRoot,

        [string]$CacheDirectory,

        [switch]$SkipDownload
    )

    # Normalize architecture for display and folder naming
    $archNormalized = ConvertTo-ArchitectureName -Architecture $Architecture
    # Convert a64 to x64 (ARM64 uses x64 Perl via emulation)
    $perlArch = if ($archNormalized -eq "a64") { "x64" } else { $archNormalized }

    Write-SubSection "Installing Strawberry Perl $Version for $archNormalized"

    # Install to single 'default' folder (same for all host architectures)
    $installPath = Join-Path $DestinationRoot "strawberry-perl\$Version\default"

    # Set default cache directory if not provided
    if (-not $CacheDirectory) {
        # Use default cache directory based on dist folder name
        $distFolderName = Split-Path $DestinationRoot -Leaf
        $CacheDirectory = Join-Path $env:USERPROFILE "swblocks\$distFolderName-downloads-cache"
    }

    New-DirectoryIfNotExists -Path $CacheDirectory | Out-Null

    # Download portable version using architecture-appropriate URL (32bit vs 64bit)
    $downloadInfo = Get-ToolDownloadUrl -Tool "StrawberryPerl" -Version $Version -Architecture $perlArch
    $archivePath = Join-Path $CacheDirectory $downloadInfo.FileName

    if (-not $SkipDownload) {
        Invoke-WebDownload -Url $downloadInfo.Url -OutputPath $archivePath `
            -Description "Downloading Strawberry Perl $Version for $archNormalized" -SkipIfExists
    }

    if (-not (Test-Path $archivePath)) {
        throw "Archive file not found: $archivePath"
    }

    # Clean up destination directory if it exists (to avoid tar overwrite errors)
    if (Test-Path $installPath) {
        Write-Log "Removing existing installation directory: $installPath" -Level Info
        Remove-Item -Path $installPath -Recurse -Force -ErrorAction SilentlyContinue
    }

    New-DirectoryIfNotExists -Path $installPath | Out-Null

    # Extract portable package
    Expand-ToolArchive -ArchivePath $archivePath -DestinationPath $installPath `
        -Description "Extracting Strawberry Perl $Version for $archNormalized"

    Write-Log "Strawberry Perl $Version for $archNormalized installed successfully" -Level Success

    return $installPath
}

function Install-JSONSpirit {
    param(
        [Parameter(Mandatory=$true)]
        [string]$Version,

        [Parameter(Mandatory=$true)]
        [string]$DestinationRoot,

        [string]$CacheDirectory,

        [switch]$SkipDownload
    )

    Write-SubSection "Installing JSON Spirit $Version"

    $installBase = Join-Path $DestinationRoot "json-spirit\$Version"
    $installPath = Join-Path $installBase "source"

    # Set default cache directory if not provided
    if (-not $CacheDirectory) {
        # Use default cache directory based on dist folder name
        $distFolderName = Split-Path $DestinationRoot -Leaf
        $CacheDirectory = Join-Path $env:USERPROFILE "swblocks\$distFolderName-downloads-cache"
    }

    New-DirectoryIfNotExists -Path $installBase | Out-Null
    New-DirectoryIfNotExists -Path $CacheDirectory | Out-Null

    # Download source archive
    $downloadInfo = Get-ToolDownloadUrl -Tool "JSONSpirit" -Version $Version -Architecture "source"
    $archivePath = Join-Path $CacheDirectory $downloadInfo.FileName

    if (-not $SkipDownload) {
        Invoke-WebDownload -Url $downloadInfo.Url -OutputPath $archivePath `
            -Description "Downloading JSON Spirit $Version" -SkipIfExists
    }

    if (-not (Test-Path $archivePath)) {
        throw "Archive file not found: $archivePath"
    }

    # Create a temporary extraction directory
    $tempExtractPath = Join-Path $installBase "temp-extract"
    if (Test-Path $tempExtractPath) {
        Remove-Item -Path $tempExtractPath -Recurse -Force
    }
    New-DirectoryIfNotExists -Path $tempExtractPath | Out-Null

    # Extract to temp directory
    # Archive structure: json-spirit/4.08/source/(files)
    Expand-ToolArchive -ArchivePath $archivePath -DestinationPath $tempExtractPath `
        -Description "Extracting JSON Spirit $Version"

    # The archive contains json-spirit/{version}/source/
    # We want to move the 'source' directory to our final location
    $extractedSourcePath = Join-Path $tempExtractPath "json-spirit\$Version\source"
    if (Test-Path $extractedSourcePath) {
        # Remove destination if it exists
        if (Test-Path $installPath) {
            Remove-Item -Path $installPath -Recurse -Force
        }

        # Create parent directory
        $installBase = Split-Path -Parent $installPath
        New-DirectoryIfNotExists -Path $installBase | Out-Null

        # Move the source folder to final location
        # Use Rename-Item instead of Move-Item to avoid nesting issues
        Move-Item -Path $extractedSourcePath -Destination $installPath -Force

        # Verify no extra nesting occurred
        $unwantedNesting = Join-Path $installPath "4.08"
        if (Test-Path $unwantedNesting) {
            throw "Unwanted nesting detected at: $unwantedNesting - extraction or move failed"
        }

        # Clean up temp directory
        Remove-Item -Path $tempExtractPath -Recurse -Force -ErrorAction SilentlyContinue

        Write-Log "JSON Spirit $Version installed successfully" -Level Success
    } else {
        throw "Expected source directory not found after extraction at: $extractedSourcePath"
    }

    return $installPath
}

function Install-OpenJDK {
    param(
        [Parameter(Mandatory=$true)]
        [string]$Version,

        [Parameter(Mandatory=$true)]
        [string]$Architecture,

        [Parameter(Mandatory=$true)]
        [string]$DestinationRoot,

        [string]$CacheDirectory,

        [switch]$SkipDownload
    )

    # Normalize architecture for display and folder naming
    $archNormalized = ConvertTo-ArchitectureName -Architecture $Architecture
    # Convert to VS naming for file pattern lookup
    $archVS = ConvertTo-VSArchitectureName -Architecture $Architecture

    Write-SubSection "Installing OpenJDK $Version for $archNormalized"

    # Determine paths - use architecture-specific naming for multi-arch support
    $installPath = Join-Path $DestinationRoot "openjdk\$Version\$archNormalized"

    # Set default cache directory if not provided
    if (-not $CacheDirectory) {
        # Use default cache directory based on dist folder name
        $distFolderName = Split-Path $DestinationRoot -Leaf
        $CacheDirectory = Join-Path $env:USERPROFILE "swblocks\$distFolderName-downloads-cache"
    }

    New-DirectoryIfNotExists -Path $installPath | Out-Null
    New-DirectoryIfNotExists -Path $CacheDirectory | Out-Null

    # Download JDK archive using VS architecture naming for file patterns
    $downloadInfo = Get-ToolDownloadUrl -Tool "OpenJDK" -Version $Version -Architecture $archVS
    $archivePath = Join-Path $CacheDirectory $downloadInfo.FileName

    if (-not $SkipDownload) {
        Invoke-WebDownload -Url $downloadInfo.Url -OutputPath $archivePath `
            -Description "Downloading OpenJDK $Version for $archNormalized" -SkipIfExists
    }

    if (-not (Test-Path $archivePath)) {
        throw "Archive file not found: $archivePath"
    }

    # Extract JDK (archive contains jdk-<version> subdirectory)
    $tempExtractPath = Join-Path $env:TEMP "openjdk-extract-$Version-$archNormalized"

    if (Test-Path $tempExtractPath) {
        Remove-DirectoryIfExists -Path $tempExtractPath -Force
    }

    Expand-ToolArchive -ArchivePath $archivePath -DestinationPath $tempExtractPath `
        -Description "Extracting OpenJDK $Version for $archNormalized"

    # The archive extracts to a jdk-<version> subdirectory, move contents up
    $extractedDir = Get-ChildItem -Path $tempExtractPath -Directory | Select-Object -First 1

    if ($extractedDir) {
        Copy-DirectoryWithProgress -Source $extractedDir.FullName -Destination $installPath `
            -Description "Copying OpenJDK files"
    } else {
        throw "OpenJDK extraction failed - no subdirectory found"
    }

    # Clean up temp extraction
    Remove-DirectoryIfExists -Path $tempExtractPath -Force

    Write-Log "OpenJDK $Version for $archNormalized installed successfully" -Level Success

    return $installPath
}

function Install-Gradle {
    param(
        [Parameter(Mandatory=$true)]
        [string]$Version,

        [Parameter(Mandatory=$true)]
        [string]$DestinationRoot,

        [string]$CacheDirectory,

        [switch]$SkipDownload
    )

    Write-SubSection "Installing Gradle $Version"

    $installPath = Join-Path $DestinationRoot "gradle\$Version\default"

    # Set default cache directory if not provided
    if (-not $CacheDirectory) {
        # Use default cache directory based on dist folder name
        $distFolderName = Split-Path $DestinationRoot -Leaf
        $CacheDirectory = Join-Path $env:USERPROFILE "swblocks\$distFolderName-downloads-cache"
    }

    New-DirectoryIfNotExists -Path $installPath | Out-Null
    New-DirectoryIfNotExists -Path $CacheDirectory | Out-Null

    # Download Gradle archive
    $downloadInfo = Get-ToolDownloadUrl -Tool "Gradle" -Version $Version -Architecture "all"
    $archivePath = Join-Path $CacheDirectory $downloadInfo.FileName

    if (-not $SkipDownload) {
        Invoke-WebDownload -Url $downloadInfo.Url -OutputPath $archivePath `
            -Description "Downloading Gradle $Version" -SkipIfExists
    }

    if (-not (Test-Path $archivePath)) {
        throw "Archive file not found: $archivePath"
    }

    # Extract Gradle (archive contains gradle-<version> subdirectory)
    $tempExtractPath = Join-Path $env:TEMP "gradle-extract-$Version"

    if (Test-Path $tempExtractPath) {
        Remove-DirectoryIfExists -Path $tempExtractPath -Force
    }

    Expand-ToolArchive -ArchivePath $archivePath -DestinationPath $tempExtractPath `
        -Description "Extracting Gradle $Version"

    # The archive extracts to a gradle-<version> subdirectory, move contents up
    $extractedDir = Get-ChildItem -Path $tempExtractPath -Directory | Select-Object -First 1

    if ($extractedDir) {
        Copy-DirectoryWithProgress -Source $extractedDir.FullName -Destination $installPath `
            -Description "Copying Gradle files"
    } else {
        throw "Gradle extraction failed - no subdirectory found"
    }

    # Clean up temp extraction
    Remove-DirectoryIfExists -Path $tempExtractPath -Force

    Write-Log "Gradle $Version installed successfully" -Level Success

    return $installPath
}

function Install-Jom {
    param(
        [Parameter(Mandatory=$true)]
        [string]$Version,

        [Parameter(Mandatory=$true)]
        [string]$DestinationRoot,

        [string]$CacheDirectory,

        [switch]$SkipDownload
    )

    Write-SubSection "Installing Jom $Version"

    $installPath = Join-Path $DestinationRoot "jom\$Version\default"

    # Set default cache directory if not provided
    if (-not $CacheDirectory) {
        # Use default cache directory based on dist folder name
        $distFolderName = Split-Path $DestinationRoot -Leaf
        $CacheDirectory = Join-Path $env:USERPROFILE "swblocks\$distFolderName-downloads-cache"
    }

    New-DirectoryIfNotExists -Path $installPath | Out-Null
    New-DirectoryIfNotExists -Path $CacheDirectory | Out-Null

    # Download Jom archive
    $downloadInfo = Get-ToolDownloadUrl -Tool "Jom" -Version $Version -Architecture "all"
    $archivePath = Join-Path $CacheDirectory $downloadInfo.FileName

    if (-not $SkipDownload) {
        Invoke-WebDownload -Url $downloadInfo.Url -OutputPath $archivePath `
            -Description "Downloading Jom $Version" -SkipIfExists
    }

    if (-not (Test-Path $archivePath)) {
        throw "Archive file not found: $archivePath"
    }

    # Extract Jom (archive contains jom.exe and related files directly)
    Expand-ToolArchive -ArchivePath $archivePath -DestinationPath $installPath `
        -Description "Extracting Jom $Version"

    Write-Log "Jom $Version installed successfully" -Level Success

    return $installPath
}

function Install-SevenZip {
    param(
        [Parameter(Mandatory=$true)]
        [string]$Version,

        [Parameter(Mandatory=$true)]
        [string]$DestinationRoot,

        [string]$CacheDirectory,

        [switch]$SkipDownload
    )

    Write-SubSection "Installing 7-Zip $Version (extra edition)"

    $installPath = Join-Path $DestinationRoot "7zip\$Version"

    # Set default cache directory if not provided
    if (-not $CacheDirectory) {
        # Use default cache directory based on dist folder name
        $distFolderName = Split-Path $DestinationRoot -Leaf
        $CacheDirectory = Join-Path $env:USERPROFILE "swblocks\$distFolderName-downloads-cache"
    }

    New-DirectoryIfNotExists -Path $installPath | Out-Null
    New-DirectoryIfNotExists -Path $CacheDirectory | Out-Null

    # Download 7-Zip extra edition (contains 7za.exe standalone)
    $downloadInfo = Get-ToolDownloadUrl -Tool "SevenZip" -Version $Version -Architecture "all"
    $archivePath = Join-Path $CacheDirectory $downloadInfo.FileName

    if (-not $SkipDownload) {
        Invoke-WebDownload -Url $downloadInfo.Url -OutputPath $archivePath `
            -Description "Downloading 7-Zip $Version (extra edition)" -SkipIfExists
    }

    if (-not (Test-Path $archivePath)) {
        throw "Archive file not found: $archivePath"
    }

    # Extract 7-Zip using Windows 11's native tar command
    # Windows 11's tar supports 7z archives natively
    Write-Host "Extracting 7-Zip $Version (extra edition) using tar..."

    # Create a temporary extraction directory
    $tempExtractPath = Join-Path $installPath "temp_extract"
    New-DirectoryIfNotExists -Path $tempExtractPath | Out-Null

    # Use tar to extract the 7z archive
    $tarArgs = @("-xf", "`"$archivePath`"", "-C", "`"$tempExtractPath`"")
    Write-Log "Running: tar $($tarArgs -join ' ')" -Level Verbose

    $extractProcess = Start-Process -FilePath "tar.exe" -ArgumentList $tarArgs `
        -Wait -PassThru -NoNewWindow -RedirectStandardError (Join-Path $env:TEMP "7zip_extract_error.log")

    if ($extractProcess.ExitCode -ne 0) {
        $errorLog = Get-Content (Join-Path $env:TEMP "7zip_extract_error.log") -Raw -ErrorAction SilentlyContinue
        throw "Failed to extract 7-Zip archive using tar. Exit code: $($extractProcess.ExitCode). Error: $errorLog"
    }

    # The 7z extra package contains multiple architectures
    # Find and move 7za.exe and related files to the install directory
    $filesToCopy = @("7za.exe", "7za.dll", "7zxa.dll", "License.txt", "readme.txt")
    foreach ($fileName in $filesToCopy) {
        $sourceFile = Get-ChildItem -Path $tempExtractPath -Filter $fileName -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($sourceFile) {
            Copy-Item -Path $sourceFile.FullName -Destination $installPath -Force
            Write-Log "Copied $fileName to installation directory" -Level Verbose
        }
    }

    # Clean up temporary extraction directory
    Remove-Item -Path $tempExtractPath -Recurse -Force -ErrorAction SilentlyContinue

    Write-Log "7-Zip $Version (extra edition) installed successfully" -Level Success

    return $installPath
}

function Install-NASM {
    param(
        [Parameter(Mandatory=$true)]
        [string]$Version,

        [Parameter(Mandatory=$true)]
        [string]$Architecture,

        [Parameter(Mandatory=$true)]
        [string]$DestinationRoot,

        [string]$CacheDirectory,

        [switch]$SkipDownload
    )

    # Normalize architecture for display and folder naming
    $archNormalized = ConvertTo-ArchitectureName -Architecture $Architecture
    # Convert a64 to x64 (ARM64 uses x64 NASM via emulation)
    $nasmArch = if ($archNormalized -eq "a64") { "x64" } else { $archNormalized }

    Write-SubSection "Installing NASM $Version for $archNormalized"

    # NASM is installed to a single 'default' folder (same for all host architectures)
    $installPath = Join-Path $DestinationRoot "nasm\$Version\default"

    # Set default cache directory if not provided
    if (-not $CacheDirectory) {
        # Use default cache directory based on dist folder name
        $distFolderName = Split-Path $DestinationRoot -Leaf
        $CacheDirectory = Join-Path $env:USERPROFILE "swblocks\$distFolderName-downloads-cache"
    }

    New-DirectoryIfNotExists -Path $installPath | Out-Null
    New-DirectoryIfNotExists -Path $CacheDirectory | Out-Null

    # Download NASM using architecture-appropriate URL (win32 vs win64)
    $downloadInfo = Get-ToolDownloadUrl -Tool "NASM" -Version $Version -Architecture $nasmArch
    $archivePath = Join-Path $CacheDirectory $downloadInfo.FileName

    if (-not $SkipDownload) {
        Invoke-WebDownload -Url $downloadInfo.Url -OutputPath $archivePath `
            -Description "Downloading NASM $Version for $archNormalized" -SkipIfExists
    }

    if (-not (Test-Path $archivePath)) {
        throw "Archive file not found: $archivePath"
    }

    # Extract NASM
    Expand-ToolArchive -ArchivePath $archivePath -DestinationPath $installPath `
        -Description "Extracting NASM $Version for $archNormalized"

    # The archive extracts to nasm-{version}/ subdirectory
    # Move contents to the install path root
    $extractedDir = Join-Path $installPath "nasm-$Version"
    if (Test-Path $extractedDir) {
        # Move all files from nasm-{version} to install path
        Get-ChildItem -Path $extractedDir | ForEach-Object {
            Move-Item -Path $_.FullName -Destination $installPath -Force
        }
        # Remove empty directory
        Remove-Item -Path $extractedDir -Force
    }

    Write-Log "NASM $Version for $archNormalized installed successfully" -Level Success

    return $installPath
}

function Install-Boost {
    param(
        [Parameter(Mandatory=$true)]
        [string]$Version,

        [Parameter(Mandatory=$true)]
        [string]$DestinationRoot,

        [string]$CacheDirectory,

        [switch]$SkipDownload
    )

    Write-SubSection "Installing Boost $Version"

    $installPath = Join-Path $DestinationRoot "boost\$Version\source-windows"

    # Set default cache directory if not provided
    if (-not $CacheDirectory) {
        # Use default cache directory based on dist folder name
        $distFolderName = Split-Path $DestinationRoot -Leaf
        $CacheDirectory = Join-Path $env:USERPROFILE "swblocks\$distFolderName-downloads-cache"
    }

    New-DirectoryIfNotExists -Path $installPath | Out-Null
    New-DirectoryIfNotExists -Path $CacheDirectory | Out-Null

    # Download Boost source archive
    $downloadInfo = Get-ToolDownloadUrl -Tool "Boost" -Version $Version -Architecture "source"
    $archivePath = Join-Path $CacheDirectory $downloadInfo.FileName

    if (-not $SkipDownload) {
        Invoke-WebDownload -Url $downloadInfo.Url -OutputPath $archivePath `
            -Description "Downloading Boost $Version" -SkipIfExists
    }

    if (-not (Test-Path $archivePath)) {
        throw "Archive file not found: $archivePath"
    }

    # Extract Boost source (archive contains boost_<version> subdirectory)
    $tempExtractPath = Join-Path $env:TEMP "boost-extract-$Version"

    if (Test-Path $tempExtractPath) {
        Remove-DirectoryIfExists -Path $tempExtractPath -Force
    }

    Expand-ToolArchive -ArchivePath $archivePath -DestinationPath $tempExtractPath `
        -Description "Extracting Boost $Version"

    # The archive extracts to a boost_<version> subdirectory, move contents up
    $extractedDir = Get-ChildItem -Path $tempExtractPath -Directory | Select-Object -First 1

    if ($extractedDir) {
        Copy-DirectoryWithProgress -Source $extractedDir.FullName -Destination $installPath `
            -Description "Copying Boost source"
    } else {
        throw "Boost extraction failed - no subdirectory found"
    }

    # Clean up temp extraction
    Remove-DirectoryIfExists -Path $tempExtractPath -Force

    Write-Log "Boost $Version installed successfully" -Level Success

    return $installPath
}

function Install-OpenSSL {
    param(
        [Parameter(Mandatory=$true)]
        [string]$Version,

        [Parameter(Mandatory=$true)]
        [string]$DestinationRoot,

        [string]$CacheDirectory,

        [switch]$SkipDownload
    )

    Write-SubSection "Installing OpenSSL $Version"

    $installPath = Join-Path $DestinationRoot "openssl\$Version\source-windows"

    # Set default cache directory if not provided
    if (-not $CacheDirectory) {
        # Use default cache directory based on dist folder name
        $distFolderName = Split-Path $DestinationRoot -Leaf
        $CacheDirectory = Join-Path $env:USERPROFILE "swblocks\$distFolderName-downloads-cache"
    }

    New-DirectoryIfNotExists -Path $installPath | Out-Null
    New-DirectoryIfNotExists -Path $CacheDirectory | Out-Null

    # Download OpenSSL source archive
    $downloadInfo = Get-ToolDownloadUrl -Tool "OpenSSL" -Version $Version -Architecture "source"
    $archivePath = Join-Path $CacheDirectory $downloadInfo.FileName

    if (-not $SkipDownload) {
        Invoke-WebDownload -Url $downloadInfo.Url -OutputPath $archivePath `
            -Description "Downloading OpenSSL $Version" -SkipIfExists
    }

    if (-not (Test-Path $archivePath)) {
        throw "Archive file not found: $archivePath"
    }

    # Extract OpenSSL source (archive contains openssl-<version> subdirectory)
    $tempExtractPath = Join-Path $env:TEMP "openssl-extract-$Version"

    if (Test-Path $tempExtractPath) {
        Remove-DirectoryIfExists -Path $tempExtractPath -Force
    }

    Expand-ToolArchive -ArchivePath $archivePath -DestinationPath $tempExtractPath `
        -Description "Extracting OpenSSL $Version"

    # The archive extracts to a openssl-<version> subdirectory, move contents up
    $extractedDir = Get-ChildItem -Path $tempExtractPath -Directory | Select-Object -First 1

    if ($extractedDir) {
        Copy-DirectoryWithProgress -Source $extractedDir.FullName -Destination $installPath `
            -Description "Copying OpenSSL source"
    } else {
        throw "OpenSSL extraction failed - no subdirectory found"
    }

    # Clean up temp extraction
    Remove-DirectoryIfExists -Path $tempExtractPath -Force

    Write-Log "OpenSSL $Version installed successfully" -Level Success

    return $installPath
}

# Export functions
# Export-ModuleMember -Function @(
#     'Get-ToolDownloadUrl',
#     'Install-GitPortable',
#     'Install-PythonEmbeddable',
#     'Install-MSYS2',
#     'Install-StrawberryPerl',
#     'Install-JSONSpirit'
# )
