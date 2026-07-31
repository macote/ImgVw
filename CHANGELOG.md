## 1.12.1 (2026-07-30)

- Complete the MSIX AppList icon family for reliable transparent icons across Windows shell surfaces and display scales
- Reduce and vertically balance the transparent padding around Store and AppList icon artwork
- Fix new-release detection in the GitHub release helper when run with Windows PowerShell

## 1.12.0 (2026-07-28)

- Show intrinsic image dimensions and source file size in the information overlay
- Add complete light- and dark-theme unplated MSIX icon variants for clearer Start and taskbar presentation
- Add Microsoft Store download links to the project documentation

## 1.11.0 (2026-07-27)

- Improve multi-monitor loading with shared caches and loader capacity per distinct target size, including asynchronous
  secondary-size preloading without duplicate queue work
- Reset cached and queued image work when opening a new image or folder while preserving warm caches across slideshow
  starts and restarts
- Expand diagnostics to aggregate every active monitor size correctly and report sequential as well as random slideshow
  mode and cycle progress
- Add an explicit searching-subfolders state and propagate subfolder discovery to active secondary monitor browsers
- Present the welcome and no-images states as a standard resizable window with minimize/maximize controls, a
  taskbar-visible work area, and an 80%-of-work-area minimum size
- Disable navigation, deletion, slideshow, and slideshow-speed commands until at least one supported image is found
- Move the persistent filename-overlay shortcut from Enter to Space while retaining Enter and Space activation for
  focused empty-state buttons
- Refresh contributor guidance and user documentation to reflect the current plan layout, dependency build scripts,
  and filename-overlay shortcut
- Improve reliability when moving the viewer between monitors by retaining compatible image caches
- Harden JPEG decoding and image-buffer validation for malformed, oversized, or unsafe image data
- Fix a startup race when opening empty folders
- Improve image-loader shutdown and general resource ownership safety

## 1.10.0 (2026-07-17)

- Add a safe launch screen for Microsoft Store and other no-argument launches, instead of browsing the process current
  directory
- Add native image and folder pickers, drag-and-drop opening, and Open image/Open folder commands with shortcuts
- Add recovery controls for empty folders, including optional recursive subfolder searching
- Improve the welcome screen with keyboard navigation, sensible focus for empty-folder recovery, and a scalable logo
- Match the start screen to the diagnostic overlay styling and follow the system light/dark app preference, with a
  light fallback on Windows XP
- Keep active random slideshow cycles stable as images are added or deleted, avoid duplicate visible images across
  monitors, and report cycle progress in the diagnostics overlay
- Show diagnostics on every slideshow monitor and improve overlay font rendering, DPI padding, column alignment, and
  status presentation
- Prevent the F5/F6 multi-monitor slideshow shortcuts from starting multi-monitor mode when only one display is
  available
- Replace the Enter path dialog with a persistent current-file overlay on every monitor; pressing Ctrl + Alt clears
  the filename toggle while displaying diagnostics

## 1.9.0 (2026-07-09)

- Add multi-monitor random and sequential slideshow support with shared random ordering across displays
- Improve image fitting, cache warmup, and monitor/DPI transitions when moving the viewer between displays
- Add a PowerShell installer that downloads the latest release, installs per-user by default, adds ImgVw to PATH,
  registers Explorer Open With integration for supported images and folders, and supports Windows Settings uninstall
- Allow dragging the viewer between monitors and refine monitor-edge snapping
- Add native DPI-aware loading progress and loader statistics overlays for cache/loading diagnostics
- Show consistent file details in loading overlays for JPEG and HEIF images
- Improve startup handling for missing or empty folders
- Display a selected image normally when it is the only supported image in its folder
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
