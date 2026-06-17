[CmdletBinding()]
param(
    [ValidateSet("release", "debug")]
    [string] $Config = "release",

    [ValidateSet("x86", "x64")]
    [string] $Arch = "x86",

    [string] $MsysRoot = $(
        if ($env:MSYS2_ROOT) {
            $env:MSYS2_ROOT
        }
        elseif (Test-Path "C:\msys64") {
            "C:\msys64"
        }
        else {
            ""
        }
    ),

    [switch] $Clean
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot

if (-not $MsysRoot) {
    throw "MSYS2 was not found at 'C:\msys64' and MSYS2_ROOT is not set. Pass -MsysRoot or set MSYS2_ROOT."
}

$msysShell = Join-Path $MsysRoot "msys2_shell.cmd"
if (-not (Test-Path $msysShell)) {
    throw "MSYS2 shell launcher was not found at '$msysShell'. Pass -MsysRoot or set MSYS2_ROOT."
}

function ConvertTo-MsysPath {
    param([Parameter(Mandatory)][string] $Path)

    $fullPath = [System.IO.Path]::GetFullPath($Path)
    if ($fullPath -match "^([A-Za-z]):\\(.*)$") {
        $drive = $Matches[1].ToLowerInvariant()
        $rest = $Matches[2] -replace "\\", "/"
        return "/$drive/$rest"
    }

    return $fullPath -replace "\\", "/"
}

function Quote-BashArgument {
    param([Parameter(Mandatory)][string] $Value)

    return "'" + ($Value -replace "'", "'\''") + "'"
}

if ($Arch -eq "x64") {
    $shellType = "-ucrt64"
    $toolchain = "/ucrt64"
}
else {
    $shellType = "-mingw32"
    $toolchain = "/mingw32"
}

$toolchainWinPath = Join-Path $MsysRoot ($toolchain.TrimStart("/") -replace "/", "\")
$compiler = Join-Path $toolchainWinPath "bin\g++.exe"
if (-not (Test-Path $compiler)) {
    throw "Expected compiler was not found: $compiler"
}

$repoMsysPath = ConvertTo-MsysPath $repoRoot
$preprocessorScript = [System.IO.Path]::GetTempFileName() + ".cmd"
$preprocessorForWindres = $preprocessorScript -replace "\\", "/"
$windres = "windres --preprocessor=$preprocessorForWindres --preprocessor-arg=-E --preprocessor-arg=-xc-header --preprocessor-arg=-DRC_INVOKED"

$commandLines = @(
    "set -e",
    "cd $(Quote-BashArgument $repoMsysPath)"
)
if ($Clean) {
    $commandLines += "make clean"
}
$commandLines += "make config=$Config arch=$Arch CC=$toolchain/bin/g++ WINDRES=$(Quote-BashArgument $windres)"

$command = $commandLines -join "; "
$escapedCommand = $command.Replace('"', '\"')

Write-Host "Building ImgVw ($Arch, $Config)"
try {
    Set-Content -Path $preprocessorScript -Encoding ASCII -Value @(
        "@echo off",
        "`"$toolchainWinPath\bin\gcc.exe`" %*"
    )

    $startInfo = New-Object System.Diagnostics.ProcessStartInfo
    $startInfo.FileName = $msysShell
    $startInfo.Arguments = "-no-start -defterm $shellType -shell bash.exe -c `"$escapedCommand`""
    $startInfo.UseShellExecute = $false

    $process = New-Object System.Diagnostics.Process
    $process.StartInfo = $startInfo
    [void] $process.Start()
    $process.WaitForExit()

    if ($process.ExitCode -ne 0) {
        throw "MSYS build failed for ImgVw ($Arch, $Config)."
    }
}
finally {
    Remove-Item -Path $preprocessorScript -Force -ErrorAction SilentlyContinue
}

$outputPath = Join-Path $repoRoot "bin\$Arch\ImgVw.exe"
if (-not (Test-Path $outputPath)) {
    throw "Expected build output was not found: $outputPath"
}

Write-Host "Built $outputPath"
