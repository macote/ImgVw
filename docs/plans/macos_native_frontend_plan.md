# Native macOS AppKit Frontend Plan

Date: 2026-07-27

## Status

**In progress; application shell only.** The `codex/macos-portable-core` branch currently provides:

- [x] an `IMGVW_BUILD_MACOS_APP` CMake option;
- [x] an `imgvw_platform_macos` target linked to Foundation;
- [x] `MacAppDirectories` for Application Support, Caches, and temporary paths;
- [x] an `imgvw_macos_platform_tests` target that validates absolute directory paths;
- [x] an `imgvw_ui_macos` Objective-C++/AppKit target;
- [x] an `imgvw_macos` application-bundle target;
- [x] a native application menu with Quit;
- [x] a resizable AppKit window and placeholder UI;
- [x] basic Finder `openFiles` delivery to a displayed path label; and
- [x] ARC-enabled Objective-C++ boundaries.

The shell does not load or display images. It does not instantiate an application controller, browse folders, decode,
render frames, navigate, run slideshows, manage color profiles, move files to Trash, persist settings, package
dependencies, sign, or notarize the application. It has not yet been compiled or run on macOS.

This plan defines all remaining work for a fully working macOS application. Shared prerequisites belong to
`portable_core_foundation_plan.md`; avoid implementing missing core behavior inside AppKit.

## Goal

Deliver a native arm64 macOS ImgVw application that supports the primary Windows workflow through AppKit while sharing
the portable controller, browser, loader, cache, decoders, image transforms, and color policy.

A complete first release supports:

- application launch with an empty state;
- opening an image or folder from Finder, command line, native dialogs, or drag and drop;
- JPEG, PNG, and HEIF display through portable decoders;
- sequential, random, first, last, and direct navigation;
- recursive browsing;
- reload;
- sequential and random slideshows;
- current path and image-information display;
- ICC-aware color handling and profile selection;
- move to Trash and explicitly confirmed permanent deletion;
- native menus, keyboard shortcuts, mouse/trackpad input, accessibility, appearance, Retina, and multiple displays;
- settings and bundled-resource lookup from an installed application bundle;
- clean shutdown during enumeration or decoding; and
- a reproducible signed and notarized application bundle with complete license/relink materials.

## Non-Goals

The first macOS release does not require:

- pixel-identical Windows chrome;
- GTK or shared widget code;
- a Metal renderer before Core Graphics performance is measured;
- x86_64 or a universal binary unless product requirements justify it;
- animated-image playback;
- GPU decoding or image processing;
- a plugin ABI;
- BMP, GIF, TIFF, or ICO before portable decoders are registered; or
- exact replication of Windows-only shell and multi-monitor implementation details.

## Architecture Rules

- AppKit and Foundation stay under `src/ui/macos/` and `src/platform/macos/`.
- Objective-C++ is a narrow bridge, not the home of application policy.
- No `NS*`, `CF*`, `CG*`, Objective-C object pointer, block, or dispatch type enters a portable public header.
- `AppController` receives commands and publishes immutable `AppViewState`.
- macOS adapters implement shared service interfaces.
- the frontend creates and retains native presentation objects from `ImageFrame`.
- UI callbacks never wait for decode or enumeration workers.
- all UI state publication occurs on the main thread.

## Windows Non-Regression Gate

macOS work must not make the existing Windows application depend on the macOS target or wait for macOS parity.

For every macOS phase:

- keep `ImgVw.slnx`, Visual Studio projects/filters, the MSYS Makefile, and Windows test shards valid;
- compile Objective-C++ only in macOS targets;
- never add Apple framework headers, libraries, bundle assumptions, `NSString` paths, blocks, or dispatch types to
  shared or Windows targets;
- preserve Windows XP compile definitions, import behavior, and runtime fallbacks;
- keep Win32 resources, accelerators, dialogs, shell integration, Recycle Bin behavior, GDI presentation, DPI, and
  multi-monitor behavior in their existing adapters;
- preserve GDI+ fallback formats on Windows until portable replacements pass Windows parity tests;
- preserve cloud-file consent before header probes, visible decode, or preload;
- run the shared core tests and affected Windows test shards after shared changes; and
- complete the full Windows gate from `portable_core_foundation_plan.md` and
  `windows_native_frontend_migration_plan.md` before marking a shared phase complete.

Pure AppKit/Foundation changes require at least confirmation that Windows CMake configuration still excludes macOS
targets. Changes to shared headers, image code, browsing, loading, caching, commands, view state, or CMake source lists
require the complete Windows build/test gate.

## Foundation Gaps

Before feature work, finish the current shell:

1. Compile and run all macOS targets with Apple Clang and the native SDK.
2. Decide and encode the minimum supported macOS deployment target.
3. Confirm arm64 as the first release architecture.
4. Replace hard-coded bundle versions with the project/release version source.
5. Add a controlled `Info.plist` with bundle identifier, version, category, copyright, supported document types, and
   required capabilities.
6. Add application icons in an asset catalog or generated `.icns`.
7. Ensure Finder activation works before and after `applicationDidFinishLaunching`.
8. Handle multiple incoming paths deterministically and report unsupported inputs.
9. Add macOS CI for core, platform tests, bundle build, and bundle inspection.
10. Add a macOS developer build script or documented CMake preset.

The current placeholder window is temporary and should disappear when controller-driven empty/loading/error/image views
exist.

## macOS Platform Adapters

### Platform Paths

Implement the shared `PlatformPath` contract with:

- lossless filesystem identity;
- safe conversion between native filesystem representation and `NSString` display text;
- explicit handling of decomposed/precomposed Unicode;
- filename, extension, parent, and child operations;
- stable ordering separate from identity; and
- security-scoped URL support only if sandboxing later requires it.

Do not use localized/display strings for file access or cache identity.

### Filesystem and Mapping

Implement:

- inspect file, directory, missing, inaccessible, and symbolic-link cases;
- asynchronous directory enumeration;
- read-only mappings or bounded reads;
- file size and modification metadata;
- current directory and Pictures directory;
- cancellation and native error capture; and
- file-change monitoring after basic browsing is stable.

Choose Foundation or POSIX per operation behind the C++ adapter. Never expose file descriptors, `NSURL`,
`NSFileManager`, or `stat` records to the core.

### Application Directories

Expand `MacAppDirectories` behind `IAppDirectories`:

- Application Support;
- Caches;
- temporary storage;
- bundled Resources;
- optional Logs/diagnostics;
- directory creation with explicit errors; and
- cleanup ownership for temporary artifacts.

Tests must use isolated temporary locations where possible and must not modify real user settings.

### Settings

Implement `ISettingsStore` using `NSUserDefaults` or a versioned settings file behind the shared interface.

Persist only portable preferences, including:

- slideshow interval;
- random/sequential preference where appropriate;
- recursive-browse preference if product behavior requires it;
- selected default CMYK ICC profile;
- bundled fallback selection; and
- display/overlay preferences that are shared across sessions.

Define migration, invalid-value recovery, and write-failure behavior.

### Bundled Resources

Implement `IResourceProvider` using `NSBundle`:

- bundled CMYK fallback ICC profile;
- application icons and UI assets;
- license/notices resources if displayed in-app; and
- no source-tree-relative lookup.

Add a bundle-execution test that changes the working directory before loading resources.

### Trash and Permanent Delete

Implement `IFileOperations`:

- move to Trash through supported Foundation APIs;
- return the resulting URL/path if needed;
- distinguish cancellation, unsupported filesystem, missing file, permission failure, and provider failure;
- permanent deletion as a separate operation; and
- never silently fall back from Trash to permanent deletion.

The AppKit frontend owns confirmation dialogs. The core owns the command result and navigation/cache update.

### UI Dispatch, Clock, Timers, and Execution

Implement:

- main-queue `IUiDispatcher`;
- monotonic clock;
- native timer registration translated into controller commands;
- `ITaskExecutor` using portable workers or Grand Central Dispatch behind a C++ interface; and
- bounded, deterministic shutdown.

Blocks and dispatch objects must remain private to the macOS adapter.

## Portable Decode and Dependencies

The macOS application cannot become functional until shared decode output is portable.

Required:

1. Build libjpeg-turbo, libpng/zlib, Little CMS, libheif, and libde265 for arm64 macOS.
2. Decide reproducible source/version/hash and build-script policy for each dependency.
3. Refactor JPEG orientation, resizing, and output away from GDI+.
4. Implement native PNG through `libpng_support_plan.md`.
5. Refactor HEIF mappings, synchronization, errors, and output away from Win32.
6. Publish JPEG, PNG, and HEIF as immutable `ImageFrame`.
7. Run the same malformed, oversized, color, orientation, and scaling fixtures on Windows and macOS.
8. Preserve static-link LGPL relinking obligations in the app bundle and release materials.

Until this work is complete, the AppKit target must clearly remain a foundation target rather than a usable viewer.

## AppController Integration

Replace placeholder frontend state with the shared controller:

1. Construct macOS platform services and `AppController` at application startup.
2. Translate Finder, dialog, drag/drop, menu, keyboard, mouse, trackpad, timer, and lifecycle events into `AppCommand`.
3. Subscribe to immutable `AppViewState` on the main thread.
4. Reject stale view generations and presentation completions.
5. Detach notification targets before window/controller destruction.
6. Cancel and join/retire enumeration and decode work without blocking AppKit callbacks indefinitely.
7. Support reopening a window after the last window closes if the chosen application lifecycle allows it.

No macOS-only command should be added when a shared workflow command is sufficient.

## Image Presentation

### Initial Renderer

Use Core Graphics first:

- convert or wrap a completed `ImageFrame` into a retained `CGImage`;
- define byte order, alpha mode, and color-space mapping explicitly;
- cache the `CGImage` by frame identity;
- avoid a pixel copy on every draw;
- draw aspect-fit without unintended upscaling;
- render loading, error, empty, and ready states;
- never decode or wait for a worker in `drawRect`; and
- release presentation objects when state changes without invalidating frames still in use.

Consider Metal only after profiling shows a real Core Graphics limitation.

### Retina and Displays

Handle:

- point-to-pixel conversion;
- backing-scale changes without unnecessary re-decode;
- moving between Retina and non-Retina displays;
- window resizing and live-resize behavior;
- negative/global display coordinates only where native APIs expose them;
- multiple-display slideshow policy through the shared controller; and
- appearance/color-space changes.

Retain source frames when that provides better resizing behavior within the cache budget.

## Native User Experience

### Empty, Loading, Error, and Image States

Replace the placeholder labels with a native view hierarchy that presents `AppViewState`:

- empty launch with Open Image and Open Folder actions;
- folder collection state;
- loading state with current path and available metadata;
- actionable error state;
- image canvas; and
- persistent path and temporary diagnostics/image-information overlays.

### Menus and Commands

Add native application, File, View, Navigate, Slideshow, and Help menus. Map:

- Open Image / Open Folder;
- previous / next / first / last / random;
- recursive browsing;
- reload;
- slideshow modes and speed;
- path and image-information visibility;
- ICC profile selection;
- Move to Trash / Delete Permanently;
- About; and
- Quit.

Use standard macOS equivalents where conventions differ from Windows. Menu validation should reflect `AppViewState`.

### Input

Support:

- keyboard navigation;
- scroll-wheel and trackpad navigation with debouncing;
- drag and drop of one file or folder;
- Finder open events;
- command-line path activation;
- contextual menu where useful; and
- full keyboard access and focus traversal.

### Dialogs and Clipboard

Use native open panels, confirmation alerts, ICC profile selection, About panel, and pasteboard APIs. Dialog completion
must return commands/results without embedding workflow policy in Objective-C++.

### Accessibility and Appearance

Add:

- meaningful accessibility labels, roles, values, and actions;
- VoiceOver verification;
- keyboard-only operation;
- Reduce Motion behavior for any transitions;
- light, dark, and high-contrast appearance handling;
- localized layout readiness even if the first release is English-only; and
- native font and focus-ring behavior.

## Browsing, Navigation, and Slideshow

Connect shared behavior for:

- opening a lone image and its containing folder;
- opening a folder;
- stable display order;
- duplicate and missing-file handling;
- recursive enumeration;
- visible-item load priority and adjacent preloading;
- sequential/random navigation and progress;
- reload after source changes;
- deletion followed by safe next-item selection;
- sequential/random slideshow timers; and
- multi-display slideshow only after single-window behavior is stable.

Test shutdown, path replacement, and late decode/enumeration completion rigorously.

## Color Management

Complete shared JPEG/PNG/HEIF color policy, then:

- map portable frame color information to the correct Core Graphics color space;
- avoid double conversion by ColorSync/Core Graphics;
- verify embedded RGB, grayscale, CMYK, and supported HEIF SDR metadata;
- reject or clearly report unsupported HDR transfer characteristics;
- support default CMYK profile selection through `NSOpenPanel`;
- load the bundled fallback through `NSBundle`; and
- compare output with Windows reference fixtures within documented tolerances.

Retina support does not by itself imply correct color management; test both explicitly.

## Finder and Bundle Integration

Add:

- document type declarations for actually supported formats;
- Uniform Type Identifier declarations only where needed;
- Finder Open With behavior;
- drag/drop type filtering;
- command-line path forwarding;
- correct app category and bundle metadata;
- icons for application and documents where appropriate; and
- optional Quick Look or sandbox work only as separate future scope.

Do not advertise GDI+-only formats in the bundle.

## Build, Signing, and Distribution

### Build Targets

Maintain:

```text
imgvw_core
imgvw_core_tests
imgvw_platform_macos
imgvw_macos_platform_tests
imgvw_ui_macos
imgvw_macos
```

Add presets or scripts for Debug, Release, tests, and bundle inspection. The release must build without Homebrew or
source-tree paths at runtime.

### Architecture and Deployment

- arm64 is the initial target;
- choose the minimum macOS version through a tested deployment target;
- add x86_64/universal only with an explicit support decision;
- ensure every bundled static/dynamic dependency matches the architecture and deployment target; and
- inspect the final Mach-O load commands and linked frameworks.

### Two-Step Homebrew Distribution

Use two explicit distribution stages. The first enables early use without an Apple Developer Program membership; it
does not satisfy the final release criteria. The second is the normal public distribution path.

#### Step 1: Personal Tap Without an Apple Developer Account

Publish development or early-access builds through a personal Homebrew tap:

1. Create and maintain a dedicated tap repository, such as `macote/homebrew-imgvw`.
2. Publish a versioned arm64 application archive and SHA-256 checksum from a stable release URL.
3. Add a cask installable with a command such as:

   ```sh
   brew install --cask macote/imgvw/imgvw
   ```

4. Build the bundle reproducibly without Developer ID credentials. Use unsigned or ad-hoc signing only as required for
   bundle integrity; do not describe it as Apple-trusted, signed for distribution, or notarized.
5. Document that macOS Gatekeeper may block or warn about the application and that the user must make an explicit
   system-supported approval decision. Do not automate quarantine removal or security-policy bypasses.
6. Test fresh install, upgrade, reinstall, uninstall, checksum changes, application launch, and removal of installed
   artifacts.
7. Mark this channel as preview/developer distribution and keep it separate from final release claims.

Exit condition: a user can install and update the preview from the personal tap, with the unsigned/notarized limitation
clearly disclosed and no automated Gatekeeper bypass.

#### Step 2: Signed Public Homebrew Cask

After obtaining Apple Developer Program credentials:

1. Sign the complete application with an appropriate Developer ID certificate and hardened runtime.
2. Notarize the exact distributed archive, staple the ticket where applicable, and verify it with Gatekeeper.
3. Publish immutable versioned release URLs and checksums.
4. Update the personal tap first as a release candidate and verify install, upgrade, uninstall, and rollback behavior.
5. Prepare a standard public cask submission for the normal Homebrew cask repository, following its current naming,
   metadata, source, livecheck, signing, and review policies.
6. Remove the personal-tap qualifier from normal installation instructions after the public cask is accepted:

   ```sh
   brew install --cask imgvw
   ```

7. Keep the personal tap available only for prereleases or as a documented fallback, without publishing conflicting
   stable cask definitions.

Exit condition: the public Homebrew cask installs the signed and notarized release without security-policy workarounds,
and standard Homebrew upgrade and uninstall flows pass.

### Signing and Notarization

Add a reproducible release flow for:

- hardened runtime;
- Developer ID signing;
- nested-code/dependency signing order;
- entitlements only when required;
- notarization submission and status;
- stapling;
- Gatekeeper verification; and
- release artifact checksums.

Keep local unsigned developer builds and the Step 1 personal-tap preview possible. Only Step 2 artifacts may be
presented as signed, notarized, and ready for normal public distribution.

### Licensing

Bundle and publish:

- ImgVw license;
- dependency licenses/notices;
- exact dependency source/version/hash information;
- libheif/libde265 source or written/source offer as required;
- static relinking instructions and materials; and
- an audit that the application bundle contains no unintended development paths or libraries.

## Implementation Phases

### Phase 1: Validate and Harden the Shell

1. Build/run on arm64 macOS.
2. Set deployment target and bundle metadata.
3. Add icon, Info.plist, file-open lifecycle handling, and CI.
4. Expand directory adapter tests.

Exit condition: a correctly identified application bundle launches from Finder and passes platform tests.

Windows gate: CMake excludes Objective-C++ and Apple frameworks on Windows, and established Windows builds remain green.

### Phase 2: Complete Shared Core Prerequisites

1. Finish portable frame semantics.
2. Port JPEG/HEIF output and implement PNG.
3. Add paths/services/controller contracts.
4. Build dependencies and core tests on macOS.

Exit condition: a headless macOS controller can open, browse, decode, and publish an image frame.

Windows gate: shared decoder/controller changes pass the full Windows image, concurrency, application-build, and XP
compatibility matrix.

### Phase 3: Display One Image

1. Connect controller composition.
2. Add Core Graphics presentation.
3. Add empty/loading/error/image states.
4. Handle Finder, command-line, dialog, and drag/drop open.

Exit condition: the AppKit application reliably displays JPEG, PNG, and HEIF images.

Windows gate: Win32 presentation still consumes the same immutable frame contract without image-quality, DPI, or
paint-lifetime regressions.

### Phase 4: Primary Viewer Workflow

1. Add navigation, recursive browsing, reload, overlays, and information.
2. Add menus, shortcuts, mouse/trackpad input, and dialogs.
3. Add ICC selection and color-managed presentation.
4. Add Trash/permanent delete.
5. Add slideshow modes and settings.

Exit condition: the complete primary workflow is usable in one window.

Windows gate: shared command/state changes preserve every existing Windows command, navigation, slideshow, settings,
color, and deletion workflow.

### Phase 5: Native Quality and Lifecycle

1. Complete Retina, multiple-display, appearance, and accessibility behavior.
2. Verify cancellation, stale results, file changes, and shutdown.
3. Add multi-display slideshow if retained for macOS parity.
4. Compare shared fixtures and behavior with Windows.

Exit condition: macOS native-quality work passes while the full Windows regression matrix remains green.

### Phase 6: Personal-Tap Preview Distribution

1. Build release dependencies reproducibly.
2. Add licenses and relinking materials.
3. Publish the unsigned or ad-hoc-signed bundle and checksum.
4. Add and test the personal-tap cask.
5. Document Gatekeeper limitations without automating a security bypass.
6. Validate install, upgrade, uninstall, and execution outside the source tree.

Exit condition: the Step 1 personal-tap preview is usable and its trust limitations are explicit.

Windows gate: Homebrew metadata and macOS release automation remain platform-scoped and do not change Windows
packaging, dependency lookup, release artifacts, or installation instructions.

### Phase 7: Signed Public Distribution

1. Obtain and configure Apple Developer Program release credentials.
2. Sign with hardened runtime, notarize, staple, and Gatekeeper-test.
3. Verify the signed archive through the personal-tap release-candidate path.
4. Submit and validate the standard public Homebrew cask.
5. Publish normal installation, upgrade, uninstall, build, and release instructions.

Exit condition: the Step 2 public cask distributes a fully signed and notarized release.

Windows gate: Apple credentials, signing, notarization, and cask publication are isolated from the Windows release
pipeline; the full Windows release matrix still passes from the same source revision.

## Verification Matrix

Automated:

- portable core and decoder tests on macOS arm64;
- macOS path, directories, resources, settings, mappings, dispatch, and Trash contract tests;
- AppController command/state tests;
- Core Graphics conversion/lifetime tests;
- malformed and oversized decode fixtures;
- cancellation and stale-result tests;
- bundle resource and metadata inspection;
- Mach-O dependency inspection; and
- personal-tap cask install, upgrade, reinstall, and uninstall;
- explicit verification of the preview build's Gatekeeper behavior;
- signed/notarized artifact verification for release builds; and
- public Homebrew cask install, upgrade, and uninstall.

Manual:

- empty launch and window reopen;
- Finder, dialog, command-line, and drag/drop open;
- image and folder open;
- JPEG, PNG, HEIF, rotated, alpha, CMYK, ICC, malformed, and large images;
- navigation, random cycle, recursion, reload, and deletion;
- sequential/random slideshow;
- Retina/non-Retina and multiple-display transitions;
- resize, minimize/restore, fullscreen if supported, and appearance changes;
- keyboard-only and VoiceOver use;
- Trash and permanent deletion;
- shutdown during enumeration and decode; and
- launch of an installed notarized bundle without source-tree access.

## Risks and Mitigations

- **Shell mistaken for working application:** keep status explicit until controller and decode are connected.
- **Objective-C++ absorbs policy:** restrict it to native adaptation and presentation.
- **Unicode/path mismatch:** test native identity separately from `NSString` display values.
- **Frame lifetime errors:** use immutable shared ownership and retained presentation objects.
- **Retina-driven memory growth:** retain source/presentation frames under explicit cache budgets.
- **Double color conversion:** document frame color state and test ColorSync/Core Graphics behavior.
- **Dependency drift:** pin sources, versions, hashes, architecture, and deployment target.
- **XP regression from shared changes:** keep Windows release/import gates active throughout.
- **Signing surprises late in development:** inspect and sign realistic bundles before the final phase.

## Acceptance Criteria

The macOS application is fully working when:

- it completes every primary workflow listed under Goal;
- JPEG, PNG, and HEIF use the shared portable decode pipeline;
- AppKit consumes shared commands and view state without macOS-specific core exceptions;
- the established Win32 application retains its builds, XP gate, commands, image behavior, shell integration,
  Recycle Bin behavior, DPI/multi-monitor handling, and clean shutdown;
- Core Graphics presents immutable frames correctly across Retina and display changes;
- browsing, navigation, cache, slideshow, settings, color, and delete policy are shared;
- macOS paths, directories, resources, mappings, settings, dispatch, and Trash pass contract tests;
- native menus, shortcuts, dialogs, drag/drop, accessibility, and Finder integration work;
- shutdown is clean with active background work;
- the selected minimum macOS version and arm64 release matrix pass;
- the bundle runs without source-tree or package-manager runtime dependencies;
- the Step 1 personal tap can distribute preview builds without claiming Apple trust or bypassing Gatekeeper;
- the Step 2 standard public Homebrew cask installs the Developer ID-signed and notarized release;
- release artifacts are reproducibly built, licensed, signed, notarized, stapled, and Gatekeeper-verified; and
- build, test, dependency, relinking, and release documentation is complete.
