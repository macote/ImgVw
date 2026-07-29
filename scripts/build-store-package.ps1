[CmdletBinding()]
param(
    [string] $IdentityName = "Marc-AndrCt.ImgVw",
    [string] $Publisher = "CN=B2B17E87-E414-4595-A511-7C4778B76C22",
    [string] $PublisherDisplayName = "",
    [string] $X86Binary = "bin\x86\ImgVw.exe",
    [string] $X64Binary = "bin\x64\ImgVw.exe",
    [string] $OutputDirectory = "packaging\store\out"
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot

if (-not $PublisherDisplayName) {
    # Keep the executable script ASCII-safe for Windows PowerShell 5.1, which treats UTF-8 without a BOM as ANSI.
    $PublisherDisplayName = "Marc-Andr{0} C{1}t{0}" -f [char]0x00E9, [char]0x00F4
}

function Resolve-RepositoryPath([string] $Path) {
    if ([IO.Path]::IsPathRooted($Path)) {
        return $Path
    }
    return Join-Path $repoRoot $Path
}

function Find-MakeAppx {
    $kitsBin = Join-Path ${env:ProgramFiles(x86)} "Windows Kits\10\bin"
    $tool = Get-ChildItem -LiteralPath $kitsBin -Recurse -Filter MakeAppx.exe -ErrorAction SilentlyContinue |
        Where-Object { $_.Directory.Name -eq "x64" } |
        Sort-Object { [version]$_.Directory.Parent.Name } -Descending |
        Select-Object -First 1
    if (-not $tool) {
        throw "MakeAppx.exe was not found. Install the Windows 10 or Windows 11 SDK."
    }
    return $tool.FullName
}

function Invoke-Checked([string] $FilePath, [string[]] $Arguments) {
    & $FilePath @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed with exit code ${LASTEXITCODE}: $FilePath"
    }
}

function Escape-Xml([string] $Value) {
    return [Security.SecurityElement]::Escape($Value)
}

$versionResource = Get-Content -LiteralPath (Join-Path $repoRoot "resources\ImgVw.rc") -Raw
$match = [regex]::Match($versionResource, 'VALUE\s+"ProductVersion",\s+"(\d+)\.(\d+)\.(\d+)"')
if (-not $match.Success) {
    throw "Could not read the three-part ProductVersion from resources\ImgVw.rc."
}
$productVersion = $match.Groups[1..3].Value -join "."
$packageVersion = "$productVersion.0"

$binaries = @{ x86 = Resolve-RepositoryPath $X86Binary; x64 = Resolve-RepositoryPath $X64Binary }
foreach ($entry in $binaries.GetEnumerator()) {
    if (-not (Test-Path -LiteralPath $entry.Value -PathType Leaf)) {
        throw "Missing $($entry.Key) release executable: $($entry.Value)"
    }
    $embeddedVersion = (Get-Item -LiteralPath $entry.Value).VersionInfo.ProductVersion
    if ([version]$embeddedVersion -ne [version]$productVersion) {
        throw "$($entry.Key) executable version '$embeddedVersion' does not match '$productVersion'."
    }
}

$makeAppx = Find-MakeAppx
$output = Resolve-RepositoryPath $OutputDirectory
$template = Get-Content -LiteralPath (Join-Path $repoRoot "packaging\store\Package.appxmanifest.in") -Raw
$assets = Join-Path $repoRoot "packaging\store\Assets"
if (-not (Test-Path -LiteralPath $assets -PathType Container)) {
    throw "Store assets were not found: $assets"
}

if (Test-Path -LiteralPath $output) {
    Remove-Item -LiteralPath $output -Recurse -Force
}
New-Item -ItemType Directory -Path $output | Out-Null
$bundleInput = Join-Path $output "bundle-input"
New-Item -ItemType Directory -Path $bundleInput | Out-Null

foreach ($architecture in @("x86", "x64")) {
    $stage = Join-Path $output "stage-$architecture"
    New-Item -ItemType Directory -Path $stage | Out-Null
    Copy-Item -LiteralPath $binaries[$architecture] -Destination (Join-Path $stage "ImgVw.exe")
    Copy-Item -LiteralPath (Join-Path $repoRoot "LICENSE.md") -Destination $stage
    Copy-Item -LiteralPath $assets -Destination $stage -Recurse

    $manifest = $template.Replace("@IDENTITY_NAME@", (Escape-Xml $IdentityName)).
        Replace("@PUBLISHER@", (Escape-Xml $Publisher)).
        Replace("@PUBLISHER_DISPLAY_NAME@", (Escape-Xml $PublisherDisplayName)).
        Replace("@VERSION@", $packageVersion).
        Replace("@ARCHITECTURE@", $architecture)
    [IO.File]::WriteAllText((Join-Path $stage "AppxManifest.xml"), $manifest, [Text.UTF8Encoding]::new($false))

    $package = Join-Path $bundleInput "ImgVw_${packageVersion}_${architecture}.msix"
    Invoke-Checked $makeAppx @("pack", "/o", "/d", $stage, "/p", $package)
}

$bundle = Join-Path $output "ImgVw_${packageVersion}.msixbundle"
Invoke-Checked $makeAppx @("bundle", "/o", "/d", $bundleInput, "/p", $bundle)

Get-ChildItem -LiteralPath $bundleInput -Filter *.msix | Copy-Item -Destination $output
$hashes = Get-ChildItem -LiteralPath $output -File | Where-Object Extension -in ".msix", ".msixbundle" |
    Sort-Object Name | ForEach-Object { "{0}  {1}" -f (Get-FileHash -Algorithm SHA256 $_.FullName).Hash, $_.Name }
[IO.File]::WriteAllLines((Join-Path $output "SHA256SUMS.txt"), $hashes, [Text.UTF8Encoding]::new($false))

Remove-Item -LiteralPath $bundleInput -Recurse -Force
Remove-Item -LiteralPath (Join-Path $output "stage-x86"), (Join-Path $output "stage-x64") -Recurse -Force
Write-Host "Created Store upload bundle: $bundle"
