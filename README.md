# ImgVw

ImgVw is a simple, fast and portable image viewer for Windows.

## Features

- Fast display caching
- Copy and run, no installation required
- Auto-rotate JPEG images based on EXIF information
- Embedded and fallback ICC color management for CMYK JPEG images
- HEIC/HEIF still-image display
- Works on Windows XP and later

## Usage

Pass a file or folder as an argument.

| Shortcut | Description |
|:-:|:-|
| Left Arrow \| Mouse Wheel Up | Browse backward |
| Right Arrow \| Mouse Wheel Down | Browse forward |
| Home | Go to first |
| End | Go to last |
| F5 | Toggle slideshow |
| Shift + F5 | Toggle slideshow (random mode) |
| Ctrl + Shift + F5 | Toggle slideshow on all monitors (random mode) |
| F6 | Increase slideshow speed |
| F7 | Decrease slideshow speed |
| F8 | Add images found in subfolders |
| Ctrl + I | Select default CMYK ICC profile |
| Ctrl + Shift + I | Use the built-in CMYK ICC profile |
| Delete | Move to recycle bin if possible or delete |
| Shift + Delete | Delete |
| Enter | Display current file path |
| Escape | Exit |

## 3rd-party libraries

ImgVw uses the following libraries:
- [libjpeg-turbo](https://github.com/libjpeg-turbo/libjpeg-turbo)
- [Little-CMS](https://github.com/mm2/Little-CMS)
- [libheif](https://github.com/strukturag/libheif)
- [libde265](https://github.com/strukturag/libde265)

ImgVw includes the unchanged
[CGATS21 CRPC5](https://registry.color.org/profile-registry/CGATS21_CRPC5) profile as a generic fallback for untagged
CMYK JPEG files. It is an approximate viewing default, not an exact representation of every printing condition.

## Builds

Visual Studio and MSYS builds support Win32 and x64 configurations. Win32 remains available for legacy Windows
compatibility. x64 is recommended for large images or large folders, but requires architecture-matched static libraries
for libjpeg-turbo, Little CMS, libheif, and libde265.

Rebuild the bundled dependencies with:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\build-libjpeg-turbo.ps1 -Mode all -Arch all -Clean
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\build-little-cms.ps1 -Mode all -Arch all -Clean
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\build-libheif.ps1 -Mode all -Arch all -Clean
```

## License

ImgVw is BSD-licensed.
