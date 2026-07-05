# ImgVw

ImgVw is a simple, fast and portable image viewer for Windows.

## Features

- Fast display caching
- Copy and run, no installation required
- Auto-rotate images based on EXIF information
- Embedded and fallback ICC color management for CMYK JPEG images
- Works on Windows XP and later

## Usage

Pass a file or folder as an argument.

|            Shortcut             | Description                                    |
| :-----------------------------: | :--------------------------------------------- |
|  Left Arrow \| Mouse Wheel Up   | Browse backward                                |
| Right Arrow \| Mouse Wheel Down | Browse forward                                 |
|              Home               | Go to first                                    |
|               End               | Go to last                                     |
|               F1                | About                                          |
|               F3                | Toggle slideshow                               |
|               F4                | Toggle slideshow (random mode)                 |
|               F5                | Toggle slideshow on all monitors               |
|               F6                | Toggle slideshow on all monitors (random mode) |
|               F7                | Increase slideshow speed                       |
|               F8                | Decrease slideshow speed                       |
|               F9                | Add images found in subfolders                 |
|               F11               | Select default CMYK ICC profile                |
|               F12               | Use the built-in CMYK ICC profile              |
|           Ctrl + Alt            | Display loader statistics overlay              |
|             Delete              | Move to recycle bin if possible or delete      |
|         Shift + Delete          | Delete                                         |
|              Enter              | Display current file path                      |
|             Escape              | Exit                                           |

## 3rd-party libraries

ImgVw uses the following libraries:

- [libjpeg-turbo](https://github.com/libjpeg-turbo/libjpeg-turbo)
- [libheif](https://github.com/strukturag/libheif)
- [libde265](https://github.com/strukturag/libde265)
- [Little-CMS](https://github.com/mm2/Little-CMS)

ImgVw includes the unchanged
[CGATS21 CRPC5](https://registry.color.org/profile-registry/CGATS21_CRPC5) profile as a generic fallback for untagged
CMYK JPEG files. It is an approximate viewing default, not an exact representation of every printing condition.

## Builds

Visual Studio and MSYS builds support Win32 and x64 configurations. Win32 remains available for legacy Windows
compatibility. x64 is recommended for large images or large folders, but requires architecture-matched static libraries
for libjpeg-turbo, libheif, libde265 and Little CMS.

Rebuild the bundled dependencies with:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\build-libjpeg-turbo.ps1 -Mode all -Arch all -Clean
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\build-libheif.ps1 -Mode all -Arch all -Clean
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\build-little-cms.ps1 -Mode all -Arch all -Clean
```

## Relinking LGPL Libraries

Release binaries statically link libheif and libde265. To relink ImgVw with modified versions of those libraries, use
the source archive for the exact ImgVw release, rebuild the static library artifacts from the modified dependency
sources, and then rebuild ImgVw.

The bundled HEIF dependency script currently uses these exact source archives:

- libheif 1.23.0:
  `https://github.com/strukturag/libheif/releases/download/v1.23.0/libheif-1.23.0.tar.gz`
  SHA-256: `4c9182b18897617182eed12ab5eb9f9d855b3aa3a736d6bdb31abc034ec7d393`
- libde265 1.1.1:
  `https://github.com/strukturag/libde265/releases/download/v1.1.1/libde265-1.1.1.tar.gz`
  SHA-256: `fd48a927e94ed74fc7ce8829d222b9d8599fcbfe8b6448ba66705babc56ab219`

Rebuild the standard, unmodified artifacts with:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\build-libheif.ps1 -Mode all -Arch all -Clean
```

To rebuild from modified libheif and libde265 source trees, pass those trees explicitly:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\build-libheif.ps1 -Mode all -Arch all -Clean `
    -LibheifSourceDir C:\src\libheif-1.23.0 -Libde265SourceDir C:\src\libde265-1.1.1
```

Then rebuild ImgVw with Visual Studio or with the MSYS build script:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\build-msys.ps1 -Config release -Arch x86 -Clean
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\build-msys.ps1 -Config release -Arch x64 -Clean
```

The generated dependency archives are written under `3rd-party\libheif` and `3rd-party\libde265`, where the Visual
Studio project and Makefile pick them up for the final application link.

## License

ImgVw’s own source code is MIT-licensed. Bundled third-party components are distributed under their respective licenses; see LICENSE.md.
