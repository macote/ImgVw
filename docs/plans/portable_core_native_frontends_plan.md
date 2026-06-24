# Portable Core with Native Windows, Linux, and Future macOS Frontends

Date: 2026-06-21

## Goal

Evolve ImgVw into a cross-platform application with:

- one portable C++ image-viewer core;
- the existing native Win32 frontend for Windows, including the documented Windows XP target;
- a new native GTK4 frontend for Linux;
- an explicitly reserved native AppKit frontend and platform adapter boundary for a later macOS version;
- shared decoding, color management, resizing, browsing, caching, navigation, slideshow policy, and application
  commands;
- platform-specific implementations only for windows, rendering presentation, filesystem integration, dialogs,
  settings locations, trash/recycle behavior, and event-loop notification.

The migration must be incremental. The current Windows executable should remain buildable and behaviorally stable
throughout the work. Do not begin by rewriting `ImgVwWindow` in GTK or by introducing GTK into the Windows build.

## Recommended Shape

Use a ports-and-adapters structure:

```text
Win32 frontend ──┐
GTK4 frontend ───┼─ Application controller ─ Browser / loader / cache ─ Image decoders
AppKit frontend ─┘              │
                                └─ Platform service interfaces
                                       ├─ Win32 implementations
                                       ├─ Linux implementations
                                       └─ macOS implementations
```

The core owns application state and policy. Frontends translate native events into core commands and present immutable
view state. Platform adapters perform operating-system operations requested by the core.

GTK4 is the recommended Linux frontend because it provides native Linux desktop integration, accessibility, input,
clipboard, dialogs, X11/Wayland support, and a maintained rendering model. GTK must remain a frontend dependency, not
an image-decoding dependency.

AppKit is reserved for a future native macOS frontend. The first implementation iteration remains Windows and Linux
only, but core interfaces must not assume that every non-Windows system is Linux or uses GTK/GLib.

## Non-Goals

The first Linux version does not need:

- identical pixel-for-pixel window chrome on Windows and Linux;
- shared widget code;
- a working macOS executable in the first Windows/Linux iteration;
- APNG or animated-image playback;
- GPU image processing;
- a plugin ABI;
- a public SDK;
- replacing the existing Win32 frontend;
- changing the Windows XP compatibility promise;
- porting GDI+-only fallback formats before native decoders exist.

Feature parity should focus on image display and navigation first. Desktop-specific conveniences can follow.

## Current Portability Barriers

The current module folders imply separation, but Win32 types and behavior still cross the boundaries:

- `ImgBrowser` performs Win32 enumeration, owns Win32 threads/events/critical sections, and posts window messages.
- `ImgLoader` owns Win32 threads, events, semaphores, critical sections, and an `HWND`.
- `ImgItem` exposes `HANDLE`, `BOOL`, `INT`, `BYTE`, `BITMAPINFO`, heap handles, resource loading, shell paths, and
  Win32 synchronization.
- `ImgBuffer` always writes decoded pixels to a Win32 temporary file and returns a Win32 mapping.
- `ImgBitmap` creates an `HBITMAP` inside the image layer.
- `ImgItemHelper` mixes format detection, EXIF policy, GDI+ rotation, resizing, and Win32 synchronization.
- `ImgJPEGItem` depends on GDI+ rotation/resizing despite using a portable JPEG decoder.
- `ImgGDIItem` is inherently Windows-only and currently handles PNG and miscellaneous formats.
- ICC profile storage loads a Windows resource and searches `%APPDATA%` from `ImgItem`.
- paths are `std::wstring` values with Windows separators and shell functions.
- errors are a mixture of strings, Win32 codes, GDI+ status, flags, exceptions, and `goto` cleanup.
- application commands, slideshow state, browser coordination, dialogs, and rendering are combined in
  `ImgVwWindow`.

The migration should remove these dependencies from the inside out.

## Target Repository Layout

Move toward:

```text
src/
  app/
    AppController.*
    AppCommand.*
    AppViewState.*
    SlideshowController.*
  browse/
    ImageBrowser.*
    ImageFileList.*
    ImageCache.*
  image/
    ImageItem.*
    ImageBuffer.*
    ImageFrame.*
    ImageLoader.*
    ImageFormatDetector.*
    ImageItemFactory.*
    ImageTransform.*
    ColorTransform.*
    decoders/
      JpegDecoder.*
      JpegImageItem.*
      PngDecoder.*
      PngImageItem.*
      HeifDecoder.*
      HeifImageItem.*
  platform/
    PlatformPath.*
    FileSystem.h
    FileMapping.h
    FileOperations.h
    SettingsStore.h
    UiDispatcher.h
    win32/
      ...
    linux/
      ...
    macos/
      README.md
  ui/
    win32/
      ...
    gtk/
      ...
    macos/
      README.md
  diagnostics/
    ...
tests/
  core/
  platform/
```

Do not perform a broad folder move before the interfaces exist. Move files only as their ownership becomes clear, and
keep Visual Studio, filters, Makefile, and CMake targets synchronized.

During the first Windows/Linux iteration, add only:

- `src/platform/macos/README.md`
- `src/ui/macos/README.md`

These placeholders reserve ownership and record the macOS contracts described in the macOS sub-plan below. They must
not be added to shipping targets, and they must not introduce Objective-C++, AppKit, Foundation, or Apple framework
dependencies into the Windows/Linux build.

## Core Data Model

### Paths

Introduce a small `PlatformPath` value type rather than exposing `std::wstring`, `std::filesystem::path`, or native
handles through core APIs.

Required operations:

- equality and stable ordering;
- filename and extension access;
- parent and child composition;
- display text;
- conversion only inside platform adapters.

Recommended storage:

- Windows: lossless UTF-16 native path;
- Linux: native byte path plus best-effort UTF-8 display text.

Do not force Linux filenames to be valid UTF-8, and do not convert the existing Windows path pipeline to narrow strings
as a prerequisite. Avoid depending on `std::filesystem` in the XP build until the exact runtime/import behavior has
passed the compatibility gate.

### Image Frame

Replace DIB-specific decoder output with a portable `ImageFrame`:

```cpp
enum class PixelFormat
{
    Bgr8,
    Bgra8,
    Rgba8
};

enum class RowOrder
{
    TopDown,
    BottomUp
};

struct ImageFrame
{
    int width;
    int height;
    std::size_t stride;
    PixelFormat format;
    RowOrder row_order;
    std::shared_ptr<const PixelStorage> pixels;
};
```

The preferred canonical core format is top-down premultiplied BGRA8 or RGBA8. Select one after measuring conversion
costs:

- BGRA8 maps naturally to Windows DIBs and Cairo/GTK memory surfaces.
- RGBA8 maps naturally to libpng/libheif and the existing resampler.

Whichever format is selected, document alpha semantics and color space. Do not retain an implicit “BGR24 means sRGB”
contract.

The core frame should be immutable after publication to the UI. A decoder may use mutable working buffers internally,
then publish a completed frame.

### Pixel Storage

Remove Win32 temporary-file ownership from `ImgBuffer`.

Initial implementation:

- use checked `std::vector<std::uint8_t>` memory storage;
- retain existing per-image memory and dimension limits;
- let cache eviction control total memory;
- make the storage interface capable of supporting a mapped spill file later, without requiring it for the first Linux
  version.

If spill storage remains necessary, define `PixelStorage` independently from mapping handles and provide Win32 and
POSIX implementations. The UI must receive a byte span and lifetime owner, never a filename or native mapping handle.

### Results and Errors

Introduce explicit boundary results:

```cpp
enum class ErrorDomain
{
    None,
    Application,
    FileSystem,
    Decoder,
    ColorManagement,
    Platform
};

struct AppError
{
    ErrorDomain domain;
    int code;
    std::string message;
};
```

Use `Result<T, AppError>` or a project-local equivalent at filesystem, decoder, settings, and delete/trash boundaries.
Platform adapters should copy native error codes immediately and translate them into stable core errors.

Keep diagnostic details available without making user-facing messages depend on Win32 error formatting.

## Core Interfaces

Keep interfaces narrow and driven by concrete use cases.

### File System

```cpp
class IFileSystem
{
  public:
    virtual Result<FileInfo> Inspect(const PlatformPath& path) = 0;
    virtual Result<std::vector<DirectoryEntry>> Enumerate(const PlatformPath& folder) = 0;
    virtual Result<MappedReadOnlyFile> MapReadOnly(const PlatformPath& file) = 0;
    virtual PlatformPath CurrentDirectory() = 0;
    virtual PlatformPath DefaultPicturesDirectory() = 0;
};
```

Enumeration should return portable metadata and native paths. The browser owns recursion, supported-format filtering,
ordering, and cancellation policy. The platform adapter owns only directory access.

Do not expose Win32 `FindFirstFile` records or POSIX `dirent` values.

### File Operations

```cpp
class IFileOperations
{
  public:
    virtual FileOperationResult Delete(const PlatformPath& file, FileDeleteMode mode) = 0;
};
```

Windows uses the existing shell/recycle-bin behavior. Linux should use the desktop trash specification through a
well-maintained API where available, with permanent deletion as a distinct command. Never emulate trash by moving files
to an arbitrary application folder.

The frontend supplies confirmation UI when required; the core requests the operation.

### Settings and Application Data

Move ICC profile selection and application paths out of `ImgItem`.

Define:

- `ISettingsStore` for user preferences;
- `IAppDirectories` for configuration, cache, data, and temporary locations;
- `IResourceProvider` for built-in ICC data and other bundled resources.

Windows implementations use the existing application-data location and compiled resources. Linux implementations use
XDG configuration/cache/data directories and installed resources.

The bundled CMYK fallback profile should become a platform-neutral byte asset consumed through `IResourceProvider`.

### UI Dispatch

Replace `SetNotificationWindow(HWND, UINT)` and direct `PostMessage` calls with:

```cpp
class IUiDispatcher
{
  public:
    virtual void Post(std::function<void()> task) = 0;
};
```

The Win32 adapter posts one private window message and drains a protected callback queue on the UI thread. The GTK
adapter schedules work on the GLib main context.

Core background workers may publish state only through this dispatcher. They must not call frontend widgets directly.

### Clock and Timers

Move slideshow and mouse-idle policy into testable controllers using an injected monotonic clock. Frontends own native
timer registration and translate timer expiration into application commands.

The core should operate on `std::chrono` durations rather than `UINT` milliseconds or performance-counter structures.

## Application Controller

Extract an `AppController` from `ImgVwWindow`.

Responsibilities:

- accept `AppCommand` values;
- coordinate browser, loader, cache, settings, and file operations;
- own slideshow state and timing policy;
- expose an immutable `AppViewState`;
- notify the frontend when view state changes;
- never include Win32, GTK/GLib, or Apple framework headers.

Suggested commands:

```text
OpenPath
Next
Previous
First
Last
Random
Reload
BrowseSubfolders
ToggleSlideshow
ToggleRandomSlideshow
IncreaseSlideshowSpeed
DecreaseSlideshowSpeed
DeleteToTrash
DeletePermanently
SelectDefaultCmykProfile
UseBundledCmykProfile
ShowCurrentPath
Quit
```

Suggested view state:

- current path and display text;
- item state: empty, queued, loading, ready, error;
- immutable current `ImageFrame`;
- image dimensions and centered destination rectangle;
- error text;
- slideshow state and interval;
- whether previous/next navigation is available.

The frontend handles menus, key bindings, dialogs, cursor visibility, native text drawing, and window invalidation.

## Browser, Cache, and Loader

### Browser

Refactor `ImgBrowser` into a platform-independent `ImageBrowser`.

- Inject `IFileSystem`, `ImageItemFactory`, `ImageLoader`, and `IUiDispatcher`.
- Move Win32 directory enumeration into `Win32FileSystem`.
- Keep `ImageFileList`, recursive traversal policy, content detection, navigation, and cache coordination in the core.
- Replace shared `BOOL` cancellation flags with an explicit cancellation token or atomic flag.
- Never invoke UI callbacks while holding the browser mutex.

Folder enumeration should remain asynchronous and cancellable. Define ordering semantics explicitly because Windows and
Linux filesystem enumeration order differs.

Use a documented case policy:

- preserve each platform's native path identity;
- do not apply Windows case-insensitive comparisons globally on Linux;
- sort display order with a stable project policy separate from path identity.

### Loader

Replace the Win32 thread/event implementation with standard C++ synchronization:

- `std::thread`;
- `std::mutex`;
- `std::condition_variable`;
- `std::atomic<bool>` or a cancellation token;
- a bounded worker pool rather than one new OS thread per image.

This change requires an XP gate. Standard-library synchronization may import APIs not available on XP with modern
toolchains. If the selected Windows toolchain fails the gate, retain a `Win32TaskExecutor` implementation behind an
`ITaskExecutor` interface and use a standard/POSIX implementation on Linux.

Do not make Windows XP compatibility depend on assumptions about `std::thread`; verify the final executable imports and
runtime behavior.

The loader should publish completion by item ID or stable shared ownership. It should not expose worker handles or
per-item completion events.

### Cache

Make cache policy independent from platform and target window handles.

- Key by `PlatformPath` plus decode/display parameters.
- Track approximate pixel bytes.
- Apply an explicit item or byte budget.
- Prefer retaining decoded source frames if window resizing will be supported without re-decoding; otherwise document
  that target-size changes invalidate entries.
- Ensure eviction never invalidates a frame still presented by a frontend.

## Portable Image Pipeline

The Linux port depends on removing GDI+ from all shared decode paths.

### JPEG

Refactor `ImgJPEGItem` to:

- use `ImgJpegDecoder`;
- represent EXIF orientation with a portable `ImageOrientation` enum;
- use first-party rotation/flip code;
- use `ImgResampler` for final scaling;
- use portable Little CMS helpers;
- produce the canonical `ImageFrame`;
- remove GDI+ types from headers and implementation.

### PNG

Implement `artifacts/libpng_support_plan.md`. Native libpng support is a prerequisite for Linux parity because PNG
currently routes through GDI+.

### HEIF

Keep libheif/libde265 decode code in the portable image layer. Replace:

- `FileMapView` with `IFileSystem::MapReadOnly`;
- Win32 event signaling with loader completion;
- `MultiByteToWideChar` error conversion with portable UTF-8 error storage;
- BGR24 DIB output with the canonical frame.

### Remaining Formats

`ImgGDIItem` may remain Windows-only for BMP, GIF, TIFF, and ICO during early phases. The factory should represent
decoder availability explicitly:

- Windows: portable JPEG/PNG/HEIF plus GDI+ fallback formats.
- Linux initially: portable JPEG/PNG/HEIF only.

Do not pretend a format is supported on Linux when no decoder is registered. Later options include dedicated libraries
or a controlled cross-platform fallback, selected format by format.

### Color Management and Transforms

Extract platform-independent helpers:

- `ImageOrientation`;
- checked geometry and stride calculations;
- rotate/flip;
- alpha premultiplication/composition;
- RGBA/BGRA/BGR conversion;
- Little CMS profile validation and transforms;
- display-size calculation;
- resampling.

These helpers must not include Windows headers or allocate from the process heap.

## Frontend Contracts

### Win32 Frontend

Retain:

- `WinMain`, resources, accelerator table, menus, dialogs, icons, and message loop;
- current keyboard/mouse behavior;
- native file dialog and recycle-bin integration;
- XP-compatible rendering.

Change `ImgVwWindow` into an adapter:

1. Translate messages and commands into `AppCommand`.
2. Subscribe to `AppViewState` updates.
3. Convert the current portable frame into a Win32 presentation object.
4. Paint loading, image, error, and path states.
5. Own all `HWND`, `HDC`, `HBITMAP`, `BITMAPINFO`, GDI object, cursor, and timer details.

Create DIB sections only in `ui/win32` or a Win32 rendering adapter. Cache a presentation bitmap by frame identity so
paint does not recreate it unnecessarily.

GDI+ startup should remain only while Windows-only fallback decoders require it. Remove GDI+ initialization when the
last fallback is gone.

### GTK4 Frontend

Add a separate Linux executable target.

Responsibilities:

- create `GtkApplication` and the main window;
- bind keyboard, mouse wheel, context menu, and window actions to `AppCommand`;
- present `AppViewState`;
- display frames through a GTK-compatible immutable texture or Cairo surface;
- use GLib timers and main-context dispatch;
- use native GTK dialogs and clipboard APIs;
- integrate trash/delete behavior through the Linux platform adapter;
- support both Wayland and X11 through GTK, without backend-specific code in the core.

Avoid copying frame pixels every paint. Convert or wrap a completed immutable frame once, retain it while GTK presents
it, and release it when view state changes.

The initial GTK window may have a minimal menu rather than duplicating the exact Win32 context menu. Keyboard behavior
and navigation semantics should match.

## Future macOS Sub-Plan

macOS is a planned third frontend, not part of the first Windows/Linux delivery. The purpose of reserving it now is to
prevent Linux-specific assumptions from becoming the portable architecture.

### First Iteration: Documentation Placeholders

Create `src/platform/macos/README.md` when the platform directories are introduced. It should document:

- the core platform interfaces a macOS adapter must implement;
- expected native path behavior and UTF-8/display-string boundaries;
- Foundation/POSIX file inspection, enumeration, and read-only mapping options;
- application support, caches, temporary, and preferences locations;
- bundled-resource lookup inside an application bundle;
- Trash and permanent-delete behavior;
- main-thread dispatch, monotonic clock, timers, and background execution;
- the rule that Apple framework types must not cross into core headers;
- minimum supported macOS and compiler versions as unresolved release decisions.

Create `src/ui/macos/README.md` when the UI directories are introduced. It should document:

- AppKit as the intended native frontend;
- Objective-C++ as the narrow bridge between AppKit and the C++ `AppController`;
- translation of `NSApplication`, `NSWindow`, menu, keyboard, mouse, and gesture events into `AppCommand`;
- presentation of immutable `ImageFrame` data through Core Graphics initially, with Metal optional after profiling;
- Retina backing-scale handling and correct point-to-pixel conversion;
- native open/profile dialogs, clipboard, menus, accessibility, and application lifecycle;
- frame ownership rules that avoid a copy on every paint;
- the rule that AppKit/Foundation types must remain under `src/ui/macos` and `src/platform/macos`;
- signing, notarization, application-bundle, and universal-binary work as later packaging tasks.

The placeholder READMEs should cross-reference this plan and list their status as “reserved, not implemented.” They
should not contain speculative source APIs that conflict with the final core contracts.

### macOS Platform Adapter

After the Windows and Linux contracts are stable:

1. Add macOS implementations of `PlatformPath`, filesystem inspection/enumeration, read-only mappings, application
   directories, settings, resources, Trash/delete, clock, executor, and UI dispatch.
2. Prefer C++ or Objective-C++ implementation files behind the same C++ interfaces used by Windows and Linux.
3. Use Grand Central Dispatch only inside the adapter; the portable core must not depend on dispatch queues or blocks.
4. Preserve native filename bytes and provide separate display text, matching the path model used by the core.
5. Load the bundled ICC fallback and other assets from the application bundle through `IResourceProvider`.
6. Add the shared platform contract suite on macOS before building the UI.

### AppKit Frontend

Implement a native AppKit executable:

1. Create the application, main window, menus, actions, and lifecycle bridge.
2. Translate native events into the existing `AppCommand` set and present `AppViewState`.
3. Convert or wrap completed core frames into a retained `CGImage` or equivalent Core Graphics presentation object.
4. Handle Retina scale correctly without re-decoding solely because backing scale changes.
5. Add navigation, recursive browsing, slideshow, reload, path display, ICC selection, and Trash/permanent-delete
   workflows.
6. Add accessibility labels and native keyboard/menu conventions.
7. Add arm64 first, then an x86_64 build if the selected minimum macOS version and dependency set still justify it.

Do not use GTK for the shipping macOS frontend merely to reuse Linux widget code. Shared behavior belongs in the core;
native frontend code is intentionally platform-specific.

### macOS Build, Test, and Packaging

When implementation starts, add:

```text
imgvw_platform_macos   macOS adapters
imgvw_ui_macos         AppKit/Objective-C++ frontend
imgvw_macos            macOS application bundle target
imgvw_macos_tests      macOS adapter/frontend tests
```

Required verification:

- Clang build of `imgvw_core` and all shared decoder tests on macOS;
- platform contract tests for paths, mappings, directories, resources, settings, and Trash;
- arm64 application build on current macOS;
- x86_64 or universal build only if it remains a supported product target;
- Retina and multiple-display testing;
- open-from-Finder and command-line path handling;
- clean shutdown with enumeration and decoding active;
- application-bundle execution without source-tree-relative assets;
- code signing and notarization for distributed builds;
- dependency-license and application-bundle audit.

### macOS Entry Criteria

Begin implementation only after:

- `imgvw_core` builds without Win32, GTK, GLib, POSIX, or Apple framework types in public headers;
- Windows and Linux pass the shared core and platform contract suites;
- `AppController`, `AppCommand`, `AppViewState`, `PlatformPath`, `ImageFrame`, and service interfaces are stable enough
  that macOS does not need frontend-specific exceptions;
- JPEG, PNG, and HEIF decode headlessly on macOS with the selected dependency versions;
- minimum macOS version, architecture support, and packaging policy are decided.

This sequencing should make macOS an additional adapter/frontend project rather than another core refactor.

## Build System

Add CMake as the cross-platform build description and eventual canonical dependency graph.

Proposed targets:

```text
imgvw_core             static library, no Win32 or GTK dependencies
imgvw_platform_win32   Win32 adapters
imgvw_ui_win32         Win32 frontend
imgvw_windows          Windows executable
imgvw_platform_linux   Linux adapters
imgvw_ui_gtk           GTK4 frontend
imgvw_linux            Linux executable
imgvw_core_tests       portable unit tests
imgvw_win32_tests      Windows adapter tests
imgvw_linux_tests      Linux adapter tests

# Reserved for the later macOS phase, not created as build targets initially:
imgvw_platform_macos
imgvw_ui_macos
imgvw_macos
imgvw_macos_tests
```

Rules:

- `imgvw_core` must compile on Windows and Linux initially and be structured to compile unchanged on macOS.
- CI or a local verification target should reject Win32, GTK/GLib, and Apple framework headers in core include
  graphs.
- Link platform libraries only into platform/frontend targets.
- Discover GTK4 through `pkg-config`.
- Support system development packages on Linux initially; reproducible Linux packaging can be added after the port
  works.
- Keep the Visual Studio project and existing MSYS Makefile operational during migration.
- Do not replace the XP-tested Windows build with CMake until equivalent flags, static libraries, resources, subsystem,
  and import behavior are verified.

The existing vendored static dependencies remain valid for Windows. On Linux, begin with pinned minimum versions and
distribution packages for developer builds, then decide whether release artifacts use vendored builds, AppImage,
Flatpak, or native packages.

## Dependency and Licensing Policy

The portable core should directly depend only on:

- the C++ standard library subset proven compatible with supported targets;
- libjpeg-turbo;
- libpng and zlib after native PNG implementation;
- Little CMS;
- libheif and libde265;
- first-party code.

GTK4 and GLib belong only to Linux frontend/platform targets. Win32, shell, GDI, and temporary GDI+ dependencies belong
only to Windows targets. AppKit, Foundation, Core Graphics, Metal, and Grand Central Dispatch belong only to future
macOS frontend/platform targets.

Review static-link obligations for libheif/libde265 separately from the frontend choice. Moving to Linux does not
remove LGPL compliance requirements.

## Testing Strategy

### Portable Core Tests

Run the same test executable on Windows and Linux:

- path-independent format detection and dispatch;
- navigation and random-order behavior;
- browser recursion using an in-memory fake filesystem;
- cancellation and stale-result suppression;
- cache insertion, eviction, and lifetime;
- loader prioritization and bounded concurrency;
- application command to view-state transitions;
- slideshow timing using a fake clock;
- image size, stride, rotate/flip, alpha, and resampling helpers;
- JPEG, PNG, and HEIF fixture decoding;
- ICC profile selection and color conversion;
- malformed and oversized input handling;
- no frontend callback while a core lock is held.

### Platform Contract Tests

Run shared contract suites against each platform implementation:

- inspect file/folder/missing path;
- enumerate and map files;
- Unicode/native filename round trips;
- default pictures and application directories;
- temporary storage cleanup;
- settings persistence;
- trash/permanent deletion result mapping;
- UI dispatcher executes on the frontend thread.

Use temporary directories and do not modify user files.

### Frontend Tests

Keep frontend tests focused:

- command/key mapping;
- presentation conversion and lifetime;
- destination rectangle and scaling;
- loading/error/empty rendering;
- timer registration;
- shutdown with active enumeration and decoding.

Manual Linux verification must cover Wayland and X11 sessions where practical.

## Continuous Integration

Add, in stages:

- Windows MSVC Win32/x64 builds;
- existing MSYS x86/x64 builds;
- Windows core and adapter tests;
- Linux GCC and Clang core tests;
- Linux GTK executable build;
- formatting and clang-tidy for portable code;
- dependency/import inspection for the Windows XP release;
- sanitizer builds on Linux for core decoders and malformed fixtures.

Linux sanitizers are especially useful for exercising image parsing, cancellation, and lifetime logic even when the
shipping Windows build cannot use them.

## Migration Phases

### Phase 1: Define and Enforce the Core Boundary

1. Add CMake targets for existing portable files and tests without changing the shipping build.
2. Add an include-boundary check that rejects Win32/GDI+, GTK/GLib, and Apple framework headers from designated
   portable targets.
3. Introduce portable scalar types, geometry, errors, orientation, and immutable image-frame definitions.
4. Add core tests on Windows and Linux CI.
5. Add the reserved `src/platform/macos/README.md` and `src/ui/macos/README.md` placeholders when those parent
   directories are established.

Exit condition: a small `imgvw_core` target builds on both systems, even if the application still uses the legacy
classes.

### Phase 2: Portable Image Output

1. Replace Win32-backed `ImgBuffer` with portable pixel storage.
2. Move DIB creation from `src/image` to the Win32 frontend.
3. Extract shared color, alpha, geometry, rotation, and resampling helpers.
4. Refactor HEIF to publish portable frames.
5. Refactor JPEG orientation and resize to remove GDI+.
6. Implement native PNG through libpng.

Exit condition: JPEG, PNG, and HEIF decode identically in headless core tests on Windows and Linux.

### Phase 3: Portable Services and Async Runtime

1. Introduce `PlatformPath`, `IFileSystem`, mappings, application directories, resource provider, settings, file
   operations, clock, executor, and UI dispatcher.
2. Wrap current Win32 behavior behind those interfaces.
3. Refactor browser enumeration out of Win32 APIs.
4. Refactor loader notification and synchronization.
5. Add fake implementations for deterministic tests.
6. Complete the XP import/runtime gate for every new standard-library or adapter implementation.

Exit condition: browser, loader, cache, and settings compile without Windows headers and existing Windows behavior
passes regression tests.

### Phase 4: Application Controller

1. Define `AppCommand` and `AppViewState`.
2. Move navigation, slideshow, delete/reload, ICC-selection policy, and browser coordination out of `ImgVwWindow`.
3. Make Win32 messages a thin translation layer.
4. Ensure core state changes are delivered through `IUiDispatcher`.

Exit condition: the Windows frontend contains native UI/presentation code but no application workflow policy.

### Phase 5: Linux Platform Adapters

1. Implement Linux path, enumeration, file mapping, XDG directories, resources, settings, and delete/trash adapters.
2. Implement GLib UI dispatch and timer adapters.
3. Register only decoders available on Linux.
4. Run platform contract and sanitizer tests.

Exit condition: a headless Linux application controller can open a folder, navigate, decode, and publish frames.

### Phase 6: GTK4 Frontend

1. Add `GtkApplication`, window, image presentation, and command actions.
2. Add keyboard and mouse-wheel navigation.
3. Add slideshow, path display, recursive browsing, reload, and close.
4. Add ICC profile selection and delete/trash workflows.
5. Add desktop metadata, icon, and packaging skeleton.

Exit condition: the Linux executable supports the primary documented ImgVw workflow with JPEG, PNG, and HEIF.

### Phase 7: Parity and Packaging

1. Compare Windows and Linux behavior against a shared fixture corpus.
2. Decide how to support BMP, GIF, TIFF, and ICO on Linux.
3. Add Wayland/X11 manual verification.
4. Select Linux distribution formats.
5. Document dependencies, licenses, commands, and supported platforms.
6. Keep Windows XP verification in the release matrix.

### Future Phase 8: macOS Implementation

This phase is intentionally outside the first Windows/Linux delivery:

1. Confirm the macOS entry criteria in the macOS sub-plan.
2. Add macOS platform adapters and run the shared contract suite.
3. Add the AppKit/Objective-C++ frontend.
4. Add arm64 build, application bundle, signing, and notarization.
5. Add x86_64 or universal packaging only if it remains a supported requirement.
6. Run shared decoder, behavior, lifecycle, Retina, and packaging verification.

Exit condition: macOS supports the primary ImgVw workflow without macOS-specific exceptions in the portable core.

## Key Decisions to Resolve During Implementation

Resolve these through small measured spikes, not assumptions:

1. Canonical frame format: RGBA8 versus BGRA8.
2. Standard C++ threading on the XP toolchain versus a platform executor.
3. Memory-only decoded frames versus optional mapped spill storage.
4. Linux trash implementation and dependency.
5. Linux release packaging and whether dependencies are bundled.
6. Decoder strategy for BMP, GIF, TIFF, and ICO.
7. Whether resize-on-window-change retains source frames or re-decodes.
8. Minimum macOS version, arm64/x86_64 policy, and Core Graphics versus Metal presentation.

None of these should block the first boundary-extraction phase.

## Risks and Mitigations

- **Large simultaneous rewrite:** migrate one boundary at a time while keeping the Windows executable green.
- **XP regression from modern C++ runtime features:** inspect final imports and run the x86 build on XP after each
  threading/filesystem/toolchain change.
- **Path corruption:** preserve native path representations and test non-ASCII plus non-UTF-8 Linux filenames.
- **UI-thread lifetime bugs:** publish immutable shared frames and dispatch state updates only through frontend
  dispatchers.
- **Memory growth after removing temp files:** add cache byte budgets and checked per-image limits before switching
  storage.
- **Different filesystem ordering/case behavior:** separate native identity from stable display ordering.
- **GDI+ remains hidden in shared code:** enforce target boundaries and include scanning in CI.
- **GTK becomes coupled to the core:** link GTK only to Linux frontend/platform targets.
- **Linux abstractions become mistaken for portable abstractions:** keep macOS placeholder contracts and reject
  POSIX/GLib types in core interfaces.
- **Behavior divergence:** use shared command/state and decoder fixtures across every implemented frontend.

## Verification Matrix

Windows:

- Visual Studio Debug/Release Win32 and x64;
- MSYS Debug/Release x86 and x64;
- Windows XP SP3 x86 release test;
- current supported Windows x86/x64;
- final executable import inspection;
- no GTK/GLib dependency.

Linux:

- current supported Ubuntu and Fedora-family environments;
- GCC and Clang core builds;
- GTK4 build under Wayland and X11;
- AddressSanitizer and UndefinedBehaviorSanitizer core tests;
- package installed without access to the source tree;
- no Win32/GDI/GDI+ dependency.

macOS readiness during the first iteration:

- both macOS placeholder READMEs exist and match the final core contracts;
- no public core interface assumes GTK/GLib, XDG, Linux Trash, `/proc`, or another Linux-only facility;
- no Apple framework dependency is added to Windows or Linux targets;
- CMake target boundaries leave space for the reserved macOS targets.

Shared:

- same JPEG/PNG/HEIF fixtures;
- same navigation, cache, slideshow, and malformed-input tests;
- matching orientation, color, alpha, and scaling output within documented tolerances;
- clean shutdown while enumeration and decoding are active.

## Acceptance Criteria

The portable-core migration is complete when:

- one core library builds and passes tests on Windows and Linux;
- core public headers contain no Win32, GDI+, GTK, GLib, AppKit, Foundation, or native filesystem handles;
- JPEG, PNG, and HEIF decode through the same code on both platforms;
- browser, cache, loader, navigation, slideshow, settings policy, and commands are shared;
- Win32 and GTK frontends translate native events into the same application commands;
- each frontend presents immutable core frames without paint-time decode or worker waits;
- the existing Windows workflow remains stable;
- the Windows XP compatibility gate still passes or any incompatibility is explicitly escalated before merge;
- the Linux GTK4 executable supports opening files/folders, navigation, recursive browsing, slideshow, reload, path
  display, ICC selection, and safe delete/trash behavior;
- platform-specific dependencies are confined to their targets;
- `src/platform/macos/README.md` and `src/ui/macos/README.md` reserve and document the future macOS implementation
  without making a macOS executable part of the first delivery;
- build, test, licensing, and packaging instructions are documented.

## References

- GTK4 documentation:
  <https://docs.gtk.org/gtk4/>
- GTK supported display backends and runtime behavior:
  <https://docs.gtk.org/gtk4/running.html>
- Qt is intentionally not selected for this option; the architecture keeps the core independent enough that another
  frontend could be added later.
- libpng's reference viewer separation between platform-independent decoding and platform-dependent display:
  <https://www.libpng.org/pub/png/book/chapter13.html>
