<#
Builds the static Little CMS artifacts consumed by ImgVw.

MSYS artifacts are the deployment path:
  powershell -ExecutionPolicy Bypass -File scripts\build-little-cms.ps1 -Mode msys -Arch x86
  powershell -ExecutionPolicy Bypass -File scripts\build-little-cms.ps1 -Mode msys -Arch x64

Visual C++ artifacts are only for Visual Studio local development:
  powershell -ExecutionPolicy Bypass -File scripts\build-little-cms.ps1 -Mode vs -Arch all
#>

param(
    [string]$Version = "2.19.1",
    [string]$Tag = "lcms2.19.1",
    [string]$ExpectedCommit = "21c582a594fe5279f90c0b93437c398f93bf62b0",
    [ValidateSet("all", "vs", "msys", "source")]
    [string]$Mode = "all",
    [ValidateSet("all", "x86", "x64")]
    [string]$Arch = "all",
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path,
    [string]$WorkRoot = (Join-Path $env:TEMP "imgvw-little-cms"),
    [string]$MsysRoot = "",
    [switch]$Clean
)

$ErrorActionPreference = "Stop"

$ConfiguredBuildDirs = @()
$MsysToolPathsInitialized = $false
$MsysShellMarker = "IMGVW_LITTLE_CMS_IN_MSYS_SHELL"

function Invoke-Checked {
    param(
        [string]$FilePath,
        [string[]]$Arguments
    )

    & $FilePath @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$FilePath failed with exit code $LASTEXITCODE"
    }
}

function Add-MsysToolPaths {
    if ($script:MsysToolPathsInitialized) {
        return
    }

    $script:MsysToolPathsInitialized = $true

    $candidateMsysRoots = @()
    if ($MsysRoot) {
        $candidateMsysRoots += $MsysRoot
    }
    if ($env:MSYS2_ROOT) {
        $candidateMsysRoots += $env:MSYS2_ROOT
    }
    $candidateMsysRoots += "C:\msys64"
    $candidateMsysRoots += Get-PSDrive -PSProvider FileSystem |
        ForEach-Object { Join-Path $_.Root "msys64" } |
        Where-Object { Test-Path $_ }
    $candidateMsysRoots = $candidateMsysRoots | Where-Object { $_ } | Select-Object -Unique

    $candidateRoots = @()

    foreach ($candidateMsysRoot in $candidateMsysRoots) {
        if ($env:MSYSTEM_PREFIX -and $env:MSYSTEM_PREFIX -match "^/([^/]+)$") {
            $candidateRoots += (Join-Path $candidateMsysRoot $matches[1])
        }

        switch ($env:MSYSTEM) {
        "UCRT64" { $candidateRoots += (Join-Path $candidateMsysRoot "ucrt64") }
        "MINGW64" { $candidateRoots += (Join-Path $candidateMsysRoot "mingw64") }
        "MINGW32" { $candidateRoots += (Join-Path $candidateMsysRoot "mingw32") }
        "CLANG64" { $candidateRoots += (Join-Path $candidateMsysRoot "clang64") }
        default {
            if ($Arch -eq "x64" -or $Arch -eq "all") {
                $candidateRoots += (Join-Path $candidateMsysRoot "ucrt64")
                $candidateRoots += (Join-Path $candidateMsysRoot "mingw64")
            }
            if ($Arch -eq "x86" -or $Arch -eq "all") {
                $candidateRoots += (Join-Path $candidateMsysRoot "mingw32")
            }
        }
        }

        $candidateRoots += (Join-Path $candidateMsysRoot "usr")
    }

    $toolPaths = $candidateRoots |
        ForEach-Object { Join-Path $_ "bin" } |
        Where-Object { Test-Path $_ } |
        Select-Object -Unique

    foreach ($toolPath in $toolPaths) {
        if ($env:PATH -notlike "*$toolPath*") {
            $env:PATH = "$toolPath;$env:PATH"
        }
    }
}

function Require-Command {
    param([string]$Name)

    Add-MsysToolPaths

    if (-not (Get-Command $Name -ErrorAction SilentlyContinue)) {
        throw "Required command not found in PATH: $Name"
    }
}

function Get-Architectures {
    if ($Arch -eq "all") {
        return @("x86", "x64")
    }

    return @($Arch)
}

function ConvertTo-MsysPath {
    param([Parameter(Mandatory)][string]$Path)

    $fullPath = [System.IO.Path]::GetFullPath($Path)
    if ($fullPath -match "^([A-Za-z]):\\(.*)$") {
        $drive = $Matches[1].ToLowerInvariant()
        $rest = $Matches[2] -replace "\\", "/"
        return "/$drive/$rest"
    }

    return $fullPath -replace "\\", "/"
}

function Quote-BashArgument {
    param([Parameter(Mandatory)][string]$Value)

    return "'" + ($Value -replace "'", "'\''") + "'"
}

function Get-MsysRoot {
    if ($MsysRoot) {
        return $MsysRoot
    }
    if ($env:MSYS2_ROOT) {
        return $env:MSYS2_ROOT
    }
    if (Test-Path "C:\msys64") {
        return "C:\msys64"
    }

    return ""
}

function Invoke-InMsysShell {
    param([ValidateSet("x86", "x64")] [string]$TargetArch)

    $resolvedMsysRoot = Get-MsysRoot
    if (-not $resolvedMsysRoot) {
        throw "MSYS2 root was not found. Pass -MsysRoot or set MSYS2_ROOT."
    }

    $msysShell = Join-Path $resolvedMsysRoot "msys2_shell.cmd"
    if (-not (Test-Path $msysShell)) {
        throw "MSYS2 shell launcher was not found at '$msysShell'. Pass -MsysRoot or set MSYS2_ROOT."
    }

    $shellType = if ($TargetArch -eq "x86") { "-mingw32" } else { "-ucrt64" }
    $repoMsysPath = ConvertTo-MsysPath $RepoRoot
    $cleanArg = if ($Clean) { " -Clean" } else { "" }
    $command = @(
        "set -e",
        "export $MsysShellMarker=1",
        "cd $(Quote-BashArgument $repoMsysPath)",
        "powershell -ExecutionPolicy Bypass -File scripts/build-little-cms.ps1 -Mode msys -Arch $TargetArch -RepoRoot $(Quote-BashArgument $RepoRoot) -WorkRoot $(Quote-BashArgument $WorkRoot) -MsysRoot $(Quote-BashArgument $resolvedMsysRoot)$cleanArg"
    ) -join "; "
    $escapedCommand = $command.Replace('"', '\"')

    Write-Host "Building Little CMS MSYS $TargetArch via $shellType"
    Invoke-Checked $msysShell @("-no-start", "-defterm", $shellType, "-shell", "bash.exe", "-c", $escapedCommand)
}

function Initialize-Source {
    $sourceDir = Join-Path $WorkRoot "Little-CMS-$Tag"
    $archive = Join-Path $WorkRoot "$Tag.zip"
    $url = "https://github.com/mm2/Little-CMS/archive/refs/tags/$Tag.zip"

    New-Item -ItemType Directory -Force -Path $WorkRoot | Out-Null

    if ($Clean -and (Test-Path $sourceDir)) {
        Remove-Item -Recurse -Force $sourceDir
    }

    if (-not (Test-Path $sourceDir)) {
        if (-not (Test-Path $archive)) {
            Invoke-WebRequest -Uri $url -OutFile $archive
        }

        $extractRoot = Join-Path $WorkRoot "extract"
        if (Test-Path $extractRoot) {
            Remove-Item -Recurse -Force $extractRoot
        }
        New-Item -ItemType Directory -Force -Path $extractRoot | Out-Null
        Expand-Archive -Force -Path $archive -DestinationPath $extractRoot

        $expandedDir = Get-ChildItem -LiteralPath $extractRoot -Directory | Select-Object -First 1
        if (-not $expandedDir) {
            throw "Could not find extracted Little CMS source directory."
        }

        Move-Item -Force -LiteralPath $expandedDir.FullName -Destination $sourceDir
        Remove-Item -Recurse -Force $extractRoot
    }

    Require-Command git
    $actualCommit = (& git ls-remote https://github.com/mm2/Little-CMS.git "refs/tags/$Tag").Split()[0]
    if ($LASTEXITCODE -ne 0 -or -not $actualCommit) {
        throw "Could not verify Little CMS tag $Tag."
    }
    if ($actualCommit -ne $ExpectedCommit) {
        throw "Unexpected Little CMS tag commit for $Tag. Expected $ExpectedCommit, got $actualCommit."
    }

    return $sourceDir
}

function Enter-VsDevShell {
    param([ValidateSet("x86", "x64")] [string]$TargetArch)

    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) {
        throw "vswhere.exe not found. Install Visual Studio Build Tools or Visual Studio."
    }

    $vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
        -property installationPath
    if (-not $vsPath) {
        throw "No Visual C++ toolset found."
    }

    $devCmd = Join-Path $vsPath "Common7\Tools\VsDevCmd.bat"
    $archArg = if ($TargetArch -eq "x86") { "x86" } else { "amd64" }

    $cmdExe = if ($env:ComSpec -and (Test-Path $env:ComSpec)) {
        $env:ComSpec
    }
    else {
        Join-Path $env:SystemRoot "System32\cmd.exe"
    }

    & $cmdExe /c "`"$devCmd`" -arch=$archArg -host_arch=amd64 >nul && set" |
        ForEach-Object {
            if ($_ -match "^(.*?)=(.*)$") {
                Set-Item -Path "env:$($matches[1])" -Value $matches[2]
            }
        }
}

function Get-CMakeBuildArguments {
    param([string]$SourceDir)

    return @(
        "-DCMAKE_BUILD_TYPE=Release",
        "-DLCMS2_BUILD_SHARED=OFF",
        "-DLCMS2_BUILD_STATIC=ON",
        "-DLCMS2_BUILD_TOOLS=OFF",
        "-DLCMS2_BUILD_TESTS=OFF",
        "-DLCMS2_WITH_FASTFLOAT=OFF",
        "-DLCMS2_WITH_THREADED_PLUGIN=OFF",
        $SourceDir
    )
}

function Build-Vs {
    param(
        [ValidateSet("x86", "x64")] [string]$TargetArch,
        [string]$SourceDir,
        [string]$VendorDir
    )

    Enter-VsDevShell $TargetArch
    Require-Command cmake
    Require-Command nmake

    $buildDir = Join-Path $WorkRoot "build-vs-$TargetArch"
    if ($Clean -and (Test-Path $buildDir)) {
        Remove-Item -Recurse -Force $buildDir
    }

    New-Item -ItemType Directory -Force -Path $buildDir | Out-Null

    Push-Location $buildDir
    try {
        Invoke-Checked cmake (@("-G", "NMake Makefiles") + (Get-CMakeBuildArguments -SourceDir $SourceDir))
        $script:ConfiguredBuildDirs += $buildDir
        Invoke-Checked nmake @("lcms2")
    }
    finally {
        Pop-Location
    }

    $destDir = if ($TargetArch -eq "x64") { Join-Path $VendorDir "x64" } else { $VendorDir }
    New-Item -ItemType Directory -Force -Path $destDir | Out-Null
    Copy-Item -Force (Join-Path $buildDir "lcms2.lib") (Join-Path $destDir "lcms2_static.lib")
}

function Build-Msys {
    param(
        [ValidateSet("x86", "x64")] [string]$TargetArch,
        [string]$SourceDir,
        [string]$VendorDir
    )

    Require-Command cmake
    Require-Command make
    Require-Command gcc

    $expectedMachine = if ($TargetArch -eq "x64") { "x86_64-w64-mingw32" } else { "i686-w64-mingw32" }
    $actualMachine = (& gcc -dumpmachine).Trim()
    if ($actualMachine -ne $expectedMachine) {
        throw "Run this from the matching MSYS2 shell. Expected gcc -dumpmachine=$expectedMachine, got $actualMachine."
    }

    $buildDir = Join-Path $WorkRoot "build-msys-$TargetArch"
    if ($Clean -and (Test-Path $buildDir)) {
        Remove-Item -Recurse -Force $buildDir
    }

    New-Item -ItemType Directory -Force -Path $buildDir | Out-Null

    Push-Location $buildDir
    try {
        Invoke-Checked cmake (@("-G", "MSYS Makefiles") + (Get-CMakeBuildArguments -SourceDir $SourceDir))
        $script:ConfiguredBuildDirs += $buildDir
        Invoke-Checked make @("lcms2")
    }
    finally {
        Pop-Location
    }

    $destDir = if ($TargetArch -eq "x64") { Join-Path $VendorDir "ucrt64" } else { $VendorDir }
    New-Item -ItemType Directory -Force -Path $destDir | Out-Null
    Copy-Item -Force (Join-Path $buildDir "liblcms2.a") (Join-Path $destDir "liblcms2.a")
}

function Copy-HeadersAndDocs {
    param(
        [string]$SourceDir,
        [string]$VendorDir
    )

    Copy-Item -Force (Join-Path $SourceDir "include\lcms2.h") (Join-Path $VendorDir "lcms2.h")
    Copy-Item -Force (Join-Path $SourceDir "LICENSE") (Join-Path $VendorDir "LICENSE")
    Copy-Item -Force (Join-Path $SourceDir "README.md") (Join-Path $VendorDir "README.md")

    Get-ChildItem -LiteralPath $VendorDir -File |
        Where-Object { $_.Name -match "^(\d+\.\d+(\.\d+)?|[0-9a-f]{40})$" } |
        Remove-Item -Force

    Set-Content -NoNewline -Path (Join-Path $VendorDir $Version) -Value ""
}

$vendorDir = Join-Path $RepoRoot "3rd-party\Little-CMS"

if ($Mode -eq "msys" -and -not (Get-Item "env:$MsysShellMarker" -ErrorAction SilentlyContinue)) {
    foreach ($targetArch in Get-Architectures) {
        Invoke-InMsysShell -TargetArch $targetArch
    }
    exit 0
}

$sourceDir = Initialize-Source

if ($Mode -eq "source") {
    Write-Host "Prepared Little CMS source at $sourceDir"
    exit 0
}

if ($Mode -eq "all" -or $Mode -eq "vs") {
    foreach ($targetArch in Get-Architectures) {
        Build-Vs -TargetArch $targetArch -SourceDir $sourceDir -VendorDir $vendorDir
    }
}

if ($Mode -eq "all" -or $Mode -eq "msys") {
    foreach ($targetArch in Get-Architectures) {
        Build-Msys -TargetArch $targetArch -SourceDir $sourceDir -VendorDir $vendorDir
    }
}

Copy-HeadersAndDocs -SourceDir $sourceDir -VendorDir $vendorDir
