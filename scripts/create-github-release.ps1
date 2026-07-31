<#
Creates or updates a GitHub release for ImgVw.

Example:
  powershell -NoProfile -ExecutionPolicy Bypass -File scripts\create-github-release.ps1 `
      -Tag v1.9.1 -Title "ImgVw 1.9.1"
#>

[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string] $Tag,

    [string] $Title = "",

    [string] $Target = "HEAD",

    [string] $Notes = "",

    [string] $NotesFile = "",

    [string] $RepoRoot = "",

    [string] $X86Binary = "bin\x86\ImgVw.exe",

    [string] $X64Binary = "bin\x64\ImgVw.exe",

    [switch] $Draft,

    [switch] $Prerelease,

    [switch] $ReplaceExistingAssets
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Invoke-Checked {
    param(
        [Parameter(Mandatory)][string] $FilePath,
        [Parameter(Mandatory)][string[]] $Arguments
    )

    & $FilePath @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$FilePath failed with exit code $LASTEXITCODE"
    }
}

function Resolve-RepoPath {
    param([Parameter(Mandatory)][string] $Path)

    if ([System.IO.Path]::IsPathRooted($Path)) {
        return [System.IO.Path]::GetFullPath($Path)
    }

    return [System.IO.Path]::GetFullPath((Join-Path $RepoRoot $Path))
}

function Add-StagedAsset {
    param(
        [Parameter(Mandatory)][AllowEmptyCollection()][System.Collections.Generic.List[string]] $Assets,
        [Parameter(Mandatory)][string] $Path,
        [Parameter(Mandatory)][string] $ReleaseName,
        [Parameter(Mandatory)][string] $StagingDir
    )

    $resolvedPath = Resolve-RepoPath $Path
    if (-not (Test-Path -LiteralPath $resolvedPath -PathType Leaf)) {
        throw "Release asset was not found: $resolvedPath"
    }

    $stagedPath = Join-Path $StagingDir $ReleaseName
    Copy-Item -LiteralPath $resolvedPath -Destination $stagedPath -Force
    $Assets.Add($stagedPath)
}

if (-not (Get-Command gh -ErrorAction SilentlyContinue)) {
    throw "GitHub CLI was not found in PATH. Install gh and authenticate with 'gh auth login'."
}

if (-not $RepoRoot) {
    if ($PSScriptRoot) {
        $RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
    }
    else {
        $RepoRoot = (Resolve-Path ".").Path
    }
}

$licensePath = Resolve-RepoPath "LICENSE.md"
if (-not (Test-Path -LiteralPath $licensePath -PathType Leaf)) {
    throw "LICENSE.md was not found: $licensePath"
}
$readmePath = Resolve-RepoPath "README.md"
if (-not (Test-Path -LiteralPath $readmePath -PathType Leaf)) {
    throw "README.md was not found: $readmePath"
}
$installerPath = Resolve-RepoPath "scripts\install-imgvw.ps1"
if (-not (Test-Path -LiteralPath $installerPath -PathType Leaf)) {
    throw "Installer script was not found: $installerPath"
}

Push-Location $RepoRoot
$stagingDir = Join-Path ([System.IO.Path]::GetTempPath()) `
    ("imgvw-release-" + [System.Guid]::NewGuid().ToString("N"))
try {
    Invoke-Checked "git" @("rev-parse", "--is-inside-work-tree")
    $targetSha = & git rev-parse --verify $Target
    if ($LASTEXITCODE -ne 0) {
        throw "git rev-parse --verify $Target failed"
    }
    $targetSha = $targetSha.Trim()

    New-Item -ItemType Directory -Path $stagingDir | Out-Null
    $assets = [System.Collections.Generic.List[string]]::new()
    Add-StagedAsset -Assets $assets -Path $X86Binary -ReleaseName "ImgVw-x86.exe" `
        -StagingDir $stagingDir
    Add-StagedAsset -Assets $assets -Path $X64Binary -ReleaseName "ImgVw-x64.exe" `
        -StagingDir $stagingDir
    Add-StagedAsset -Assets $assets -Path $licensePath -ReleaseName "LICENSE.md" `
        -StagingDir $stagingDir
    Add-StagedAsset -Assets $assets -Path $readmePath -ReleaseName "README.md" `
        -StagingDir $stagingDir
    Add-StagedAsset -Assets $assets -Path $installerPath -ReleaseName "install-imgvw.ps1" `
        -StagingDir $stagingDir

    $previousErrorActionPreference = $ErrorActionPreference
    try {
        # Windows PowerShell can promote native stderr to a terminating error before LASTEXITCODE can be checked.
        $ErrorActionPreference = "Continue"
        & gh release view $Tag *> $null
        $releaseExists = $LASTEXITCODE -eq 0
    }
    finally {
        $ErrorActionPreference = $previousErrorActionPreference
    }

    if ($releaseExists) {
        if (-not $ReplaceExistingAssets) {
            throw "Release '$Tag' already exists. Pass -ReplaceExistingAssets to replace its uploaded assets."
        }

        $editArgs = @("release", "edit", $Tag)
        if ($Title) {
            $editArgs += @("--title", $Title)
        }
        if ($NotesFile) {
            $editArgs += @("--notes-file", (Resolve-RepoPath $NotesFile))
        }
        elseif ($Notes) {
            $editArgs += @("--notes", $Notes)
        }
        if ($Draft) {
            $editArgs += "--draft"
        }
        if ($Prerelease) {
            $editArgs += "--prerelease"
        }
        if ($targetSha) {
            $editArgs += @("--target", $targetSha)
        }

        Invoke-Checked "gh" $editArgs
        Write-Host "Updated release '$Tag' metadata."

        Invoke-Checked "gh" (@("release", "upload", $Tag, "--clobber") + $assets.ToArray())
        Write-Host "Updated release '$Tag' assets."
        return
    }

    $createArgs = @("release", "create", $Tag, "--target", $targetSha)
    if ($Title) {
        $createArgs += @("--title", $Title)
    }
    if ($NotesFile) {
        $createArgs += @("--notes-file", (Resolve-RepoPath $NotesFile))
    }
    elseif ($Notes) {
        $createArgs += @("--notes", $Notes)
    }
    else {
        $createArgs += "--generate-notes"
    }
    if ($Draft) {
        $createArgs += "--draft"
    }
    if ($Prerelease) {
        $createArgs += "--prerelease"
    }

    Invoke-Checked "gh" ($createArgs + $assets.ToArray())
    Write-Host "Created release '$Tag'."
}
finally {
    Remove-Item -LiteralPath $stagingDir -Recurse -Force -ErrorAction SilentlyContinue
    Pop-Location
}
