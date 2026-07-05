<#
Installs ImgVw for the current user by default, registers Explorer Open With entries,
and can uninstall entries created by this script.

Examples:
  powershell -NoProfile -ExecutionPolicy Bypass -File scripts\install-imgvw.ps1
  powershell -NoProfile -ExecutionPolicy Bypass -File scripts\install-imgvw.ps1 -Action Uninstall
  powershell -NoProfile -ExecutionPolicy Bypass -File scripts\install-imgvw.ps1 -Arch x64
#>

[CmdletBinding()]
param(
    [ValidateSet("Install", "Uninstall")]
    [string] $Action = "Install",

    [ValidateSet("CurrentUser", "AllUsers")]
    [string] $Scope = "CurrentUser",

    [string] $InstallDir = "",

    [string] $SourceExe = "",

    [ValidateSet("Auto", "x86", "x64")]
    [string] $Arch = "Auto",

    [string] $Version = "latest",

    [switch] $SkipImageRegistration,

    [switch] $SkipFolderRegistration,

    [switch] $SetLegacyDefaults,

    [switch] $NoPath,

    [switch] $Force
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = "Stop"

$InstallerScriptPath = ""
if ($MyInvocation.MyCommand.PSObject.Properties["Path"]) {
    $InstallerScriptPath = $MyInvocation.MyCommand.Path
}
$InstallerScriptText = ""
if ($MyInvocation.MyCommand.PSObject.Properties["Definition"]) {
    $InstallerScriptText = $MyInvocation.MyCommand.Definition
}
$ScriptRoot = ""
if ($InstallerScriptPath) {
    $ScriptRoot = Split-Path -Parent $InstallerScriptPath
}
if (-not $ScriptRoot) {
    $ScriptRoot = (Get-Location).Path
}

$RepoOwner = "macote"
$RepoName = "ImgVw"
$AppName = "ImgVw"
$ExeName = "ImgVw.exe"
$InstallerScriptName = "install-imgvw.ps1"
$ImageProgId = "ImgVw.Image"
$FolderVerbKey = "ImgVw.OpenFolder"
$InstallMarkerName = "install.json"
$SupportedExtensions = @(
    # Keep in sync with ImgItemHelper::GetImgFormatFromExtension().
    ".jpg",
    ".jpeg",
    ".png",
    ".heic",
    ".heif",
    ".hif",
    ".bmp",
    ".gif",
    ".ico",
    ".tif",
    ".tiff"
)

function Get-Is64BitOperatingSystem {
    if ([Environment]::GetEnvironmentVariables().Contains("PROCESSOR_ARCHITEW6432")) {
        return $true
    }

    if ([Environment]::GetEnvironmentVariables().Contains("PROCESSOR_ARCHITECTURE")) {
        $processorArchitecture = [Environment]::GetEnvironmentVariable("PROCESSOR_ARCHITECTURE")
        if ($processorArchitecture -eq "AMD64" -or $processorArchitecture -eq "IA64" -or $processorArchitecture -eq "ARM64") {
            return $true
        }
    }

    return $false
}

function Resolve-TargetArch {
    if ($Arch -eq "Auto") {
        if (Get-Is64BitOperatingSystem) {
            return "x64"
        }

        return "x86"
    }

    if ($Arch -eq "x64" -and -not (Get-Is64BitOperatingSystem)) {
        throw "The x64 build cannot be installed on a 32-bit operating system."
    }

    return $Arch
}

function Test-IsElevated {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object Security.Principal.WindowsPrincipal($identity)
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Get-DefaultInstallDir {
    if ($Scope -eq "AllUsers") {
        $programFiles = [Environment]::GetFolderPath("ProgramFiles")
        if (-not $programFiles) {
            $programFiles = $env:ProgramFiles
        }

        return (Join-Path $programFiles $AppName)
    }

    $localAppData = [Environment]::GetFolderPath("LocalApplicationData")
    if (-not $localAppData) {
        if ($env:LOCALAPPDATA) {
            $localAppData = $env:LOCALAPPDATA
        }
        elseif ($env:USERPROFILE) {
            $localAppData = Join-Path $env:USERPROFILE "Local Settings\Application Data"
        }
        else {
            throw "Could not resolve the current user's local application data directory."
        }
    }

    return (Join-Path (Join-Path $localAppData "Programs") $AppName)
}

function Get-ClassesRoot {
    if ($Scope -eq "AllUsers") {
        return "HKLM:\Software\Classes"
    }

    return "HKCU:\Software\Classes"
}

function Get-CapabilitiesRoot {
    if ($Scope -eq "AllUsers") {
        return "HKLM:\Software\$AppName"
    }

    return "HKCU:\Software\$AppName"
}

function Get-RegisteredApplicationsPath {
    if ($Scope -eq "AllUsers") {
        return "HKLM:\Software\RegisteredApplications"
    }

    return "HKCU:\Software\RegisteredApplications"
}

function Get-UninstallKeyPath {
    if ($Scope -eq "AllUsers") {
        return "HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall\$AppName"
    }

    return "HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\$AppName"
}

function New-Key {
    param([Parameter(Mandatory = $true)][string] $Path)

    if (-not (Test-Path -LiteralPath $Path)) {
        New-Item -Path $Path -Force | Out-Null
    }
}

function Set-RegistryValue {
    param(
        [Parameter(Mandatory = $true)][string] $Path,
        [Parameter(Mandatory = $true)][AllowEmptyString()][string] $Name,
        [Parameter(Mandatory = $true)][AllowNull()][object] $Value,
        [string] $Type = "String"
    )

    New-Key $Path
    if ($Name -eq "") {
        Set-Item -LiteralPath $Path -Value $Value
    }
    else {
        New-ItemProperty -LiteralPath $Path -Name $Name -Value $Value -PropertyType $Type -Force | Out-Null
    }
}

function Remove-RegistryValue {
    param(
        [Parameter(Mandatory = $true)][string] $Path,
        [Parameter(Mandatory = $true)][string] $Name
    )

    if (Test-Path -LiteralPath $Path) {
        Remove-ItemProperty -LiteralPath $Path -Name $Name -ErrorAction SilentlyContinue
    }
}

function Remove-RegistryKey {
    param([Parameter(Mandatory = $true)][string] $Path)

    if (Test-Path -LiteralPath $Path) {
        Remove-Item -LiteralPath $Path -Recurse -Force -ErrorAction SilentlyContinue
    }
}

function Get-CommandValue {
    param([Parameter(Mandatory = $true)][string] $ExePath)

    return '"' + $ExePath + '" "%1"'
}

function Quote-CommandArgument {
    param([Parameter(Mandatory = $true)][string] $Value)

    return '"' + ($Value -replace '"', '\"') + '"'
}

function Get-ExecutableMachine {
    param([Parameter(Mandatory = $true)][string] $Path)

    $stream = [System.IO.File]::Open($Path, [System.IO.FileMode]::Open, [System.IO.FileAccess]::Read, [System.IO.FileShare]::Read)
    try {
        $reader = New-Object System.IO.BinaryReader($stream)
        if ($stream.Length -lt 0x40) {
            throw "Executable is too small: $Path"
        }

        $stream.Seek(0x3c, [System.IO.SeekOrigin]::Begin) | Out-Null
        $peOffset = $reader.ReadInt32()
        if ($peOffset -lt 0 -or ($peOffset + 6) -gt $stream.Length) {
            throw "Executable has an invalid PE header offset: $Path"
        }

        $stream.Seek($peOffset, [System.IO.SeekOrigin]::Begin) | Out-Null
        $signature = $reader.ReadUInt32()
        if ($signature -ne 0x00004550) {
            throw "Executable has an invalid PE signature: $Path"
        }

        $machine = $reader.ReadUInt16()
        if ($machine -eq 0x014c) {
            return "x86"
        }
        elseif ($machine -eq 0x8664) {
            return "x64"
        }

        throw ("Unsupported executable machine type 0x{0:X4}: {1}" -f $machine, $Path)
    }
    finally {
        $stream.Close()
    }
}

function Assert-ExecutableArch {
    param(
        [Parameter(Mandatory = $true)][string] $Path,
        [Parameter(Mandatory = $true)][string] $ExpectedArch
    )

    $actualArch = Get-ExecutableMachine $Path
    if ($actualArch -ne $ExpectedArch) {
        throw "Executable architecture mismatch. Expected $ExpectedArch but found $actualArch in $Path."
    }
}

function Invoke-WebRequestCompat {
    param(
        [Parameter(Mandatory = $true)][string] $Uri,
        [string] $OutFile = ""
    )

    $parameters = @{
        Uri = $Uri
        UseBasicParsing = $true
    }
    if ($OutFile) {
        $parameters["OutFile"] = $OutFile
    }

    return Invoke-WebRequest @parameters
}

function Enable-ModernTlsIfAvailable {
    try {
        if ([enum]::GetNames([Net.SecurityProtocolType]) -contains "Tls12") {
            [Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor 3072
        }
    }
    catch {
    }
}

function ConvertFrom-JsonCompat {
    param([Parameter(Mandatory = $true)][string] $Json)

    if (Get-Command ConvertFrom-Json -ErrorAction SilentlyContinue) {
        return ($Json | ConvertFrom-Json)
    }

    $serializerType = [Type]::GetType("System.Web.Script.Serialization.JavaScriptSerializer")
    if (-not $serializerType) {
        Add-Type -AssemblyName System.Web.Extensions
        $serializerType = [Type]::GetType("System.Web.Script.Serialization.JavaScriptSerializer")
    }

    $serializer = New-Object System.Web.Script.Serialization.JavaScriptSerializer
    return $serializer.DeserializeObject($Json)
}

function Get-ObjectProperty {
    param(
        [Parameter(Mandatory = $true)][object] $Object,
        [Parameter(Mandatory = $true)][string] $Name
    )

    if ($Object -is [System.Collections.IDictionary]) {
        return $Object[$Name]
    }

    return $Object.$Name
}

function Convert-VersionTagToParts {
    param([string] $Tag)

    $parts = @()
    if (-not $Tag) {
        return $parts
    }

    $normalized = $Tag.Trim()
    if ($normalized.StartsWith("v") -or $normalized.StartsWith("V")) {
        $normalized = $normalized.Substring(1)
    }

    foreach ($part in ($normalized -split "[.-]")) {
        $value = 0
        if ([int]::TryParse($part, [ref] $value)) {
            $parts += $value
        }
        else {
            break
        }
    }

    return $parts
}

function Compare-VersionTags {
    param(
        [string] $Left,
        [string] $Right
    )

    if ($Left -eq $Right) {
        return 0
    }
    if (-not $Left) {
        return -1
    }
    if (-not $Right) {
        return 1
    }

    $leftParts = @(Convert-VersionTagToParts $Left)
    $rightParts = @(Convert-VersionTagToParts $Right)
    if ($leftParts.Count -eq 0 -or $rightParts.Count -eq 0) {
        return [string]::Compare($Left, $Right, $true)
    }

    $count = [Math]::Max($leftParts.Count, $rightParts.Count)
    for ($index = 0; $index -lt $count; ++$index) {
        $leftValue = 0
        $rightValue = 0
        if ($index -lt $leftParts.Count) {
            $leftValue = $leftParts[$index]
        }
        if ($index -lt $rightParts.Count) {
            $rightValue = $rightParts[$index]
        }

        if ($leftValue -lt $rightValue) {
            return -1
        }
        if ($leftValue -gt $rightValue) {
            return 1
        }
    }

    return 0
}

function Get-LatestRelease {
    param([Parameter(Mandatory = $true)][string] $SelectedVersion)

    if ($SelectedVersion -eq "latest") {
        $uri = "https://api.github.com/repos/$RepoOwner/$RepoName/releases/latest"
    }
    else {
        $uri = "https://api.github.com/repos/$RepoOwner/$RepoName/releases/tags/$SelectedVersion"
    }

    $response = Invoke-WebRequestCompat -Uri $uri
    $content = [string] $response.Content
    if (-not $content) {
        throw "GitHub release response was empty."
    }

    return ConvertFrom-JsonCompat $content
}

function Get-ReleaseAssets {
    param([Parameter(Mandatory = $true)][object] $Release)

    return Get-ObjectProperty -Object $Release -Name "assets"
}

function Select-ReleaseAsset {
    param(
        [Parameter(Mandatory = $true)][object] $Release,
        [Parameter(Mandatory = $true)][string] $SelectedArch
    )

    $assets = Get-ReleaseAssets $Release
    $preferredMatches = @()
    $fallbackMatches = @()
    if ($SelectedArch -eq "x64") {
        $preferredNames = @("imgvw-x64.exe", "imgvw-x64.zip")
    }
    else {
        $preferredNames = @("imgvw-x86.exe", "imgvw-x86.zip")
    }

    foreach ($asset in $assets) {
        $name = [string] (Get-ObjectProperty -Object $asset -Name "name")
        if (-not $name) {
            continue
        }

        $lowerName = $name.ToLowerInvariant()
        if ($lowerName -eq "install-imgvw.ps1") {
            continue
        }

        $isExecutableAsset = $lowerName.EndsWith(".exe") -or $lowerName.EndsWith(".zip")
        if (-not $isExecutableAsset) {
            continue
        }

        if ($preferredNames -contains $lowerName) {
            $preferredMatches += $asset
            continue
        }

        if ($SelectedArch -eq "x64") {
            if ($lowerName -match "(^|[-_.])x64([-_.]|$)" -or $lowerName -match "(^|[-_.])amd64([-_.]|$)") {
                $fallbackMatches += $asset
            }
        }
        else {
            if ($lowerName -match "(^|[-_.])x86([-_.]|$)") {
                $fallbackMatches += $asset
            }
        }
    }

    if ($preferredMatches.Count -eq 1) {
        return $preferredMatches[0]
    }
    elseif ($preferredMatches.Count -gt 1) {
        $names = @()
        foreach ($match in $preferredMatches) {
            $names += [string] (Get-ObjectProperty -Object $match -Name "name")
        }
        throw "More than one preferred release asset matched architecture '$SelectedArch': $($names -join ', ')"
    }

    $matches = $fallbackMatches
    if ($matches.Count -eq 0) {
        throw "No release asset matching architecture '$SelectedArch' was found."
    }
    elseif ($matches.Count -gt 1) {
        $names = @()
        foreach ($match in $matches) {
            $names += [string] (Get-ObjectProperty -Object $match -Name "name")
        }
        throw "More than one release asset matched architecture '$SelectedArch': $($names -join ', ')"
    }

    return $matches[0]
}

function Expand-ZipCompat {
    param(
        [Parameter(Mandatory = $true)][string] $ZipPath,
        [Parameter(Mandatory = $true)][string] $Destination
    )

    New-Item -ItemType Directory -Path $Destination -Force | Out-Null

    if (Get-Command Expand-Archive -ErrorAction SilentlyContinue) {
        Expand-Archive -LiteralPath $ZipPath -DestinationPath $Destination -Force
        return
    }

    $shell = New-Object -ComObject Shell.Application
    $zip = $shell.NameSpace($ZipPath)
    $dest = $shell.NameSpace($Destination)
    if (-not $zip -or -not $dest) {
        throw "Could not open zip archive: $ZipPath"
    }

    $dest.CopyHere($zip.Items(), 0x14)
}

function Get-DownloadedExecutable {
    param(
        [Parameter(Mandatory = $true)][string] $AssetPath,
        [Parameter(Mandatory = $true)][string] $WorkDir
    )

    $extension = [System.IO.Path]::GetExtension($AssetPath).ToLowerInvariant()
    if ($extension -eq ".exe") {
        return $AssetPath
    }

    if ($extension -ne ".zip") {
        throw "Unsupported release asset type: $AssetPath"
    }

    $extractDir = Join-Path $WorkDir "extract"
    Expand-ZipCompat -ZipPath $AssetPath -Destination $extractDir

    $candidates = @(Get-ChildItem -Path $extractDir -Recurse -Filter "*.exe" | Where-Object {
        $_.Name -ieq $ExeName -or $_.Name -ilike "ImgVw*.exe"
    })
    if ($candidates.Count -eq 0) {
        throw "No ImgVw executable was found in $AssetPath."
    }
    elseif ($candidates.Count -gt 1) {
        throw "More than one ImgVw executable was found in $AssetPath."
    }

    return $candidates[0].FullName
}

function Resolve-SourceExecutable {
    param(
        [Parameter(Mandatory = $true)][string] $SelectedArch,
        [Parameter(Mandatory = $true)][string] $WorkDir
    )

    if ($SourceExe) {
        $resolved = [System.IO.Path]::GetFullPath($SourceExe)
        if (-not (Test-Path -LiteralPath $resolved -PathType Leaf)) {
            throw "Source executable was not found: $resolved"
        }

        return @{
            Path = $resolved
            ReleaseTag = "local"
            AssetName = [System.IO.Path]::GetFileName($resolved)
            DownloadUrl = ""
        }
    }

    $localExe = Join-Path $ScriptRoot $ExeName
    if (Test-Path -LiteralPath $localExe -PathType Leaf) {
        return @{
            Path = $localExe
            ReleaseTag = "local"
            AssetName = [System.IO.Path]::GetFileName($localExe)
            DownloadUrl = ""
        }
    }

    Enable-ModernTlsIfAvailable
    $release = Get-LatestRelease -SelectedVersion $Version
    $asset = Select-ReleaseAsset -Release $release -SelectedArch $SelectedArch
    $assetName = [string] (Get-ObjectProperty -Object $asset -Name "name")
    $downloadUrl = [string] (Get-ObjectProperty -Object $asset -Name "browser_download_url")
    $releaseTag = [string] (Get-ObjectProperty -Object $release -Name "tag_name")
    if (-not $downloadUrl) {
        throw "Selected release asset does not have a download URL: $assetName"
    }

    $downloadPath = Join-Path $WorkDir $assetName
    Invoke-WebRequestCompat -Uri $downloadUrl -OutFile $downloadPath | Out-Null
    if (-not (Test-Path -LiteralPath $downloadPath -PathType Leaf)) {
        throw "Release asset was not downloaded: $downloadPath"
    }

    $downloadedItem = Get-Item -LiteralPath $downloadPath
    if ($downloadedItem.Length -le 0) {
        throw "Downloaded release asset was empty: $downloadPath"
    }

    return @{
        Path = (Get-DownloadedExecutable -AssetPath $downloadPath -WorkDir $WorkDir)
        ReleaseTag = $releaseTag
        AssetName = $assetName
        DownloadUrl = $downloadUrl
    }
}

function Register-ImageTypes {
    param([Parameter(Mandatory = $true)][string] $InstalledExe)

    $classesRoot = Get-ClassesRoot
    $capabilitiesRoot = Get-CapabilitiesRoot
    $registeredApplicationsPath = Get-RegisteredApplicationsPath
    $command = Get-CommandValue $InstalledExe
    $applicationsRoot = Join-Path $classesRoot "Applications\$ExeName"
    $progIdRoot = Join-Path $classesRoot $ImageProgId

    Set-RegistryValue -Path $applicationsRoot -Name "FriendlyAppName" -Value $AppName
    Set-RegistryValue -Path (Join-Path $applicationsRoot "shell\open\command") -Name "" -Value $command
    Set-RegistryValue -Path (Join-Path $applicationsRoot "DefaultIcon") -Name "" -Value "$InstalledExe,0"

    $supportedTypesPath = Join-Path $applicationsRoot "SupportedTypes"
    New-Key $supportedTypesPath
    foreach ($extension in $SupportedExtensions) {
        Set-RegistryValue -Path $supportedTypesPath -Name $extension -Value ""
    }

    Set-RegistryValue -Path $progIdRoot -Name "" -Value "ImgVw Image"
    Set-RegistryValue -Path (Join-Path $progIdRoot "DefaultIcon") -Name "" -Value "$InstalledExe,0"
    Set-RegistryValue -Path (Join-Path $progIdRoot "shell\open\command") -Name "" -Value $command

    $fileAssociationsPath = Join-Path $capabilitiesRoot "Capabilities\FileAssociations"
    Set-RegistryValue -Path (Join-Path $capabilitiesRoot "Capabilities") -Name "ApplicationName" -Value $AppName
    Set-RegistryValue -Path (Join-Path $capabilitiesRoot "Capabilities") -Name "ApplicationDescription" -Value "Fast Windows image viewer"
    foreach ($extension in $SupportedExtensions) {
        Set-RegistryValue -Path (Join-Path $classesRoot "$extension\OpenWithProgids") -Name $ImageProgId -Value ([byte[]] @()) -Type "Binary"
        Set-RegistryValue -Path (Join-Path $classesRoot "$extension\OpenWithList\$ExeName") -Name "" -Value ""
        Set-RegistryValue -Path $fileAssociationsPath -Name $extension -Value $ImageProgId
        if ($SetLegacyDefaults) {
            Set-RegistryValue -Path (Join-Path $classesRoot $extension) -Name "" -Value $ImageProgId
        }
    }

    Set-RegistryValue -Path $registeredApplicationsPath -Name $AppName -Value "Software\$AppName\Capabilities"
}

function Register-Folders {
    param([Parameter(Mandatory = $true)][string] $InstalledExe)

    $classesRoot = Get-ClassesRoot
    $command = Get-CommandValue $InstalledExe
    foreach ($shellRoot in @("Directory", "Drive")) {
        $verbPath = Join-Path $classesRoot "$shellRoot\shell\$FolderVerbKey"
        Set-RegistryValue -Path $verbPath -Name "" -Value "Open with ImgVw"
        Set-RegistryValue -Path $verbPath -Name "Icon" -Value "$InstalledExe,0"
        Set-RegistryValue -Path (Join-Path $verbPath "command") -Name "" -Value $command
    }
}

function Get-DirectorySizeKb {
    param([Parameter(Mandatory = $true)][string] $Directory)

    if (-not (Test-Path -LiteralPath $Directory -PathType Container)) {
        return 0
    }

    $totalBytes = 0
    foreach ($file in @(Get-ChildItem -LiteralPath $Directory -Recurse -Force | Where-Object { -not $_.PSIsContainer })) {
        $totalBytes += $file.Length
    }

    return [int] [Math]::Ceiling($totalBytes / 1024)
}

function Register-UninstallEntry {
    param(
        [Parameter(Mandatory = $true)][string] $InstallDirectory,
        [Parameter(Mandatory = $true)][string] $InstalledExe,
        [Parameter(Mandatory = $true)][string] $InstalledScript,
        [Parameter(Mandatory = $true)][hashtable] $SourceInfo
    )

    $uninstallKey = Get-UninstallKeyPath
    $uninstallCommand = "powershell -NoProfile -ExecutionPolicy Bypass -File " +
        (Quote-CommandArgument $InstalledScript) + " -Action Uninstall -Scope $Scope -InstallDir " +
        (Quote-CommandArgument $InstallDirectory)

    Set-RegistryValue -Path $uninstallKey -Name "DisplayName" -Value $AppName
    Set-RegistryValue -Path $uninstallKey -Name "DisplayVersion" -Value $SourceInfo.ReleaseTag
    Set-RegistryValue -Path $uninstallKey -Name "Publisher" -Value "Marc-Andre Cote"
    Set-RegistryValue -Path $uninstallKey -Name "InstallLocation" -Value $InstallDirectory
    Set-RegistryValue -Path $uninstallKey -Name "DisplayIcon" -Value "$InstalledExe,0"
    Set-RegistryValue -Path $uninstallKey -Name "UninstallString" -Value $uninstallCommand
    Set-RegistryValue -Path $uninstallKey -Name "QuietUninstallString" -Value $uninstallCommand
    Set-RegistryValue -Path $uninstallKey -Name "NoModify" -Value 1 -Type "DWord"
    Set-RegistryValue -Path $uninstallKey -Name "NoRepair" -Value 1 -Type "DWord"
    Set-RegistryValue -Path $uninstallKey -Name "EstimatedSize" -Value (Get-DirectorySizeKb $InstallDirectory) -Type "DWord"
}

function Unregister-ImageTypes {
    $classesRoot = Get-ClassesRoot
    $applicationsRoot = Join-Path $classesRoot "Applications\$ExeName"
    $progIdRoot = Join-Path $classesRoot $ImageProgId
    foreach ($extension in $SupportedExtensions) {
        Remove-RegistryValue -Path (Join-Path $classesRoot "$extension\OpenWithProgids") -Name $ImageProgId
        Remove-RegistryKey -Path (Join-Path $classesRoot "$extension\OpenWithList\$ExeName")
    }

    Remove-RegistryKey -Path $applicationsRoot
    Remove-RegistryKey -Path $progIdRoot
    Remove-RegistryKey -Path (Get-CapabilitiesRoot)
    Remove-RegistryValue -Path (Get-RegisteredApplicationsPath) -Name $AppName
}

function Unregister-Folders {
    $classesRoot = Get-ClassesRoot
    foreach ($shellRoot in @("Directory", "Drive")) {
        Remove-RegistryKey -Path (Join-Path $classesRoot "$shellRoot\shell\$FolderVerbKey")
    }
}

function Unregister-UninstallEntry {
    Remove-RegistryKey -Path (Get-UninstallKeyPath)
}

function Get-EnvironmentPathRegistry {
    if ($Scope -eq "AllUsers") {
        return @{
            Path = "HKLM:\SYSTEM\CurrentControlSet\Control\Session Manager\Environment"
            Name = "Path"
        }
    }

    return @{
        Path = "HKCU:\Environment"
        Name = "Path"
    }
}

function Add-ToPath {
    param([Parameter(Mandatory = $true)][string] $Directory)

    $environmentPath = Get-EnvironmentPathRegistry
    New-Key $environmentPath.Path
    $currentPath = ""
    $property = Get-ItemProperty -LiteralPath $environmentPath.Path -Name $environmentPath.Name -ErrorAction SilentlyContinue
    if ($property -and ($property.PSObject.Properties.Name -contains $environmentPath.Name)) {
        $currentPath = [string] $property.($environmentPath.Name)
    }

    $parts = @()
    if ($currentPath) {
        $parts = $currentPath -split ";"
    }

    foreach ($part in $parts) {
        if ($part.TrimEnd("\") -ieq $Directory.TrimEnd("\")) {
            return $false
        }
    }

    if ($currentPath) {
        $newPath = $currentPath.TrimEnd(";") + ";" + $Directory
    }
    else {
        $newPath = $Directory
    }

    Set-RegistryValue -Path $environmentPath.Path -Name $environmentPath.Name -Value $newPath -Type "ExpandString"
    return $true
}

function Remove-FromPath {
    param([Parameter(Mandatory = $true)][string] $Directory)

    $environmentPath = Get-EnvironmentPathRegistry
    if (-not (Test-Path -LiteralPath $environmentPath.Path)) {
        return $false
    }

    $property = Get-ItemProperty -LiteralPath $environmentPath.Path -Name $environmentPath.Name -ErrorAction SilentlyContinue
    if (-not $property -or -not ($property.PSObject.Properties.Name -contains $environmentPath.Name)) {
        return $false
    }

    $currentPath = [string] $property.($environmentPath.Name)
    $parts = @()
    foreach ($part in ($currentPath -split ";")) {
        if ($part -and ($part.TrimEnd("\") -ine $Directory.TrimEnd("\"))) {
            $parts += $part
        }
    }

    $newPath = $parts -join ";"
    if ($newPath -eq $currentPath) {
        return $false
    }

    Set-RegistryValue -Path $environmentPath.Path -Name $environmentPath.Name -Value $newPath -Type "ExpandString"
    return $true
}

function Invoke-ShellRefresh {
    try {
        $signature = @"
using System;
using System.Runtime.InteropServices;
public static class ImgVwShellRefresh
{
    [DllImport("shell32.dll")]
    public static extern void SHChangeNotify(int eventId, uint flags, IntPtr item1, IntPtr item2);

    [DllImport("user32.dll", SetLastError = true, CharSet = CharSet.Auto)]
    public static extern IntPtr SendMessageTimeout(IntPtr window, uint message, UIntPtr wParam, string lParam,
        uint flags, uint timeout, out UIntPtr result);
}
"@
        if (-not ("ImgVwShellRefresh" -as [type])) {
            Add-Type -TypeDefinition $signature
        }

        [ImgVwShellRefresh]::SHChangeNotify(0x08000000, 0, [IntPtr]::Zero, [IntPtr]::Zero)
        $result = [UIntPtr]::Zero
        [ImgVwShellRefresh]::SendMessageTimeout([IntPtr] 0xffff, 0x001a, [UIntPtr]::Zero, "Environment", 0x0002, 5000, [ref] $result) | Out-Null
    }
    catch {
        Write-Warning "Could not refresh Explorer association state automatically. Restart Explorer or sign out if changes are not visible."
    }
}

function Write-InstallMarker {
    param(
        [Parameter(Mandatory = $true)][string] $Directory,
        [Parameter(Mandatory = $true)][string] $SelectedArch,
        [Parameter(Mandatory = $true)][hashtable] $SourceInfo,
        [Parameter(Mandatory = $true)][bool] $AddedToPath
    )

    $markerPath = Join-Path $Directory $InstallMarkerName
    $data = @{
        app = $AppName
        installedAt = (Get-Date).ToString("o")
        scope = $Scope
        arch = $SelectedArch
        releaseTag = $SourceInfo.ReleaseTag
        assetName = $SourceInfo.AssetName
        downloadUrl = $SourceInfo.DownloadUrl
        exePath = (Join-Path $Directory $ExeName)
        installerPath = (Join-Path $Directory $InstallerScriptName)
        addedToPath = $AddedToPath
        registeredImages = (-not $SkipImageRegistration)
        registeredFolders = (-not $SkipFolderRegistration)
    }

    if (Get-Command ConvertTo-Json -ErrorAction SilentlyContinue) {
        $data | ConvertTo-Json | Set-Content -LiteralPath $markerPath -Encoding ASCII
    }
    else {
        $lines = @()
        foreach ($key in $data.Keys) {
            $lines += "$key=$($data[$key])"
        }
        Set-Content -LiteralPath $markerPath -Value $lines -Encoding ASCII
    }
}

function Test-InstallMarker {
    param([Parameter(Mandatory = $true)][string] $Directory)

    return (Test-Path -LiteralPath (Join-Path $Directory $InstallMarkerName) -PathType Leaf)
}

function Read-InstallMarker {
    param([Parameter(Mandatory = $true)][string] $Directory)

    $markerPath = Join-Path $Directory $InstallMarkerName
    if (-not (Test-Path -LiteralPath $markerPath -PathType Leaf)) {
        return $null
    }

    $content = Get-Content -LiteralPath $markerPath -Raw
    if (-not $content) {
        return $null
    }

    if (Get-Command ConvertFrom-Json -ErrorAction SilentlyContinue) {
        try {
            return ($content | ConvertFrom-Json)
        }
        catch {
        }
    }

    $marker = @{}
    foreach ($line in ($content -split "`r?`n")) {
        $separator = $line.IndexOf("=")
        if ($separator -gt 0) {
            $marker[$line.Substring(0, $separator)] = $line.Substring($separator + 1)
        }
    }

    return $marker
}

function Get-MarkerValue {
    param(
        [Parameter(Mandatory = $true)][object] $Marker,
        [Parameter(Mandatory = $true)][string] $Name
    )

    if ($Marker -is [System.Collections.IDictionary]) {
        return [string] $Marker[$Name]
    }

    if ($Marker.PSObject.Properties[$Name]) {
        return [string] $Marker.$Name
    }

    return ""
}

function Get-InstallPlan {
    param(
        [Parameter(Mandatory = $true)][string] $TargetDirectory,
        [Parameter(Mandatory = $true)][string] $TargetExe,
        [Parameter(Mandatory = $true)][string] $SelectedArch,
        [Parameter(Mandatory = $true)][hashtable] $SourceInfo
    )

    if ($Force -or $SourceExe -or -not (Test-Path -LiteralPath $TargetExe -PathType Leaf)) {
        return "Install"
    }

    $marker = Read-InstallMarker -Directory $TargetDirectory
    if (-not $marker) {
        Write-Host "Existing ImgVw installation has no install marker; refreshing registration and files."
        return "Install"
    }

    $installedArch = Get-MarkerValue -Marker $marker -Name "arch"
    if ($installedArch -and $installedArch -ne $SelectedArch) {
        Write-Host "Changing ImgVw architecture from $installedArch to $SelectedArch."
        return "Install"
    }

    $installedTag = Get-MarkerValue -Marker $marker -Name "releaseTag"
    $requestedTag = $SourceInfo.ReleaseTag
    if ($requestedTag -eq "local") {
        return "Install"
    }

    $comparison = Compare-VersionTags -Left $installedTag -Right $requestedTag
    if ($comparison -lt 0) {
        Write-Host "Upgrading ImgVw from $installedTag to $requestedTag."
        return "Install"
    }
    elseif ($comparison -eq 0) {
        Write-Host "ImgVw $installedTag is already installed. Refreshing registration."
        return "Refresh"
    }

    Write-Host "Installed ImgVw version $installedTag is newer than requested $requestedTag. Use -Force to reinstall."
    return "Skip"
}

function Install-InstallerScript {
    param([Parameter(Mandatory = $true)][string] $Directory)

    $targetScript = Join-Path $Directory $InstallerScriptName
    if ($InstallerScriptPath -and (Test-Path -LiteralPath $InstallerScriptPath -PathType Leaf)) {
        Copy-Item -LiteralPath $InstallerScriptPath -Destination $targetScript -Force
    }
    elseif ($InstallerScriptText) {
        Set-Content -LiteralPath $targetScript -Value $InstallerScriptText -Encoding ASCII
    }
    else {
        throw "Could not persist the installer script for Windows Apps uninstall support."
    }

    return $targetScript
}

function Install-ImgVw {
    $selectedArch = Resolve-TargetArch
    $targetDir = $InstallDir
    if (-not $targetDir) {
        $targetDir = Get-DefaultInstallDir
    }
    $targetDir = [System.IO.Path]::GetFullPath($targetDir)
    $targetExe = Join-Path $targetDir $ExeName

    if ($Scope -eq "AllUsers" -and -not (Test-IsElevated)) {
        throw "AllUsers installation requires an elevated PowerShell session."
    }

    $workDir = Join-Path ([System.IO.Path]::GetTempPath()) ("imgvw-install-" + [System.Guid]::NewGuid().ToString("N"))
    New-Item -ItemType Directory -Path $workDir -Force | Out-Null
    try {
        $sourceInfo = Resolve-SourceExecutable -SelectedArch $selectedArch -WorkDir $workDir
        Assert-ExecutableArch -Path $sourceInfo.Path -ExpectedArch $selectedArch
        $installPlan = Get-InstallPlan -TargetDirectory $targetDir -TargetExe $targetExe `
            -SelectedArch $selectedArch -SourceInfo $sourceInfo

        if ($installPlan -eq "Skip") {
            return
        }

        if ($installPlan -eq "Install" -and (Test-Path -LiteralPath $targetExe -PathType Leaf) -and -not $Force) {
            try {
                $stream = [System.IO.File]::Open($targetExe, [System.IO.FileMode]::Open, [System.IO.FileAccess]::ReadWrite, [System.IO.FileShare]::None)
                $stream.Close()
            }
            catch {
                throw "ImgVw.exe appears to be in use. Close ImgVw or rerun with -Force after stopping it."
            }
        }

        New-Item -ItemType Directory -Path $targetDir -Force | Out-Null
        if ($installPlan -eq "Install") {
            Copy-Item -LiteralPath $sourceInfo.Path -Destination $targetExe -Force
        }
        $targetScript = Install-InstallerScript -Directory $targetDir

        foreach ($docName in @("README.md", "LICENSE.md")) {
            $docPath = Join-Path $ScriptRoot "..\$docName"
            if (Test-Path -LiteralPath $docPath -PathType Leaf) {
                Copy-Item -LiteralPath $docPath -Destination (Join-Path $targetDir $docName) -Force
            }
        }

        if (-not $SkipImageRegistration) {
            Register-ImageTypes -InstalledExe $targetExe
        }
        if (-not $SkipFolderRegistration) {
            Register-Folders -InstalledExe $targetExe
        }

        $addedToPath = $false
        if (-not $NoPath) {
            $addedToPath = Add-ToPath -Directory $targetDir
        }

        Register-UninstallEntry -InstallDirectory $targetDir -InstalledExe $targetExe -InstalledScript $targetScript -SourceInfo $sourceInfo
        Write-InstallMarker -Directory $targetDir -SelectedArch $selectedArch -SourceInfo $sourceInfo -AddedToPath $addedToPath
        Invoke-ShellRefresh

        if ($installPlan -eq "Install") {
            Write-Host "Installed ImgVw $selectedArch to $targetExe"
        }
        else {
            Write-Host "ImgVw $selectedArch is installed at $targetExe"
        }
        if (-not $NoPath) {
            Write-Host "ImgVw.exe will be available in PATH from new terminals."
        }
        if ([Environment]::OSVersion.Version.Major -ge 10) {
            Write-Host "ImgVw can be uninstalled from Windows Settings > Apps > Installed apps (or Apps & features) > ImgVw > Uninstall."
        }
        if ($SetLegacyDefaults -and [Environment]::OSVersion.Version.Major -ge 6) {
            Write-Host "Open Windows Default Apps or Explorer Open With to choose ImgVw as the default image viewer."
        }
    }
    finally {
        Remove-Item -LiteralPath $workDir -Recurse -Force -ErrorAction SilentlyContinue
    }
}

function Uninstall-ImgVw {
    $targetDir = $InstallDir
    if (-not $targetDir) {
        $targetDir = Get-DefaultInstallDir
    }
    $targetDir = [System.IO.Path]::GetFullPath($targetDir)

    if ($Scope -eq "AllUsers" -and -not (Test-IsElevated)) {
        throw "AllUsers uninstall requires an elevated PowerShell session."
    }

    if (-not $SkipImageRegistration) {
        Unregister-ImageTypes
    }
    if (-not $SkipFolderRegistration) {
        Unregister-Folders
    }
    if (-not $NoPath) {
        Remove-FromPath -Directory $targetDir | Out-Null
    }
    Unregister-UninstallEntry

    if (Test-InstallMarker -Directory $targetDir) {
        foreach ($fileName in @($ExeName, $InstallerScriptName, "README.md", "LICENSE.md", $InstallMarkerName)) {
            $path = Join-Path $targetDir $fileName
            if (Test-Path -LiteralPath $path -PathType Leaf) {
                Remove-Item -LiteralPath $path -Force
            }
        }

        $remaining = @()
        if (Test-Path -LiteralPath $targetDir -PathType Container) {
            $remaining = @(Get-ChildItem -LiteralPath $targetDir -Force)
        }
        if ($remaining.Count -eq 0) {
            Remove-Item -LiteralPath $targetDir -Force
        }
    }
    else {
        Write-Warning "Install marker was not found. Registry entries were removed, but files in $targetDir were left in place."
    }

    Invoke-ShellRefresh
    Write-Host "Uninstalled ImgVw registration for $Scope."
}

if ($Action -eq "Install") {
    Install-ImgVw
}
else {
    Uninstall-ImgVw
}
