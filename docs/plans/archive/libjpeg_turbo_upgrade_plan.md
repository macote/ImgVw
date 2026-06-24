# libjpeg-turbo Upgrade Plan

Date: 2026-06-17

## Goal

Upgrade ImgVw's vendored `3rd-party/libjpeg-turbo` package from 1.5.2 to a current supported libjpeg-turbo release, with
static libraries consumable by both existing build paths:

- Visual Studio: Win32 and x64, linked as `jpeg-static.lib`.
- MSYS/Makefile: x86 and x64, linked as `libjpeg.a`.

Also determine whether the upgrade can remove the vendored `3rd-party/easyexif` dependency.

## Current State

ImgVw currently vendors only the libjpeg-turbo headers and static libjpeg API libraries needed by the custom
`src/image/turbojpeg_ImgVw.cpp` wrapper:

- Headers in `3rd-party/libjpeg-turbo/`.
- Visual Studio Win32 library at `3rd-party/libjpeg-turbo/jpeg-static.lib`.
- Visual Studio x64 library at `3rd-party/libjpeg-turbo/x64/jpeg-static.lib`.
- MSYS x86 library at `3rd-party/libjpeg-turbo/libjpeg.a`.
- MSYS x64 library expected at `3rd-party/libjpeg-turbo/ucrt64/libjpeg.a`.

The code uses the libjpeg API directly for decompression, saved APP1/APP2 marker access, and ICC assembly. It includes
`turbojpeg.h` mostly for TurboJPEG constants such as pixel formats, color spaces, scaling macros, and scaling factors.

`easyexif` is used only here:

- `ImgJPEGItem::Load()` calls `turbojpeg::LocateEXIFSegment()` to get the saved APP1 EXIF marker.
- `ImgItemHelper::GetExifOrientationFromData()` calls `easyexif::EXIFInfo::parseFromEXIFSegment()`.
- Only `EXIFInfo::Orientation` is consumed.

## Recommended Target Version

Use libjpeg-turbo `3.1.4.1` as the initial upgrade target.

Rationale:

- GitHub marks `3.1.4.1` as the latest stable release.
- `3.1.90` exists, but it is labeled as a `3.2 beta1` pre-release.
- `3.1.4.1` includes fixes in the TurboJPEG 2.x compatibility wrapper around scaled lossless decompression and several
  hardening fixes. ImgVw uses custom scaled decompression paths and saved markers, so staying on the stable line is the
  conservative choice.

## EXIF Dependency Finding

The libjpeg-turbo upgrade does not, by itself, eliminate the need for EXIF parsing.

What libjpeg-turbo provides:

- The libjpeg API can save APP markers via `jpeg_save_markers()`, which ImgVw already uses.
- The newer TurboJPEG 3 API can extract ICC profiles when marker saving is configured, and 3.1.4.1 improves repeated ICC
  profile retrieval.
- It still does not parse EXIF orientation into a viewer-ready rotation value.

What ImgVw still needs:

- Parse the TIFF structure inside the APP1 `Exif\0\0` payload.
- Read tag `0x0112` from IFD0.
- Map orientation values 1-8 to the existing `Gdiplus::RotateFlipType` mapping.

Recommended path:

1. Upgrade libjpeg-turbo first and keep `easyexif` unchanged.
2. After the JPEG upgrade is verified, remove `easyexif` in a small follow-up by replacing it with an ImgVw-owned
   orientation-only parser.

That parser can be less than 100 lines because ImgVw only needs one SHORT tag from IFD0. It should validate:

- APP1 payload starts with `Exif\0\0`.
- TIFF endian marker is `II` or `MM`.
- TIFF magic is `42`.
- IFD0 offset and entry count are within marker bounds.
- Entry type is SHORT (`3`) and count is at least 1.

Do not attempt to parse general EXIF metadata unless the product needs it.

## Proposed Build Output Layout

Preserve the current include and link paths so the first upgrade does not require project file churn:

```text
3rd-party/libjpeg-turbo/
  3.1.4.1
  LICENSE.md
  README.md
  README.ijg
  jconfig.h
  jerror.h
  jmorecfg.h
  jpeglib.h
  turbojpeg.h
  jpeg-static.lib          # VS Win32
  libjpeg.a                # MSYS x86
  x64/
    jpeg-static.lib        # VS x64
  ucrt64/
    libjpeg.a              # MSYS x64
```

Do not vendor libjpeg-turbo build trees or generated object files.

## Proposed Script

Add `scripts/build-libjpeg-turbo.ps1`. The script should download the official release tarball, build out-of-tree, and
copy only the files ImgVw consumes into `3rd-party/libjpeg-turbo`.

Initial implementation sketch:

```powershell
param(
    [string]$Version = "3.1.4.1",
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path,
    [string]$WorkRoot = (Join-Path $env:TEMP "imgvw-libjpeg-turbo")
)

$ErrorActionPreference = "Stop"

function Invoke-Checked {
    param([string]$FilePath, [string[]]$Arguments)
    & $FilePath @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$FilePath failed with exit code $LASTEXITCODE"
    }
}

function Require-Command {
    param([string]$Name)
    if (-not (Get-Command $Name -ErrorAction SilentlyContinue)) {
        throw "Required command not found in PATH: $Name"
    }
}

function Enter-VsDevShell {
    param([ValidateSet("x86", "x64")] [string]$Arch)

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
    $archArg = if ($Arch -eq "x86") { "x86" } else { "amd64" }

    cmd /c "`"$devCmd`" -arch=$archArg -host_arch=amd64 >nul && set" |
        ForEach-Object {
            if ($_ -match "^(.*?)=(.*)$") {
                Set-Item -Path "env:$($matches[1])" -Value $matches[2]
            }
        }
}

function Build-Vs {
    param(
        [ValidateSet("x86", "x64")] [string]$Arch,
        [string]$SourceDir,
        [string]$BuildRoot,
        [string]$VendorDir
    )

    Enter-VsDevShell $Arch

    $buildDir = Join-Path $BuildRoot "build-vs-$Arch"
    New-Item -ItemType Directory -Force -Path $buildDir | Out-Null

    Push-Location $buildDir
    try {
        Invoke-Checked cmake @(
            "-G", "NMake Makefiles",
            "-DCMAKE_BUILD_TYPE=Release",
            "-DENABLE_SHARED=0",
            "-DWITH_JAVA=0",
            "-DWITH_TESTS=0",
            $SourceDir
        )
        Invoke-Checked nmake @("jpeg-static")
    }
    finally {
        Pop-Location
    }

    $destDir = if ($Arch -eq "x64") { Join-Path $VendorDir "x64" } else { $VendorDir }
    New-Item -ItemType Directory -Force -Path $destDir | Out-Null
    Copy-Item -Force (Join-Path $buildDir "jpeg-static.lib") (Join-Path $destDir "jpeg-static.lib")
}

function Build-Msys {
    param(
        [ValidateSet("x86", "x64")] [string]$Arch,
        [string]$SourceDir,
        [string]$BuildRoot,
        [string]$VendorDir
    )

    $expectedMachine = if ($Arch -eq "x64") { "x86_64-w64-mingw32" } else { "i686-w64-mingw32" }
    $actualMachine = (& gcc -dumpmachine).Trim()
    if ($actualMachine -ne $expectedMachine) {
        throw "Run the $Arch MSYS2 shell. Expected gcc -dumpmachine=$expectedMachine, got $actualMachine."
    }

    $buildDir = Join-Path $BuildRoot "build-msys-$Arch"
    New-Item -ItemType Directory -Force -Path $buildDir | Out-Null

    Push-Location $buildDir
    try {
        Invoke-Checked cmake @(
            "-G", "MSYS Makefiles",
            "-DCMAKE_BUILD_TYPE=Release",
            "-DENABLE_SHARED=0",
            "-DWITH_JAVA=0",
            "-DWITH_TESTS=0",
            $SourceDir
        )
        Invoke-Checked make @("jpeg-static")
    }
    finally {
        Pop-Location
    }

    $destDir = if ($Arch -eq "x64") { Join-Path $VendorDir "ucrt64" } else { $VendorDir }
    New-Item -ItemType Directory -Force -Path $destDir | Out-Null
    Copy-Item -Force (Join-Path $buildDir "libjpeg.a") (Join-Path $destDir "libjpeg.a")
}

Require-Command cmake
Require-Command tar

$vendorDir = Join-Path $RepoRoot "3rd-party\libjpeg-turbo"
$url = "https://github.com/libjpeg-turbo/libjpeg-turbo/releases/download/$Version/libjpeg-turbo-$Version.tar.gz"
$archive = Join-Path $WorkRoot "libjpeg-turbo-$Version.tar.gz"
$sourceDir = Join-Path $WorkRoot "libjpeg-turbo-$Version"

New-Item -ItemType Directory -Force -Path $WorkRoot | Out-Null
Invoke-WebRequest -Uri $url -OutFile $archive
Remove-Item -Recurse -Force $sourceDir -ErrorAction SilentlyContinue
tar -xzf $archive -C $WorkRoot

Build-Vs -Arch x86 -SourceDir $sourceDir -BuildRoot $WorkRoot -VendorDir $vendorDir
Build-Vs -Arch x64 -SourceDir $sourceDir -BuildRoot $WorkRoot -VendorDir $vendorDir

# Run these from the matching MSYS2 shell, or split into separate invocations if PATH cannot switch safely.
Build-Msys -Arch x86 -SourceDir $sourceDir -BuildRoot $WorkRoot -VendorDir $vendorDir
Build-Msys -Arch x64 -SourceDir $sourceDir -BuildRoot $WorkRoot -VendorDir $vendorDir

Copy-Item -Force (Join-Path $sourceDir "jconfig.h") (Join-Path $vendorDir "jconfig.h")
Copy-Item -Force (Join-Path $sourceDir "jerror.h") (Join-Path $vendorDir "jerror.h")
Copy-Item -Force (Join-Path $sourceDir "jmorecfg.h") (Join-Path $vendorDir "jmorecfg.h")
Copy-Item -Force (Join-Path $sourceDir "jpeglib.h") (Join-Path $vendorDir "jpeglib.h")
Copy-Item -Force (Join-Path $sourceDir "turbojpeg.h") (Join-Path $vendorDir "turbojpeg.h")
Copy-Item -Force (Join-Path $sourceDir "LICENSE.md") (Join-Path $vendorDir "LICENSE.md")
Copy-Item -Force (Join-Path $sourceDir "README.md") (Join-Path $vendorDir "README.md")
Copy-Item -Force (Join-Path $sourceDir "README.ijg") (Join-Path $vendorDir "README.ijg")
Set-Content -NoNewline -Path (Join-Path $vendorDir $Version) -Value ""
```

Script notes:

- This is a proposed starting point, not a final committed build script.
- In practice, the MSYS x86 and x64 builds may need to be separate invocations from separate MSYS2 shells, because each
  shell has a different compiler toolchain in `PATH`.
- If the release tarball includes generated `jconfig.h` that is unsuitable for direct copying, copy `jconfig.h` from the
  matching configured build directory instead. Verify by building both ImgVw entrypoints.
- Consider adding optional SHA256/signature verification before extraction. Upstream documents digital-signature
  verification for official release assets.

## Integration Steps

1. Add the build script under `scripts/`.
2. Run Visual Studio x86 and x64 libjpeg-turbo builds.
3. Run MSYS x86 and x64 libjpeg-turbo builds.
4. Replace vendored libjpeg-turbo headers, license/readme files, and static libraries only.
5. Build ImgVw:
   - `make`
   - `make arch=x64`
   - Visual Studio Win32 Debug or Release
   - Visual Studio x64 Debug or Release
6. Fix any compile breaks in `src/image/turbojpeg_ImgVw.cpp`.
7. Smoke test JPEG behavior:
   - Baseline JPEG.
   - Progressive JPEG.
   - Large JPEG near current file-size handling limits.
   - JPEG with EXIF orientation values 1, 3, 6, and 8.
   - JPEG with embedded ICC profile, especially CMYK/YCCK.
   - JPEG with many APP markers, since current code saves APP1 and APP2 before header decode.

## Expected Code Risks

- `turbojpeg.h` constants or enum names may have changed since 1.5.2. Keep the wrapper changes minimal and local.
- The custom wrapper defines `TJFLAG_NOCLEANUP` as `32768`; verify it does not collide with a newer upstream flag.
- `jpeg_save_markers()` behavior is still available, but marker-heavy JPEGs should be tested because ImgVw reads headers
  with saved APP1/APP2 markers enabled.
- Static libraries built with one Visual C++ runtime/toolset should not be mixed with incompatible older Visual Studio
  toolsets. Build with the same Visual Studio generation used by ImgVw, or document the required toolset.
- Preserve Windows XP compatibility expectations by avoiding new Windows API usage in ImgVw code. The dependency build
  itself should not force ImgVw to load newer Windows APIs.

## Optional Follow-Up: Remove easyexif

After the libjpeg-turbo upgrade lands cleanly:

1. Add `src/image/ExifOrientation.h/.cpp` with `UINT ReadExifOrientation(const BYTE* data, UINT size)`.
2. Move only the APP1/TIFF orientation parsing logic into that file.
3. Replace `ImgItemHelper::GetExifOrientationFromData()` internals with the new parser, or remove that helper and call
   the parser directly.
4. Remove `3rd-party/easyexif` from:
   - `ImgVw.vcxproj`
   - `ImgVw.vcxproj.filters`
   - `Makefile` object list and include paths
   - `scripts/tidy.ps1`
   - `README.md`
   - `LICENSE.md`
5. Add focused tests if a native test harness exists. If not, keep a small corpus under `artifacts/` or document manual
   EXIF orientation smoke tests.

## References Checked

- libjpeg-turbo GitHub releases: latest stable `3.1.4.1`, pre-release `3.1.90`.
- libjpeg-turbo official binary compatibility notes: Windows XP and later are supported by upstream binaries; MinGW,
  MinGW-w64, and Visual C++ are supported.
- libjpeg-turbo `BUILDING.md`: CMake/NMake and MSYS Makefiles are the documented Windows build paths; outputs include
  `jpeg-static.lib` for Visual C++ and `libjpeg.a` for MinGW/MSYS.
- libjpeg-turbo issue #703: EXIF orientation/auto-rotation was closed as `enhancement` / `won't implement`.
