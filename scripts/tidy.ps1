[CmdletBinding()]
param(
    [switch] $Fix
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

$clangTidy = Find-ClangTool "clang-tidy.exe"
$sources = Get-ChildItem -Path (Join-Path $repoRoot "src") -Recurse -File -Include *.cpp | Sort-Object FullName

$compileArgs = @(
    "-x", "c++",
    "-std=c++17",
    "-I$repoRoot",
    "-I$(Join-Path $repoRoot 'src\app')",
    "-I$(Join-Path $repoRoot 'src\browse')",
    "-I$(Join-Path $repoRoot 'src\diagnostics')",
    "-I$(Join-Path $repoRoot 'src\image')",
    "-I$(Join-Path $repoRoot 'src\platform\win32')",
    "-I$(Join-Path $repoRoot 'src\ui\win32')",
    "-I$(Join-Path $repoRoot 'resources')",
    "-isystem", "$(Join-Path $repoRoot '3rd-party\libjpeg-turbo')",
    "-isystem", "$(Join-Path $repoRoot '3rd-party\Little-CMS')",
    "-DWINVER=0x0501",
    "-D_WIN32_WINNT=0x0501",
    "-DUNICODE",
    "-D_UNICODE",
    "-D_CRT_SECURE_NO_WARNINGS"
)

foreach ($source in $sources) {
    $arguments = @($source.FullName)
    if ($Fix) {
        $arguments += "-fix"
    }

    $arguments += "--"
    $arguments += $compileArgs

    & $clangTidy @arguments
}
