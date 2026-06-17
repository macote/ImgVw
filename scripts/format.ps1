[CmdletBinding()]
param(
    [switch] $Check
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot

function Find-ClangTool {
    param([Parameter(Mandatory)][string] $Name)

    $pathCommand = Get-Command $Name -ErrorAction SilentlyContinue
    if ($pathCommand) {
        return $pathCommand.Source
    }

    $candidates = @(
        "${env:ProgramFiles}\Microsoft Visual Studio\18\Community\VC\Tools\Llvm\x64\bin\$Name",
        "${env:ProgramFiles}\Microsoft Visual Studio\18\Community\VC\Tools\Llvm\bin\$Name",
        "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    )

    foreach ($candidate in $candidates) {
        if ($candidate -like "*vswhere.exe") {
            if (-not (Test-Path $candidate)) {
                continue
            }

            $matches = & $candidate -latest -products * -find "VC\Tools\Llvm\x64\bin\$Name"
            if ($matches) {
                return $matches[0]
            }
        }
        elseif (Test-Path $candidate) {
            return $candidate
        }
    }

    throw "$Name was not found. Install LLVM, add it to PATH, or install the Visual Studio LLVM tools."
}

$clangFormat = Find-ClangTool "clang-format.exe"
$files = Get-ChildItem -Path `
    (Join-Path $repoRoot "src"), `
    (Join-Path $repoRoot "resources") `
    -Recurse -File -Include *.cpp,*.h,*.rc |
    Sort-Object FullName

if ($Check) {
    & $clangFormat --dry-run --Werror @($files.FullName)
}
else {
    & $clangFormat -i @($files.FullName)
}
