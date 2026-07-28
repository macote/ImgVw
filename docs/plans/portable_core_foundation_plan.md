# Portable Core Foundation Plan

Date: 2026-07-27

## Status

**In progress; initial boundary only.** The `codex/macos-portable-core` work establishes:

- an `imgvw_core` CMake target that builds without native UI frameworks;
- portable `AppCommand`, `AppError`, `ImageFrame`, and image-geometry definitions;
- existing navigation, EXIF, and resampling code in the portable target;
- an include-boundary check for Win32, GTK/GLib, and Apple framework headers; and
- focused portable-core tests that build and pass on Windows.

This is not yet the application core. Browsing, loading, decoding, caching, settings, file operations, application
state, and frontend notification still depend on Win32 types or behavior.

This plan owns the shared foundation used by all three platforms. Platform-specific delivery is tracked separately:

- `windows_native_frontend_migration_plan.md`
- `macos_native_frontend_plan.md`
- `linux_native_frontend_plan.md`

## Goal

Create one portable C++ image-viewer core that:

- compiles unchanged on Windows, macOS, and Linux;
- owns image decode policy, browsing, navigation, caching, slideshow state, and application commands;
- publishes immutable view state and image frames to native frontends;
- requests filesystem, settings, resource, trash/delete, clock, execution, and UI-dispatch work through narrow
  interfaces;
- contains no Win32, GDI+, AppKit, Foundation, Core Graphics, GTK, GLib, Objective-C, or native handle types in public
  headers; and
- preserves the shipping Windows workflow and documented Windows XP target during migration.

The core must make AppKit and GTK additional adapters, not reasons to fork application behavior.

## Non-Goals

This plan does not:

- implement AppKit or GTK widgets;
- define macOS or Linux packaging;
- replace the established Visual Studio or MSYS Windows release builds before equivalent CMake behavior is proven;
- change the Windows XP compatibility promise;
- add animated-image playback, GPU decoding, a plugin ABI, or a public SDK; or
- require feature support for a format that has no portable decoder.

## Current Refactor Baseline

The completed Windows refactors provide seams to evolve:

- `ImgVwWindow` delegates empty-state UI, display presentation, overlays, cursor behavior, dragging, monitor placement,
  and slideshow state to focused components.
- `DisplayPresenter` consumes an immutable `DisplaySnapshot`.
- `FolderScanner`, `BrowseSession`, and `PreloadScheduler` separate enumeration, collection lifetime, and preload work
  from the `ImgBrowser` facade.
- `ImgFormatDetector`, `ImgFormatResolver`, and `ImgDispatcher` separate content detection, supported-format policy,
  and image-item construction.
- loader/browser cancellation, generations, shutdown, stale notifications, and worker ownership have focused tests.
- tests are divided into core, platform, image, concurrency, and UI shards.

These components are behavioral references, not automatically portable APIs. Preserve their tested lifetime and
stale-result contracts while removing Win32 dependencies.

## Windows Continuity Contract

The existing Win32 application remains the shipping reference implementation throughout this plan. Portable work must
extend the architecture without placing the Windows application into a partially migrated or non-shipping state.

Required rules:

- `ImgVw.slnx`, `ImgVw.vcxproj`, `ImgVw.vcxproj.filters`, and the MSYS Makefile remain supported build entrypoints.
- Keep Visual Studio, filters, the Makefile, tests, and CMake synchronized whenever shared source files are added,
  moved, renamed, or removed.
- The Win32 application must build and run after every migration phase, not only after the final controller extraction.
- Preserve current commands, shortcuts, image presentation, browsing, preloading, slideshows, overlays, file
  operations, settings, empty/error states, and shutdown behavior unless a separate approved change intentionally
  alters them.
- Preserve the documented Windows XP target. New C++ runtime, threading, filesystem, and synchronization choices must
  pass import inspection and XP runtime verification or remain behind a Win32 implementation.
- Do not link AppKit, Foundation, Core Graphics, GTK, GLib, POSIX-only libraries, or macOS/Linux adapter objects into
  Windows targets.
- Do not remove GDI+ fallback formats from Windows until equivalent portable decoders are integrated and parity-tested.
- Keep Windows shell dialogs, Recycle Bin behavior, resources, accelerators, icons, DPI handling, and GDI presentation
  inside the Win32 frontend/adapters.
- Preserve cloud-file consent gates across format probing, visible loads, and preloads while those paths move into the
  core.
- Never make the Win32 paint or message path wait for portable workers.

### Incremental Replacement Method

For each migrated subsystem:

1. Capture or retain focused tests for current Windows behavior.
2. Introduce the portable contract beside the current implementation.
3. Wrap the existing Win32 behavior behind that contract.
4. Switch one composition path at a time.
5. Run focused and full Windows verification.
6. Remove the legacy path only after equivalent behavior and teardown are proven.

Avoid broad renaming while ownership, threading, or presentation behavior is changing. A portable abstraction is not
complete if Windows requires scattered platform conditionals to use it.

### Windows Gate for Every Phase

Before a phase is considered complete:

- all relevant focused test shards pass on MSYS x86 and x64;
- the full MSYS test suite passes on x86 and x64;
- MSYS Release application builds pass on x86 and x64;
- Visual Studio Release builds pass on Win32 and x64;
- formatting and applicable static analysis pass;
- final Windows executables have no Apple or GTK/GLib dependency;
- XP-sensitive changes receive import inspection and an XP smoke test; and
- affected workflows receive manual Win32 smoke coverage.

Routine work may use incremental builds. Use clean builds for build-system, source-list, configuration, dependency, or
release-validation changes.

## Target Architecture

```text
Win32 frontend ──┐
AppKit frontend ─┼─ AppController ─ Browser / loader / cache ─ Portable decoders
GTK4 frontend ───┘        │
                          └─ Platform service interfaces
                                 ├─ Win32 adapters
                                 ├─ macOS adapters
                                 └─ Linux adapters
```

The core owns state and policy. Frontends translate native events into commands and present immutable view state.
Platform adapters perform operating-system work requested by the core.

## Shared Data Contracts

### Application Commands and View State

Keep `AppCommand` platform-neutral. Refine the initial command list only through shared user workflows.

Add `AppViewState` with:

- current item ID, path display text, and item status;
- empty, collecting, queued, loading, ready, and error states;
- immutable current `ImageFrame`;
- source dimensions and portable destination geometry;
- navigation availability and sequential/random progress;
- slideshow mode and interval;
- recursive-browse state;
- user-facing error text; and
- state generation or identity needed to reject stale frontend updates.

Do not put menu titles, key codes, `HWND`, `NSImage`, `CGImage`, `GdkTexture`, or native dialog state in the view model.

### Errors and Results

Expand `AppError` into a stable subsystem boundary with:

- error domain;
- portable code where one exists;
- retained native code for diagnostics;
- diagnostic message; and
- enough context to generate a user-facing message at the frontend boundary.

Introduce a project-local `Result<T, AppError>` or equivalent. Use it at filesystem, mapping, decoder, color,
settings, resources, and file-operation boundaries. Do not mix exceptions, flags, strings, and native status values at
the same boundary.

### Paths

Introduce `PlatformPath` instead of exposing `std::wstring`, `NSString`, `std::filesystem::path`, or native byte paths.

Required behavior:

- lossless native identity;
- equality and stable ordering;
- parent, filename, extension, and child composition;
- separate display text;
- no implicit Unicode normalization for identity; and
- native conversion only inside platform adapters.

Keep the Windows representation lossless for XP. macOS must distinguish filesystem identity from Cocoa display text.
Linux must not require filenames to be valid UTF-8.

### Image Frames

Evolve the initial `ImageFrame` into the canonical decoder/frontend boundary:

- immutable shared pixel ownership;
- checked positive dimensions and stride;
- explicit pixel format, row order, alpha semantics, and color space;
- no DIB, mapping handle, temporary filename, `CGImage`, or GTK texture;
- stable frame identity for presentation caching; and
- optional source-versus-presentation frame distinction if resize behavior requires it.

Choose canonical RGBA8 or BGRA8 after measuring Windows DIB, Core Graphics, and GTK conversion costs. Document whether
alpha is straight or premultiplied.

## Shared Service Interfaces

Define interfaces from concrete workflows:

- `IFileSystem`: inspect, enumerate, map/read, current directory, and pictures directory;
- `IFileOperations`: move to Trash/Recycle Bin and permanent delete as distinct actions;
- `IAppDirectories`: configuration, cache, data, resources, and temporary locations;
- `ISettingsStore`: portable preferences without prescribing registry, plist, or file storage;
- `IResourceProvider`: bundled ICC profile and other byte assets;
- `IUiDispatcher`: publish work on the native UI thread;
- `IClock` and frontend timer boundary: monotonic slideshow and idle timing;
- `ITaskExecutor`: bounded background work and cancellation; and
- optional file-monitor interface after basic enumeration is portable.

Never expose Win32 structures, Objective-C objects, blocks, dispatch queues, POSIX records, GLib objects, or native
handles through these interfaces.

## Browser, Loader, and Cache

### Browser

Refactor the existing collaborators rather than replacing their behavior:

1. Make `FolderScanner` consume `IFileSystem` and portable directory entries.
2. Replace `std::wstring` paths with `PlatformPath`.
3. Replace Win32 cancellation events and scalar types with a cancellation token.
4. Keep recursion, format filtering, ordering, and supported-format policy in the core.
5. Preserve generation-based stale-result rejection.
6. Never notify a frontend while holding a browser lock.

Define display ordering separately from native path identity because case and normalization behavior differ by platform.

### Loader

Replace notification windows and worker handles with `IUiDispatcher`, `ITaskExecutor`, stable item identity, and shared
ownership. Preserve:

- bounded shutdown;
- idempotent stop results;
- cancellation independent from state destruction;
- priority for the visible item over preloads; and
- late completion rejection.

Every standard-library threading change requires a Windows XP import/runtime gate. If the shipping toolchain cannot
support the selected primitives on XP, keep a Win32 executor behind the shared interface.

### Cache

Make cache policy independent from windows and target handles:

- key by `PlatformPath` plus decode/presentation parameters;
- track approximate decoded bytes;
- apply explicit byte and item budgets;
- retain immutable frames while a frontend presents them; and
- reuse source frames across Retina/DPI changes where practical.

## Portable Image Pipeline

### Pixel Storage

Replace the temporary-file-backed `ImgBuffer` contract with checked memory storage owned by `ImageFrame`. Retain current
per-image limits and add cache byte budgets before switching. If spill storage is later required, hide mappings behind
`PixelStorage`.

### JPEG

- keep libjpeg-turbo decoding;
- use portable EXIF orientation;
- replace GDI+ rotate/flip/resize paths;
- publish the canonical frame; and
- apply shared Little CMS transforms.

### PNG

Complete `libpng_support_plan.md`. Native PNG decoding is required before macOS or Linux can claim primary format
parity because the current fallback is GDI+.

### HEIF

- keep libheif/libde265 decoding in the core;
- replace Win32 mappings, events, and error conversion;
- publish the canonical frame;
- preserve decode limits; and
- complete supported SDR ICC/NCLX behavior.

### Remaining Formats

Windows may retain GDI+ fallback support for BMP, GIF, TIFF, and ICO temporarily. macOS and Linux must advertise only
registered portable decoders. Decide dedicated portable support format by format after JPEG, PNG, and HEIF work.

## Application Controller

Extract `AppController` from top-level Win32 orchestration.

Responsibilities:

- accept `AppCommand`;
- coordinate browser, loader, cache, settings, resources, and file operations;
- own navigation and slideshow policy;
- publish immutable `AppViewState` through `IUiDispatcher`;
- provide deterministic shutdown; and
- contain no frontend framework headers.

Frontend responsibilities remain native window lifecycle, menus, shortcuts, dialogs, cursor behavior, timers,
presentation objects, painting, accessibility, clipboard, and invalidation.

## Build System and Boundary Enforcement

Use CMake as the cross-platform dependency graph:

```text
imgvw_core
imgvw_core_tests
imgvw_platform_win32
imgvw_platform_macos
imgvw_platform_linux
imgvw_ui_win32
imgvw_ui_macos
imgvw_ui_gtk
```

The initial CMake target is intentionally small. Expand it only when a file is actually portable.

Rules:

- the core target builds on Windows, macOS, and Linux;
- platform frameworks link only to platform/frontend targets;
- include scanning rejects native framework headers in core files;
- Visual Studio and the MSYS Makefile remain synchronized for shipping Windows sources;
- CMake does not replace the XP-tested release path until flags, resources, static dependencies, subsystem, runtime,
  and imports are equivalent; and
- no generated output under `bin/` or `obj/` is committed.

## Migration Phases

### Phase 1: Establish the Small Core Target

- [x] Add initial command, error, frame, and geometry types.
- [x] Add navigation, EXIF, and resampling to `imgvw_core`.
- [x] Add portable-core tests.
- [x] Add native-header boundary scanning.
- [ ] Build and test the target on macOS and Linux CI, not only Windows.
- [ ] Document canonical frame alpha and color semantics.

Exit condition: the initial target and tests pass unchanged on all three platforms.

Windows gate: the established Visual Studio/MSYS application and test targets remain unchanged and green.

### Phase 2: Portable Decode Output

1. Replace Win32-backed pixel storage.
2. Move DIB creation fully into the Win32 presentation layer.
3. Complete portable geometry, alpha, rotate/flip, and color helpers.
4. Refactor JPEG and HEIF to publish `ImageFrame`.
5. Implement native PNG.
6. Run the same fixture tests on all platforms.

Exit condition: JPEG, PNG, and HEIF decode headlessly into identical documented frame contracts.

Windows gate: current image quality, format fallback behavior, decode limits, color handling, and presentation remain
stable; portable frames are converted to Win32 presentation objects outside the core.

### Phase 3: Paths and Platform Services

1. Add `PlatformPath` and result types.
2. Define filesystem, mappings, directories, resources, settings, file operations, clocks, executor, and UI dispatch.
3. Wrap existing Windows behavior first to preserve regression coverage.
4. Add fake implementations and shared contract tests.
5. Complete the Windows XP gate for each runtime change.

Exit condition: platform services are replaceable without conditionals in the core.

Windows gate: the Win32 adapters preserve native paths, dialogs, settings/resources, Recycle Bin operations, error
codes, and XP-compatible runtime behavior.

### Phase 4: Portable Browser, Loader, and Cache

1. Port `FolderScanner`, `BrowseSession`, and `PreloadScheduler` contracts.
2. Port loader synchronization and completion publication.
3. Remove native window/handle dependencies from cache and items.
4. Preserve cancellation, generation, priority, and shutdown tests.

Exit condition: headless browsing, navigation, preload, decode, and cache behavior runs against fake services.

Windows gate: existing cancellation, preload priority, multi-monitor cache reuse, late-notification rejection, and
bounded shutdown tests continue to pass.

### Phase 5: Application Controller

1. Define complete commands and view state.
2. Move workflow policy out of `ImgVwWindow`.
3. Adapt Win32 messages to the controller.
4. Add command/state, slideshow, deletion, settings, and shutdown tests.

Exit condition: the Win32 frontend is a native adapter and composition root; macOS and Linux can use the same
controller without exceptions.

Windows gate: every existing command and primary workflow passes controller transition tests plus manual Win32
regression coverage before the legacy orchestration path is removed.

## Verification

Shared automated verification:

- Windows MSVC and MSYS core tests on x86/x64;
- macOS arm64 Clang core and decoder tests;
- Linux GCC and Clang core and decoder tests;
- malformed-input and oversized-dimension fixtures;
- sanitizer runs on macOS and Linux;
- include-boundary scan;
- deterministic cancellation and stale-result tests; and
- `git diff --check`, formatting, and focused static analysis.

Windows release gates remain:

- Visual Studio and MSYS Release x86/x64;
- Windows XP SP3 x86 runtime and import inspection;
- current Windows smoke tests; and
- no Apple or GTK runtime dependency.

`windows_native_frontend_migration_plan.md` is the detailed source of truth for Win32 adapter implementation, staged
controller cutover, XP validation, and the complete Windows regression matrix.

Manual Windows regression for affected phases must cover:

- empty, image, folder, missing-path, and invalid-path launch;
- file/folder dialogs, drag and drop, navigation, recursive browse, reload, and delete;
- JPEG, PNG/GDI+, HEIF, rotated, CMYK, ICC, malformed, oversized, and slow-loading images;
- loading/error/ready presentation, path and diagnostics overlays;
- resize, minimize/restore, DPI, theme, drag, and monitor transitions;
- sequential/random single- and multi-monitor slideshows; and
- navigation and shutdown during active enumeration or decoding.

## Risks and Mitigations

- **Simultaneous rewrite:** migrate one boundary at a time and keep Windows green.
- **Prototype contracts become permanent too early:** treat initial types as narrow foundations and stabilize them
  through real decoder/controller use.
- **XP runtime regression:** keep platform executors and inspect release imports.
- **Path corruption:** preserve native identity separately from display strings.
- **UI lifetime bugs:** publish immutable frames and state only through UI dispatchers.
- **Memory growth:** enforce per-image and cache byte limits before removing spill files.
- **Platform leakage:** enforce target linkage and include scans in CI.
- **Behavior divergence:** share controller transitions, fixtures, and platform contract suites.

## Acceptance Criteria

The foundational migration is complete when:

- `imgvw_core` builds and passes the same tests on Windows, macOS, and Linux;
- public core headers contain no native UI, filesystem-record, or handle types;
- JPEG, PNG, and HEIF share decode, transform, color, and limit behavior;
- browser, loader, cache, navigation, slideshow, settings policy, and commands are shared;
- native services pass common contract tests;
- Win32 uses the shared controller without losing existing behavior;
- established Visual Studio and MSYS Windows build paths remain supported and green;
- Windows shell integration, GDI presentation, fallback formats, DPI/multi-monitor behavior, and Recycle Bin semantics
  remain intact;
- immutable frames are presented without paint-time decoding or worker waits;
- platform dependencies are confined to their targets; and
- the Windows XP gate remains satisfied or a compatibility change is explicitly approved.
