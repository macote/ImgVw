<#
Builds the static libheif and libde265 artifacts consumed by ImgVw.

MSYS artifacts are the deployment path:
  powershell -NoProfile -ExecutionPolicy Bypass -File scripts\build-libheif.ps1 -Mode msys -Arch x86
  powershell -NoProfile -ExecutionPolicy Bypass -File scripts\build-libheif.ps1 -Mode msys -Arch x64

Visual C++ artifacts are for Visual Studio local development:
  powershell -NoProfile -ExecutionPolicy Bypass -File scripts\build-libheif.ps1 -Mode vs -Arch all
#>

param(
    [string]$LibheifVersion = "1.23.0",
    [string]$Libde265Version = "1.1.1",
    [ValidateSet("all", "vs", "msys", "source")]
    [string]$Mode = "all",
    [ValidateSet("all", "x86", "x64")]
    [string]$Arch = "all",
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path,
    [string]$WorkRoot = (Join-Path $env:TEMP "imgvw-libheif"),
    [string]$MsysRoot = "",
    [switch]$Clean
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$LibheifHashes = @{
    "1.23.0" = "4c9182b18897617182eed12ab5eb9f9d855b3aa3a736d6bdb31abc034ec7d393"
}
$Libde265Hashes = @{
    "1.1.1" = "fd48a927e94ed74fc7ce8829d222b9d8599fcbfe8b6448ba66705babc56ab219"
}
$MsysShellMarker = "IMGVW_LIBHEIF_IN_MSYS_SHELL"
$MsysToolPathsInitialized = $false

function Invoke-Checked {
    param(
        [Parameter(Mandatory)][string]$FilePath,
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
    $candidateMsysRoots = $candidateMsysRoots | Where-Object { $_ } | Select-Object -Unique

    $candidateRoots = @()
    foreach ($candidateMsysRoot in $candidateMsysRoots) {
        switch ($env:MSYSTEM) {
        "UCRT64" { $candidateRoots += (Join-Path $candidateMsysRoot "ucrt64") }
        "MINGW32" { $candidateRoots += (Join-Path $candidateMsysRoot "mingw32") }
        default {
            if ($Arch -eq "x64" -or $Arch -eq "all") {
                $candidateRoots += (Join-Path $candidateMsysRoot "ucrt64")
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
    param([Parameter(Mandatory)][string]$Name)

    if ($Mode -eq "msys" -or $env:MSYSTEM) {
        Add-MsysToolPaths
    }
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

    $fullPath = [IO.Path]::GetFullPath($Path)
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
    param([ValidateSet("x86", "x64")][string]$TargetArch)

    $resolvedMsysRoot = Get-MsysRoot
    if (-not $resolvedMsysRoot) {
        throw "MSYS2 root was not found. Pass -MsysRoot or set MSYS2_ROOT."
    }

    $msysShell = Join-Path $resolvedMsysRoot "msys2_shell.cmd"
    if (-not (Test-Path $msysShell)) {
        throw "MSYS2 shell launcher was not found at '$msysShell'."
    }

    $shellType = if ($TargetArch -eq "x86") { "-mingw32" } else { "-ucrt64" }
    $repoMsysPath = ConvertTo-MsysPath $RepoRoot
    $cleanArg = if ($Clean) { " -Clean" } else { "" }
    $scriptInvocation =
        "powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-libheif.ps1" +
        " -LibheifVersion $LibheifVersion -Libde265Version $Libde265Version -Mode msys -Arch $TargetArch" +
        " -RepoRoot $(Quote-BashArgument $RepoRoot) -WorkRoot $(Quote-BashArgument $WorkRoot)" +
        " -MsysRoot $(Quote-BashArgument $resolvedMsysRoot)$cleanArg"
    $command = @(
        "set -e",
        "export $MsysShellMarker=1",
        "cd $(Quote-BashArgument $repoMsysPath)",
        $scriptInvocation
    ) -join "; "
    $escapedCommand = $command.Replace('"', '\"')

    Write-Host "Building libheif MSYS $TargetArch via $shellType"
    Invoke-Checked $msysShell @("-no-start", "-defterm", $shellType, "-shell", "bash.exe", "-c", $escapedCommand)
}

function Get-VerifiedArchive {
    param(
        [Parameter(Mandatory)][string]$Name,
        [Parameter(Mandatory)][string]$Url,
        [Parameter(Mandatory)][string]$ExpectedHash
    )

    $archive = Join-Path $WorkRoot $Name
    New-Item -ItemType Directory -Force -Path $WorkRoot | Out-Null

    if (-not (Test-Path $archive)) {
        Invoke-WebRequest -Uri $Url -OutFile $archive
    }

    $actualHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $archive).Hash.ToLowerInvariant()
    if ($actualHash -ne $ExpectedHash) {
        throw "SHA256 mismatch for $archive. Expected $ExpectedHash, got $actualHash."
    }

    return $archive
}

function Expand-SourceArchive {
    param(
        [Parameter(Mandatory)][string]$Archive,
        [Parameter(Mandatory)][string]$SourceDir
    )

    if ($Clean -and (Test-Path $SourceDir)) {
        Remove-Item -Recurse -Force $SourceDir
    }

    if (-not (Test-Path $SourceDir)) {
        $tar = Join-Path $env:SystemRoot "System32\tar.exe"
        if (-not (Test-Path $tar)) {
            throw "Required command not found: $tar"
        }
        Invoke-Checked $tar @("-xzf", $Archive, "-C", $WorkRoot)
    }
}

function Initialize-Sources {
    if (-not $LibheifHashes.ContainsKey($LibheifVersion)) {
        throw "No pinned SHA256 is available for libheif $LibheifVersion."
    }
    if (-not $Libde265Hashes.ContainsKey($Libde265Version)) {
        throw "No pinned SHA256 is available for libde265 $Libde265Version."
    }

    $libheifArchive = Get-VerifiedArchive -Name "libheif-$LibheifVersion.tar.gz" `
        -Url "https://github.com/strukturag/libheif/releases/download/v$LibheifVersion/libheif-$LibheifVersion.tar.gz" `
        -ExpectedHash $LibheifHashes[$LibheifVersion]
    $libde265Archive = Get-VerifiedArchive -Name "libde265-$Libde265Version.tar.gz" `
        -Url "https://github.com/strukturag/libde265/releases/download/v$Libde265Version/libde265-$Libde265Version.tar.gz" `
        -ExpectedHash $Libde265Hashes[$Libde265Version]

    $libheifSource = Join-Path $WorkRoot "libheif-$LibheifVersion"
    $libde265Source = Join-Path $WorkRoot "libde265-$Libde265Version"
    Expand-SourceArchive -Archive $libheifArchive -SourceDir $libheifSource
    Expand-SourceArchive -Archive $libde265Archive -SourceDir $libde265Source

    return @{
        Libheif = $libheifSource
        Libde265 = $libde265Source
    }
}

function Enter-VsDevShell {
    param([ValidateSet("x86", "x64")][string]$TargetArch)

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
                Set-Item -Path "env:$($Matches[1])" -Value $Matches[2]
            }
        }

    $cmakeBin = Join-Path $vsPath "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"
    if (Test-Path $cmakeBin) {
        $env:PATH = "$cmakeBin;$env:PATH"
    }
}

function Get-Libde265Arguments {
    param(
        [ValidateSet("vs", "msys")][string]$Toolchain,
        [Parameter(Mandatory)][string]$InstallDir,
        [Parameter(Mandatory)][string]$SourceDir
    )

    $arguments = @(
        "-DCMAKE_BUILD_TYPE=Release",
        "-DCMAKE_INSTALL_PREFIX=$InstallDir",
        "-DBUILD_SHARED_LIBS=OFF",
        "-DENABLE_DECODER=ON",
        "-DENABLE_ENCODER=OFF",
        "-DENABLE_SDL=OFF",
        "-DENABLE_SHERLOCK265=OFF",
        "-DENABLE_INTERNAL_DEVELOPMENT_TOOLS=OFF",
        "-DWITH_FUZZERS=OFF",
        "-DFORCE_FULL_VISIBILITY=ON",
        "-DENABLE_AVX2=OFF",
        "-DENABLE_AVX512=OFF",
        $SourceDir
    )
    if ($Toolchain -eq "vs") {
        $arguments = $arguments[0..1] + "-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL" +
            $arguments[2..($arguments.Count - 1)]
    }
    return $arguments
}

function Get-LibheifArguments {
    param(
        [ValidateSet("vs", "msys")][string]$Toolchain,
        [Parameter(Mandatory)][string]$InstallDir,
        [Parameter(Mandatory)][string]$Libde265InstallDir,
        [Parameter(Mandatory)][string]$SourceDir
    )

    $staticDecoderDefinition = if ($Toolchain -eq "vs") {
        "/EHsc /DLIBDE265_STATIC_BUILD"
    }
    else {
        "-DLIBDE265_STATIC_BUILD"
    }

    $arguments = @(
        "-DCMAKE_BUILD_TYPE=Release",
        "-DCMAKE_INSTALL_PREFIX=$InstallDir",
        "-DCMAKE_PREFIX_PATH=$Libde265InstallDir",
        "-DCMAKE_CXX_FLAGS=$staticDecoderDefinition",
        "-DBUILD_SHARED_LIBS=OFF",
        "-DBUILD_TESTING=OFF",
        "-DBUILD_DOCUMENTATION=OFF",
        "-DBUILD_DEVELOPMENT_TOOLS=OFF",
        "-DENABLE_PLUGIN_LOADING=OFF",
        "-DENABLE_EXPERIMENTAL_FEATURES=OFF",
        "-DENABLE_MULTITHREADING_SUPPORT=ON",
        "-DENABLE_PARALLEL_TILE_DECODING=OFF",
        "-DWITH_EXAMPLES=OFF",
        "-DWITH_EXAMPLE_HEIF_THUMB=OFF",
        "-DWITH_EXAMPLE_HEIF_VIEW=OFF",
        "-DWITH_GDK_PIXBUF=OFF",
        "-DWITH_LIBDE265=ON",
        "-DWITH_LIBDE265_PLUGIN=OFF",
        "-DWITH_X265=OFF",
        "-DWITH_KVAZAAR=OFF",
        "-DWITH_AOM_DECODER=OFF",
        "-DWITH_AOM_ENCODER=OFF",
        "-DWITH_DAV1D=OFF",
        "-DWITH_SvtEnc=OFF",
        "-DWITH_RAV1E=OFF",
        "-DWITH_JPEG_DECODER=OFF",
        "-DWITH_JPEG_ENCODER=OFF",
        "-DWITH_OpenJPEG_DECODER=OFF",
        "-DWITH_OpenJPEG_ENCODER=OFF",
        "-DWITH_OPENJPH_ENCODER=OFF",
        "-DWITH_FFMPEG_DECODER=OFF",
        "-DWITH_OpenH264_DECODER=OFF",
        "-DWITH_X264=OFF",
        "-DWITH_VVDEC=OFF",
        "-DWITH_VVENC=OFF",
        "-DWITH_UVG266=OFF",
        "-DWITH_UNCOMPRESSED_CODEC=OFF",
        "-DWITH_HEADER_COMPRESSION=OFF",
        "-DWITH_LIBSHARPYUV=OFF",
        $SourceDir
    )
    if ($Toolchain -eq "vs") {
        $arguments = $arguments[0..2] + "-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL" +
            $arguments[3..($arguments.Count - 1)]
    }
    return $arguments
}

function Build-CMakeProject {
    param(
        [Parameter(Mandatory)][string]$Generator,
        [Parameter(Mandatory)][string]$BuildDir,
        [Parameter(Mandatory)][string[]]$ConfigureArguments
    )

    if ($Clean -and (Test-Path $BuildDir)) {
        Remove-Item -Recurse -Force $BuildDir
    }
    New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null

    Push-Location $BuildDir
    try {
        Invoke-Checked cmake (@("-G", $Generator) + $ConfigureArguments)
        Invoke-Checked cmake @("--build", ".", "--config", "Release")
        Invoke-Checked cmake @("--install", ".", "--config", "Release")
    }
    finally {
        Pop-Location
    }
}

function Assert-BuildConfiguration {
    param([Parameter(Mandatory)][string]$LibheifBuildDir)

    $cache = Get-Content -LiteralPath (Join-Path $LibheifBuildDir "CMakeCache.txt") -Raw
    $required = @(
        "BUILD_SHARED_LIBS:BOOL=OFF",
        "ENABLE_PLUGIN_LOADING:BOOL=OFF",
        "WITH_LIBDE265:BOOL=ON",
        "WITH_LIBDE265_PLUGIN:BOOL=OFF",
        "WITH_X265:BOOL=OFF",
        "WITH_AOM_DECODER:BOOL=OFF",
        "WITH_DAV1D:BOOL=OFF"
    )
    foreach ($entry in $required) {
        if ($cache -notmatch [regex]::Escape($entry)) {
            throw "Unexpected libheif configuration: '$entry' was not found in CMakeCache.txt."
        }
    }
}

function Copy-DirectoryContents {
    param(
        [Parameter(Mandatory)][string]$Source,
        [Parameter(Mandatory)][string]$Destination
    )

    New-Item -ItemType Directory -Force -Path $Destination | Out-Null
    Copy-Item -Recurse -Force (Join-Path $Source "*") $Destination
}

function Copy-CommonFiles {
    param(
        [Parameter(Mandatory)][hashtable]$Sources,
        [Parameter(Mandatory)][string]$LibheifInstallDir,
        [Parameter(Mandatory)][string]$Libde265InstallDir
    )

    $libheifVendor = Join-Path $RepoRoot "3rd-party\libheif"
    $libde265Vendor = Join-Path $RepoRoot "3rd-party\libde265"

    $installedLibheifHeaders = Join-Path $LibheifInstallDir "include\libheif"
    if (-not (Test-Path $installedLibheifHeaders)) {
        throw "Installed libheif headers were not found at $installedLibheifHeaders."
    }
    Copy-DirectoryContents -Source $installedLibheifHeaders -Destination (Join-Path $libheifVendor "libheif")

    $de265Header = Get-ChildItem -Path (Join-Path $Libde265InstallDir "include") -Recurse -Filter de265.h |
        Select-Object -First 1
    if (-not $de265Header) {
        throw "Installed libde265 header was not found."
    }
    New-Item -ItemType Directory -Force -Path $libde265Vendor | Out-Null
    Copy-Item -Force $de265Header.FullName (Join-Path $libde265Vendor "de265.h")

    Copy-Item -Force (Join-Path $Sources.Libheif "COPYING") (Join-Path $libheifVendor "COPYING")
    Copy-Item -Force (Join-Path $Sources.Libheif "README.md") (Join-Path $libheifVendor "README.md")
    Copy-Item -Force (Join-Path $Sources.Libde265 "COPYING") (Join-Path $libde265Vendor "COPYING")
    Copy-Item -Force (Join-Path $Sources.Libde265 "README.md") (Join-Path $libde265Vendor "README.md")

    Get-ChildItem -LiteralPath $libheifVendor -File -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -match "^\d+\.\d+\.\d+$" } |
        Remove-Item -Force
    Get-ChildItem -LiteralPath $libde265Vendor -File -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -match "^\d+\.\d+\.\d+$" } |
        Remove-Item -Force

    Set-Content -NoNewline -Path (Join-Path $libheifVendor $LibheifVersion) -Value ""
    Set-Content -NoNewline -Path (Join-Path $libde265Vendor $Libde265Version) -Value ""
}

function Find-StaticLibrary {
    param(
        [Parameter(Mandatory)][string]$InstallDir,
        [Parameter(Mandatory)][string[]]$Names
    )

    foreach ($name in $Names) {
        $library = Get-ChildItem -Path $InstallDir -Recurse -Filter $name | Select-Object -First 1
        if ($library) {
            return $library.FullName
        }
    }

    throw "Could not find any of these libraries under ${InstallDir}: $($Names -join ', ')"
}

function Copy-BuildArtifacts {
    param(
        [ValidateSet("vs", "msys")][string]$Toolchain,
        [ValidateSet("x86", "x64")][string]$TargetArch,
        [Parameter(Mandatory)][string]$LibheifInstallDir,
        [Parameter(Mandatory)][string]$Libde265InstallDir
    )

    $libheifVendor = Join-Path $RepoRoot "3rd-party\libheif"
    $libde265Vendor = Join-Path $RepoRoot "3rd-party\libde265"
    if ($Toolchain -eq "vs") {
        $destinationSuffix = if ($TargetArch -eq "x64") { "x64" } else { "" }
        $heifName = "heif.lib"
        $de265Name = "libde265.lib"
        $heifSource = Find-StaticLibrary -InstallDir $LibheifInstallDir -Names @("heif.lib", "libheif.lib")
        $de265Source = Find-StaticLibrary -InstallDir $Libde265InstallDir -Names @("libde265.lib", "de265.lib")
    }
    else {
        $destinationSuffix = if ($TargetArch -eq "x64") { "ucrt64" } else { "" }
        $heifName = "libheif.a"
        $de265Name = "libde265.a"
        $heifSource = Find-StaticLibrary -InstallDir $LibheifInstallDir -Names @("libheif.a")
        $de265Source = Find-StaticLibrary -InstallDir $Libde265InstallDir -Names @("libde265.a")
    }

    $heifDestination = if ($destinationSuffix) {
        Join-Path $libheifVendor $destinationSuffix
    }
    else {
        $libheifVendor
    }
    $de265Destination = if ($destinationSuffix) {
        Join-Path $libde265Vendor $destinationSuffix
    }
    else {
        $libde265Vendor
    }
    New-Item -ItemType Directory -Force -Path $heifDestination, $de265Destination | Out-Null
    Copy-Item -Force $heifSource (Join-Path $heifDestination $heifName)
    Copy-Item -Force $de265Source (Join-Path $de265Destination $de265Name)
}

function Build-Toolchain {
    param(
        [ValidateSet("vs", "msys")][string]$Toolchain,
        [ValidateSet("x86", "x64")][string]$TargetArch,
        [Parameter(Mandatory)][hashtable]$Sources
    )

    if ($Toolchain -eq "vs") {
        Enter-VsDevShell $TargetArch
        Require-Command cmake
        Require-Command nmake
        $generator = "NMake Makefiles"
    }
    else {
        Require-Command cmake
        Require-Command make
        Require-Command gcc
        $expectedMachine = if ($TargetArch -eq "x64") { "x86_64-w64-mingw32" } else { "i686-w64-mingw32" }
        $actualMachine = (& gcc -dumpmachine).Trim()
        if ($actualMachine -ne $expectedMachine) {
            throw "Expected gcc -dumpmachine=$expectedMachine, got $actualMachine."
        }
        $generator = "MSYS Makefiles"
    }

    $prefix = "$Toolchain-$TargetArch"
    $libde265Build = Join-Path $WorkRoot "build-libde265-$prefix"
    $libheifBuild = Join-Path $WorkRoot "build-libheif-$prefix"
    $libde265Install = Join-Path $WorkRoot "install-libde265-$prefix"
    $libheifInstall = Join-Path $WorkRoot "install-libheif-$prefix"
    if ($Clean) {
        foreach ($path in @($libde265Install, $libheifInstall)) {
            if (Test-Path $path) {
                Remove-Item -Recurse -Force $path
            }
        }
    }

    Build-CMakeProject -Generator $generator -BuildDir $libde265Build `
        -ConfigureArguments (Get-Libde265Arguments -Toolchain $Toolchain -InstallDir $libde265Install `
            -SourceDir $Sources.Libde265)
    Build-CMakeProject -Generator $generator -BuildDir $libheifBuild `
        -ConfigureArguments (Get-LibheifArguments -Toolchain $Toolchain -InstallDir $libheifInstall `
            -Libde265InstallDir $libde265Install -SourceDir $Sources.Libheif)
    Assert-BuildConfiguration -LibheifBuildDir $libheifBuild
    Copy-BuildArtifacts -Toolchain $Toolchain -TargetArch $TargetArch `
        -LibheifInstallDir $libheifInstall -Libde265InstallDir $libde265Install
    Copy-CommonFiles -Sources $Sources -LibheifInstallDir $libheifInstall -Libde265InstallDir $libde265Install
}

if ($Mode -eq "msys" -and -not (Get-Item "env:$MsysShellMarker" -ErrorAction SilentlyContinue)) {
    foreach ($targetArch in Get-Architectures) {
        Invoke-InMsysShell -TargetArch $targetArch
    }
    exit 0
}

$sources = Initialize-Sources
if ($Mode -eq "source") {
    Write-Host "Prepared libheif source at $($sources.Libheif)"
    Write-Host "Prepared libde265 source at $($sources.Libde265)"
    exit 0
}

if ($Mode -eq "all" -or $Mode -eq "vs") {
    foreach ($targetArch in Get-Architectures) {
        Build-Toolchain -Toolchain vs -TargetArch $targetArch -Sources $sources
    }
}

if ($Mode -eq "all" -and -not (Get-Item "env:$MsysShellMarker" -ErrorAction SilentlyContinue)) {
    foreach ($targetArch in Get-Architectures) {
        Invoke-InMsysShell -TargetArch $targetArch
    }
    exit 0
}

if ($Mode -eq "msys") {
    foreach ($targetArch in Get-Architectures) {
        Build-Toolchain -Toolchain msys -TargetArch $targetArch -Sources $sources
    }
}
