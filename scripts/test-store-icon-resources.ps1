[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string] $ManifestPath,

    [Parameter(Mandatory)]
    [string] $PriDumpPath
)

$ErrorActionPreference = "Stop"
[xml] $manifest = [IO.File]::ReadAllText($ManifestPath)
[xml] $pri = [IO.File]::ReadAllText($PriDumpPath)
$identity = $manifest.Package.Identity.Name
$resources = @($pri.SelectNodes("//NamedResource"))

# Match logical URIs, not just candidate counts or physical PNG paths: the shell uses the manifest lookup path.
$logos = $manifest.SelectNodes("//@*[contains(local-name(), 'Logo')] | //*[local-name()='Logo']")
foreach ($logo in $logos) {
    $path = $logo.get_InnerText().Replace('\', '/')
    $uri = "ms-resource://$identity/Files/$path"
    $matches = @($resources | Where-Object { $_.GetAttribute("uri") -eq $uri })
    if ($matches.Count -ne 1) {
        throw "Manifest logo is not indexed at its lookup path: $uri"
    }
}

# Check each shipped AppList variant belongs to the manifest's logical icon and retains its qualifiers.
$packageRoot = Split-Path -Parent $ManifestPath
foreach ($logo in $manifest.SelectNodes("//@Square44x44Logo")) {
    $path = $logo.get_InnerText().Replace('\', '/')
    $uri = "ms-resource://$identity/Files/$path"
    $resource = $resources | Where-Object { $_.GetAttribute("uri") -eq $uri }
    $iconPath = Join-Path $packageRoot $path
    $stem = [IO.Path]::GetFileNameWithoutExtension($iconPath)
    $variants = @(Get-ChildItem -LiteralPath (Split-Path -Parent $iconPath) -Filter "$stem.targetsize-*.png")
    if ($variants.Count -eq 0) {
        throw "No AppList target-size variants found for $path"
    }
    foreach ($variant in $variants) {
        $match = [regex]::Match($variant.Name, '\.targetsize-(\d+)(?:_altform-(\w+))?\.png$')
        if (-not $match.Success) {
            throw "Unexpected AppList variant filename: $($variant.Name)"
        }
        $candidatePath = (Join-Path (Split-Path -Parent $path) $variant.Name).Replace('\', '/')
        $candidates = @($resource.Candidate | Where-Object { $_.Value.Replace('\', '/') -eq $candidatePath })
        if ($candidates.Count -ne 1) {
            throw "AppList variant is not indexed under ${uri}: $candidatePath"
        }
        $qualifiers = $candidates[0].QualifierSet.Qualifier
        $size = @($qualifiers | Where-Object { $_.name -eq "TargetSize" -and $_.value -eq $match.Groups[1].Value })
        $form = @($qualifiers | Where-Object { $_.name -eq "AlternateForm" })
        $expectedForm = $match.Groups[2].Value
        if ($size.Count -ne 1 -or
            ($expectedForm -and ($form.Count -ne 1 -or $form[0].value -ne $expectedForm)) -or
            (-not $expectedForm -and $form.Count -ne 0)) {
            throw "Incorrect AppList qualifiers for $candidatePath"
        }
    }
    Write-Host "Verified manifest icon lookup and $($variants.Count) target-size variants: $path"
}
