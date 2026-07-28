# Native Windows Frontend Migration Plan

Date: 2026-07-27

## Status

**Pending; reference implementation is stable.** The existing Win32 application is the shipping product and behavioral
reference. Recent refactors have already separated presentation, empty-state UI, overlays, cursor/drag behavior,
monitor placement, slideshow state, browser collection, folder scanning, preload scheduling, format resolution, and
ownership helpers into focused components.

The initial `imgvw_core` target exists on `codex/macos-portable-core`, but the Windows application does not use its
`AppCommand`, `AppError`, `ImageFrame`, or geometry contracts. Windows browsing, loading, pixel storage, image items,
settings, resources, notifications, and top-level orchestration still expose Win32 types or behavior.

This plan migrates the existing frontend onto `portable_core_foundation_plan.md`. It complements:

- `macos_native_frontend_plan.md`
- `linux_native_frontend_plan.md`

## Goal

Make the current Win32 application a native adapter and composition root for the shared portable core while preserving:

- all existing user-visible Windows behavior;
- the documented Windows XP SP3 x86 target;
- current Win32/x64 builds;
- Visual Studio and MSYS build paths;
- native resources, menus, accelerators, dialogs, icons, shell integration, Recycle Bin, and cloud-file behavior;
- GDI-based presentation and current DPI/multi-monitor behavior;
- existing format support, including temporary GDI+ fallbacks;
- cancellation, preload priority, stale-result rejection, and bounded shutdown; and
- release packaging and LGPL relinking instructions.

This is an incremental migration of a working frontend, not a rewrite.

## Non-Goals

This plan does not:

- replace Win32 widgets with AppKit, GTK, WinUI, WPF, Qt, or another toolkit;
- change the Windows XP compatibility promise;
- make CMake the shipping Windows build before parity with Visual Studio/MSYS is proven;
- remove GDI+ fallback formats before portable decoders replace them;
- redesign established shortcuts or workflows as part of architecture work;
- require the Win32 frontend to share widget or presentation code with macOS/Linux;
- introduce GPU rendering without a separate measured plan; or
- combine broad renaming or formatting with ownership-sensitive migration.

## Relationship to the Foundational Plan

`portable_core_foundation_plan.md` owns:

- shared data and result contracts;
- `PlatformPath`;
- `ImageFrame`;
- service interfaces;
- portable decoders and transforms;
- browser, loader, and cache policy;
- `AppController`, `AppCommand`, and `AppViewState`; and
- shared tests and platform contract suites.

This plan owns:

- Win32 implementations of those interfaces;
- adaptation of current Windows subsystems;
- Win32 presentation of portable frames and view state;
- staged replacement inside `ImgVwWindow`;
- Windows build/project integration;
- XP compatibility gates; and
- Windows regression and release verification.

If the plans conflict, preserve current Windows lifetime, shutdown, notification-generation, cloud-consent, and
presentation behavior until the shared replacement proves equivalent.

## Current Win32 Baseline

### Frontend and Presentation

The current frontend already has useful boundaries:

- `ImgVwWindow` is the top-level composition root and message adapter.
- `DisplayPresenter` consumes immutable display input and selects ready/loading/error paint behavior.
- `ImgRenderer` owns image rendering operations.
- `EmptyStateView` owns the launch/searching UI.
- `InfoOverlay` owns path, information, diagnostics, font, layout, and composited drawing.
- `CursorController`, `WindowDragController`, and `MonitorPlacement` isolate interaction policy.
- `SlideShowStateMachine` and `MultiMonitorSlideShowCoordinator` isolate slideshow behavior.
- `BrowseWindowState` and `DisplaySession` isolate browse and presentation session state.

Keep these native components where their behavior is frontend-specific. Move only shared application policy into the
controller.

### Browser and Loader

- `ImgBrowser` is a lifetime-safe facade.
- `FolderScanner` owns enumeration and format-resolution input.
- `BrowseSession` owns collection generation, cancellation, and readiness.
- `PreloadScheduler` owns target-size work and worker lifetime.
- `ImgLoader` owns prioritized load queues, worker state, completion, and shutdown.

Preserve all generation, cancellation, priority, notification-target, and bounded-stop contracts during adaptation.

### Image and Platform

- image items still expose Win32 scalar, bitmap, mapping, heap, resource, path, and synchronization details;
- `ImgBuffer` uses Win32 temporary files and mappings;
- `ImgBitmap` creates `HBITMAP` in the image layer;
- GDI+ remains required for fallback formats and some shared transforms;
- settings/resources and default ICC handling remain Win32-oriented; and
- shell dialogs, file operations, header probes, and platform RAII live under `src/platform/win32/`.

## Windows Behavior Invariants

Every phase must preserve:

- empty launch and open-image/open-folder actions;
- direct file, folder, invalid, and missing-path startup;
- keyboard, mouse wheel, context menu, drag and drop, and close behavior;
- sequential/random navigation and progress;
- recursive browsing;
- reload;
- visible-item priority and adjacent/multi-monitor preloading;
- path and diagnostics/image-information overlays;
- sequential/random single- and multi-monitor slideshows;
- resize, minimize/restore, DPI, theme, drag, snapping, and monitor transitions;
- JPEG, HEIF, GDI+-backed PNG/BMP/GIF/TIFF/ICO, orientation, CMYK, and ICC behavior;
- decode limits and malformed-input handling;
- Recycle Bin and permanent-delete distinction;
- online-only image consent before probing, loading, or preloading;
- settings and bundled ICC fallback behavior; and
- clean shutdown while collection or decoding is active.

Changes to these behaviors require separate user-facing scope and tests.

## Target Windows Structure

```text
imgvw_windows
  ├─ imgvw_ui_win32
  │    ├─ WinMain / Window / ImgVwWindow
  │    ├─ menus, accelerators, dialogs, drag/drop
  │    ├─ empty/loading/error/image views
  │    ├─ overlays, cursor, DPI, monitor, slideshow presentation
  │    └─ ImageFrame -> DIB/HBITMAP presentation cache
  ├─ imgvw_platform_win32
  │    ├─ paths and filesystem
  │    ├─ mappings and file metadata
  │    ├─ settings and resources
  │    ├─ Recycle Bin/delete and cloud-file state
  │    ├─ UI dispatch, clock, timers, executor
  │    └─ XP-compatible native helpers
  └─ imgvw_core
       ├─ controller and view state
       ├─ browser / loader / cache
       └─ portable image pipeline
```

Native implementation details may remain in established Visual Studio/MSYS targets while CMake target names are
introduced incrementally.

## Win32 Platform Adapters

### Paths and Filesystem

Implement the shared contracts using lossless UTF-16:

- `PlatformPath` native storage/conversion;
- file/folder inspection;
- directory enumeration;
- current and Pictures directories;
- read-only mapping or bounded reads;
- file size/modification metadata;
- stable native errors captured immediately; and
- XP-compatible APIs or guarded modern alternatives.

Do not convert Windows paths to UTF-8 as a migration prerequisite. Do not depend on `std::filesystem` until its runtime
and imports pass the XP gate.

Adapt `FolderScanner` to consume portable directory entries while retaining current recursion, cancellation,
content-probe, and enumeration-result behavior.

### Settings and Application Directories

Adapt current registry/application-data behavior behind:

- `ISettingsStore`;
- `IAppDirectories`; and
- `IResourceProvider`.

Preserve:

- current setting defaults and recovery;
- default CMYK ICC selection;
- bundled fallback profile loading;
- resource IDs and compiled-resource ownership;
- existing user data locations; and
- explicit errors for initialization or persistence failures.

Do not change settings locations or formats merely for cross-platform symmetry.

### File Operations and Cloud Files

Implement `IFileOperations` with:

- existing shell/Recycling behavior;
- permanent deletion as a separate operation;
- explicit cancellation and failure results;
- immediate native error capture; and
- safe selection/cache updates after deletion.

Integrate `cloud_file_download_consent_plan.md` before or alongside path/filesystem migration. Placeholder detection
must remain metadata-only, use dynamically discovered modern APIs where needed, and preserve XP startup/import
behavior. Consent must gate:

- header/content probes;
- direct visible loads;
- reload;
- adjacent preloads;
- target-size preloads; and
- single/multi-monitor slideshow preloads.

### UI Dispatch

Replace public `SetNotificationWindow(HWND, UINT)` and direct core `PostMessage` dependencies with `IUiDispatcher`.

The Win32 adapter should:

- retain one private message for queue notification;
- own a protected callback/state queue;
- drain it only on the UI thread;
- reject publication after target teardown;
- preserve generation checks; and
- avoid calling application/frontend code while lower-level locks are held.

### Clock, Timers, and Executor

- expose a monotonic clock to shared policy;
- keep `SetTimer`/`KillTimer` and timer IDs inside the frontend;
- translate timer expiry into `AppCommand`;
- use `std::chrono` in the core;
- implement `ITaskExecutor` with current Win32 primitives if standard C++ threading fails the XP gate; and
- preserve bounded shutdown without blocking paint/message paths.

### File Monitoring

Keep current folder/file monitoring behavior behind an optional shared interface only after basic paths and enumeration
are stable. Preserve cache reuse and reload behavior across same-folder and monitor transitions.

## Portable Frame to Win32 Presentation

Move DIB/HBITMAP creation out of the image layer:

1. Define canonical `ImageFrame` format, row order, alpha semantics, and color state.
2. Make portable decoders publish immutable frames.
3. Add a Win32 presentation object that owns DIB/HBITMAP/GDI resources through RAII.
4. Convert or wrap a frame once per frame identity/presentation parameters.
5. Cache presentation objects while displayed.
6. Paint without decoding, mapping, conversion, or worker waits.
7. Preserve current aspect-fit/no-upscale behavior.
8. Preserve DPI and monitor-size changes without unnecessary re-decode where source frames are retained.

`HWND`, `HDC`, `HBITMAP`, `BITMAPINFO`, GDI handles, and DIB layout must remain under Win32 frontend/platform targets.

### GDI+ Transition

Keep GDI+ initialization and Windows-only image items while fallback formats require them. Remove shared GDI+ use in
this order:

1. portable JPEG orientation/transform/output;
2. native PNG;
3. portable HEIF output;
4. shared color/alpha/geometry helpers; and
5. remaining formats only when dedicated portable support exists.

Do not reduce Windows format support during the transition. Remove GDI+ startup only after the final registered Windows
decoder no longer uses it.

## AppController Integration

### Command Adaptation

Map current Win32 messages, accelerator commands, menu actions, mouse/drag events, dialog results, and timers into
`AppCommand`. Preserve command enablement and context-menu behavior.

### View-State Adaptation

Subscribe to `AppViewState` on the UI thread and adapt it into:

- `DisplayPresenter` input;
- empty/searching/loading/error/image state;
- overlays and diagnostics;
- cursor and invalidation actions;
- menu validation;
- timer registration; and
- presentation-object selection.

Do not duplicate controller state in `ImgVwWindow` beyond native transient UI state. Remove old fields only after the
new state path is authoritative and regression-tested.

### Staged Cutover

Use a feature-by-feature cutover:

1. read-only state publication and presentation;
2. open/reload;
3. sequential navigation;
4. random navigation and progress;
5. recursive browsing;
6. single-window slideshow;
7. settings and ICC selection;
8. delete/Recycle Bin;
9. multi-monitor slideshow; and
10. shutdown/lifecycle ownership.

During each step, keep one authoritative path. Avoid indefinitely maintaining two mutable implementations.

## Build-System Integration

### Existing Build Paths

Keep synchronized:

- `ImgVw.slnx`;
- `ImgVw.vcxproj`;
- `ImgVw.vcxproj.filters`;
- `ImgVw.Tests.vcxproj`;
- root Makefile;
- `tests/Makefile`; and
- repository build/test scripts.

When adding or moving a shared file, update every build that compiles it in the same change.

### CMake

Grow:

```text
imgvw_core
imgvw_core_tests
imgvw_platform_win32
imgvw_win32_platform_tests
imgvw_ui_win32
imgvw_windows
```

CMake is initially an additional validation graph. It becomes eligible as a Windows release path only after:

- resources, subsystem, manifests, icons, accelerators, and dialogs match;
- static dependency architecture/configuration matches;
- Debug/Release and Win32/x64 flags match;
- runtime library and static-link behavior match;
- XP imports and runtime behavior match;
- output naming/packaging matches; and
- the established builds remain available through a deliberate transition.

## Implementation Phases

### Phase 0: Baseline and Characterization

1. Record current build/test results.
2. Retain focused tests for command mapping, presentation, browser/loader lifetime, settings, file operations, and
   shutdown.
3. Record manual Windows regression results.
4. Capture release imports and supported-format behavior.

Exit condition: current behavior has an evidence-backed baseline before shared migration.

### Phase 1: Build and Type Foundation

1. Add initial portable types and tests to appropriate Windows build graphs without changing runtime behavior.
2. Add Windows platform/frontend CMake target skeletons.
3. Enforce native-header boundaries.
4. Verify no Apple/GTK dependencies enter Windows.

Exit condition: all established Windows builds remain green and the small core target coexists with them.

### Phase 2: Windows Service Adapters

1. Add path/filesystem/mapping adapters.
2. Add directories/settings/resource adapters.
3. Add file-operation/cloud-consent adapters.
4. Add UI dispatch, clock, timer, and executor adapters.
5. Run shared platform contract tests plus XP gates.

Exit condition: existing Windows behavior is reachable through shared service interfaces.

### Phase 3: Portable Image Output and Win32 Presentation

1. Replace Win32-backed core pixel storage.
2. Port JPEG, PNG, and HEIF output.
3. Add immutable-frame-to-DIB presentation caching.
4. Keep fallback formats registered.
5. Compare image, color, alpha, orientation, scaling, and limit fixtures.
6. Verify DPI/monitor transitions and paint lifetime.

Exit condition: portable frames drive Win32 presentation without losing format support or image quality.

### Phase 4: Browser, Loader, and Cache Cutover

1. Adapt folder scanning and paths.
2. Adapt loader executor, dispatch, and item identity.
3. Adapt cache keys, byte budgets, and frame lifetimes.
4. Preserve preload priority, generations, cancellation, and shutdown.
5. Verify cloud consent on every probe/load/preload route.

Exit condition: the Windows app uses portable browse/load/cache policy with native adapters.

### Phase 5: AppController Cutover

1. Connect commands and view state.
2. Move open, navigation, recursion, reload, slideshow, settings, color, and deletion policy.
3. Cut over single-window workflows first.
4. Cut over multi-monitor coordination.
5. Remove superseded orchestration only after parity tests.

Exit condition: `ImgVwWindow` owns native adaptation/presentation rather than shared workflow policy.

### Phase 6: Cleanup and Release Validation

1. Remove obsolete Win32 dependencies from core targets.
2. Remove GDI+ shared paths only where portable replacements exist.
3. Reconcile all build graphs and documentation.
4. Run full automated and manual matrices.
5. Inspect release imports and licenses.
6. Run Windows XP and current-Windows release smoke tests.

Exit condition: the migrated Windows application is release-ready with no behavior or compatibility regression.

## Verification

### Routine Shared Change

- focused affected test shard on x86 and x64;
- incremental MSYS application build for one architecture where proportionate;
- relevant Visual Studio build where project/source lists changed;
- formatting and focused analysis; and
- manual smoke test for affected UI behavior.

### Phase and Release Gate

Automated:

- full MSYS test suite on x86 and x64;
- MSYS Release application build on x86 and x64;
- Visual Studio Release build on Win32 and x64;
- focused Core, Platform, Image, Concurrency, and UI suites;
- shared core/platform contract tests;
- malformed and oversized decode fixtures;
- format/color/orientation/scaling comparison;
- formatting and static analysis;
- executable import inspection;
- no Apple, GTK, or GLib linkage; and
- Windows XP runtime smoke test for XP-sensitive phases.

Manual:

- empty, file, folder, missing, and invalid launch;
- native dialogs, drag/drop, context menu, accelerators, and mouse wheel;
- navigation, recursion, reload, deletion, and cloud consent;
- JPEG, HEIF, PNG/GDI+, BMP, GIF, TIFF, ICO, rotated, CMYK, ICC, malformed, large, and slow images;
- empty/loading/error/ready presentation and overlays;
- resize, minimize/restore, DPI, theme, drag, snap, and monitor transition;
- sequential/random single- and multi-monitor slideshows; and
- navigation and shutdown during collection/decode.

## Risks and Mitigations

- **Working app destabilized by architecture work:** use adjacent contracts and one subsystem cutover at a time.
- **XP imports introduced indirectly:** inspect final executables and retain Win32 implementations behind interfaces.
- **Two authoritative state paths:** cut over one workflow at a time and remove superseded state after parity.
- **GDI+ format regression:** retain Windows fallbacks until portable decoder registration is complete.
- **Paint lifetime regression:** cache RAII presentation objects backed by immutable frame ownership.
- **Cloud recall regression:** test every probe, visible load, reload, and preload route.
- **Build graphs diverge:** update Visual Studio, filters, Makefiles, tests, and CMake together.
- **Platform leakage:** enforce include and linkage checks.
- **Multi-monitor behavior regresses late:** preserve current coordinator tests and defer its cutover until
  single-window controller behavior is stable.

## Acceptance Criteria

The Windows migration is complete when:

- the existing Windows application uses the shared core/controller and Win32 platform adapters;
- `ImgVwWindow` is a native adapter/composition root without duplicated shared workflow policy;
- portable JPEG, PNG, and HEIF frames are presented through Win32-owned DIB/GDI objects;
- existing GDI+ fallback formats remain supported until portable replacements are intentionally delivered;
- commands, navigation, browsing, caching, preloading, slideshows, settings, color, deletion, overlays, DPI, and
  multi-monitor behavior remain stable;
- cloud-file consent gates every content-triggering path;
- cancellation, stale results, and shutdown remain safe;
- Visual Studio and MSYS Debug/Release Win32/x64 paths remain supported;
- Windows XP SP3 x86 imports and runtime behavior pass;
- no Apple or GTK/GLib dependency enters Windows targets;
- Windows packaging, licenses, and LGPL relinking documentation remain correct; and
- the complete automated and manual Windows release matrix passes.
