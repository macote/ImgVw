# Native Linux GTK4 Frontend Plan

Date: 2026-07-27

## Status

**Pending.** The current source tree has no Linux platform adapter or GTK frontend. The historical
`codex/portable-core` branch contains an early GTK window and XDG-directory experiment, but it predates major source
renames and browser/UI refactors. Use it only as a design reference; do not replay it over current `master`.

This plan depends on `portable_core_foundation_plan.md`. Linux work may add adapters and build validation early, but
feature work should not fork incomplete core behavior.

## Goal

Deliver a native GTK4 Linux application that uses the same portable controller, browser, loader, cache, decoders, color
management, and immutable frames as Windows and macOS.

The application should support:

- opening an image or folder from the command line, desktop, dialog, or drag and drop;
- sequential and random navigation;
- recursive browsing;
- reload;
- sequential and random slideshows;
- current path and image-information display;
- JPEG, PNG, and HEIF through portable decoders;
- ICC profile selection and shared color policy;
- move to Trash and explicit permanent deletion;
- keyboard, mouse wheel, context-menu, and native application actions; and
- clean shutdown while enumeration or decoding is active.

## Non-Goals

The first Linux release does not require:

- pixel-identical Windows chrome;
- shared widget code;
- X11- or Wayland-specific code in the core;
- BMP, GIF, TIFF, or ICO before portable decoders are registered;
- animated playback;
- GPU image processing;
- a plugin ABI; or
- every Windows multi-monitor behavior before the primary single-window workflow is stable.

## Windows Non-Regression Gate

Linux work must remain additive to the shipping Win32 application.

For every Linux phase:

- keep Visual Studio projects/filters, the MSYS Makefile, and Windows test shards synchronized for shared sources;
- link GTK, GLib, GIO, and Linux/POSIX libraries only to Linux platform/frontend targets;
- never expose GTK/GLib objects, file descriptors, `dirent`, Linux byte paths, XDG locations, or desktop Trash details
  through shared or Windows headers;
- preserve Windows XP compile definitions, import behavior, and native fallbacks;
- preserve Win32 resources, accelerators, dialogs, shell integration, Recycle Bin behavior, GDI presentation, DPI, and
  multi-monitor behavior;
- preserve Windows GDI+ fallback formats until portable replacements are proven;
- preserve cloud-file consent gates when shared probing/loading code changes;
- run affected Windows test shards for every shared change; and
- complete the full Windows gate in `portable_core_foundation_plan.md` and
  `windows_native_frontend_migration_plan.md` before a shared phase exits.

Linux-only adapter or GTK changes still require confirming that Windows configuration excludes Linux sources and
dependencies. Shared path, decoder, browser, loader, cache, command, view-state, or CMake changes require the full
Windows build/test and XP-sensitive verification matrix.

## Required Core Entry Criteria

Begin substantial GTK behavior after:

- `imgvw_core` builds with GCC and Clang on Linux;
- `PlatformPath`, `ImageFrame`, `AppController`, `AppCommand`, and `AppViewState` are usable without Win32 or Apple
  exceptions;
- filesystem, settings, resources, file operations, task execution, clock, and UI dispatch have stable interfaces;
- browser, loader, cache, cancellation, and stale-result tests run headlessly;
- JPEG and HEIF publish portable frames; and
- native PNG is available or clearly tracked as a release blocker.

## Linux Platform Adapters

### Paths and Filesystem

Implement:

- lossless native-byte path identity;
- best-effort UTF-8 display text without rejecting non-UTF-8 names;
- file/folder inspection;
- asynchronous directory enumeration;
- read-only mappings or bounded reads;
- current and Pictures directories; and
- portable metadata and error mapping.

Do not expose `dirent`, file descriptors, `GFile`, or GLib strings to the core.

Ordering must follow the shared display policy while preserving case-sensitive native identity.

### Application Directories and Resources

Use:

- `XDG_CONFIG_HOME` with `~/.config` fallback;
- `XDG_CACHE_HOME` with `~/.cache` fallback;
- `XDG_DATA_HOME` with `~/.local/share` fallback;
- an appropriate runtime/temporary location; and
- installed resource lookup independent from the source tree.

Environment overrides must be absolute and validated. Store the bundled ICC fallback as a platform-neutral resource.

### Settings

Implement `ISettingsStore` without making the core depend on GSettings schemas. A small versioned file is acceptable if
it provides atomic writes, clear error results, and migration behavior.

### Trash and Delete

Use a maintained desktop Trash implementation that follows the freedesktop.org specification. Prefer a suitable GLib
or GIO API in the adapter. Permanent deletion remains a separate command and confirmation workflow.

Never emulate Trash by moving files into an ImgVw-owned directory.

### Dispatch, Timers, and Background Work

- schedule UI publication on the GLib main context;
- translate GTK/GLib timers into shared controller commands;
- use the shared task executor or a Linux implementation behind `ITaskExecutor`;
- preserve cancellation and bounded shutdown; and
- keep all GLib types out of core headers.

## GTK4 Frontend

### Application and Window

Create:

- `GtkApplication`;
- main image window;
- application and window actions;
- native open-file/open-folder dialogs;
- keyboard shortcuts and context menu;
- drag-and-drop input;
- desktop open-file activation; and
- lifecycle/shutdown adaptation.

### Frame Presentation

Convert or wrap each completed immutable `ImageFrame` once into a retained `GdkTexture` or Cairo-compatible
presentation object. Do not copy pixels on every draw and do not decode or wait for workers in paint callbacks.

Handle:

- aspect-fit without unintended upscaling;
- resize and scale-factor changes;
- alpha and color semantics;
- loading, empty, and error states; and
- frame lifetime across asynchronous state changes.

### Interaction and Features

Map native input to shared commands:

- previous/next/first/last/random;
- open image/folder and recursive browsing;
- slideshow modes and interval changes;
- reload;
- path and image-information visibility;
- ICC profile selection;
- Trash and permanent delete; and
- quit.

Use GTK accessibility roles, labels, focus behavior, and native shortcuts. Match navigation semantics, not Windows
widget layout.

## Build and Packaging

Add CMake targets:

```text
imgvw_platform_linux
imgvw_linux_platform_tests
imgvw_ui_gtk
imgvw_linux
```

Build rules:

- discover GTK4 through `pkg-config`;
- keep GTK/GLib linked only to Linux targets;
- build `imgvw_core` unchanged;
- start with distribution development packages;
- pin documented minimum dependency versions; and
- keep release packaging separate from the Windows build.

Evaluate AppImage, Flatpak, and native packages only after the application works from an installed prefix. Packaging
must include icons, desktop metadata, MIME associations, licenses, and LGPL source/relink materials.

## Implementation Phases

### Phase 1: Linux Core and Contract Build

1. Add Linux GCC/Clang CMake jobs.
2. Run portable core and decoder tests.
3. Add path, directory, mapping, resource, settings, and dispatch adapters.
4. Run shared platform contract tests.
5. Add ASan and UBSan jobs.

Exit condition: a headless controller opens a directory, navigates, decodes, and publishes frames on Linux.

Windows gate: shared service and decoder changes pass the full Windows build, tests, image regression, and XP matrix.

### Phase 2: GTK Application Foundation

1. Add `GtkApplication`, window, actions, and lifecycle.
2. Connect `AppController`.
3. Present empty/loading/error states.
4. Handle desktop and command-line open requests.
5. Verify clean shutdown during active work.

Exit condition: the GTK application can open and display one portable frame.

Windows gate: GTK/GLib remain absent from Windows targets and Win32 frame presentation remains green.

### Phase 3: Primary Workflow

1. Add navigation, recursive browsing, reload, and image information.
2. Add keyboard, mouse wheel, context menu, dialogs, and drag and drop.
3. Add slideshow modes and timers.
4. Add ICC selection.
5. Add Trash and permanent delete.

Exit condition: the primary documented ImgVw workflow works under GTK4.

Windows gate: shared commands and view-state transitions preserve the existing Windows workflow.

### Phase 4: Desktop Integration and Parity

1. Verify Wayland and X11.
2. Add accessibility and scale-factor testing.
3. Compare fixtures and controller behavior with Windows and macOS.
4. Decide remaining-format support.
5. Add installed-prefix resource tests.

Exit condition: Linux desktop integration passes without changing Windows shell, resources, settings, or file-operation
behavior.

### Phase 5: Distribution

1. Select package formats.
2. Add icons, desktop entry, MIME metadata, and release automation.
3. Audit dynamic dependencies and licenses.
4. Validate clean installation, upgrade, and removal.

Exit condition: Linux packages install and run without altering Windows packaging, dependency lookup, release
artifacts, or installation behavior.

Windows gate: Linux packaging tools and metadata remain excluded from Windows targets and the full Windows release
matrix passes from the same source revision.

## Verification Matrix

- current supported Ubuntu and Fedora-family environments;
- GCC and Clang;
- Wayland and X11;
- standard and HiDPI scale factors;
- light and dark desktop appearance;
- non-ASCII and non-UTF-8 filenames;
- local, removable, and unavailable files;
- JPEG, PNG, HEIF, malformed, and oversized fixtures;
- navigation and shutdown during enumeration/decode;
- Trash success, cancellation, unsupported cases, and permanent deletion;
- ASan and UBSan core/decoder tests; and
- installed package execution without source-tree assets.

## Acceptance Criteria

The Linux frontend is complete when:

- the GTK4 application supports the primary workflow listed in this plan;
- it uses the shared controller and portable decoders without Linux-specific core exceptions;
- the established Win32 application retains its build paths, XP compatibility, commands, format behavior, shell
  integration, Recycle Bin behavior, DPI/multi-monitor handling, and clean shutdown;
- GTK/GLib types and dependencies are confined to Linux targets;
- immutable frames are retained without per-paint decode or copy;
- native path identity and Trash behavior pass contract tests;
- Wayland and X11 verification passes;
- shutdown is clean with active background work;
- an installed package finds all resources; and
- build, dependency, licensing, relinking, and packaging instructions are documented.
