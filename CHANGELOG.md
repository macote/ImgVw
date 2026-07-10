## 1.9.0 (2026-07-09)

- Add multi-monitor random and sequential slideshow support with shared random ordering across displays
- Improve image fitting, cache warmup, and monitor/DPI transitions when moving the viewer between displays
- Add a PowerShell installer that downloads the latest release, installs per-user by default, adds ImgVw to PATH,
  registers Explorer Open With integration for supported images and folders, and supports Windows Settings uninstall
- Allow dragging the viewer between monitors and refine monitor-edge snapping
- Add native DPI-aware loading progress and loader statistics overlays for cache/loading diagnostics
- Show consistent file details in loading overlays for JPEG and HEIF images
- Improve startup handling for missing or empty folders
- Fix slideshow toggle/cursor behavior and restore the painted slide after stopping multi-monitor slideshow mode
- Fix context menus on secondary monitors during multi-monitor slideshows
- Hide multi-monitor menu items on single-monitor systems and remove the Ctrl+Q exit accelerator
- Refresh the application icon with high-resolution assets

## 1.8.0 (2026-06-24)

- Add HEIF/HEIC format display support
- Replace external easyexif dependency with a native orientation parser and direct pixel buffer rotation
- Implement content-based image format detection and dispatch using header signatures
- Bundle CGATS21 CRPC5 profile as generic CMYK fallback and add profile validation
- Upgrade Little CMS to 2.19.1 and libjpeg-turbo to 3.1.4.1
- Reorganize folder structure to separate UI, image, browser, and platform layers
- Remove UI thread blocking during image loading, restore slideshow loading priority, and implement bounded thread shutdown waits

## 1.7.0 (2019-01-08)

- Add the ability to browse subfolders
- Add the ability to use a default ICC profile
- Add support for high DPI
- Add context menu

## 1.6.0 (2018-02-10)

- Fix EXIF rotate bug
- Fix delete bug
- Release loaded image thread handles

## 1.5.0 (2017-12-05)

- Update mouse handling code

## 1.4.0 (2017-08-28)

- Support CMYK color in JPEG images

## 1.3.0 (2017-07-12)

- Slideshow
- Browse to first and last with Home and End
- Fix cleanup issue at program exit

## 1.2.0 (2017-06-04)

- Delete current file permanently with Shift + Delete
- Browse with mouse wheel

## 1.1.0 (2017-05-05)

- Auto-rotate JPEG images based on EXIF information
- Send current image to recycle bin on Delete key press

## 1.0.0 (2017-04-25)
- Initial release
